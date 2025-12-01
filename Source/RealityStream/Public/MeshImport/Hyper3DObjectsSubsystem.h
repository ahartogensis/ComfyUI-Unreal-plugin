#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Hyper3DObjectsSubsystem.generated.h"

class UProceduralMeshComponent;
class UTexture2D;
class UMaterial;

/**
 * Imports all OBJ meshes (with optional textures) found in the plugin's MeshImport folder
 * and animates them as floating objects orbiting around the world origin.
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
	void SetReferenceLocation(const FVector& ReferenceLocation);

protected:
	virtual UWorld* GetWorld() const override;

private:
	struct FObjectInstance
	{
		FString SourceObjPath;
		TWeakObjectPtr<AActor> Actor;
		float BaseX = 0.f;  // Random X position in box
		float BaseY = 0.f;  // Random Y position in box
		float BaseAngle = 0.f;
		float OrbitRadius = 300.f;
		float RotationSpeed = 0.3f; // radians per second
		float BobFrequency = 0.5f;  // Hz
		float BobAmplitude = 60.f;  // centimeters
		float BaseHeight = 200.f;   // centimeters
		TWeakObjectPtr<UTexture2D> DiffuseTexture;
		TWeakObjectPtr<UTexture2D> MetallicTexture;
		TWeakObjectPtr<UTexture2D> NormalTexture;
		TWeakObjectPtr<UTexture2D> RoughnessTexture;
		TWeakObjectPtr<UTexture2D> PBRTexture;
		TWeakObjectPtr<UTexture2D> ShadedTexture;
	};

	void HandlePostWorldInit(UWorld* World, const UWorld::InitializationValues IVS);
	void HandleWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources);

	void StartTimers(UWorld* World);
	void StopTimers();

	void RefreshObjects();
	void UpdateObjectLayout();
	void UpdateObjectMotion();

	struct FTextureSet
	{
		UTexture2D* Diffuse = nullptr;
		UTexture2D* Metallic = nullptr;
		UTexture2D* Normal = nullptr;
		UTexture2D* Roughness = nullptr;
		UTexture2D* PBR = nullptr;
		UTexture2D* Shaded = nullptr;
	};

	bool SpawnObjectFromOBJ(const FString& ObjPath);
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
	void ApplyMaterial(UProceduralMeshComponent* MeshComp, AActor* Owner, const FTextureSet& Textures, const TArray<FColor>& VertexColors) const;

	FString GetImportDirectory() const;
	void DestroyAllObjects();

private:
	TWeakObjectPtr<UWorld> CachedWorld;
	FDelegateHandle PostWorldInitHandle;
	FDelegateHandle WorldCleanupHandle;

	FTimerHandle RefreshTimerHandle;
	FTimerHandle MotionTimerHandle;

	UPROPERTY()
	TArray<TObjectPtr<UTexture2D>> LoadedTextures;

	TArray<FObjectInstance> ObjectInstances;

	// Cached settings
	float BaseOrbitRadius = 100.f;  // Fixed radius for circle layout (no orbiting) - reduced to bring closer to origin
	float OrbitRadiusVariance = 50.f;  // Variance in circle radius - reduced
	float BaseHeight = 0.f;  // Base height for objects
	float HeightVariance = 100.f;  // Height variance for different heights
	float BaseRotationSpeed = 0.0f; // No rotation since we're not orbiting
	float RotationSpeedVariance = 0.0f;
	float BaseBobFrequency = 0.35f; // Hz - bobbing frequency
	float BobFrequencyVariance = 0.25f;
	float BaseBobAmplitude = 10.f; // cm - minimal bobbing amplitude
	float BobAmplitudeVariance = 5.f; // Minimal variance
	float ImportScaleMultiplier = 15.0f; // Smaller size for better scene matching
	FRotator BaseMeshRotation = FRotator(0.f, 0.f, -90.f);

	// Reference location for object positioning (defaults to origin)
	FVector ReferenceLocation = FVector::ZeroVector;

	bool bImportsActive = false;
};

