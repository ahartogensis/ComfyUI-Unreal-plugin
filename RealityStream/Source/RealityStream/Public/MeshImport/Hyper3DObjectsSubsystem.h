#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Hyper3DObjectsSubsystem.generated.h"

class UProceduralMeshComponent;
class UInstancedStaticMeshComponent;
class UStaticMesh;
class UTexture2D;
class UMaterial;
class UMaterialInstanceDynamic;

/**
 * Imports OBJ meshes (with optional textures) from MeshImport/<current PLY base name>/...
 * where the folder name matches the loaded splat's PLY filename without extension.
 * Other OBJs under MeshImport are ignored until that splat is active.
 */
UCLASS(BlueprintType)
class REALITYSTREAM_API UHyper3DObjectsSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintCallable, Category = "Hyper3DObjects")
	void ActivateObjectImports();

	UFUNCTION(BlueprintCallable, Category = "Hyper3DObjects")
	void DeactivateObjectImports();

	// Set the reference location for object positioning (objects will be placed relative to this location)
	UFUNCTION(BlueprintCallable, Category = "Hyper3DObjects")
	void SetReferenceLocation(const FVector& InReferenceLocation);

	UFUNCTION(BlueprintPure, Category = "Hyper3DObjects")
	FVector GetReferenceLocation() const;

	// Set the total number of instances to spawn across all object meshes (randomly distributed, default: 20)
	UFUNCTION(BlueprintCallable, Category = "Hyper3DObjects")
	void SetTotalInstances(int32 InTotalInstances);

	// Update placement BoxSize from splat dimensions when bDrivePlacementBoxFromSplat is true (also on splat bounds updates via delegate)
	UFUNCTION(BlueprintCallable, Category = "Hyper3DObjects")
	void UpdateFromSplatDimensions();

	// Set the ComfyStream actor location to avoid intersection (objects will be placed away from this location)
	UFUNCTION(BlueprintCallable, Category = "Hyper3DObjects")
	void SetComfyStreamExclusionZone(const FVector& ComfyStreamLocation);

	// Automatically find and set ComfyStream actor exclusion zone (searches for AComfyStreamActor in the world)
	UFUNCTION(BlueprintCallable, Category = "Hyper3DObjects")
	void FindAndSetComfyStreamExclusionZone();

	/**
	 * If true (default), BoxSize is overwritten from splat bounds on each update (capped at 200).
	 * If false, BoxSize is only changed from Blueprint or SetPlacementBoxSize — use this to pin placement (e.g. BoxSize = 0).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hyper3DObjects|Placement")
	bool bDrivePlacementBoxFromSplat = true;

	/** Edge length of the placement square in XY around ReferenceLocation (world units). When zero, XY spread is disabled. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hyper3DObjects|Placement", meta = (ClampMin = "0"))
	float BoxSize = 200.0f;

	UFUNCTION(BlueprintCallable, Category = "Hyper3DObjects|Placement")
	void SetPlacementBoxSize(float InBoxSize);

	/** Uniform scale applied to each imported OBJ actor (default in subsystem is 50). Updates existing spawns immediately. */
	UFUNCTION(BlueprintCallable, Category = "Hyper3DObjects|Placement", meta = (ClampMin = "0.001"))
	void SetHyper3DObjectScale(float InUniformScale);

	UFUNCTION(BlueprintPure, Category = "Hyper3DObjects|Placement")
	float GetHyper3DObjectScale() const;

	// Get whether 3D object opacity fading is enabled
	UFUNCTION(BlueprintCallable, Category = "Hyper3DObjects|Material")
	bool GetHyper3DObjectFadeEnabled() const;

	// Set whether 3D object opacity fading is enabled
	UFUNCTION(BlueprintCallable, Category = "Hyper3DObjects|Material")
	void SetHyper3DObjectFadeEnabled(bool bEnabled);

	// Get the fade in duration for 3D objects in seconds
	UFUNCTION(BlueprintCallable, Category = "Hyper3DObjects|Material")
	float GetHyper3DObjectFadeInDuration() const;

	// Set the fade in duration for 3D objects (0.01 to 60 seconds)
	UFUNCTION(BlueprintCallable, Category = "Hyper3DObjects|Material")
	void SetHyper3DObjectFadeInDuration(float DurationSeconds);

	// Get the hold duration (time at full opacity) for 3D objects in seconds
	UFUNCTION(BlueprintCallable, Category = "Hyper3DObjects|Material")
	float GetHyper3DObjectHoldDuration() const;

	// Set the hold duration for 3D objects (0 = stay visible forever)
	UFUNCTION(BlueprintCallable, Category = "Hyper3DObjects|Material")
	void SetHyper3DObjectHoldDuration(float DurationSeconds);

	/** When true, opacity animates on each splat change: 0→1 over OpacityFadeInDurationSeconds, holds for OpacityHoldAtFullSeconds, then 0 (0 hold = stay visible). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hyper3DObjects|Material")
	bool bFadeOpacityOnSplatChange = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hyper3DObjects|Material", meta = (ClampMin = "0.01", ClampMax = "60.0"))
	float OpacityFadeInDurationSeconds = 2.0f;

	/**
	 * Seconds to keep opacity at 1 after fade-in completes, then it returns to 0 until the next splat change.
	 * 0 = stay fully visible (no auto-hide).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hyper3DObjects|Material", meta = (ClampMin = "0"))
	float OpacityHoldAtFullSeconds = 10.0f;

	/** Must match the scalar parameter on M_ProceduralMeshTexture (default name: Opacity). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hyper3DObjects|Material")
	FName OpacityScalarParameterName = TEXT("Opacity");

protected:
	virtual UWorld* GetWorld() const override;

private:
	struct FObjectInstance
	{
		FString SourceObjPath;
		TWeakObjectPtr<AActor> Actor;
		float BaseX = 0.f;  // Random X position in box
		float BaseY = 0.f;  // Random Y position in box
		float BaseHeight = 200.f;   // centimeters
		FRotator RandomRotation;  // Random rotation for this object
		TWeakObjectPtr<UTexture2D> DiffuseTexture;
		TWeakObjectPtr<UTexture2D> MetallicTexture;
		TWeakObjectPtr<UTexture2D> NormalTexture;
		TWeakObjectPtr<UTexture2D> RoughnessTexture;
		TWeakObjectPtr<UTexture2D> PBRTexture;
		TWeakObjectPtr<UTexture2D> ShadedTexture;
		TWeakObjectPtr<UMaterialInstanceDynamic> ProceduralMaterialMID;
	};

	void HandlePostWorldInit(UWorld* World, const UWorld::InitializationValues IVS);
	void HandleWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources);
	
	// Delegate handler for splat bounds updates
	UFUNCTION()
	void OnSplatBoundsUpdatedHandler(FBox NewBounds);

	void StartTimers(UWorld* World);
	void StopTimers();

	void RefreshObjects();
	void UpdateObjectLayout();
	void UpdateObjectMotion();

	void BeginHyper3DOpacityFade();
	void UpdateHyper3DOpacityFade(float WorldTimeSeconds);
	float ComputeHyper3DOpacityAlpha(float WorldTimeSeconds) const;
	void ApplyHyper3DOpacityToMID(UMaterialInstanceDynamic* MID, float Alpha) const;

	struct FTextureSet
	{
		UTexture2D* Diffuse = nullptr;
		UTexture2D* Metallic = nullptr;
		UTexture2D* Normal = nullptr;
		UTexture2D* Roughness = nullptr;
		UTexture2D* PBR = nullptr;
		UTexture2D* Shaded = nullptr;
	};

	// Cached OBJ mesh data to avoid reloading the same file multiple times
	struct FCachedMeshData
	{
		TArray<FVector> Vertices;
		TArray<int32> Triangles;
		TArray<FVector> Normals;
		TArray<FVector2D> UVs;
		TArray<FColor> Colors;
		FString MtlFile;
		FTextureSet TextureSet;
		bool bIsValid = false;
	};

	bool SpawnObjectGroupFromOBJ(const FString& ObjPath);
	bool SpawnObjectFromCachedData(const FString& ObjPath, const FCachedMeshData& CachedData);
	AActor* CreateObjectActor(const FString& ObjPath, FTextureSet& TextureSet);
	UProceduralMeshComponent* CreateProceduralMeshFromOBJ(
		AActor* Owner,
		const TArray<FVector>& Vertices,
		const TArray<int32>& Triangles,
		const TArray<FVector>& Normals,
		const TArray<FVector2D>& UVs,
		const TArray<FColor>& Colors);

	bool LoadOBJ(
		const FString& ObjPath,
		TArray<FVector>& OutVertices,
		TArray<int32>& OutTriangles,
		TArray<FVector>& OutNormals,
		TArray<FVector2D>& OutUVs,
		TArray<FColor>& OutColors,
		FString& OutMtlFile
	);

	FTextureSet ResolveAllTexturesForOBJ(const FString& ObjPath, const FString& MtlFile) const;
	FString FindFallbackTexture(const FString& ObjPath) const;
	FString ResolveTextureForOBJ(const FString& ObjPath, const FString& MtlFile) const;
	FString ExtractTextureFromMTL(const FString& MtlPath) const;
	FString FindTextureInDirectory(const FString& Directory, const FString& TextureName) const;
	FTextureSet FindAllTexturesInDirectory(const FString& Directory) const;

	UTexture2D* LoadTextureFromFile(const FString& TexturePath) const;
	UTexture2D* ImportTextureAsAsset(const FString& TexturePath) const;
	UMaterialInterface* GetOrCreateBaseMaterial() const;
#if WITH_EDITOR
	UMaterial* CreateMaterialWithTextureParameters() const;
#endif
	UMaterialInstanceDynamic* ApplyMaterial(UProceduralMeshComponent* MeshComp, AActor* Owner, const FTextureSet& Textures, const TArray<FColor>& VertexColors);

	FString GetImportDirectory() const;
	void DestroyAllObjects();

	void ApplyHyper3DScaleToAllInstances();

private:
	TWeakObjectPtr<UWorld> CachedWorld;
	FDelegateHandle PostWorldInitHandle;
	FDelegateHandle WorldCleanupHandle;

	FTimerHandle RefreshTimerHandle;
	FTimerHandle MotionTimerHandle;

	UPROPERTY()
	TArray<TObjectPtr<UTexture2D>> LoadedTextures;

	TArray<FObjectInstance> ObjectInstances;

	// Cache for loaded OBJ mesh data (keyed by OBJ file path)
	TMap<FString, FCachedMeshData> MeshDataCache;

	// Track how many instances of each OBJ file have been spawned (for better distribution)
	TMap<FString, int32> ObjInstanceCounts;

	int32 TotalInstances = 20;  // Total number of instances to spawn across all OBJ files (randomly distributed, default: 20)
	float BaseHeight = 0.f;  // Base height for objects
	float HeightVariance = 100.f;  // Height variance for different heights
	float MinSpacingDistance = 50.0f;  // Minimum distance between objects (in world units) to ensure spacing
	float ComfyStreamExclusionDistance = 100.0f;  // Minimum distance from ComfyStream actor to avoid intersection
	float SplatPointExclusionDistance = 100.0f;  // Minimum distance from splat points to avoid intersection (increased from 50.0f)
	float ImportScaleMultiplier = 50.0f; 
	FRotator BaseMeshRotation = FRotator(0.f, 0.f, -90.f);

	// Reference location for object positioning (defaults to origin)
	FVector ReferenceLocation = FVector::ZeroVector;
	
	// ComfyStream actor location to avoid (set via SetComfyStreamExclusionZone)
	FVector ComfyStreamExclusionLocation = FVector::ZeroVector;
	bool bHasComfyStreamExclusion = false;  // Whether exclusion zone is set

	bool bImportsActive = false;

	float OpacityFadeStartWorldTime = 0.f;
	bool bOpacityFadeTickActive = false;
	/** After hold+fade cycle completed with opacity 0; new MIDs use 0 until next BeginHyper3DOpacityFade. */
	bool bOpacityCycleEndedHidden = false;
};

