#pragma once

#include "CoreMinimal.h"
#include "Engine/Texture2D.h"
#include "ComfyStreamTypes.generated.h"

// Event for when a new texture is received from ComfyUI
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTextureReceived, UTexture2D*, Texture);

// Event for when a connection status changes
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnConnectionStatusChanged, bool, bConnected);

// Event for when an error occurs
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnError, const FString&, ErrorMessage);

//one channel, segmentation 
UENUM(BlueprintType)
enum class EComfyChannel : uint8
{
	Segmentation	UMETA(DisplayName = "Segmentation Channel")
};


// Connection status
UENUM(BlueprintType)
enum class EComfyConnectionStatus : uint8
{
	Disconnected	UMETA(DisplayName = "Disconnected"),
	Connecting		UMETA(DisplayName = "Connecting"),
	Connected		UMETA(DisplayName = "Connected"),
	Error			UMETA(DisplayName = "Error")
};

// Configuration structure for ComfyUI connection
USTRUCT(BlueprintType)
struct FComfyStreamConfig
{
	GENERATED_BODY()

	// Base URL for ComfyUI WebViewer WebSocket server
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ComfyUI")
	FString ServerURL = TEXT("ws://localhost:8001");

	// Channel number for this stream
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ComfyUI")
	int32 ChannelNumber = 1;

	// Channel type
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ComfyUI")
	EComfyChannel ChannelType = EComfyChannel::Segmentation;

	// Keep-alive ping interval in seconds
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ComfyUI")
	float PingInterval = 20.0f;

	// Auto-reconnect on disconnect
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ComfyUI")
	bool bAutoReconnect = true;

	// Reconnect delay in seconds
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ComfyUI")
	float ReconnectDelay = 5.0f;

	// Enable smooth interpolation between texture updates
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ComfyUI|Interpolation")
	bool bEnableLerpSmoothing = false;

	//Lerp speed (alpha per second). Higher = faster blend.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="ComfyUI|Interpolation", meta=(ClampMin="0.01", ClampMax="20.0"))
	float LerpSpeed = 5.0f;

	// Threshold for considering lerp complete (0-1)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ComfyUI|Interpolation", meta = (ClampMin = "0.001", ClampMax = "1.0"))
	float LerpThreshold = 0.01f;

	FComfyStreamConfig()
	{
		ServerURL = TEXT("ws://localhost:8001");
		ChannelNumber = 1;
		ChannelType = EComfyChannel::Segmentation;
		PingInterval = 20.0f;
		bAutoReconnect = true;
		ReconnectDelay = 5.0f;

		// Interpolation defaults
		bEnableLerpSmoothing = false;
		LerpSpeed = 5.0f;
		LerpThreshold = 0.01f;
	}
};

// Structure for managing lerp-based texture transitions
USTRUCT(BlueprintType)
struct FComfyLerpState
{
	GENERATED_BODY()

	// Current texture being displayed
	UPROPERTY()
	UTexture2D* CurrentTexture = nullptr;

	// Target texture to lerp towards
	UPROPERTY()
	UTexture2D* TargetTexture = nullptr;

	// Current lerp alpha (0-1)
	UPROPERTY()
	float CurrentAlpha = 1.0f;

	// Whether we're currently lerping
	UPROPERTY()
	bool bIsLerping = false;

	FComfyLerpState()
		: CurrentTexture(nullptr)
		, TargetTexture(nullptr)
		, CurrentAlpha(1.0f)
		, bIsLerping(false)
	{
	}

	// Start a new lerp transition
	void StartLerp(UTexture2D* NewTarget)
	{
		if (NewTarget && NewTarget != TargetTexture)
		{
			TargetTexture = NewTarget;
			CurrentAlpha = 0.0f;
			bIsLerping = true;
		}
	}

	// Update lerp state (call in Tick)
	void UpdateLerp(float DeltaTime, float LerpSpeed, float Threshold)
	{
		if (bIsLerping && TargetTexture)
		{
			CurrentAlpha = FMath::Clamp(CurrentAlpha + (DeltaTime * LerpSpeed), 0.0f, 1.0f);

			// Check if lerp is complete
			if (CurrentAlpha >= 1.0f - Threshold)
			{
				CurrentAlpha = 1.0f;
				CurrentTexture = TargetTexture;
				bIsLerping = false;
			}
		}
	}

	// Get the current alpha for material blending
	float GetBlendAlpha() const
	{
		return CurrentAlpha;
	}

	// Check if we have both textures for blending
	bool CanBlend() const
	{
		return bIsLerping && CurrentTexture != nullptr && TargetTexture != nullptr;
	}
};
