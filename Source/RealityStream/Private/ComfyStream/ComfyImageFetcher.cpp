#include "ComfyStream/ComfyImageFetcher.h"
#include "ComfyStream/ComfyPngDecoder.h"
#include "IWebSocket.h"
#include "WebSocketsModule.h"
#include "Async/Async.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "Misc/Base64.h"

UComfyImageFetcher::UComfyImageFetcher()
{
	Config = FComfyStreamConfig();
	PngDecoder = nullptr;
	ConnectionStatus = EComfyConnectionStatus::Disconnected;
	bIsPolling = false;
	CurrentChannel = 1;
	WebSocketPort = 8001;
}

void UComfyImageFetcher::StartPolling(const FString& ServerURL, int32 ChannelNumber)
{
	StopPolling();

	if (!PngDecoder)
		PngDecoder = NewObject<UComfyPngDecoder>(this);

	CurrentServerURL = ServerURL;
	CurrentChannel = ChannelNumber;
	bIsPolling = true;

	SetConnectionStatus(EComfyConnectionStatus::Connecting);

	FString WebSocketURL = BuildWebSocketURL(ServerURL, ChannelNumber);
	UE_LOG(LogTemp, Display, TEXT("[ComfyImageFetcher] Connecting to %s"), *WebSocketURL);

	WebSocket = FWebSocketsModule::Get().CreateWebSocket(WebSocketURL);

	if (WebSocket.IsValid())
	{
		WebSocket->OnConnected().AddUObject(this, &UComfyImageFetcher::OnWebSocketConnected);
		WebSocket->OnConnectionError().AddUObject(this, &UComfyImageFetcher::OnWebSocketConnectionError);
		WebSocket->OnClosed().AddUObject(this, &UComfyImageFetcher::OnWebSocketClosed);
		WebSocket->OnRawMessage().AddUObject(this, &UComfyImageFetcher::OnWebSocketMessage);

		WebSocket->Connect();
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[ComfyImageFetcher] Failed to create WebSocket"));
		SetConnectionStatus(EComfyConnectionStatus::Error);
	}
}

void UComfyImageFetcher::StopPolling()
{
	bIsPolling = false;

	if (WebSocket.IsValid() && WebSocket->IsConnected())
		WebSocket->Close();

	WebSocket.Reset();
	ChunkBuffer.Empty();
	bReceivingChunks = false;
	SetConnectionStatus(EComfyConnectionStatus::Disconnected);
}

bool UComfyImageFetcher::IsPolling() const
{
	return bIsPolling;
}

// ============================================================
// PNG SPLITTER (ROBUST CHUNK-BASED PARSER)
// ============================================================

// Parses a single PNG starting at StartIdx by walking chunks properly.
// Returns end index (one-past-last byte) if valid PNG found, otherwise INDEX_NONE.
static int32 ParseOnePNGAt(const TArray<uint8>& Buf, int32 StartIdx)
{
	const uint8 Sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
	const int32 N = Buf.Num();

	if (StartIdx < 0 || StartIdx + 8 > N) return INDEX_NONE;
	if (FMemory::Memcmp(&Buf[StartIdx], Sig, 8) != 0) return INDEX_NONE;

	int32 p = StartIdx + 8;

	// Walk chunks: [len:4][type:4][data:len][crc:4]
	while (true)
	{
		if (p + 8 > N) return INDEX_NONE; // Need len+type at minimum
		
		// Read big-endian length
		uint32 Len = (uint32(Buf[p]) << 24) | (uint32(Buf[p + 1]) << 16) | (uint32(Buf[p + 2]) << 8) | uint32(Buf[p + 3]);
		
		// Check chunk type (IEND marks end of PNG)
		char Type[4] = {(char)Buf[p + 4], (char)Buf[p + 5], (char)Buf[p + 6], (char)Buf[p + 7]};

		p += 8; // Past len+type

		// Bounds check for data + CRC
		if (p + int32(Len) + 4 > N) return INDEX_NONE;

		// Advance over data
		p += int32(Len);
		// Advance over CRC
		p += 4;

		// Found IEND chunk - PNG complete
		if (Type[0] == 'I' && Type[1] == 'E' && Type[2] == 'N' && Type[3] == 'D')
		{
			return p; // p is one past last byte of PNG
		}
	}
}

