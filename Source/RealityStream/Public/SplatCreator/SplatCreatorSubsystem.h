#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "SplatCreatorSubsystem.generated.h"

class UInstancedStaticMeshComponent;
class UPrimitiveComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UTexture2D;
class UCanvas;

// Delegate for when splat bounds are updated
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSplatBoundsUpdated, FBox, NewBounds);

// Enum for bobbing direction
UENUM(BlueprintType)
enum class EBobbingDirection : uint8
{
	None,
	Up,
	Down,
	Left,
	Right
};


//Preview Image
USTRUCT(BlueprintType)
struct FImagePreviewTarget
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<UPrimitiveComponent> PlaneComponent = nullptr;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> Material = nullptr;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> MID = nullptr;

	UPROPERTY()
	FName TargetName = NAME_None;
};

UCLASS(BlueprintType)
class REALITYSTREAM_API USplatCreatorSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// Start the point cloud system - CALL THIS FROM BLUEPRINT to initialize
	// In Blueprint: Get Splat Creator Subsystem -> Start Point Cloud System
	UFUNCTION(BlueprintCallable, Category = "SplatCreator")
	void StartPointCloudSystem();

	// Get the full bounding box of the current splat
	UFUNCTION(BlueprintCallable, Category = "SplatCreator")
	FBox GetSplatBounds() const;

	// Get dense point regions (points with high density) for object placement
	// Returns positions of points that are in dense areas (small sphere sizes indicate density)
	// DensityThreshold: maximum sphere size to consider as dense (default 0.15, where 0.1=dense, 0.3=sparse)
	UFUNCTION(BlueprintCallable, Category = "SplatCreator")
	TArray<FVector> GetDensePointRegions(float DensityThreshold = 0.15f) const;

	// Check if a position is too close to any splat point (for intersection avoidance)
	// Returns true if the position is within MinDistance of any splat point
	// bCheckHorizontalOnly: if true, only checks X,Y distance (ignores Z), useful when objects and splats are at different heights
	UFUNCTION(BlueprintCallable, Category = "SplatCreator")
	bool IsPositionTooCloseToSplatPoints(const FVector& Position, float MinDistance = 50.0f, bool bCheckHorizontalOnly = true) const;

	// Handle OSC message to control splat bobbing animation
	// Message should contain "up", "down", "left", or "right" to control bobbing direction
	// Call this from BP_OSC blueprint when receiving OSC messages
	UFUNCTION(BlueprintCallable, Category = "SplatCreator")
	void HandleOSCMessage(const FString& Message);

	// Cycle to the next splat. Called by ComfyStreamActor when bCycleSplatOnComfyFrame is true and a new frame is received.
	UFUNCTION(BlueprintCallable, Category = "SplatCreator")
	void CycleToNextSplat();

	// Register or update an image preview target for a specific blueprint/plane.
	// Each target name stores its own plane, material, and MID.
	UFUNCTION(BlueprintCallable, Category = "SplatCreator|ComfyUI")
	void SetImagePreviewTarget(FName TargetName, UPrimitiveComponent* PlaneComponent, UMaterialInterface* Material);

	UFUNCTION(BlueprintCallable, Category = "SplatCreator|ComfyUI")
	void RemoveImagePreviewTarget(FName TargetName);
	
	/** If true, send the current splat's image to ComfyUI when loading. If false, send the next splat's image (default). */
	UFUNCTION(BlueprintCallable, Category = "SplatCreator|ComfyUI")
	void SetSendCurrentSplatImageToComfyUI(bool bSendCurrent);

	/** If true, send current splat's image. If false, send next splat's image (for ComfyUI to process ahead of cycle). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SplatCreator|ComfyUI", meta = (EditCondition = "bSendImageToComfyUIOnPlyChange"))
	bool bSendCurrentSplatImageToComfyUI = false;

	UPROPERTY(EditAnywhere, Category = "SplatCreator|ComfyUI", meta = (EditCondition = "bSendImageToComfyUIOnPlyChange"))
	TSoftObjectPtr<UMaterialInterface> ImagePreviewMaterialAsset = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/_GENERATED/Materials/M_image.M_image")));

	/** If true, overlay text (e.g. filename) onto the preview image using a canvas render target */
	UPROPERTY(EditAnywhere, Category = "SplatCreator|ComfyUI|Image Preview")
	bool bAddTextToImagePreview = true;

	/** Text to display on the preview. Use {0} for filename, {1} for index (e.g. "Frame: {0} ({1}/{2})") */
	UPROPERTY(EditAnywhere, Category = "SplatCreator|ComfyUI|Image Preview", meta = (EditCondition = "bAddTextToImagePreview"))
	FString ImagePreviewTextFormat = TEXT("{0}");

	/** Position of the text overlay in pixels (X, Y from top-left) */
	UPROPERTY(EditAnywhere, Category = "SplatCreator|ComfyUI|Image Preview", meta = (EditCondition = "bAddTextToImagePreview"))
	FVector2D ImagePreviewTextPosition = FVector2D(10, 10);

	/** Scale of the text (3.0 = default size) */
	UPROPERTY(EditAnywhere, Category = "SplatCreator|ComfyUI|Image Preview", meta = (EditCondition = "bAddTextToImagePreview"))
	float ImagePreviewTextScale = 3.0f;

	/** If true, image preview starts at full opacity and fades to 0 when cycle changes. M_image must have an "Opacity" scalar parameter. */
	UPROPERTY(EditAnywhere, Category = "SplatCreator|ComfyUI|Image Preview")
	bool bFadeImagePreviewOpacity = true;

	/** Seconds at full opacity before fade starts */
	UPROPERTY(EditAnywhere, Category = "SplatCreator|ComfyUI|Image Preview", meta = (EditCondition = "bFadeImagePreviewOpacity", ClampMin = "0.0", ClampMax = "60.0"))
	float ImagePreviewOpacityHoldDuration = 4.0f;

	/** Duration in seconds for opacity to fade from 100% to 0% after the hold */
	UPROPERTY(EditAnywhere, Category = "SplatCreator|ComfyUI|Image Preview", meta = (EditCondition = "bFadeImagePreviewOpacity", ClampMin = "0.1", ClampMax = "60.0"))
	float ImagePreviewOpacityFadeDuration = 4.0f;

	// Event broadcast when splat bounds are updated
	UPROPERTY(BlueprintAssignable, Category = "SplatCreator")
	FOnSplatBoundsUpdated OnSplatBoundsUpdated;

	// --- Plane-to-3D Material Morph (GPU-based, flat->3D transition) ---
	/** Material with World Position Offset for plane-to-3D. Must have MorphProgress scalar param and use PerInstanceCustomData index 4 for Y offset (negated for 180° yaw). */
	UPROPERTY(EditAnywhere, Category = "SplatCreator|Plane Morph")
	FSoftObjectPath PlaneMorphMaterialPath = FSoftObjectPath(TEXT("/Game/_GENERATED/Materials/M_SplatMorph.M_SplatMorph"));

	/** Duration of the plane-to-3D morph in seconds */
	UPROPERTY(EditAnywhere, Category = "SplatCreator|Plane Morph", meta = (ClampMin = "0.1", ClampMax = "5.0"))
	float PlaneMorphDuration = 1.5f;

	/** World Y of the flat plane */
	UPROPERTY(EditAnywhere, Category = "SplatCreator|Plane Morph")
	float PlaneMorphY = -160.0f;

	/** If true, use reversed culling so points are visible when camera is inside the cloud. Prefer enabling Two Sided on the material instead. */
	UPROPERTY(EditAnywhere, Category = "SplatCreator|Rendering")
	bool bVisibleFromInside = true;

	/** Interval in seconds between automatic PLY cycle changes */
	UPROPERTY(EditAnywhere, Category = "SplatCreator|Cycle", meta = (EditCondition = "!bCycleSplatOnComfyFrame", ClampMin = "1.0", ClampMax = "300.0"))
	float CycleIntervalSeconds = 16.0f;

	/** Delay in seconds after cycle change before morphing starts */
	UPROPERTY(EditAnywhere, Category = "SplatCreator|Cycle", meta = (ClampMin = "0.0", ClampMax = "60.0"))
	float MorphStartDelaySeconds = 8.0f;

