#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "SplatCreatorSubsystem.generated.h"

class UHierarchicalInstancedStaticMeshComponent;

// Delegate for when splat bounds are updated
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSplatBoundsUpdated, FBox, NewBounds);

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
	
	FString GetSplatCreatorFolder() const;
};