TArray<TArray<uint8>> UComfyImageFetcher::SplitPNGStream(const TArray<uint8>& Buffer)
{
	TArray<TArray<uint8>> Out;
	const int32 N = Buffer.Num();
	int32 i = 0;

	// PNG signature bytes for fast scanning
	const uint8 Sig0 = 0x89, Sig1 = 'P', Sig2 = 'N', Sig3 = 'G';
	const uint8 Sig4 = 0x0D, Sig5 = 0x0A, Sig6 = 0x1A, Sig7 = 0x0A;

	while (i + 8 <= N)
	{
		// Fast signature check
		if (Buffer[i] == Sig0 && Buffer[i + 1] == Sig1 && Buffer[i + 2] == Sig2 && Buffer[i + 3] == Sig3 &&
			Buffer[i + 4] == Sig4 && Buffer[i + 5] == Sig5 && Buffer[i + 6] == Sig6 && Buffer[i + 7] == Sig7)
		{
			int32 End = ParseOnePNGAt(Buffer, i);
			if (End == INDEX_NONE)
			{
				// Incomplete PNG - might be chunked, stop here
				UE_LOG(LogTemp, Warning, TEXT("[Comfy] PNG signature found at offset %d but parser failed. Payload may be corrupted or incomplete."), i);
				break;
			}
			
			TArray<uint8> One;
			One.Append(&Buffer[i], End - i);
			Out.Add(MoveTemp(One));
			i = End; // Continue after this PNG
		}
		else
		{
			++i;
		}
	}
	return Out;
}

// ============================================================
// WEBSOCKET EVENT HANDLERS
// ============================================================

void UComfyImageFetcher::OnWebSocketConnected()
{
	AsyncTask(ENamedThreads::GameThread, [this]()
	{
		OnWebSocketConnected_GameThread();
	});
}

void UComfyImageFetcher::OnWebSocketConnected_GameThread()
{
	UE_LOG(LogTemp, Display, TEXT("[ComfyImageFetcher] WebSocket connected to channel %d"), CurrentChannel);
	SetConnectionStatus(EComfyConnectionStatus::Connected);
}

void UComfyImageFetcher::OnWebSocketConnectionError(const FString& Error)
{
	AsyncTask(ENamedThreads::GameThread, [this, Error]()
	{
		OnWebSocketConnectionError_GameThread(Error);
	});
}

void UComfyImageFetcher::OnWebSocketConnectionError_GameThread(const FString& Error)
{
	UE_LOG(LogTemp, Error, TEXT("[ComfyImageFetcher] WebSocket connection error: %s"), *Error);
	SetConnectionStatus(EComfyConnectionStatus::Error);
	OnError.Broadcast(Error);
}

void UComfyImageFetcher::OnWebSocketClosed(int32 StatusCode, const FString& Reason, bool bWasClean)
{
	AsyncTask(ENamedThreads::GameThread, [this, StatusCode, Reason, bWasClean]()
	{
		OnWebSocketClosed_GameThread(StatusCode, Reason, bWasClean);
	});
}

void UComfyImageFetcher::OnWebSocketClosed_GameThread(int32 StatusCode, const FString& Reason, bool bWasClean)
{
	UE_LOG(LogTemp, Warning, TEXT("[ComfyImageFetcher] WebSocket closed. Code: %d, Reason: %s, Clean: %s"), 
		StatusCode, *Reason, bWasClean ? TEXT("Yes") : TEXT("No"));
	SetConnectionStatus(EComfyConnectionStatus::Disconnected);
}

