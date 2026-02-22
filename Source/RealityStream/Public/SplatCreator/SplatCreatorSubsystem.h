#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "SplatCreatorSubsystem.generated.h"

class UHierarchicalInstancedStaticMeshComponent;

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

	// Get the bounding box dimensions of the current splat (returns size in X and Y)
	UFUNCTION(BlueprintCallable, Category = "SplatCreator")
	FVector2D GetSplatDimensions() const;

	// Get the center position of the current splat's bounding box
	UFUNCTION(BlueprintCallable, Category = "SplatCreator")
	FVector GetSplatCenter() const;

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

	// Event broadcast when splat bounds are updated (useful for other subsystems to react)
	UPROPERTY(BlueprintAssignable, Category = "SplatCreator")
	FOnSplatBoundsUpdated OnSplatBoundsUpdated;

private:
	bool bIsInitialized = false;
	// PLY file management
	TArray<FString> PlyFiles;
	int32 CurrentFileIndex = 0;
	FTimerHandle CycleTimer;
	
	// Point cloud rendering
	AActor* CurrentPointCloudActor = nullptr;
	UHierarchicalInstancedStaticMeshComponent* PointCloudComponent = nullptr;
	
	// Morphing transition - smooth interpolation between point clouds
	FTimerHandle MorphTimer;
	bool bIsMorphing = false;
	TArray<FVector> OldPositions;
	TArray<FVector> NewPositions;
	TArray<FColor> OldColors;
	TArray<FColor> NewColors;
	TArray<float> SphereSizes; // Adaptive sphere sizes for each point
	float MorphProgress = 0.0f;
	float MorphDuration = 1.5f; // Total morph duration
	float MorphStartTime = 0.0f;
	int32 MorphUpdateIndex = 0;
	
	// Current splat bounding box (stored after loading)
	FBox CurrentSplatBounds;
	bool bHasSplatBounds = false;
	
	// Current point positions (scaled and offset) for dense region detection
	TArray<FVector> CurrentPointPositions;
	
	// Bobbing animation system
	EBobbingDirection CurrentBobbingDirection = EBobbingDirection::None;
	FTimerHandle BobbingTimer;
	bool bIsBobbing = false;
	float BobbingTime = 0.0f;
	float BaseBobbingSpeed = 2.0f; // Base oscillations per second
	float BobbingSpeedMultiplier = 1.0f; // Speed multiplier (1.0 = normal, 2.0 = faster, 0.5 = slower)
	float BobbingAmplitude = 20.0f; // Distance to bob in Unreal units
	TArray<FVector> BasePointPositions; // Store original positions for bobbing
	
	// Scaling system
	float SplatScaleMultiplier = 1.0f; // Scale multiplier (1.0 = normal, >1.0 = bigger, <1.0 = smaller)
	FVector SplatCenter; // Center point for scaling
	bool bHasSplatCenter = false;
	
	// ComfyUI image send (when PLY changes, send matching JPG/PNG to channel 2)
	UPROPERTY(EditAnywhere, Category = "SplatCreator|ComfyUI")
	bool bSendImageToComfyUIOnPlyChange = true;
	UPROPERTY(EditAnywhere, Category = "SplatCreator|ComfyUI", meta = (EditCondition = "bSendImageToComfyUIOnPlyChange"))
	FString ComfyUIWebSocketHost = TEXT("localhost");
	UPROPERTY(EditAnywhere, Category = "SplatCreator|ComfyUI", meta = (EditCondition = "bSendImageToComfyUIOnPlyChange"))
	int32 ComfyUIImageChannel = 2;
	UPROPERTY()
	class UComfyImageSender* ComfyImageSender = nullptr;

	// Random movement system
	FTimerHandle RandomMovementTimer;
	bool bIsRandomMoving = false;
	float BaseRandomMovementSpeed = 50.0f; // Base units per second
	float RandomMovementSpeedMultiplier = 1.0f; // Speed multiplier for random movement
	float RandomMovementRadius = 100.0f; // Maximum distance from base position
	TArray<FVector> RandomVelocities; // Per-sphere random velocities
	TArray<FVector> RandomTargets; // Per-sphere random target positions
	TArray<FVector> RandomCurrentPositions; // Current positions of spheres during random movement
	float RandomChangeInterval = 2.0f; // How often to change random directions (seconds)
	float RandomChangeTimer = 0.0f;
	
	// Smooth interpolation system for stopping
	bool bIsInterpolatingToBase = false;
	float InterpolationTime = 0.0f;
	float InterpolationDuration = 1.0f; // Duration of interpolation in seconds
	TArray<FVector> InterpolationStartPositions; // Starting positions for interpolation
	
	// Functions
	void ScanForPLYFiles();
	void CycleToNextPLY();
	void LoadPLYFile(const FString& PLYPath);
	bool ParsePLYFile(const FString& PLYPath, TArray<FVector>& OutPositions, TArray<FColor>& OutColors);
	void SamplePointsUniformly(const TArray<FVector>& InPositions, const TArray<FColor>& InColors, TArray<FVector>& OutPositions, TArray<FColor>& OutColors);
	void CalculateAdaptiveSphereSizes(const TArray<FVector>& Positions, TArray<float>& OutSphereSizes);
	void CreatePointCloud(const TArray<FVector>& Positions, const TArray<FColor>& Colors);
	void UpdateMorph();
	void CompleteMorph();
	void UpdateBobbing();
	void StartBobbing(EBobbingDirection Direction);
	void StopBobbing(bool bSmoothInterpolation = true);
	void StartRandomMovement();
	void UpdateRandomMovement();
	void StopRandomMovement(bool bSmoothInterpolation = true);
	void UpdateInterpolationToBase();
	void ScaleSplat(float NewScaleMultiplier);
	void UpdateSplatScale();
	void ResetToNormal(); // Reset all transformations to default state
	void TrySendImageToComfyUI(const FString& PLYPath);
	
	FString GetSplatCreatorFolder() const;
};