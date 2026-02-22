#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "ComfyImageSender.generated.h"

class IWebSocket;

/**
 * Sends images to ComfyUI over WebSocket on a specified channel.
 * Used by SplatCreatorSubsystem to send reference images when PLY files change.
 */
UCLASS()
class REALITYSTREAM_API UComfyImageSender : public UObject
{
	GENERATED_BODY()

public:
	UComfyImageSender();

	/** Send an image (PNG or JPG bytes) to ComfyUI on the configured channel. */
	UFUNCTION(BlueprintCallable, Category = "ComfyUI")
	void SendImage(const TArray<uint8>& ImageData);

	/** Configure connection and ensure WebSocket is connected before sending. */
	void ConfigureAndSend(const FString& ServerURL, int32 ChannelNumber, const TArray<uint8>& ImageData);

	/** Disconnect and release the WebSocket. */
	void Disconnect();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ComfyUI")
	int32 WebSocketPort = 8001;

private:
	TSharedPtr<IWebSocket> WebSocket;
	FString CurrentServerURL;
	int32 CurrentChannel = 2;
	TArray<uint8> PendingImageData;
	bool bPendingSend = false;

	void EnsureConnection();
	void OnWebSocketConnected();
	void OnWebSocketConnectionError(const FString& Error);
	void OnWebSocketClosed(int32 StatusCode, const FString& Reason, bool bWasClean);
	void SendPendingImage();
	FString BuildWebSocketURL(const FString& ServerURL, int32 ChannelNumber) const;
};