void UComfyImageFetcher::OnWebSocketMessageSent(const FString& MessageString)
{
	UE_LOG(LogTemp, VeryVerbose, TEXT("[ComfyImageFetcher] Message sent: %s"), *MessageString);
}

void UComfyImageFetcher::SetConnectionStatus(EComfyConnectionStatus NewStatus)
{
	if (ConnectionStatus != NewStatus)
	{
		ConnectionStatus = NewStatus;
		OnConnectionStatusChanged.Broadcast(NewStatus == EComfyConnectionStatus::Connected);
	}
}

// ============================================================
// WEBSOCKET MESSAGE HANDLING
// ============================================================

void UComfyImageFetcher::OnWebSocketMessage(const void* Data, SIZE_T Size, SIZE_T BytesRemaining)
{
	TArray<uint8> Copy;
	Copy.Append(static_cast<const uint8*>(Data), Size);

	AsyncTask(ENamedThreads::GameThread, [this, Copy, Size, BytesRemaining]()
	{
		if (!bReceivingChunks && ChunkBuffer.Num() > 0) ChunkBuffer.Empty();
		ChunkBuffer.Append(Copy);

		if (BytesRemaining > 0)
		{
			bReceivingChunks = true;
			return;
		}

		bReceivingChunks = false;
		ProcessImageData(ChunkBuffer);
		ChunkBuffer.Empty();
	});
}

