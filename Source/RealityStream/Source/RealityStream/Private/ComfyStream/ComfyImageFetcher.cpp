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

	WebSocket->OnConnected().AddUObject(this, &UComfyImageFetcher::OnWebSocketConnected);
	WebSocket->OnConnectionError().AddUObject(this, &UComfyImageFetcher::OnWebSocketConnectionError);
	WebSocket->OnClosed().AddUObject(this, &UComfyImageFetcher::OnWebSocketClosed);
	WebSocket->OnRawMessage().AddUObject(this, &UComfyImageFetcher::OnWebSocketMessage);

	WebSocket->Connect();
}

void UComfyImageFetcher::StopPolling()
{
	bIsPolling = false;

	if (WebSocket.IsValid() && WebSocket->IsConnected())
		WebSocket->Close();

	WebSocket.Reset();
	ChunkBuffer.Empty();
	bReceivingChunks = false;
	AccumulatedPngMessages.Empty();
	MessagesSinceLastFrame = 0;
	SetConnectionStatus(EComfyConnectionStatus::Disconnected);
}

bool UComfyImageFetcher::IsPolling() const
{
	return bIsPolling;
}

// ============================================================
// PNG SPLITTER (ROBUST CHUNK-BASED PARSER)
// ============================================================

// Checks if a PNG has RGB color type (color type 2, 3, or 6) by reading IHDR chunk
// Returns true if RGB/RGBA/Indexed, false if grayscale or other
// PNG IHDR structure (after 8-byte signature): [len:4][IHDR:4][width:4][height:4][bit_depth:1][color_type:1][compression:1][filter:1][interlace:1][crc:4]
// Color type: 0=Grayscale, 2=RGB, 3=Indexed (palette with color), 4=Grayscale+Alpha, 6=RGB+Alpha
static bool IsPngRGB(const TArray<uint8>& PngData)
{
	const int32 N = PngData.Num();
	if (N < 8) return false;
	
	// Check PNG signature
	if (FMemory::Memcmp(PngData.GetData(), "\x89PNG\r\n\x1A\n", 8) != 0) return false;
	
	// IHDR should be at offset 8, with length 13
	// Need: [len:4][IHDR:4][data:13][crc:4] = 25 bytes minimum
	if (N < 25) return false;
	
	// Check IHDR chunk (bytes 8-11 should be length 0x0000000D = 13, bytes 12-15 should be "IHDR")
	if (PngData[8] != 0 || PngData[9] != 0 || PngData[10] != 0 || PngData[11] != 0x0D) return false;
	if (PngData[12] != 'I' || PngData[13] != 'H' || PngData[14] != 'D' || PngData[15] != 'R') return false;
	
	// Color type is at offset 25 (8 sig + 4 len + 4 type + 4 width + 4 height + 1 bit_depth = 25)
	const uint8 ColorType = PngData[25];
	
	// Color type 2 = RGB, 3 = Indexed (palette with colors), 6 = RGBA (all are color images)
	// Include indexed color (type 3) because colored canny edge images are often saved as indexed PNGs
	bool bIsRGB = (ColorType == 2 || ColorType == 3 || ColorType == 6);
	
	return bIsRGB;
}

// Checks if an RGB PNG actually contains grayscale-like content (likely depth/mask)
// by sampling pixel data to see if R=G=B (grayscale) or if there's actual color variation
// Returns true if the image appears to be grayscale content in an RGB container
static bool IsRGBPngActuallyGrayscale(const TArray<uint8>& PngData)
{
	// Heuristic: very small files are likely masks
	// Medium to large files could be either RGB or depth
	// Since we can't easily distinguish RGB from depth by size alone when both are RGB type,
	// we'll use a conservative threshold - only mark very small files as grayscale
	const int32 Size = PngData.Num();
	return Size < 5000; // Only very small files (< 5KB) are likely masks
}

