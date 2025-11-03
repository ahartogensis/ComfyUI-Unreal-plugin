#pragma once

#include "CoreMinimal.h"
#include "ComfyStreamTypes.h"
#include "ComfyImageFetcher.generated.h"

class UComfyPngDecoder;
class IWebSocket;

//Handles connection between ComfyUI and Unreal Engine 5.6 thourgh websockets
UCLASS()
class REALITYSTREAM_API UComfyImageFetcher : public UObject
{
	GENERATED_BODY()

public:
	UComfyImageFetcher();

	UPROPERTY(BlueprintAssignable)
	FOnTextureReceived OnTextureReceived;

	UPROPERTY(BlueprintAssignable)
	FOnConnectionStatusChanged OnConnectionStatusChanged;

	UPROPERTY(BlueprintAssignable)
	FOnError OnError;

	UFUNCTION(BlueprintCallable)
	void StartPolling(const FString& ServerURL, int32 ChannelNumber = 1);

	UFUNCTION(BlueprintCallable)
	void StopPolling();

	UFUNCTION(BlueprintCallable)
	bool IsPolling() const;

	//default websocket port is 8001 
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 WebSocketPort = 8001;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FComfyStreamConfig Config;

private:
	EComfyConnectionStatus ConnectionStatus = EComfyConnectionStatus::Disconnected;
	//Decodes png files 
	UPROPERTY() UComfyPngDecoder* PngDecoder = nullptr;

	//Websocket instance on channel 1 
	TSharedPtr<IWebSocket> WebSocket;
	bool bIsPolling = false;
	int32 CurrentChannel = 1;
	FString CurrentServerURL;

	//Preventing cross stream image corruption by creating member 
	//variables for chunked image reception
	TArray<uint8> ChunkBuffer;
	bool bReceivingChunks = false;

	//WebSocket events (may be called from worker threads)
	void OnWebSocketConnected();
	void OnWebSocketConnectionError(const FString& Error);
	void OnWebSocketClosed(int32 StatusCode, const FString& Reason, bool bWasClean);
	void OnWebSocketMessage(const void* Data, SIZE_T Size, SIZE_T BytesRemaining);
	void OnWebSocketMessageSent(const FString& MessageString);

	//Game thread implementations of WebSocket callbacks
	void OnWebSocketConnected_GameThread();
	void OnWebSocketConnectionError_GameThread(const FString& Error);
	void OnWebSocketClosed_GameThread(int32 StatusCode, const FString& Reason, bool bWasClean);
	void OnWebSocketMessage_GameThread(const void* Data, SIZE_T Size, SIZE_T BytesRemaining);

	void ProcessImageData(const TArray<uint8>& Data);
	void SetConnectionStatus(EComfyConnectionStatus NewStatus);
	FString BuildWebSocketURL(const FString& ServerURL, int32 ChannelNumber);
	TArray<TArray<uint8>> SplitPNGStream(const TArray<uint8>& Buffer);
};