void UComfyImageFetcher::ProcessImageData(const TArray<uint8>& In)
{
	if (In.Num() < 4) return;
	if (!IsValid(PngDecoder))
	{
		UE_LOG(LogTemp, Error, TEXT("[Comfy] PNG decoder not available"));
		return;
	}

	// Handle optional 8-byte binary header [1,2] (BE or LE) used by WebViewer
	int32 Offset = 0;
	if (In.Num() >= 8)
	{
		uint32 H1_BE = (In[0] << 24) | (In[1] << 16) | (In[2] << 8) | In[3];
		uint32 H2_BE = (In[4] << 24) | (In[5] << 16) | (In[6] << 8) | In[7];
		uint32 H1_LE = In[0] | (In[1] << 8) | (In[2] << 16) | (In[3] << 24);
		uint32 H2_LE = In[4] | (In[5] << 8) | (In[6] << 16) | (In[7] << 24);
		
		if ((H1_BE == 1 && H2_BE == 2) || (H1_LE == 1 && H2_LE == 2))
		{
			Offset = 8;
			UE_LOG(LogTemp, Verbose, TEXT("[Comfy] Recognized 8B header [1,2]"));
		}
	}

	// Handle optional tiny JSON preamble `{...}\n` (older WebViewer "meta")
	if (In.Num() > Offset && In[Offset] == '{')
	{
		// First check if it's a full JSON bundle
		FString JsonString = FString(UTF8_TO_TCHAR((const char*)&In.GetData()[Offset]));
		TSharedPtr<FJsonObject> JsonObject;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);

		if (FJsonSerializer::Deserialize(Reader, JsonObject) && JsonObject.IsValid())
		{
			FString Type;
			if (JsonObject->TryGetStringField(TEXT("type"), Type) && Type == TEXT("bundle"))
			{
				UE_LOG(LogTemp, Display, TEXT("[Comfy] Bundle received"));

				const TArray<TSharedPtr<FJsonValue>>* ImagesArray;
				if (JsonObject->TryGetArrayField(TEXT("images"), ImagesArray))
				{
					for (const auto& ImgVal : *ImagesArray)
					{
						const TSharedPtr<FJsonObject>* ImgObj;
						if (!ImgVal->TryGetObject(ImgObj) || !ImgObj || !ImgObj->IsValid()) continue;

						FString Name, Base64Str;
						(*ImgObj)->TryGetStringField(TEXT("name"), Name);
						(*ImgObj)->TryGetStringField(TEXT("data"), Base64Str);

						if (Base64Str.IsEmpty()) continue;

						TArray<uint8> Bytes;
						FBase64::Decode(Base64Str, Bytes);

						if (!IsValid(PngDecoder)) continue;
						UTexture2D* Tex = PngDecoder->DecodePNGToTexture(Bytes);
						if (!Tex)
						{
							UE_LOG(LogTemp, Error, TEXT("[Comfy] Failed to decode %s frame"), *Name);
							continue;
						}

						UE_LOG(LogTemp, Display, TEXT("[Comfy] %s frame decoded"), *Name);
						OnTextureReceived.Broadcast(Tex);
					}
					return;
				}
			}
		}

		// Not a bundle, strip JSON preamble for raw PNG
		for (int32 i = Offset; i + 1 < In.Num(); ++i)
		{
			if (In[i] == '}' && In[i + 1] == '\n')
			{
				UE_LOG(LogTemp, Verbose, TEXT("[Comfy] Stripped JSON preamble (%d..%d)"), Offset, i + 1);
				Offset = i + 2;
				break;
			}
		}
	}

	// Slice to image payload
	TArray<uint8> Payload;
	if (Offset < In.Num())
	{
		Payload.Append(&In[Offset], In.Num() - Offset);
	}
	else
	{
		Payload = In;
	}

	// Robustly split concatenated PNGs
	auto Pngs = SplitPNGStream(Payload);
	if (Pngs.Num() == 0)
	{
		// Check if there's a PNG signature at all
		bool bHasSignature = false;
		for (int32 i = 0; i < FMath::Min(Payload.Num(), 100); ++i)
		{
			if (Payload[i] == 0x89 && i + 7 < Payload.Num() &&
				Payload[i + 1] == 'P' && Payload[i + 2] == 'N' && Payload[i + 3] == 'G')
			{
				bHasSignature = true;
				break;
			}
		}
		
		if (bHasSignature)
		{
			UE_LOG(LogTemp, Warning, TEXT("[Comfy] PNG signature found but parser failed. Payload may be corrupted or incomplete."));
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("[Comfy] No PNG frames found in payload (%d bytes). No PNG signature detected."), Payload.Num());
		}
		return;
	}

	UE_LOG(LogTemp, Display, TEXT("[Comfy] Extracted %d PNG frame(s)"), Pngs.Num());

	// Expected order from Comfy config: rgb, depth, mask
	for (int32 i = 0; i < Pngs.Num(); ++i)
	{
		if (!IsValid(PngDecoder))
		{
			UE_LOG(LogTemp, Error, TEXT("[Comfy] PNG decoder invalid during decode"));
			return;
		}

		UTexture2D* Tex = PngDecoder->DecodePNGToTexture(Pngs[i]);
		if (!Tex)
		{
			UE_LOG(LogTemp, Error, TEXT("[Comfy] PNG decode failed for frame %d (size %d)"), i, Pngs[i].Num());
			continue;
		}

		UE_LOG(LogTemp, Display, TEXT("[Comfy] Frame %d decoded OK"), i);
		OnTextureReceived.Broadcast(Tex);
	}
}

// ============================================================

FString UComfyImageFetcher::BuildWebSocketURL(const FString& ServerURL, int32 ChannelNumber)
{
	FString Host = ServerURL;
	Host.RemoveFromStart(TEXT("http://"));
	Host.RemoveFromStart(TEXT("https://"));
	Host.RemoveFromStart(TEXT("ws://"));
	Host.RemoveFromStart(TEXT("wss://"));
	int32 Colon;
	if (Host.FindChar(':', Colon)) Host = Host.Left(Colon);
	Host.RemoveFromEnd(TEXT("/"));

	return FString::Printf(TEXT("ws://%s:%d/image?channel=%d"), *Host, WebSocketPort, ChannelNumber);
}
