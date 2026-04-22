#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/StaticMeshComponent.h"
#include "ComfyStreamComponent.h"
#include "ComfyFrameBuffer.h"
#include "ComfyStreamActor.generated.h"

// Holds an interpolated frame plus how long it should remain active
USTRUCT(BlueprintType)
struct FInterpolatedFrame
{
	GENERATED_BODY()

	UPROPERTY()
	FComfyFrame Frame;

	UPROPERTY()
	float TimeRemaining = 0.0f;
};

// Tracks spawned actor state for lerp/fade handling
USTRUCT(BlueprintType)
struct FActorLerpData
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<AActor> Actor = nullptr;

	UPROPERTY()
	FVector Position = FVector::ZeroVector;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> Material = nullptr;

	UPROPERTY()
	float LerpAlpha = 1.0f;

	UPROPERTY()
	bool bIsLerping = false;

	UPROPERTY()
	float OpacityAlpha = 1.0f;

	UPROPERTY()
	bool bIsFadingOut = false;

	// Timer handles are not UPROPERTY types and do not need GC tracking
	FTimerHandle DestroyTimer;
	FTimerHandle LerpTimer;
};

// receives 3 texture maps from ComfyUI and applies to a material
UCLASS()
class REALITYSTREAM_API AComfyStreamActor : public AActor
{
	GENERATED_BODY()

public:
	AComfyStreamActor();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaTime) override;

public:
	// visualize streamed textures on display mesh
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ComfyStream")
	TObjectPtr<UStaticMeshComponent> DisplayMesh = nullptr;

	// Comfy stream component
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ComfyStream")
	TObjectPtr<UComfyStreamComponent> ComfyStreamComponent = nullptr;

	// base material made with parameters RGB, Mask, and Depth
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ComfyStream")
	TObjectPtr<UMaterialInterface> BaseMaterial = nullptr;

	// network config for segmentation channel
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ComfyStream")
	FComfyStreamConfig SegmentationChannelConfig;

	// Actor lifetime and lerp settings
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Actor Spawning")
	float ActorLifetimeSeconds = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Actor Spawning")
	float LerpSpeed = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Actor Spawning")
	float LocationThreshold = 50.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Actor Spawning")
	float FadeOutDuration = 0.5f;

	// Delay before applying new frames (for staggered multi-actor setups). 0 = apply immediately.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ComfyStream", meta = (ClampMin = "0.0", Tooltip = "Seconds to wait before applying a new frame. Use 0 for first actor, higher values for 2nd/3rd actors for delayed change."))
	float FrameApplyDelaySeconds = 0.0f;

	// Frame interpolation settings
	// Generates intermediate blended frames between consecutive frames for smoother transitions
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Frame Interpolation", meta = (Tooltip = "Enable frame interpolation to generate smooth transitions between frames"))
	bool bEnableInterpolation = true;

	// Number of intermediate frames to generate between each pair of frames
	// Higher values = smoother transitions but more CPU/GPU cost
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Frame Interpolation", meta = (EditCondition = "bEnableInterpolation", ClampMin = "1", ClampMax = "60", Tooltip = "Number of interpolated frames between each real frame. Higher = smoother but more expensive."))
	int32 NumInterpolatedFrames = 20;

	// Total duration to display all interpolated frames before showing the final frame
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Frame Interpolation", meta = (EditCondition = "bEnableInterpolation", ClampMin = "0.1", Tooltip = "Time in seconds to display all interpolated frames"))
	float InterpolationDuration = 1.0f;

	// Use smooth easing function instead of linear interpolation for more natural motion
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Frame Interpolation", meta = (EditCondition = "bEnableInterpolation", Tooltip = "Use smooth easing (ease-in-out) for more natural transitions"))
	bool bUseSmoothEasing = true;

	// Blueprint events
	UFUNCTION(BlueprintImplementableEvent)
	void OnTextureReceived(UTexture2D* Texture);

	UFUNCTION(BlueprintImplementableEvent)
	void OnConnectionStatusChanged(bool bConnected);

	UFUNCTION(BlueprintImplementableEvent)
	void OnError(const FString& ErrorMessage);

	// connect and disconnect functions
	UFUNCTION(BlueprintCallable)
	void ConnectSegmentationChannel();

	UFUNCTION(BlueprintCallable)
	void DisconnectAll();

private:
	// External helper objects
	UPROPERTY()
	TObjectPtr<UComfyFrameBuffer> FrameBuffer = nullptr;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> DynMat = nullptr;

	// Last known complete frame
	UPROPERTY()
	FComfyFrame LatestFrame;

	// Last frame that was actually applied to an actor (to prevent duplicate updates)
	UPROPERTY()
	FComfyFrame LastAppliedFrame;

	// Previous frame for interpolation
	UPROPERTY()
	FComfyFrame PreviousFrame;

	// Interpolation queue
	UPROPERTY()
	TArray<FInterpolatedFrame> InterpolationQueue;

	UPROPERTY()
	float InterpolationTimer = 0.0f;

	// Delayed frame apply (when FrameApplyDelaySeconds > 0)
	UPROPERTY()
	FComfyFrame PendingDelayedFrame;

	FTimerHandle DelayedApplyTimer;

	// sequence index for textures
	UPROPERTY()
	int32 SeqIndex = 0;

	// Track which channels have been received in the current frame
	UPROPERTY()
	bool bHasRGB = false;

	UPROPERTY()
	bool bHasDepth = false;

	UPROPERTY()
	bool bHasMask = false;

	// Spawned actors from textures
	UPROPERTY()
	TArray<TObjectPtr<AActor>> SpawnedTextureActors;

	// Track actor positions and lerp states
	UPROPERTY()
	TArray<FActorLerpData> ActorData;

	// Internal helper functions (must be UFUNCTION for dynamic delegate binding)
	UFUNCTION()
	void HandleStreamTexture(UTexture2D* Texture);

	UFUNCTION()
	void HandleConnectionChanged(bool bConnected);

	UFUNCTION()
	void HandleStreamError(const FString& Error);

	// When FrameBuffer emits a complete triplet
	UFUNCTION()
	void HandleFullFrame(const FComfyFrame& Frame);

	// Timer callback for delayed frame apply
	UFUNCTION()
	void ApplyDelayedFrame();

	// Apply textures to the material
	void ApplyTexturesToMaterial(const FComfyFrame& Frame);

	// Shared apply path (immediate or from delayed callback)
	void ApplyNewFrame(const FComfyFrame& Frame);

	// Spawn actor at specified world position
	void SpawnTextureActor(const FComfyFrame& Frame, const FVector& WorldPosition);

	// Find existing actor at location or spawn new
	AActor* FindOrSpawnActorAtLocation(const FVector& WorldPosition);

	// Update material lerp
	void UpdateActorLerp(FActorLerpData& ActorDataEntry, const FComfyFrame& Frame, float DeltaTime);

	// Destroy actor after delay
	void DestroyActorDelayed(AActor* Actor);

	// Scale actor to match texture dimensions
	void ScaleActorToTextureSize(AActor* Actor, const FComfyFrame& Frame);

	// Frame interpolation functions
	void GenerateInterpolatedFrames(const FComfyFrame& FromFrame, const FComfyFrame& ToFrame);
	UTexture2D* BlendTextures(UTexture2D* TextureA, UTexture2D* TextureB, float Alpha);
	void ApplyInterpolatedFrame(const FComfyFrame& Frame);
};