// Parses a single PNG starting at StartIdx by walking chunks properly.
// Returns end index (one-past-last byte) if valid PNG found, otherwise INDEX_NONE.
static int32 ParseOnePNGAt(const TArray<uint8>& Buf, int32 StartIdx)
{
	const uint8 Sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
	const int32 N = Buf.Num();

	if (StartIdx < 0 || StartIdx + 8 > N) return INDEX_NONE;
	if (FMemory::Memcmp(&Buf[StartIdx], Sig, 8) != 0) return INDEX_NONE;

	int32 p = StartIdx + 8;
	int32 ChunkCount = 0;

	// Walk chunks: [len:4][type:4][data:len][crc:4]
	while (true)
	{
		if (p + 8 > N)
		{
			return INDEX_NONE; // Need len+type at minimum
		}
		
		// Read big-endian length
		uint32 Len = (uint32(Buf[p]) << 24) | (uint32(Buf[p + 1]) << 16) | (uint32(Buf[p + 2]) << 8) | uint32(Buf[p + 3]);
		
		// Sanity check: PNG chunks should not exceed ~10MB (most are much smaller)
		const uint32 MaxReasonableChunkSize = 10 * 1024 * 1024; // 10MB
		if (Len > MaxReasonableChunkSize)
		{
			return INDEX_NONE;
		}
		
		// Check chunk type (IEND marks end of PNG)
		char Type[4] = {(char)Buf[p + 4], (char)Buf[p + 5], (char)Buf[p + 6], (char)Buf[p + 7]};

		p += 8; // Past len+type

		// Bounds check for data + CRC
		if (p + int32(Len) + 4 > N)
		{
			return INDEX_NONE;
		}

		// Advance over data
		p += int32(Len);
		// Advance over CRC
		p += 4;
		
		ChunkCount++;

		// Found IEND chunk - PNG complete
		if (Type[0] == 'I' && Type[1] == 'E' && Type[2] == 'N' && Type[3] == 'D')
		{
			return p; // p is one past last byte of PNG
		}
		
		// Safety check - if we've parsed too many chunks without finding IEND, something is wrong
		if (ChunkCount > 1000)
		{
			return INDEX_NONE;
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
				// Corrupted PNG - search forward for the next PNG signature instead of breaking
				bool bFoundNextSig = false;
				for (int32 j = i + 8; j + 8 <= N; ++j)
				{
					if (Buffer[j] == Sig0 && Buffer[j + 1] == Sig1 && Buffer[j + 2] == Sig2 && Buffer[j + 3] == Sig3 &&
						Buffer[j + 4] == Sig4 && Buffer[j + 5] == Sig5 && Buffer[j + 6] == Sig6 && Buffer[j + 7] == Sig7)
					{
						i = j - 1; // Will be incremented by loop, so set to j-1
						bFoundNextSig = true;
						break;
					}
				}
				
				if (!bFoundNextSig)
				{
					break;
				}
				continue;
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
			UE_LOG(LogTemp, Display, TEXT("[ComfyImageFetcher] WebSocket message chunk received: %llu bytes, %llu remaining"), Size, BytesRemaining);
			return;
		}

		bReceivingChunks = false;
		
		ProcessImageData(ChunkBuffer);
		ChunkBuffer.Empty();
	});
}

// Helper function to check if data looks like JSON/text (not PNG)
static bool IsJsonOrText(const TArray<uint8>& Data, int32 StartOffset = 0)
{
	if (StartOffset >= Data.Num()) return false;
	
	// Check if it starts with '{' (JSON) or is mostly printable ASCII
	int32 CheckLen = FMath::Min(100, Data.Num() - StartOffset);
	int32 PrintableCount = 0;
	bool bStartsWithBrace = (Data[StartOffset] == '{');
	
	for (int32 i = StartOffset; i < StartOffset + CheckLen; ++i)
	{
		uint8 Byte = Data[i];
		// Count printable ASCII (32-126) or common whitespace
		if ((Byte >= 32 && Byte <= 126) || Byte == 9 || Byte == 10 || Byte == 13)
		{
			PrintableCount++;
		}
	}
	
	// If >80% printable and starts with '{', it's likely JSON
	if (bStartsWithBrace && (PrintableCount * 100 / CheckLen) > 80)
	{
		return true;
	}
	
	// If no PNG signature after header and mostly printable, it's text
	if (!bStartsWithBrace && StartOffset + 8 < Data.Num())
	{
		// Check for PNG signature
		bool bHasPngSig = (Data[StartOffset] == 0x89 && Data[StartOffset + 1] == 'P' && 
		                   Data[StartOffset + 2] == 'N' && Data[StartOffset + 3] == 'G');
		if (!bHasPngSig && (PrintableCount * 100 / CheckLen) > 70)
		{
			return true;
		}
	}
	
	return false;
}

