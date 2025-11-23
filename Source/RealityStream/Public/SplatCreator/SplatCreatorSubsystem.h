#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "SplatCreatorSubsystem.generated.h"

class UProceduralMeshComponent;

UCLASS(BlueprintType)
class REALITYSTREAM_API USplatCreatorSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	//Entry point: Reconstruct video → spawn Gaussian mesh or import existing PLY 
	UFUNCTION(BlueprintCallable, Category = "SplatCreator")
	void CheckAndImportSplat(const FString& VideoPath = TEXT(""));

private:
	// Auto-cycle through PLY files
	TArray<FString> PlyFiles;
	int32 CurrentFileIndex = 0;
	AActor* CurrentMeshActor = nullptr;
	FTimerHandle LoopTimer;
	FString CurrentlyConvertingFile; // Track which file is being converted to avoid duplicate conversions
	bool bIsShuttingDown = false; // Track if subsystem is shutting down to prevent async crashes
	bool bIsSpawningActor = false; // Track if we're currently spawning an actor to prevent concurrent spawns

	// Docker container management
	void StartDockerContainer();
	void StopDockerContainer();
	bool IsDockerContainerRunning();
	bool EnsureDockerContainerRunning();
	void ScanForPLYFiles();
	void CycleMeshes();
	void StartAutoCycleTimer();

	bool bHasStartedImportLoop = false;

	// Convert current PLY to OBJ
	void ConvertCurrentPLY();

	// Core pipeline
	void ImportExistingSplat();
	void ProcessVideoToMesh(const FString& VideoPath);
	bool RunDockerReconstructionPipeline(const FString& VideoPath);

	// File operations
	bool ConvertPLYToOBJ(const FString& PLYPath, const FString& OBJPath);
	bool ConvertPLYToOBJ_Internal(const FString& PLYPath, const FString& OBJPath);
	FString GetProjectDirectory() const;
	FString GetOutputDirectory() const;
	FString GetDataDirectory() const;

	// Runtime import
	AActor* ImportAndSpawnOBJMesh(const FString& OBJPath, FVector Location, FRotator Rotation, FVector Scale);
	UProceduralMeshComponent* CreateProceduralMeshFromOBJ(const FString& OBJPath, AActor* Owner);
	bool ParseOBJFile(const FString& OBJPath, TArray<FVector>& OutVertices, TArray<int32>& OutTriangles, TArray<FVector>& OutNormals, TArray<FColor>& OutColors);

	// Async auto-spawn polling
	FTimerHandle AutoSpawnTimer;
	void CheckAndAutoSpawnMesh();
};
