#include "ComfyStream/ComfyImageSender.h"
#include "IWebSocket.h"
#include "WebSocketsModule.h"
#include "Async/Async.h"
#include "Modules/ModuleManager.h"

UComfyImageSender::UComfyImageSender()
{
	CurrentChannel = 2;
	WebSocketPort = 8001;
}

void UComfyImageSender::SendImage(const TArray<uint8>& ImageData)
{
	if (ImageData.Num() == 0) return;
	if (CurrentServerURL.IsEmpty()) return;

	PendingImageData = ImageData;
	bPendingSend = true;
	EnsureConnection();
}

void UComfyImageSender::ConfigureAndSend(const FString& ServerURL, int32 ChannelNumber, const TArray<uint8>& ImageData)
{
	if (ImageData.Num() == 0) return;
	if (ServerURL.IsEmpty()) return;

	// If URL or channel changed, close existing connection
	if (WebSocket.IsValid() && (CurrentServerURL != ServerURL || CurrentChannel != ChannelNumber))
	{
		if (WebSocket->IsConnected()) WebSocket->Close();
		WebSocket.Reset();
	}

	CurrentServerURL = ServerURL;
	CurrentChannel = ChannelNumber;
	PendingImageData = ImageData;
	bPendingSend = true;
	EnsureConnection();
}

void UComfyImageSender::EnsureConnection()
{
	if (!bPendingSend || PendingImageData.Num() == 0) return;

	if (WebSocket.IsValid() && WebSocket->IsConnected())
	{
		SendPendingImage();
		return;
	}

	// Create new connection if needed
	if (!WebSocket.IsValid())
	{
		FModuleManager::LoadModuleChecked<FWebSocketsModule>(TEXT("WebSockets"));
		FString WebSocketURL = BuildWebSocketURL(CurrentServerURL, CurrentChannel);

		WebSocket = FWebSocketsModule::Get().CreateWebSocket(WebSocketURL);

		WebSocket->OnConnected().AddUObject(this, &UComfyImageSender::OnWebSocketConnected);
		WebSocket->OnConnectionError().AddUObject(this, &UComfyImageSender::OnWebSocketConnectionError);
		WebSocket->OnClosed().AddUObject(this, &UComfyImageSender::OnWebSocketClosed);

		WebSocket->Connect();
	}
}

void UComfyImageSender::OnWebSocketConnected()
{
	AsyncTask(ENamedThreads::GameThread, [this]()
	{
		if (bPendingSend && PendingImageData.Num() > 0 && WebSocket.IsValid() && WebSocket->IsConnected())
		{
			SendPendingImage();
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[ComfyImageSender] Connected but cannot send - pending=%d data=%d ws=%d"), 
				bPendingSend ? 1 : 0, PendingImageData.Num(), WebSocket.IsValid() && WebSocket->IsConnected() ? 1 : 0);
		}
	});
}

void UComfyImageSender::OnWebSocketConnectionError(const FString& Error)
{
	bPendingSend = false;
	PendingImageData.Empty();
	WebSocket.Reset();
}

void UComfyImageSender::OnWebSocketClosed(int32 StatusCode, const FString& Reason, bool bWasClean)
{
	WebSocket.Reset();
}

void UComfyImageSender::Disconnect()
{
	bPendingSend = false;
	PendingImageData.Empty();
	if (WebSocket.IsValid())
	{
		if (WebSocket->IsConnected()) WebSocket->Close();
		WebSocket.Reset();
	}
}

void UComfyImageSender::SendPendingImage()
{
	if (!WebSocket.IsValid() || !WebSocket->IsConnected() || PendingImageData.Num() == 0)
	{
		return;
	}

	// ComfyUI WebViewer expects 8-byte header [1, 2] (big-endian) before raw PNG/JPEG data
	TArray<uint8> MessageWithHeader;
	MessageWithHeader.SetNum(8 + PendingImageData.Num());
	MessageWithHeader[0] = 0; MessageWithHeader[1] = 0; MessageWithHeader[2] = 0; MessageWithHeader[3] = 1;  // uint32 1
	MessageWithHeader[4] = 0; MessageWithHeader[5] = 0; MessageWithHeader[6] = 0; MessageWithHeader[7] = 2;  // uint32 2
	FMemory::Memcpy(MessageWithHeader.GetData() + 8, PendingImageData.GetData(), PendingImageData.Num());

	WebSocket->Send(MessageWithHeader.GetData(), MessageWithHeader.Num(), true);

	bPendingSend = false;
	PendingImageData.Empty();
}

FString UComfyImageSender::BuildWebSocketURL(const FString& ServerURL, int32 ChannelNumber) const
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