private:
	bool bIsInitialized = false;

	// PLY file management
	TArray<FString> PlyFiles;
	int32 CurrentFileIndex = 0;
	int32 NextFileIndex = -1;
	FTimerHandle CycleTimer;

	// Point cloud rendering
	UPROPERTY(Transient)
	TObjectPtr<AActor> CurrentPointCloudActor = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UInstancedStaticMeshComponent> PointCloudComponent = nullptr;

	UPROPERTY(Transient)
	TArray<FImagePreviewTarget> ImagePreviewTargets;

	// Plane-to-3D material morph (GPU-based)
	FTimerHandle PlaneMorphTimer;
	FTimerHandle MorphStartDelayTimer;
	bool bIsPlaneMorphing = false;
	float PlaneMorphStartTime = 0.0f;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> SplatMorphMID = nullptr;

	TArray<float> SphereSizes;

	// Current splat bounding box
	FBox CurrentSplatBounds;
	bool bHasSplatBounds = false;

	// Current point positions
	TArray<FVector> CurrentPointPositions;

	// Bobbing animation system
	EBobbingDirection CurrentBobbingDirection = EBobbingDirection::None;
	FTimerHandle BobbingTimer;
	bool bIsBobbing = false;
	float BobbingTime = 0.0f;
	float BaseBobbingSpeed = 2.0f;
	float BobbingSpeedMultiplier = 1.0f;
	float BobbingAmplitude = 20.0f;
	TArray<FVector> BasePointPositions;

	// Scaling system
	float SplatScaleMultiplier = 1.0f;

	// ComfyUI image send
	UPROPERTY(EditAnywhere, Category = "SplatCreator|ComfyUI")
	bool bSendImageToComfyUIOnPlyChange = true;

	// When true, splat changes when a new frame is received from ComfyUI instead of on a timer
	UPROPERTY(EditAnywhere, Category = "SplatCreator|ComfyUI", meta = (EditCondition = "bSendImageToComfyUIOnPlyChange"))
	bool bCycleSplatOnComfyFrame = false;

	UPROPERTY(EditAnywhere, Category = "SplatCreator|ComfyUI", meta = (EditCondition = "bSendImageToComfyUIOnPlyChange"))
	FString ComfyUIWebSocketHost = TEXT("127.0.0.1");

	UPROPERTY(EditAnywhere, Category = "SplatCreator|ComfyUI", meta = (EditCondition = "bSendImageToComfyUIOnPlyChange"))
	int32 ComfyUIImageChannel = 2;

	UPROPERTY()
	TObjectPtr<class UComfyImageSender> ComfyImageSender = nullptr;

	UPROPERTY()
	TObjectPtr<class UComfyPngDecoder> ImageDecoder = nullptr;

	// Text overlay on image
	UPROPERTY()
	TObjectPtr<class UCanvasRenderTarget2D> CanvasRenderTargetForText = nullptr;

	UPROPERTY()
	TObjectPtr<UTexture2D> TextOverlaySourceTexture = nullptr;

	FString TextOverlayDisplayText;

	UFUNCTION()
	void OnCanvasRenderTargetUpdate(UCanvas* Canvas, int32 Width, int32 Height);

	FTimerHandle ImagePreviewOpacityFadeTimer;
	float ImagePreviewOpacityFadeStartTime = 0.0f;
	void UpdateImagePreviewOpacityFade();

	// Random movement system
	FTimerHandle RandomMovementTimer;
	bool bIsRandomMoving = false;
	float BaseRandomMovementSpeed = 50.0f;
	float RandomMovementSpeedMultiplier = 1.0f;
	float RandomMovementRadius = 100.0f;
	TArray<FVector> RandomVelocities;
	TArray<FVector> RandomTargets;
	TArray<FVector> RandomCurrentPositions;
	float RandomChangeInterval = 2.0f;
	float RandomChangeTimer = 0.0f;

	// Smooth interpolation system for stopping
	bool bIsInterpolatingToBase = false;
	float InterpolationTime = 0.0f;
	float InterpolationDuration = 1.0f;
	TArray<FVector> InterpolationStartPositions;

	// Functions
	void ScanForPLYFiles();
	void CycleToNextPLY();
	void LoadPLYFile(const FString& PLYPath);
	bool ParsePLYFile(const FString& PLYPath, TArray<FVector>& OutPositions, TArray<FColor>& OutColors);
	void SamplePointsUniformly(const TArray<FVector>& InPositions, const TArray<FColor>& InColors, TArray<FVector>& OutPositions, TArray<FColor>& OutColors);
	void CalculateAdaptiveSphereSizes(const TArray<FVector>& Positions, TArray<float>& OutSphereSizes);
	void CreatePointCloud(const TArray<FVector>& Positions, const TArray<FColor>& Colors);
	void UpdatePlaneMorph();
	void CompletePlaneMorph();
	void StartDelayedMorph();
	void UpdateBobbing();
	void StartBobbing(EBobbingDirection Direction);
	void StopBobbing(bool bSmoothInterpolation = true);
	void StartRandomMovement();
	void UpdateRandomMovement();
	void StopRandomMovement(bool bSmoothInterpolation = true);
	void UpdateInterpolationToBase();
	void ScaleSplat(float NewScaleMultiplier);
	void UpdateSplatScale();
	void ResetToNormal();
	void TrySendImageToComfyUI(const FString& PLYPath);
	void UpdateImagePreview(const FString& PLYPath);

	FString GetSplatCreatorFolder() const;
};