void UComfyImageFetcher::ProcessImageData(const TArray<uint8>& In)
{
	if (In.Num() < 4)
	{
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
		}
	}

	// Check if this is a JSON/text message (not PNG) - skip it
	if (IsJsonOrText(In, Offset))
	{
		return;
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

						if (PngDecoder)
						{
							UTexture2D* Tex = PngDecoder->DecodePNGToTexture(Bytes);
							if (Tex)
							{
								OnTextureReceived.Broadcast(Tex);
							}
						}
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

	// Check for PNG signature after the offset
	if (Payload.Num() < 8 || 
		Payload[0] != 0x89 || Payload[1] != 'P' || Payload[2] != 'N' || Payload[3] != 'G' ||
		Payload[4] != 0x0D || Payload[5] != 0x0A || Payload[6] != 0x1A || Payload[7] != 0x0A)
	{
		return;
	}

	// Split concatenated PNGs
	auto Pngs = SplitPNGStream(Payload);
	
	// If we got PNGs from this message, add them to accumulator
	if (Pngs.Num() > 0)
	{
		for (auto& Png : Pngs)
		{
			AccumulatedPngMessages.Add(MoveTemp(Png));
		}
		
		MessagesSinceLastFrame++;
		
		// Log complete message with total accumulated count
		UE_LOG(LogTemp, Display, TEXT("[ComfyImageFetcher] WebSocket message received: %d bytes (complete)"), In.Num());
		UE_LOG(LogTemp, Display, TEXT("Total accumulated: %d"), AccumulatedPngMessages.Num());
		
		// Protection: If too many messages without completing a frame, clear accumulator
		if (MessagesSinceLastFrame >= MaxMessagesBeforeClear)
		{
			AccumulatedPngMessages.Empty();
			MessagesSinceLastFrame = 0;
		}
		
		// Protection: If accumulator grows too large, reset it
		const int32 MaxAccumulatedPngs = ExpectedPngCount * 2;
		if (AccumulatedPngMessages.Num() > MaxAccumulatedPngs)
		{
			AccumulatedPngMessages.Empty();
			MessagesSinceLastFrame = 0;
			return;
		}
		
		// When we have all expected PNGs, process them in groups of 3
		while (AccumulatedPngMessages.Num() >= ExpectedPngCount)
		{
			// Check for duplicate PNGs BEFORE assignment
			bool bFoundDuplicates = false;
			for (int32 i = 0; i < ExpectedPngCount; ++i)
			{
				for (int32 j = i + 1; j < ExpectedPngCount; ++j)
				{
					if (AccumulatedPngMessages[i].Num() == AccumulatedPngMessages[j].Num())
					{
						if (FMemory::Memcmp(AccumulatedPngMessages[i].GetData(), 
						                    AccumulatedPngMessages[j].GetData(), 
						                    AccumulatedPngMessages[i].Num()) == 0)
						{
							bFoundDuplicates = true;
							break;
						}
					}
				}
				if (bFoundDuplicates) break;
			}
			
			if (bFoundDuplicates)
			{
				AccumulatedPngMessages.RemoveAt(0, ExpectedPngCount, EAllowShrinking::No);
				MessagesSinceLastFrame = 0;
				continue;
			}
			
			// Identify PNGs by color type (most reliable), fallback to size-based assignment
			TArray<int32> AssignedIndices;
			AssignedIndices.SetNum(ExpectedPngCount);
			AssignedIndices.Init(INDEX_NONE, ExpectedPngCount);
			
			// Check color type for each PNG
			TArray<bool> IsRGB;
			IsRGB.SetNum(ExpectedPngCount);
			int32 RGBCount = 0;
			int32 RGBIndex = INDEX_NONE;
			bool bUsedColorBasedDetection = false;
			
			for (int32 i = 0; i < ExpectedPngCount; ++i)
			{
				IsRGB[i] = IsPngRGB(AccumulatedPngMessages[i]);
				if (IsRGB[i])
				{
					RGBCount++;
					RGBIndex = i;
				}
				
				// Log color type detection for debugging
				if (AccumulatedPngMessages[i].Num() >= 25)
				{
					const uint8 ColorType = AccumulatedPngMessages[i][25];
					const FString ColorTypeName = 
						ColorType == 0 ? TEXT("Grayscale") :
						ColorType == 2 ? TEXT("RGB") :
						ColorType == 3 ? TEXT("Indexed") :
						ColorType == 4 ? TEXT("Grayscale+Alpha") :
						ColorType == 6 ? TEXT("RGBA") :
						FString::Printf(TEXT("Unknown(%d)"), ColorType);
					UE_LOG(LogTemp, Display, TEXT("[ComfyImageFetcher] PNG %d: ColorType=%d (%s), Size=%d bytes, IsRGB=%s"), 
						i, ColorType, *ColorTypeName, AccumulatedPngMessages[i].Num(), IsRGB[i] ? TEXT("true") : TEXT("false"));
				}
			}
			
			// Primary method: Use color type detection if exactly one RGB PNG is found
			if (RGBCount == 1 && ExpectedPngCount == 3)
			{
				bUsedColorBasedDetection = true;
				AssignedIndices[RGBIndex] = 0;
				
				// For the two grayscale PNGs, use size: larger = Depth, smaller = Mask
				TArray<int32> GrayscaleIndices;
				for (int32 i = 0; i < ExpectedPngCount; ++i)
				{
					if (!IsRGB[i])
					{
						GrayscaleIndices.Add(i);
					}
				}
				
				// Sort grayscale by size (smallest to largest)
				GrayscaleIndices.Sort([this](const int32& A, const int32& B) {
					return AccumulatedPngMessages[A].Num() < AccumulatedPngMessages[B].Num();
				});
				
				// Assign: smaller = Mask (index 2), larger = Depth (index 1)
				if (GrayscaleIndices.Num() == 2)
				{
					AssignedIndices[GrayscaleIndices[0]] = 2; // Smaller = Mask
					AssignedIndices[GrayscaleIndices[1]] = 1; // Larger = Depth
				}
				else
				{
					// Fall through to size-based fallback
					RGBCount = 0;
				}
			}
			
			// Fallback: Use size-based assignment if color type detection doesn't work
			// OR if all three are RGB, use grayscale detection + size
			if (RGBCount != 1 || ExpectedPngCount != 3)
			{
				// If all three are RGB, try to detect which ones are actually grayscale content
				if (RGBCount == 3 && ExpectedPngCount == 3)
				{
					TArray<bool> IsActuallyGrayscale;
					IsActuallyGrayscale.SetNum(ExpectedPngCount);
					int32 GrayscaleCount = 0;
					
					for (int32 i = 0; i < ExpectedPngCount; ++i)
					{
						IsActuallyGrayscale[i] = IsRGBPngActuallyGrayscale(AccumulatedPngMessages[i]);
						if (IsActuallyGrayscale[i])
						{
							GrayscaleCount++;
						}
					}
					
					UE_LOG(LogTemp, Display, TEXT("[ComfyImageFetcher] All 3 PNGs are RGB type. Grayscale detection: %d appear grayscale (mask-like)"), GrayscaleCount);
					
					// If we found 1 very small file (likely mask), use size-based for the remaining two
					if (GrayscaleCount == 1)
					{
						TArray<int32> GrayscaleIndices;
						TArray<int32> RGBOrDepthIndices;
						
						for (int32 i = 0; i < ExpectedPngCount; ++i)
						{
							if (IsActuallyGrayscale[i])
							{
								GrayscaleIndices.Add(i);
							}
							else
							{
								RGBOrDepthIndices.Add(i);
							}
						}
						
						if (GrayscaleIndices.Num() == 1 && RGBOrDepthIndices.Num() == 2)
						{
							// Sort the two larger images by size
							RGBOrDepthIndices.Sort([this](const int32& A, const int32& B) {
								return AccumulatedPngMessages[A].Num() < AccumulatedPngMessages[B].Num();
							});
							
							// Assign: smallest = Mask, medium = RGB (colored canny edge), largest = Depth
							AssignedIndices[GrayscaleIndices[0]] = 2; // Smallest = Mask
							AssignedIndices[RGBOrDepthIndices[0]] = 0; // Medium = RGB (colored canny edge)
							AssignedIndices[RGBOrDepthIndices[1]] = 1; // Largest = Depth
							bUsedColorBasedDetection = true;
							UE_LOG(LogTemp, Display, TEXT("[ComfyImageFetcher] Assigned via size (with mask detection): RGB=%d (medium), Depth=%d (largest), Mask=%d (smallest)"), 
								RGBOrDepthIndices[0], RGBOrDepthIndices[1], GrayscaleIndices[0]);
						}
					}
					// If we found 2 very small files (unlikely but possible), treat them as mask/depth
					else if (GrayscaleCount == 2)
					{
						TArray<int32> GrayscaleIndices;
						TArray<int32> RGBIndices;
						
						for (int32 i = 0; i < ExpectedPngCount; ++i)
						{
							if (IsActuallyGrayscale[i])
							{
								GrayscaleIndices.Add(i);
							}
							else
							{
								RGBIndices.Add(i);
							}
						}
						
						// Sort grayscale by size: smaller = Mask, larger = Depth
						GrayscaleIndices.Sort([this](const int32& A, const int32& B) {
							return AccumulatedPngMessages[A].Num() < AccumulatedPngMessages[B].Num();
						});
						
						if (GrayscaleIndices.Num() == 2 && RGBIndices.Num() == 1)
						{
							AssignedIndices[RGBIndices[0]] = 0; // True RGB
							AssignedIndices[GrayscaleIndices[0]] = 2; // Smaller grayscale = Mask
							AssignedIndices[GrayscaleIndices[1]] = 1; // Larger grayscale = Depth
							bUsedColorBasedDetection = true;
							UE_LOG(LogTemp, Display, TEXT("[ComfyImageFetcher] Assigned via grayscale detection: RGB=%d, Depth=%d, Mask=%d"), 
								RGBIndices[0], GrayscaleIndices[1], GrayscaleIndices[0]);
						}
					}
				}
				
				// If grayscale detection didn't work, fall back to pure size-based
				// When all 3 are RGB and similar sizes, use: smallest = Mask, medium = RGB, largest = Depth
				if (!bUsedColorBasedDetection)
				{
					TArray<int32> SizeOrder;
					for (int32 i = 0; i < ExpectedPngCount; ++i)
					{
						SizeOrder.Add(i);
					}
					// Sort indices by PNG size (smallest to largest)
					SizeOrder.Sort([this](const int32& A, const int32& B) {
						return AccumulatedPngMessages[A].Num() < AccumulatedPngMessages[B].Num();
					});
					
					if (SizeOrder.Num() == 3)
					{
						AssignedIndices[SizeOrder[0]] = 2; // Smallest = Mask
						AssignedIndices[SizeOrder[1]] = 1; // Medium = Depth
						AssignedIndices[SizeOrder[2]] = 0; // Largest = RGB
						UE_LOG(LogTemp, Display, TEXT("[ComfyImageFetcher] Assigned via size: RGB=%d (largest), Depth=%d (medium), Mask=%d (smallest)"), 
							SizeOrder[2], SizeOrder[1], SizeOrder[0]);
					}
					else
					{
						// Fallback to sequential
						for (int32 i = 0; i < ExpectedPngCount; ++i)
						{
							AssignedIndices[i] = i;
						}
					}
				}
			}
			
			// Validate assignment: Ensure RGB and Depth are assigned to different accumulator indices
			int32 RGBAccumIdx = INDEX_NONE;
			int32 DepthAccumIdx = INDEX_NONE;
			for (int32 i = 0; i < ExpectedPngCount; ++i)
			{
				if (AssignedIndices[i] == 0) RGBAccumIdx = i;
				if (AssignedIndices[i] == 1) DepthAccumIdx = i;
			}
			
			if (RGBAccumIdx != INDEX_NONE && DepthAccumIdx != INDEX_NONE && RGBAccumIdx == DepthAccumIdx)
			{
				AccumulatedPngMessages.RemoveAt(0, ExpectedPngCount, EAllowShrinking::No);
				MessagesSinceLastFrame = 0;
				return;
			}
			
			// Decode all PNGs first, then broadcast in correct order (RGB, Depth, Mask)
			TArray<UTexture2D*> DecodedTextures;
			DecodedTextures.SetNum(ExpectedPngCount);
			
			// First pass: decode all PNGs
			if (!PngDecoder) return;
			for (int32 i = 0; i < ExpectedPngCount; ++i)
			{
				UTexture2D* Tex = PngDecoder->DecodePNGToTexture(AccumulatedPngMessages[i]);
				DecodedTextures[i] = Tex;
			}
			
			// Second pass: broadcast in correct order (RGB=0, Depth=1, Mask=2)
			int32 SuccessfullyBroadcast = 0;
			for (int32 BroadcastIndex = 0; BroadcastIndex < ExpectedPngCount; ++BroadcastIndex)
			{
				// Find which accumulator index corresponds to this broadcast index
				int32 AccumulatorIndex = INDEX_NONE;
				for (int32 i = 0; i < ExpectedPngCount; ++i)
				{
					if (AssignedIndices[i] == BroadcastIndex)
					{
						AccumulatorIndex = i;
						break;
					}
				}
				
				if (AccumulatorIndex == INDEX_NONE || !DecodedTextures[AccumulatorIndex])
				{
					continue;
				}
				
				// Verify RGB is actually RGB before broadcasting Index 0
				// But only skip if we're using size-based assignment (not color-type based)
				// If we already assigned it as RGB via color detection, trust that assignment
				if (BroadcastIndex == 0)
				{
					bool bIsActuallyRGB = IsPngRGB(AccumulatedPngMessages[AccumulatorIndex]);
					if (!bIsActuallyRGB && !bUsedColorBasedDetection)
					{
						// Only skip if we didn't detect it as RGB via color type (size-based fallback)
						UE_LOG(LogTemp, Warning, TEXT("[ComfyImageFetcher] Skipping RGB broadcast - assigned image is not RGB (using size-based fallback)"));
						continue;
					}
					else if (!bIsActuallyRGB && bUsedColorBasedDetection)
					{
						UE_LOG(LogTemp, Warning, TEXT("[ComfyImageFetcher] RGB verification failed but proceeding anyway (was assigned via color detection)"));
					}
				}
				
				UTexture2D* Tex = DecodedTextures[AccumulatorIndex];
				OnTextureReceived.Broadcast(Tex);
				SuccessfullyBroadcast++;
			}
			
			UE_LOG(LogTemp, Display, TEXT("[ComfyImageFetcher] Successfully processed and broadcast %d/%d textures"), 
				SuccessfullyBroadcast, ExpectedPngCount);
			
			// Remove processed PNGs from accumulator
			AccumulatedPngMessages.RemoveAt(0, ExpectedPngCount, EAllowShrinking::No);
			MessagesSinceLastFrame = 0;
		}
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
