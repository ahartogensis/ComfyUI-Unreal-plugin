#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ComfyStreamTypes.h"
#include "ComfyStreamComponent.generated.h"

class UComfyImageFetcher;
class UComfyPngDecoder;

//Connects to one ComfyUI websocket channel and sets up texture broadcasting 
//pairs textures as they arrive 
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class REALITYSTREAM_API UComfyStreamComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UComfyStreamComponent();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float Delta, ELevelTick, FActorComponentTickFunction*) override;

public:
	//Events
	UPROPERTY(BlueprintAssignable, Category = "ComfyStream")
	FOnTextureReceived OnTextureReceived;

	UPROPERTY(BlueprintAssignable, Category = "ComfyStream")
	FOnConnectionStatusChanged OnConnectionStatusChanged;

	UPROPERTY(BlueprintAssignable, Category = "ComfyStream")
	FOnError OnError;

	//before BeginPlay, connect configs 
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="ComfyStream")
	FComfyStreamConfig StreamConfig;

	//Intrinsics knobs for DepthAnything webcam
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Reconstruction")
	float FocalScale = 1.2f; 

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Reconstruction")
	float DepthScaleUnits = 500.0f;

	//Connection control
	UFUNCTION(BlueprintCallable) void Connect();
	UFUNCTION(BlueprintCallable) void Disconnect();
	UFUNCTION(BlueprintCallable) bool IsConnected() const;
	UFUNCTION(BlueprintCallable) EComfyConnectionStatus GetConnectionStatus() const;

private:
	UPROPERTY() UComfyImageFetcher* ImageFetcher = nullptr;
	UPROPERTY() UComfyPngDecoder*   PngDecoder   = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="ComfyStream", meta=(AllowPrivateAccess="true"))
	EComfyConnectionStatus ConnectionStatus = EComfyConnectionStatus::Disconnected;

	FTimerHandle ReconnectTimer;

	UFUNCTION() void OnTextureReceivedInternal(UTexture2D* Texture);
	UFUNCTION() void OnConnectionStatusChangedInternal(bool bConnected);
	UFUNCTION() void OnErrorInternal(const FString& ErrorMessage);

	void AttemptReconnect();
	void UpdateLerpTransition(float DeltaTime);

	//Per-channel lerp states
	FComfyLerpState RGBLerpState;
	FComfyLerpState DepthLerpState;
	FComfyLerpState MaskLerpState;
};
