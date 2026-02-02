#include "MeshImport/Hyper3DObjectsSubsystem.h"
#include "SplatCreator/SplatCreatorSubsystem.h"
#include "ComfyStream/ComfyStreamActor.h"

#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "GameFramework/Actor.h"
#include "Components/SceneComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "ProceduralMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "StaticMeshResources.h"
#include "RawIndexBuffer.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/Guid.h"
#include "Engine/Texture2D.h"
#include "Math/RandomStream.h"
#include "Modules/ModuleManager.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "HAL/PlatformFilemanager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/DateTime.h"

#if WITH_EDITOR
#include "AssetToolsModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Factories/TextureFactory.h"
#include "Editor.h"
#include "ObjectTools.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#endif


namespace Hyper3DObjectsImport
{
	// Primary material path - the specific material the user wants to use
	static constexpr TCHAR ProceduralMeshTextureMaterialPath[] = TEXT("/Game/_GENERATED/Materials/M_ProceduralMeshTexture.M_ProceduralMeshTexture");
	// Fallback paths (in case primary is not found)
	static constexpr TCHAR ProceduralMeshTextureMaterialPathAlt[] = TEXT("/Game/M_ProceduralMeshTexture.M_ProceduralMeshTexture");
	static constexpr TCHAR ProceduralMeshTextureMaterialPathAlt2[] = TEXT("/Game/ImportedTextures/M_ProceduralMeshTexture.M_ProceduralMeshTexture");
	// Try to use materials that exist in UE5.6 (only as last resort fallback)
	static constexpr TCHAR VertexColorMaterialPathA[] = TEXT("/Game/_GENERATED/Materials/M_VertexColor.M_VertexColor");
	static constexpr TCHAR VertexColorMaterialPathB[] = TEXT("/Game/M_VertexColor.M_VertexColor");
	static constexpr TCHAR EditorVertexColorMaterialPath[] = TEXT("/Engine/EditorMaterials/WidgetVertexColorMaterial");

	static float DegsPerRad(float Radians)
	{
		return Radians * 57.29577951308232f;
	}
}

int debug = 0; //0 = off, 1 = on

// ============================================================
// Initialize
// ============================================================
void UHyper3DObjectsSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	if(debug) UE_LOG(LogTemp, Display, TEXT("[Hyper3DObjects] Subsystem initialized"));

	PostWorldInitHandle = FWorldDelegates::OnPostWorldInitialization.AddUObject(
		this, &UHyper3DObjectsSubsystem::HandlePostWorldInit);

	WorldCleanupHandle = FWorldDelegates::OnWorldCleanup.AddUObject(
		this, &UHyper3DObjectsSubsystem::HandleWorldCleanup);

	bImportsActive = false;
}

void UHyper3DObjectsSubsystem::Deinitialize()
{
	StopTimers();
	DestroyAllObjects();

	// Unsubscribe from splat bounds updates
	if (UWorld* World = GetWorld())
	{
		if (UGameInstance* GameInstance = World->GetGameInstance())
		{
			if (USplatCreatorSubsystem* SplatSubsystem = GameInstance->GetSubsystem<USplatCreatorSubsystem>())
			{
				SplatSubsystem->OnSplatBoundsUpdated.RemoveDynamic(this, &UHyper3DObjectsSubsystem::OnSplatBoundsUpdatedHandler);
			}
		}
	}

	if (PostWorldInitHandle.IsValid())
	{
		FWorldDelegates::OnPostWorldInitialization.Remove(PostWorldInitHandle);
		PostWorldInitHandle.Reset();
	}

	if (WorldCleanupHandle.IsValid())
	{
		FWorldDelegates::OnWorldCleanup.Remove(WorldCleanupHandle);
		WorldCleanupHandle.Reset();
	}

	CachedWorld.Reset();
	bImportsActive = false;

	if(debug) UE_LOG(LogTemp, Display, TEXT("[Hyper3DObjects] Subsystem deinitialized"));

	Super::Deinitialize();
}

UWorld* UHyper3DObjectsSubsystem::GetWorld() const
{
	if (CachedWorld.IsValid())
	{
		return CachedWorld.Get();
	}

	return Super::GetWorld();
}

void UHyper3DObjectsSubsystem::ActivateObjectImports()
{
	if (bImportsActive)
	{
		return;
	}

	bImportsActive = true;

	UWorld* World = GetWorld();
	if (!World)
	{
		if(debug) UE_LOG(LogTemp, Warning, TEXT("[Hyper3DObjects] Cannot activate imports - no valid world"));
		return;
	}

	// Try to get splat dimensions if they're already available (otherwise wait for OnSplatBoundsUpdated delegate)
	UpdateFromSplatDimensions();

	StartTimers(World);
	RefreshObjects();
}

//helper to get dimensions when the splat is created

void UHyper3DObjectsSubsystem::UpdateFromSplatDimensions()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (UGameInstance* GameInstance = World->GetGameInstance())
	{
		if (USplatCreatorSubsystem* SplatSubsystem = GameInstance->GetSubsystem<USplatCreatorSubsystem>())
		{
			// Check if splat bounds are actually available
			FBox CurrentSplatBounds = SplatSubsystem->GetSplatBounds();
			FVector BoxSizeVector = CurrentSplatBounds.GetSize();
			
			// Check if the box has any meaningful size or return empty box
			if (BoxSizeVector.Size() < 1.0f)
			{
				// Splat bounds not available yet - will be updated when OnSplatBoundsUpdated is called
				if(debug) UE_LOG(LogTemp, Display, TEXT("[Hyper3DObjects] Splat bounds not available yet, waiting for splat to load..."));
				return;
			}

			FVector2D SplatDimensions = SplatSubsystem->GetSplatDimensions();
			
			// Use splat dimensions only to constrain the box size
			const float DesiredBoxSize = 200.0f;
			float SplatBoxSize = FMath::Min(SplatDimensions.X, SplatDimensions.Y);
			
			if (SplatBoxSize > 0.0f)
			{
				// Use the smaller of desired size or splat size
				BoxSize = FMath::Min(DesiredBoxSize, SplatBoxSize);
			}
			else
			{
				// Splat dimensions invalid, use default
				BoxSize = DesiredBoxSize;
			}
			
			// Update object layout if objects are already spawned
			if (bImportsActive)
			{
				UpdateObjectLayout();
			}
		}
		else
		{
			if(debug) UE_LOG(LogTemp, Warning, TEXT("[Hyper3DObjects] SplatCreatorSubsystem not found, using default BoxSize: %.1f"), BoxSize);
		}
	}
}

void UHyper3DObjectsSubsystem::OnSplatBoundsUpdatedHandler(FBox NewBounds)
{
	// Called automatically when splat bounds are updated
	UpdateFromSplatDimensions();
}

void UHyper3DObjectsSubsystem::DeactivateObjectImports()
{
	if (!bImportsActive)
	{
		return;
	}

	bImportsActive = false;
	StopTimers();
	DestroyAllObjects();
}


// ============================================================
// Get parameters from the Blueprint variables
// ============================================================

void UHyper3DObjectsSubsystem::SetReferenceLocation(const FVector& InReferenceLocation)
{
	ReferenceLocation = InReferenceLocation;
	if(debug) UE_LOG(LogTemp, Display, TEXT("[Hyper3DObjects] Reference location set to: %s"), *ReferenceLocation.ToString());
	
	// Update object positions immediately if objects are already spawned
	if (bImportsActive)
	{
		UpdateObjectMotion();
	}
}

void UHyper3DObjectsSubsystem::SetComfyStreamExclusionZone(const FVector& ComfyStreamLocation)
{
	ComfyStreamExclusionLocation = ComfyStreamLocation;
	bHasComfyStreamExclusion = true;
	if(debug) UE_LOG(LogTemp, Display, TEXT("[Hyper3DObjects] ComfyStream exclusion zone set at: %s (exclusion distance: %.1f)"), 
		*ComfyStreamExclusionLocation.ToString(), ComfyStreamExclusionDistance);
	
	// Update object layout to avoid the exclusion zone if objects are already spawned
	if (bImportsActive)
	{
		UpdateObjectLayout();
		UpdateObjectMotion();
	}
}

void UHyper3DObjectsSubsystem::FindAndSetComfyStreamExclusionZone()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		if(debug) UE_LOG(LogTemp, Warning, TEXT("[Hyper3DObjects] Cannot find ComfyStream actor - no world available"));
		return;
	}

	// Find all ComfyStreamActor instances in the world
	TArray<AActor*> FoundActors;
	UGameplayStatics::GetAllActorsOfClass(World, AComfyStreamActor::StaticClass(), FoundActors);

	if (FoundActors.Num() == 0)
	{
		if(debug) UE_LOG(LogTemp, Warning, TEXT("[Hyper3DObjects] No ComfyStreamActor found in the world"));
		bHasComfyStreamExclusion = false;
		return;
	}

	// Use the first ComfyStream actor found
	AActor* ComfyStreamActor = FoundActors[0];
	FVector ComfyStreamLocation = ComfyStreamActor->GetActorLocation();
	
	if(debug) UE_LOG(LogTemp, Display, TEXT("[Hyper3DObjects] Found ComfyStreamActor at: %s"), *ComfyStreamLocation.ToString());
	SetComfyStreamExclusionZone(ComfyStreamLocation);
}

void UHyper3DObjectsSubsystem::SetTotalInstances(int32 InTotalInstances)
{
	int32 OldValue = TotalInstances;
	TotalInstances = FMath::Max(1, InTotalInstances); // Ensure at least 1
	if(debug) UE_LOG(LogTemp, Display, TEXT("[Hyper3DObjects] Total instances changed from %d to %d (will be randomly distributed across all OBJ files)"), OldValue, TotalInstances);
	
	// Refresh objects to spawn/remove instances as needed
	if (bImportsActive)
	{
		if(debug) UE_LOG(LogTemp, Display, TEXT("[Hyper3DObjects] Refreshing objects to update instance count..."));
		RefreshObjects();
	}
	else
	{
		if(debug) UE_LOG(LogTemp, Warning, TEXT("[Hyper3DObjects] Imports are not active. Call ActivateObjectImports() first to see the changes."));
	}
}

void UHyper3DObjectsSubsystem::HandlePostWorldInit(UWorld* World, const UWorld::InitializationValues IVS)
{
	if (!World || !World->IsGameWorld())
	{
		return;
	}

	CachedWorld = World;

	// Subscribe to splat bounds updates when world is ready
	if (UGameInstance* GameInstance = World->GetGameInstance())
	{
		if (USplatCreatorSubsystem* SplatSubsystem = GameInstance->GetSubsystem<USplatCreatorSubsystem>())
		{
			SplatSubsystem->OnSplatBoundsUpdated.AddDynamic(this, &UHyper3DObjectsSubsystem::OnSplatBoundsUpdatedHandler);
		}
	}

	if (bImportsActive)
	{
		StartTimers(World);
		RefreshObjects();
	}
}

void UHyper3DObjectsSubsystem::HandleWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources)
{
	if (CachedWorld.Get() != World)
	{
		return;
	}

	StopTimers();
	DestroyAllObjects();

	CachedWorld.Reset();
	bImportsActive = false;
}


// ============================================================
// Set up cycle
// ============================================================

void UHyper3DObjectsSubsystem::StartTimers(UWorld* World)
{
	if (!World)
	{
		return;
	}

	FTimerManager& TimerManager = World->GetTimerManager();

	if (!TimerManager.IsTimerActive(RefreshTimerHandle))
	{
		TimerManager.SetTimer(
			RefreshTimerHandle,
			this,
			&UHyper3DObjectsSubsystem::RefreshObjects,
			15.0f,
			true,
			2.0f
		);
	}

	if (!TimerManager.IsTimerActive(MotionTimerHandle))
	{
		TimerManager.SetTimer(
			MotionTimerHandle,
			this,
			&UHyper3DObjectsSubsystem::UpdateObjectMotion,
			0.02f,
			true,
			0.02f
		);
	}
}

void UHyper3DObjectsSubsystem::StopTimers()
{
	if (UWorld* World = GetWorld())
	{
		FTimerManager& TimerManager = World->GetTimerManager();

		if (TimerManager.IsTimerActive(RefreshTimerHandle))
		{
			TimerManager.ClearTimer(RefreshTimerHandle);
		}
		if (TimerManager.IsTimerActive(MotionTimerHandle))
		{
			TimerManager.ClearTimer(MotionTimerHandle);
		}
	}

	RefreshTimerHandle.Invalidate();
	MotionTimerHandle.Invalidate();
}

void UHyper3DObjectsSubsystem::RefreshObjects()
{
	if (!bImportsActive)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld())
	{
		return;
	}

	const FString ImportDir = GetImportDirectory();
	if (!FPaths::DirectoryExists(ImportDir))
	{
		if(debug) UE_LOG(LogTemp, Warning, TEXT("[Hyper3DObjects] MeshImport directory not found: %s"), *ImportDir);
		return;
	}

	auto RemoveObjectGroupAt = [this](int32 Index)
	{
		if (!ObjectInstances.IsValidIndex(Index))
		{
			return;
		}

		FObjectInstance& Instance = ObjectInstances[Index];
		if (AActor* Actor = Instance.Actor.Get())
		{
			Actor->Destroy();
		}

		if (UTexture2D* Texture = Instance.DiffuseTexture.Get())
		{
			LoadedTextures.Remove(Texture);
		}

		ObjectInstances.RemoveAt(Index);
	};

	TArray<FString> ObjFilePaths;
	IFileManager::Get().FindFilesRecursive(ObjFilePaths, *ImportDir, TEXT("*.obj"), true, false);

	ObjFilePaths.Sort();

	TArray<FString> DesiredPathList;
	DesiredPathList.Reserve(ObjFilePaths.Num());
	TSet<FString> DesiredPathSet;

	for (const FString& Path : ObjFilePaths)
	{
		const FString AbsolutePath = FPaths::ConvertRelativePathToFull(Path);
		if (!DesiredPathSet.Contains(AbsolutePath))
		{
			DesiredPathSet.Add(AbsolutePath);
			DesiredPathList.Add(AbsolutePath);
		}
	}

	// Remove stale objects (objects whose OBJ file no longer exists)
	for (int32 Index = ObjectInstances.Num() - 1; Index >= 0; --Index)
	{
		FObjectInstance& Instance = ObjectInstances[Index];
		if (!DesiredPathSet.Contains(Instance.SourceObjPath))
		{
			// Remove from cache as well
			MeshDataCache.Remove(Instance.SourceObjPath);
			RemoveObjectGroupAt(Index);
		}
		else if (!Instance.Actor.IsValid())
		{
			RemoveObjectGroupAt(Index);
		}
	}

	// Count total instances across all OBJ files and track per-OBJ counts
	int32 CurrentTotalInstances = 0;
	ObjInstanceCounts.Empty();
	for (const FObjectInstance& Instance : ObjectInstances)
	{
		if (Instance.Actor.IsValid())
		{
			CurrentTotalInstances++;
			ObjInstanceCounts.FindOrAdd(Instance.SourceObjPath)++;
		}
	}
	
	if(debug) UE_LOG(LogTemp, Display, TEXT("[Hyper3DObjects] Found %d OBJ files, current total instances: %d, target: %d"), 
		DesiredPathList.Num(), CurrentTotalInstances, TotalInstances);

	// Remove excess instances if we have too many (remove randomly)
	if (CurrentTotalInstances > TotalInstances)
	{
		int32 InstancesToRemove = CurrentTotalInstances - TotalInstances;
		if(debug) UE_LOG(LogTemp, Display, TEXT("[Hyper3DObjects] Removing %d excess instances"), InstancesToRemove);
		
		FRandomStream RandomStream(FDateTime::Now().GetTicks());
		TArray<int32> ValidIndices;
		for (int32 Index = 0; Index < ObjectInstances.Num(); ++Index)
		{
			if (ObjectInstances[Index].Actor.IsValid())
			{
				ValidIndices.Add(Index);
			}
		}
		
		// Shuffle and remove
		for (int32 i = 0; i < InstancesToRemove && ValidIndices.Num() > 0; ++i)
		{
			int32 RandomIndex = RandomStream.RandRange(0, ValidIndices.Num() - 1);
			int32 InstanceIndex = ValidIndices[RandomIndex];
			ValidIndices.RemoveAt(RandomIndex);
			RemoveObjectGroupAt(InstanceIndex);
		}
	}
	// Spawn additional instances if we have too few (spawn randomly across OBJ files)
	else if (CurrentTotalInstances < TotalInstances && DesiredPathList.Num() > 0)
	{
		int32 InstancesToSpawn = TotalInstances - CurrentTotalInstances;
		if(debug) UE_LOG(LogTemp, Display, TEXT("[Hyper3DObjects] Need to spawn %d more instances (randomly distributed)"), InstancesToSpawn);
		
		// Ensure all OBJ files are cached
		for (const FString& FullPath : DesiredPathList)
		{
			FCachedMeshData* CachedData = MeshDataCache.Find(FullPath);
			if (!CachedData || !CachedData->bIsValid)
			{
				// Load and cache the OBJ data
				FCachedMeshData NewCache;
				FString MtlFile;
				if (LoadOBJ(FullPath, NewCache.Vertices, NewCache.Triangles, NewCache.Normals, NewCache.UVs, NewCache.Colors, MtlFile))
				{
					NewCache.MtlFile = MtlFile;
					NewCache.TextureSet = ResolveAllTexturesForOBJ(FullPath, MtlFile);
					NewCache.bIsValid = true;
					MeshDataCache.Add(FullPath, NewCache);
					if(debug) UE_LOG(LogTemp, Display, TEXT("[Hyper3DObjects] Cached OBJ data for %s"), *FPaths::GetCleanFilename(FullPath));
				}
			}
		}
		
		// Spawn instances with weighted random selection favoring OBJ files with fewer instances
		const int32 MaxSpawnPerFrame = 10; // Spawn up to 10 per refresh cycle
		int32 SpawnCount = FMath::Min(InstancesToSpawn, MaxSpawnPerFrame);
		
		// Use a persistent random stream seeded with current time for this refresh cycle
		FRandomStream RandomStream(FDateTime::Now().GetTicks());
		
		// Log current distribution
		if(debug) UE_LOG(LogTemp, Display, TEXT("[Hyper3DObjects] Current distribution:"));
		for (const FString& ObjPath : DesiredPathList)
		{
			int32 Count = ObjInstanceCounts.FindRef(ObjPath);
			if(debug) UE_LOG(LogTemp, Display, TEXT("  %s: %d instances"), *FPaths::GetCleanFilename(ObjPath), Count);
		}
		
		for (int32 i = 0; i < SpawnCount; ++i)
		{
			// Build weighted list: OBJ files with fewer instances get much higher weight
			TArray<FString> WeightedObjList;
			int32 TotalWeight = 0;
			
			for (const FString& ObjPath : DesiredPathList)
			{
				int32 CurrentCount = ObjInstanceCounts.FindRef(ObjPath);
				// Stronger weighting: files with 0 instances get weight 100, 1 instance gets 50, 2 gets 33, etc.
				// This ensures much better distribution
				int32 Weight = FMath::Max(1, 100 / (CurrentCount + 1));
				for (int32 w = 0; w < Weight; ++w)
				{
					WeightedObjList.Add(ObjPath);
				}
				TotalWeight += Weight;
			}
			
			if (WeightedObjList.Num() > 0)
			{
				// Pick randomly from weighted list
				int32 RandomIndex = RandomStream.RandRange(0, WeightedObjList.Num() - 1);
				const FString& SelectedObjPath = WeightedObjList[RandomIndex];
			
			FCachedMeshData* CachedData = MeshDataCache.Find(SelectedObjPath);
			if (CachedData && CachedData->bIsValid)
			{
					if (SpawnObjectFromCachedData(SelectedObjPath, *CachedData))
					{
						// Update count for this OBJ file immediately so next selection is informed
						ObjInstanceCounts.FindOrAdd(SelectedObjPath)++;
						if(debug) UE_LOG(LogTemp, Verbose, TEXT("[Hyper3DObjects] Spawned instance %d/%d from %s (now has %d instances)"), 
							i + 1, SpawnCount, *FPaths::GetCleanFilename(SelectedObjPath), ObjInstanceCounts.FindRef(SelectedObjPath));
					}
					else
				{
					if(debug) UE_LOG(LogTemp, Warning, TEXT("[Hyper3DObjects] Failed to spawn instance from %s"), *FPaths::GetCleanFilename(SelectedObjPath));
				}
				
				// Yield to game thread every few spawns to prevent freezing
				if ((i + 1) % 5 == 0)
				{
					FPlatformProcess::Sleep(0.001f); // 1ms sleep to yield
					}
				}
			}
		}
		
		// Log final distribution
		if(debug) UE_LOG(LogTemp, Display, TEXT("[Hyper3DObjects] Final distribution after spawning:"));
		for (const FString& ObjPath : DesiredPathList)
		{
			int32 Count = ObjInstanceCounts.FindRef(ObjPath);
			if(debug) UE_LOG(LogTemp, Display, TEXT("  %s: %d instances"), *FPaths::GetCleanFilename(ObjPath), Count);
		}
		
		if (SpawnCount < InstancesToSpawn)
		{
			if(debug) UE_LOG(LogTemp, Display, TEXT("[Hyper3DObjects] Spawned %d/%d instances this cycle. Will complete %d remaining in next refresh cycle."), 
				SpawnCount, InstancesToSpawn, InstancesToSpawn - SpawnCount);
		}
	}

	LoadedTextures.RemoveAll([](const TObjectPtr<UTexture2D>& Texture)
	{
		return Texture.Get() == nullptr;
	});

	UpdateObjectLayout();
	UpdateObjectMotion();
}

void UHyper3DObjectsSubsystem::UpdateObjectLayout()
{
	const int32 Count = ObjectInstances.Num();
	if (Count == 0)
	{
		return;
	}

	// Use current time as seed so layout changes when parameters change
	FRandomStream Stream(FDateTime::Now().GetTicks());
	
	// Place objects in a random box around ReferenceLocation
	// Box size is 200x200 by default, but constrained by splat dimensions if smaller
	const float HalfBoxSize = BoxSize * 0.5f;
	
	// Scale exclusion distances based on box size (relative to default 200x200 box)
	// This makes constraints adapt to available space automatically
	const float BoxSizeScale = FMath::Clamp(BoxSize / 200.0f, 0.25f, 1.0f); // Scale between 25% and 100%
	const float ScaledMinSpacing = MinSpacingDistance * BoxSizeScale;
	const float ScaledSplatExclusion = SplatPointExclusionDistance * BoxSizeScale;
	const float ScaledComfyExclusion = ComfyStreamExclusionDistance * BoxSizeScale;
	
	// Track placed positions to ensure minimum spacing
	TArray<FVector2D> PlacedPositions;
	PlacedPositions.Reserve(Count);
		
		for (int32 Index = 0; Index < Count; ++Index)
		{
			FObjectInstance& Instance = ObjectInstances[Index];
			
			// Try to find a position that's far enough from other objects
			FVector2D NewPosition = FVector2D::ZeroVector; // Initialize to avoid warning
			int32 MaxAttempts = 50; // Reduced attempts since constraints are now adaptive
			bool bFoundValidPosition = false;
			
			// Calculate random height for this object (will be used in check)
			float RandomHeight = BaseHeight + Stream.FRandRange(-HeightVariance, HeightVariance);
			
			for (int32 Attempt = 0; Attempt < MaxAttempts; ++Attempt)
			{
				// Try a random position
				NewPosition = FVector2D(
					Stream.FRandRange(-HalfBoxSize, HalfBoxSize),
					Stream.FRandRange(-HalfBoxSize, HalfBoxSize)
				);
				
				// Check if this position is far enough from all existing positions
				bool bTooClose = false;
				for (const FVector2D& ExistingPos : PlacedPositions)
				{
					float Distance = (NewPosition - ExistingPos).Size();
					if (Distance < ScaledMinSpacing)
					{
						bTooClose = true;
						break;
					}
				}
				
				// Also check if this position is too close to ComfyStream actor (if exclusion zone is set)
				if (!bTooClose && bHasComfyStreamExclusion)
				{
					FVector2D ComfyStreamPos2D(ComfyStreamExclusionLocation.X - ReferenceLocation.X, 
					                           ComfyStreamExclusionLocation.Y - ReferenceLocation.Y);
					float DistanceToComfyStream = (NewPosition - ComfyStreamPos2D).Size();
					if (DistanceToComfyStream < ScaledComfyExclusion)
					{
						bTooClose = true;
					}
				}
				
				// Check if this position is too close to any splat points
				if (!bTooClose)
				{
					UWorld* World = GetWorld();
					if (World)
					{
						if (UGameInstance* GameInstance = World->GetGameInstance())
						{
							if (USplatCreatorSubsystem* SplatSubsystem = GameInstance->GetSubsystem<USplatCreatorSubsystem>())
							{
								// Check horizontal (X,Y) distance only - splat points may be at different Z heights
								// Use the actual object position (with random height)
								FVector TestPosition3D = ReferenceLocation + FVector(NewPosition.X, NewPosition.Y, RandomHeight);
								if (SplatSubsystem->IsPositionTooCloseToSplatPoints(TestPosition3D, ScaledSplatExclusion, true))
								{
									bTooClose = true;
								}
							}
						}
					}
				}
				
				if (!bTooClose)
				{
					bFoundValidPosition = true;
					break;
				}
			}
			
			// If we couldn't find a valid position, use the last tried position anyway
			// This ensures all objects get placed
			if (!bFoundValidPosition)
			{
				if(debug) UE_LOG(LogTemp, Verbose, TEXT("[Hyper3DObjects] Could not find ideal position for object %d after %d attempts. Using best available position."), 
					Index, MaxAttempts);
			}
			
			// Use the position (even if not ideal, to ensure all objects get placed)
			Instance.BaseX = NewPosition.X;
			Instance.BaseY = NewPosition.Y;
			PlacedPositions.Add(NewPosition);
			
			// Random height variation for each object
			Instance.BaseHeight = RandomHeight;
			// Random rotation: only randomize Yaw (Z-axis rotation), keep Pitch and Roll at base values
			Instance.RandomRotation = FRotator(
				BaseMeshRotation.Pitch,                                      // Keep base pitch
				BaseMeshRotation.Yaw + Stream.FRandRange(0.0f, 360.0f),     // Full 360 degree random yaw (Z-axis)
				BaseMeshRotation.Roll                                        // Keep base roll
			);
		}
	
		if(debug) UE_LOG(LogTemp, Display, TEXT("[Hyper3DObjects] Placed %d objects in %.1fx%.1f box centered at ReferenceLocation"), 
		Count, BoxSize, BoxSize);
}

void UHyper3DObjectsSubsystem::UpdateObjectMotion()
{
	if (!bImportsActive)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const float Time = World->GetTimeSeconds();

	for (FObjectInstance& Instance : ObjectInstances)
	{
		AActor* Actor = Instance.Actor.Get();
		if (!Actor)
		{
			continue;
		}

		// Use random positions from box (already set in UpdateObjectLayout)
		const float X = Instance.BaseX;
		const float Y = Instance.BaseY;
		// Use base height directly (no bouncing)
		const float Height = Instance.BaseHeight;

		// Position relative to reference location
		const FVector Location = ReferenceLocation + FVector(X, Y, Height);
		Actor->SetActorLocation(Location);

		// Apply random rotation for this object
		Actor->SetActorRotation(Instance.RandomRotation);
	}
}


// ============================================================
// Spawn objects from path 
// ============================================================


bool UHyper3DObjectsSubsystem::SpawnObjectGroupFromOBJ(const FString& ObjPath)
{
	if (!FPaths::FileExists(ObjPath))
	{
		return false;
	}

	double StartTime = FPlatformTime::Seconds();
	
	FString MtlFile;
	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FColor> Colors;

	if(debug) UE_LOG(LogTemp, Display, TEXT("[Hyper3DObjects] Loading OBJ file: %s"), *FPaths::GetCleanFilename(ObjPath));
	if (!LoadOBJ(ObjPath, Vertices, Triangles, Normals, UVs, Colors, MtlFile))
	{
		return false;
	}
	double LoadOBJTime = FPlatformTime::Seconds();
	if(debug) UE_LOG(LogTemp, Display, TEXT("[Hyper3DObjects] LoadOBJ took %.3f seconds, loaded %d vertices, %d triangles"), 
		LoadOBJTime - StartTime, Vertices.Num(), Triangles.Num() / 3);

	// Resolve all textures for this OBJ
	FTextureSet TextureSet = ResolveAllTexturesForOBJ(ObjPath, MtlFile);
	double TextureTime = FPlatformTime::Seconds();
	if(debug) UE_LOG(LogTemp, Display, TEXT("[Hyper3DObjects] Texture resolution took %.3f seconds"), TextureTime - LoadOBJTime);
	
	// Load all textures
	AActor* ObjectActor = CreateObjectActor(ObjPath, TextureSet);
	if (!ObjectActor)
	{
		return false;
	}

	UProceduralMeshComponent* MeshComp = CreateProceduralMeshFromOBJ(
		ObjectActor,
		Vertices,
		Triangles,
		Normals,
		UVs,
		Colors);
	if (!MeshComp)
	{
		ObjectActor->Destroy();
		return false;
	}

	ObjectActor->SetActorScale3D(FVector(ImportScaleMultiplier));
	ObjectActor->SetActorRotation(BaseMeshRotation);
	if (USceneComponent* RootComponent = ObjectActor->GetRootComponent())
	{
		RootComponent->SetWorldScale3D(FVector(ImportScaleMultiplier));
		RootComponent->SetWorldRotation(BaseMeshRotation);
	}

	ApplyMaterial(MeshComp, ObjectActor, TextureSet, Colors);

	FObjectInstance Instance;
	Instance.SourceObjPath = ObjPath;
	Instance.Actor = ObjectActor;
	Instance.RandomRotation = BaseMeshRotation; // Will be randomized in UpdateObjectLayout
	Instance.DiffuseTexture = TextureSet.Diffuse;
	Instance.MetallicTexture = TextureSet.Metallic;
	Instance.NormalTexture = TextureSet.Normal;
	Instance.RoughnessTexture = TextureSet.Roughness;
	Instance.PBRTexture = TextureSet.PBR;
	Instance.ShadedTexture = TextureSet.Shaded;

	ObjectInstances.Add(Instance);

	// Track all loaded textures
	if (TextureSet.Diffuse) LoadedTextures.Add(TextureSet.Diffuse);
	if (TextureSet.Metallic) LoadedTextures.Add(TextureSet.Metallic);
	if (TextureSet.Normal) LoadedTextures.Add(TextureSet.Normal);
	if (TextureSet.Roughness) LoadedTextures.Add(TextureSet.Roughness);
	if (TextureSet.PBR) LoadedTextures.Add(TextureSet.PBR);
	if (TextureSet.Shaded) LoadedTextures.Add(TextureSet.Shaded);

	double TotalTime = FPlatformTime::Seconds() - StartTime;
	if(debug) UE_LOG(LogTemp, Display, TEXT("[Hyper3DObjects] Total spawn time: %.3f seconds for %s"), TotalTime, *FPaths::GetCleanFilename(ObjPath));

	return true;
}

bool UHyper3DObjectsSubsystem::SpawnObjectFromCachedData(const FString& ObjPath, const FCachedMeshData& CachedData)
{
	if (!CachedData.bIsValid)
	{
		return false;
	}

	// Load all textures (these are already resolved in the cache)
	AActor* ObjectActor = CreateObjectActor(ObjPath, const_cast<FTextureSet&>(CachedData.TextureSet));
	if (!ObjectActor)
	{
		return false;
	}

	UProceduralMeshComponent* MeshComp = CreateProceduralMeshFromOBJ(
		ObjectActor,
		CachedData.Vertices,
		CachedData.Triangles,
		CachedData.Normals,
		CachedData.UVs,
		CachedData.Colors);
	if (!MeshComp)
	{
		ObjectActor->Destroy();
		return false;
	}

	ObjectActor->SetActorScale3D(FVector(ImportScaleMultiplier));
	ObjectActor->SetActorRotation(BaseMeshRotation);
	if (USceneComponent* RootComponent = ObjectActor->GetRootComponent())
	{
		RootComponent->SetWorldScale3D(FVector(ImportScaleMultiplier));
		RootComponent->SetWorldRotation(BaseMeshRotation);
	}

	ApplyMaterial(MeshComp, ObjectActor, CachedData.TextureSet, CachedData.Colors);

	FObjectInstance Instance;
	Instance.SourceObjPath = ObjPath;
	Instance.Actor = ObjectActor;
	Instance.RandomRotation = BaseMeshRotation; // Will be randomized in UpdateObjectLayout
	Instance.DiffuseTexture = CachedData.TextureSet.Diffuse;
	Instance.MetallicTexture = CachedData.TextureSet.Metallic;
	Instance.NormalTexture = CachedData.TextureSet.Normal;
	Instance.RoughnessTexture = CachedData.TextureSet.Roughness;
	Instance.PBRTexture = CachedData.TextureSet.PBR;
	Instance.ShadedTexture = CachedData.TextureSet.Shaded;

	ObjectInstances.Add(Instance);

	return true;
}

// ============================================================
// Set up actors 
// ============================================================

AActor* UHyper3DObjectsSubsystem::CreateObjectActor(const FString& ObjPath, FTextureSet& TextureSet)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	AActor* ObjectActor = World->SpawnActor<AActor>();
	if (!ObjectActor)
	{
		return nullptr;
	}

	USceneComponent* Root = NewObject<USceneComponent>(ObjectActor);
	ObjectActor->AddInstanceComponent(Root);
	Root->RegisterComponent();
	Root->SetMobility(EComponentMobility::Movable);
	ObjectActor->SetRootComponent(Root);

	// Load any textures that weren't already loaded by ResolveAllTexturesForOBJ
	const FString Directory = FPaths::GetPath(ObjPath);
	
	// Helper function to load texture from path
	auto LoadTextureFromPath = [this](const FString& TexturePath) -> UTexture2D*
	{
		if (TexturePath.IsEmpty() || !FPaths::FileExists(TexturePath))
		{
			return nullptr;
		}

		if(debug) UE_LOG(LogTemp, Display, TEXT("[Hyper3DObjects] Loading texture from: %s"), *TexturePath);
		
		UTexture2D* Texture = nullptr;
#if WITH_EDITOR
		Texture = ImportTextureAsAsset(TexturePath);
		if (!Texture)
		{
			Texture = LoadTextureFromFile(TexturePath);
		}
#else
		Texture = LoadTextureFromFile(TexturePath);
#endif
		
		if (Texture)
		{
			if(debug) UE_LOG(LogTemp, Display, TEXT("[Hyper3DObjects] Successfully loaded texture: %s (Size: %dx%d)"), 
				*Texture->GetName(), Texture->GetSizeX(), Texture->GetSizeY());
		}
		else
		{
			if(debug) UE_LOG(LogTemp, Warning, TEXT("[Hyper3DObjects] Failed to load texture from: %s"), *TexturePath);
		}
		
		return Texture;
	};
	
	if (TextureSet.Diffuse == nullptr)
	{
		FString DiffusePath = this->FindTextureInDirectory(Directory, TEXT("texture_diffuse"));
		if (DiffusePath.IsEmpty()) DiffusePath = this->FindTextureInDirectory(Directory, TEXT("diffuse"));
		if (!DiffusePath.IsEmpty()) TextureSet.Diffuse = LoadTextureFromPath(DiffusePath);
	}

	if (TextureSet.Metallic == nullptr)
	{
		FString MetallicPath = this->FindTextureInDirectory(Directory, TEXT("texture_metallic"));
		if (MetallicPath.IsEmpty()) MetallicPath = this->FindTextureInDirectory(Directory, TEXT("metallic"));
		if (!MetallicPath.IsEmpty()) TextureSet.Metallic = LoadTextureFromPath(MetallicPath);
	}

	if (TextureSet.Normal == nullptr)
	{
		FString NormalPath = this->FindTextureInDirectory(Directory, TEXT("texture_normal"));
		if (NormalPath.IsEmpty()) NormalPath = this->FindTextureInDirectory(Directory, TEXT("normal"));
		if (!NormalPath.IsEmpty()) TextureSet.Normal = LoadTextureFromPath(NormalPath);
	}

	if (TextureSet.Roughness == nullptr)
	{
		FString RoughnessPath = this->FindTextureInDirectory(Directory, TEXT("texture_roughness"));
		if (RoughnessPath.IsEmpty()) RoughnessPath = this->FindTextureInDirectory(Directory, TEXT("roughness"));
		if (!RoughnessPath.IsEmpty()) TextureSet.Roughness = LoadTextureFromPath(RoughnessPath);
	}

	if (TextureSet.PBR == nullptr)
	{
		FString PBRPath = this->FindTextureInDirectory(Directory, TEXT("texture_pbr"));
		if (PBRPath.IsEmpty()) PBRPath = this->FindTextureInDirectory(Directory, TEXT("pbr"));
		if (!PBRPath.IsEmpty()) TextureSet.PBR = LoadTextureFromPath(PBRPath);
	}

	if (TextureSet.Shaded == nullptr)
	{
		FString ShadedPath = this->FindTextureInDirectory(Directory, TEXT("shaded"));
		if (!ShadedPath.IsEmpty()) TextureSet.Shaded = LoadTextureFromPath(ShadedPath);
	}

	return ObjectActor;
}

UProceduralMeshComponent* UHyper3DObjectsSubsystem::CreateProceduralMeshFromOBJ(
	AActor* Owner,
	const TArray<FVector>& Vertices,
	const TArray<int32>& Triangles,
	const TArray<FVector>& Normals,
	const TArray<FVector2D>& UVs,
	const TArray<FColor>& Colors)
{
	if (!Owner)
	{
		return nullptr;
	}

	UProceduralMeshComponent* MeshComp = NewObject<UProceduralMeshComponent>(Owner);
	if (!MeshComp)
	{
		return nullptr;
	}

	MeshComp->SetupAttachment(Owner->GetRootComponent());
	Owner->AddInstanceComponent(MeshComp);

	TArray<FProcMeshTangent> Tangents;
	MeshComp->CreateMeshSection(0, Vertices, Triangles, Normals, UVs, Colors, Tangents, false);
	MeshComp->SetMobility(EComponentMobility::Movable);
	// Disable collision - make objects non-collidable
	MeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MeshComp->RegisterComponent();

	return MeshComp;
}

bool UHyper3DObjectsSubsystem::LoadOBJ(
	const FString& ObjPath,
	TArray<FVector>& OutVertices,
	TArray<int32>& OutTriangles,
	TArray<FVector>& OutNormals,
	TArray<FVector2D>& OutUVs,
	TArray<FColor>& OutColors,
	FString& OutMtlFile)
{
	TArray<FString> Lines;
	if (!FFileHelper::LoadFileToStringArray(Lines, *ObjPath))
	{
		UE_LOG(LogTemp, Error, TEXT("[Hyper3DObjects] Failed to load OBJ: %s"), *ObjPath);
		return false;
	}

	TArray<FVector> Positions;
	TArray<FVector> NormalVectors;
	TArray<FVector2D> TexCoords;
	TArray<FColor> VertexColors;

	struct FOBJIndex
	{
		int32 Position = -1;
		int32 TexCoord = -1;
		int32 Normal = -1;
	};

	TArray<TArray<FOBJIndex>> Faces;
	Faces.Reserve(Lines.Num());

	for (const FString& RawLine : Lines)
	{
		const FString Line = RawLine.TrimStart();

		if (Line.StartsWith(TEXT("v ")))
		{
			TArray<FString> Parts;
			Line.ParseIntoArray(Parts, TEXT(" "), true);

			if (Parts.Num() >= 4)
			{
				Positions.Add(FVector(
					FCString::Atof(*Parts[1]),
					FCString::Atof(*Parts[2]),
					FCString::Atof(*Parts[3])
				));

				if (Parts.Num() >= 7)
				{
					VertexColors.Add(FLinearColor(
						FCString::Atof(*Parts[4]) / 255.0f,
						FCString::Atof(*Parts[5]) / 255.0f,
						FCString::Atof(*Parts[6]) / 255.0f
					).ToFColor(true));
				}
				else
				{
					VertexColors.Add(FColor::White);
				}
			}
		}
		else if (Line.StartsWith(TEXT("vt ")))
		{
			TArray<FString> Parts;
			Line.ParseIntoArray(Parts, TEXT(" "), true);

			if (Parts.Num() >= 3)
			{
				TexCoords.Add(FVector2D(
					FCString::Atof(*Parts[1]),
					1.0f - FCString::Atof(*Parts[2])
				));
			}
		}
		else if (Line.StartsWith(TEXT("vn ")))
		{
			TArray<FString> Parts;
			Line.ParseIntoArray(Parts, TEXT(" "), true);

			if (Parts.Num() >= 4)
			{
				NormalVectors.Add(FVector(
					FCString::Atof(*Parts[1]),
					FCString::Atof(*Parts[2]),
					FCString::Atof(*Parts[3])
				));
			}
		}
		else if (Line.StartsWith(TEXT("f ")))
		{
			TArray<FString> Parts;
			Line.ParseIntoArray(Parts, TEXT(" "), true);

			TArray<FOBJIndex> Face;
			for (int32 PartIdx = 1; PartIdx < Parts.Num(); ++PartIdx)
			{
				TArray<FString> Indices;
				Parts[PartIdx].ParseIntoArray(Indices, TEXT("/"), false);

				FOBJIndex Idx;
				if (Indices.Num() > 0 && !Indices[0].IsEmpty())
				{
					Idx.Position = FCString::Atoi(*Indices[0]) - 1;
				}
				if (Indices.Num() > 1 && !Indices[1].IsEmpty())
				{
					Idx.TexCoord = FCString::Atoi(*Indices[1]) - 1;
				}
				if (Indices.Num() > 2 && !Indices[2].IsEmpty())
				{
					Idx.Normal = FCString::Atoi(*Indices[2]) - 1;
				}

				Face.Add(Idx);
			}

			if (Face.Num() >= 3)
			{
				Faces.Add(Face);
			}
		}
		else if (Line.StartsWith(TEXT("mtllib ")))
		{
			TArray<FString> Parts;
			Line.ParseIntoArray(Parts, TEXT(" "), true);
			if (Parts.Num() >= 2)
			{
				OutMtlFile = Parts[1];
			}
		}
	}

	OutVertices.Reset();
	OutTriangles.Reset();
	OutNormals.Reset();
	OutUVs.Reset();
	OutColors.Reset();

	int32 VertexCounter = 0;
	for (const TArray<FOBJIndex>& Face : Faces)
	{
		for (int32 TriIdx = 1; TriIdx < Face.Num() - 1; ++TriIdx)
		{
			const FOBJIndex Indices[3] = { Face[0], Face[TriIdx], Face[TriIdx + 1] };

			for (int32 Corner = 0; Corner < 3; ++Corner)
			{
				const FOBJIndex& Idx = Indices[Corner];

				if (!Positions.IsValidIndex(Idx.Position))
				{
					if(debug)(LogTemp, Warning, TEXT("[Hyper3DObjects] Invalid position index in OBJ: %s"), *ObjPath);
					return false;
				}

				OutVertices.Add(Positions[Idx.Position]);
				OutTriangles.Add(VertexCounter++);

				if (NormalVectors.IsValidIndex(Idx.Normal))
				{
					OutNormals.Add(NormalVectors[Idx.Normal]);
				}
				else
				{
					OutNormals.Add(FVector::UpVector);
				}

				if (TexCoords.IsValidIndex(Idx.TexCoord))
				{
					OutUVs.Add(TexCoords[Idx.TexCoord]);
				}
				else
				{
					OutUVs.Add(FVector2D::ZeroVector);
				}

				if (VertexColors.IsValidIndex(Idx.Position))
				{
					OutColors.Add(VertexColors[Idx.Position]);
				}
				else
				{
					OutColors.Add(FColor::White);
				}
			}
		}
	}

	return OutVertices.Num() > 0 && OutTriangles.Num() > 0;
}


// ============================================================
// Set up textures 
// ============================================================


UHyper3DObjectsSubsystem::FTextureSet UHyper3DObjectsSubsystem::ResolveAllTexturesForOBJ(const FString& ObjPath, const FString& MtlFile) const
{
	FTextureSet TextureSet;
	const FString Directory = FPaths::GetPath(ObjPath);
	
	// First, try to find all textures in the directory
	TextureSet = FindAllTexturesInDirectory(Directory);
	
	// If we found textures, return them
	if (TextureSet.Diffuse || TextureSet.Metallic || TextureSet.Normal || 
	    TextureSet.Roughness || TextureSet.PBR || TextureSet.Shaded)
	{
		return TextureSet;
	}
	
	// Fallback: try MTL file for diffuse
	if (!MtlFile.IsEmpty())
	{
		const FString AbsoluteMtl = FPaths::ConvertRelativePathToFull(Directory, MtlFile);
		if (FPaths::FileExists(AbsoluteMtl))
		{
			const FString DiffusePath = ExtractTextureFromMTL(AbsoluteMtl);
			if (!DiffusePath.IsEmpty() && FPaths::FileExists(DiffusePath))
			{
				TextureSet.Diffuse = LoadTextureFromFile(DiffusePath);
			}
		}
	}
	
	return TextureSet;
}

FString UHyper3DObjectsSubsystem::ResolveTextureForOBJ(const FString& ObjPath, const FString& MtlFile) const
{
	FTextureSet TextureSet = this->ResolveAllTexturesForOBJ(ObjPath, MtlFile);
	
	// Return diffuse path if available (for backward compatibility)
	if (TextureSet.Diffuse)
	{
		// We can't get the path from the texture, so try to find it
		return this->FindTextureInDirectory(FPaths::GetPath(ObjPath), TEXT("texture_diffuse"));
	}
	
	return this->FindFallbackTexture(ObjPath);
}

FString UHyper3DObjectsSubsystem::ExtractTextureFromMTL(const FString& MtlPath) const
{
	TArray<FString> Lines;
	if (!FFileHelper::LoadFileToStringArray(Lines, *MtlPath))
	{
		return FString();
	}

	for (const FString& RawLine : Lines)
	{
		const FString Line = RawLine.TrimStart();
		if (Line.StartsWith(TEXT("map_Kd ")))
		{
			TArray<FString> Parts;
			Line.ParseIntoArray(Parts, TEXT(" "), true);
			if (Parts.Num() >= 2)
			{
				const FString TextureRelative = Parts.Last();
				return FPaths::ConvertRelativePathToFull(FPaths::GetPath(MtlPath), TextureRelative);
			}
		}
	}

	return FString();
}

FString UHyper3DObjectsSubsystem::FindTextureInDirectory(const FString& Directory, const FString& TextureName) const
{
	const TArray<FString> PossibleExtensions = { TEXT(".png"), TEXT(".jpg"), TEXT(".jpeg"), TEXT(".bmp"), TEXT(".tga") };
	
	for (const FString& Extension : PossibleExtensions)
	{
		const FString Candidate = Directory / (TextureName + Extension);
		if (FPaths::FileExists(Candidate))
		{
			return Candidate;
		}
	}
	
	return FString();
}

UHyper3DObjectsSubsystem::FTextureSet UHyper3DObjectsSubsystem::FindAllTexturesInDirectory(const FString& Directory) const
{
	FTextureSet TextureSet;
	
	// Try to find each texture type
	FString DiffusePath = FindTextureInDirectory(Directory, TEXT("texture_diffuse"));
	if (DiffusePath.IsEmpty()) DiffusePath = FindTextureInDirectory(Directory, TEXT("diffuse"));
	if (!DiffusePath.IsEmpty()) TextureSet.Diffuse = LoadTextureFromFile(DiffusePath);
	
	FString MetallicPath = FindTextureInDirectory(Directory, TEXT("texture_metallic"));
	if (MetallicPath.IsEmpty()) MetallicPath = FindTextureInDirectory(Directory, TEXT("metallic"));
	if (!MetallicPath.IsEmpty()) TextureSet.Metallic = LoadTextureFromFile(MetallicPath);
	
	FString NormalPath = FindTextureInDirectory(Directory, TEXT("texture_normal"));
	if (NormalPath.IsEmpty()) NormalPath = FindTextureInDirectory(Directory, TEXT("normal"));
	if (!NormalPath.IsEmpty()) TextureSet.Normal = LoadTextureFromFile(NormalPath);
	
	FString RoughnessPath = FindTextureInDirectory(Directory, TEXT("texture_roughness"));
	if (RoughnessPath.IsEmpty()) RoughnessPath = FindTextureInDirectory(Directory, TEXT("roughness"));
	if (!RoughnessPath.IsEmpty()) TextureSet.Roughness = LoadTextureFromFile(RoughnessPath);
	return TextureSet;
}

FString UHyper3DObjectsSubsystem::FindFallbackTexture(const FString& ObjPath) const
{
	const FString Directory = FPaths::GetPath(ObjPath);
	const FString BaseName = FPaths::GetBaseFilename(ObjPath);

	const TArray<FString> PossibleExtensions = { TEXT(".png"), TEXT(".jpg"), TEXT(".jpeg") };
	
	// First, try textures with the same name as the OBJ file
	for (const FString& Extension : PossibleExtensions)
	{
		const FString Candidate = Directory / (BaseName + Extension);
		if (FPaths::FileExists(Candidate))
		{
			if(debug) UE_LOG(LogTemp, Display, TEXT("[Hyper3DObjects] Found texture matching OBJ name: %s"), *Candidate);
			return Candidate;
		}
	}
	if(debug) UE_LOG(LogTemp, Warning, TEXT("[Hyper3DObjects] No texture found for OBJ: %s"), *ObjPath);
	return FString();
}

UTexture2D* UHyper3DObjectsSubsystem::ImportTextureAsAsset(const FString& TexturePath) const
{
#if WITH_EDITOR
	if (TexturePath.IsEmpty() || !FPaths::FileExists(TexturePath))
	{
		return nullptr;
	}

	// Determine the target asset path
	FString TextureFileName = FPaths::GetBaseFilename(TexturePath);
	FString TargetPackagePath = TEXT("/Game/ImportedTextures/");
	FString TargetAssetPath = TargetPackagePath + TextureFileName;

	// Check if texture already exists
	UTexture2D* ExistingTexture = LoadObject<UTexture2D>(nullptr, *TargetAssetPath);
	if (ExistingTexture)
	{
		if(debug) UE_LOG(LogTemp, Display, TEXT("[Hyper3DObjects] Using existing texture asset: %s"), *TargetAssetPath);
		return ExistingTexture;
	}

	FAssetToolsModule& AssetToolsModule = FModuleManager::Get().LoadModuleChecked<FAssetToolsModule>("AssetTools");
	
	// Create import task
	TArray<FString> ImportPaths;
	ImportPaths.Add(TexturePath);

	// Import using AssetTools
	TArray<UObject*> ImportedAssets = AssetToolsModule.Get().ImportAssets(ImportPaths, TargetPackagePath);
	
	if (ImportedAssets.Num() > 0)
	{
		UTexture2D* ImportedTexture = Cast<UTexture2D>(ImportedAssets[0]);
		if (ImportedTexture)
		{
			// Configure texture settings
			ImportedTexture->SRGB = true;
			ImportedTexture->CompressionSettings = TC_Default;
			ImportedTexture->MipGenSettings = TMGS_FromTextureGroup;

			// Mark package as dirty
			ImportedTexture->MarkPackageDirty();
			FAssetRegistryModule::AssetCreated(ImportedTexture);

			if(debug) UE_LOG(LogTemp, Display, TEXT("[Hyper3DObjects] Successfully imported texture as asset: %s"), *TargetAssetPath);
			return ImportedTexture;
		}
	}

	if(debug) UE_LOG(LogTemp, Warning, TEXT("[Hyper3DObjects] Failed to import texture as asset: %s"), *TexturePath);
	return nullptr;
#else
	return nullptr;
#endif
}

UTexture2D* UHyper3DObjectsSubsystem::LoadTextureFromFile(const FString& TexturePath) const
{
	if (TexturePath.IsEmpty() || !FPaths::FileExists(TexturePath))
	{
		return nullptr;
	}

	TArray<uint8> FileData;
	if (!FFileHelper::LoadFileToArray(FileData, *TexturePath))
	{
		return nullptr;
	}

	const FString Extension = FPaths::GetExtension(TexturePath);
	EImageFormat ImageFormat = EImageFormat::Invalid;

	if (Extension.Equals(TEXT("png"), ESearchCase::IgnoreCase))
	{
		ImageFormat = EImageFormat::PNG;
	}
	else if (Extension.Equals(TEXT("jpg"), ESearchCase::IgnoreCase) || Extension.Equals(TEXT("jpeg"), ESearchCase::IgnoreCase))
	{
		ImageFormat = EImageFormat::JPEG;
	}
	else if (Extension.Equals(TEXT("bmp"), ESearchCase::IgnoreCase))
	{
		ImageFormat = EImageFormat::BMP;
	}
	else if (Extension.Equals(TEXT("tga"), ESearchCase::IgnoreCase))
	{
		ImageFormat = EImageFormat::TGA;
	}

	if (ImageFormat == EImageFormat::Invalid)
	{
		return nullptr;
	}

	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>("ImageWrapper");
	TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(ImageFormat);
	if (!ImageWrapper.IsValid() || !ImageWrapper->SetCompressed(FileData.GetData(), FileData.Num()))
	{
		return nullptr;
	}

	TArray<uint8> RawData;
	if (!ImageWrapper->GetRaw(ERGBFormat::BGRA, 8, RawData))
	{
		return nullptr;
	}

	const int32 Width = ImageWrapper->GetWidth();
	const int32 Height = ImageWrapper->GetHeight();
	if (Width <= 0 || Height <= 0)
	{
		return nullptr;
	}

	UTexture2D* Texture = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8);
	if (!Texture)
	{
		return nullptr;
	}

	Texture->SRGB = true;
#if WITH_EDITORONLY_DATA
	Texture->CompressionNone = true;
	Texture->MipGenSettings = TMGS_NoMipmaps;
#endif

	if (!Texture->GetPlatformData() || Texture->GetPlatformData()->Mips.Num() == 0)
	{
		return nullptr;
	}

	void* TextureData = Texture->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(TextureData, RawData.GetData(), RawData.Num());
	Texture->GetPlatformData()->Mips[0].BulkData.Unlock();
	Texture->UpdateResource();

	return Texture;
}

UMaterialInterface* UHyper3DObjectsSubsystem::GetOrCreateBaseMaterial() const
{
	// Try to load existing materials - prioritize user's material with texture parameters
	UMaterialInterface* BaseMaterial = nullptr;
	
	// First, try to find the user's material using AssetRegistry (more reliable)
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	
	// Try the specific path first: /Game/_GENERATED/Materials/M_ProceduralMeshTexture
	// Use SoftObjectPath instead of deprecated GetAssetByObjectPath
	FSoftObjectPath SpecificMaterialPath(TEXT("/Game/_GENERATED/Materials/M_ProceduralMeshTexture.M_ProceduralMeshTexture"));
	BaseMaterial = Cast<UMaterialInterface>(SpecificMaterialPath.TryLoad());
	if (BaseMaterial)
	{
		if(debug) UE_LOG(LogTemp, Display, TEXT("[Hyper3DObjects] Found user's material via SoftObjectPath at specific path: %s"), *BaseMaterial->GetPathName());
		return BaseMaterial;
	}
	
	// Search for materials with "ProceduralMeshTexture" in the name (fallback)
	TArray<FAssetData> AssetDataList;
	FARFilter Filter;
	Filter.ClassPaths.Add(UMaterial::StaticClass()->GetClassPathName());
	Filter.ClassPaths.Add(UMaterialInstance::StaticClass()->GetClassPathName());
	Filter.bRecursivePaths = true;
	
	// Try specific paths first
	Filter.PackagePaths.Add(FName("/Game/_GENERATED/Materials")); // Prioritize the _GENERATED folder
	AssetRegistryModule.Get().GetAssets(Filter, AssetDataList);
	
	// Look for materials with "ProceduralMeshTexture" or "M_ProceduralMeshTexture" in the name
	for (const FAssetData& AssetData : AssetDataList)
	{
		FString AssetName = AssetData.AssetName.ToString();
		if (AssetName.Contains(TEXT("ProceduralMeshTexture"), ESearchCase::IgnoreCase) ||
			AssetName.Contains(TEXT("M_ProceduralMeshTexture"), ESearchCase::IgnoreCase))
		{
			BaseMaterial = Cast<UMaterialInterface>(AssetData.GetAsset());
			if (BaseMaterial)
			{
				if(debug) UE_LOG(LogTemp, Display, TEXT("[Hyper3DObjects] Found user's material via AssetRegistry: %s"), *BaseMaterial->GetPathName());
				return BaseMaterial;
			}
		}
	}
	
	// Also search in /Game root (broader search)
	AssetDataList.Empty();
	Filter.PackagePaths.Empty();
	Filter.PackagePaths.Add(FName("/Game"));
	AssetRegistryModule.Get().GetAssets(Filter, AssetDataList);
	
	for (const FAssetData& AssetData : AssetDataList)
	{
		FString AssetName = AssetData.AssetName.ToString();
		if (AssetName.Contains(TEXT("ProceduralMeshTexture"), ESearchCase::IgnoreCase) ||
			AssetName.Contains(TEXT("M_ProceduralMeshTexture"), ESearchCase::IgnoreCase))
		{
			BaseMaterial = Cast<UMaterialInterface>(AssetData.GetAsset());
			if (BaseMaterial)
			{
				if(debug) UE_LOG(LogTemp, Display, TEXT("[Hyper3DObjects] Found user's material via AssetRegistry (broader search): %s"), *BaseMaterial->GetPathName());
				return BaseMaterial;
			}
		}
	}
	
	// Try direct path loading - prioritize the specific path the user wants
	// First try: /Game/_GENERATED/Materials/M_ProceduralMeshTexture.M_ProceduralMeshTexture
	BaseMaterial = LoadObject<UMaterialInterface>(nullptr, Hyper3DObjectsImport::ProceduralMeshTextureMaterialPath);
	if (BaseMaterial)
	{
		if(debug) UE_LOG(LogTemp, Display, TEXT("[Hyper3DObjects] Found user's material via direct path: %s"), *BaseMaterial->GetPathName());
		return BaseMaterial;
	}
	
	// Fallback paths (in case primary is not found)
	if (!BaseMaterial)
	{
		BaseMaterial = LoadObject<UMaterialInterface>(nullptr, Hyper3DObjectsImport::ProceduralMeshTextureMaterialPathAlt);
	}
	if (!BaseMaterial)
	{
		BaseMaterial = LoadObject<UMaterialInterface>(nullptr, Hyper3DObjectsImport::ProceduralMeshTextureMaterialPathAlt2);
	}
	if (BaseMaterial)
	{
		if(debug) UE_LOG(LogTemp, Display, TEXT("[Hyper3DObjects] Found user's material via fallback path: %s"), *BaseMaterial->GetPathName());
		return BaseMaterial;
	}
	
	if(debug) UE_LOG(LogTemp, Warning, TEXT("[Hyper3DObjects] Could not find M_ProceduralMeshTexture material. Searched paths:"));
	if(debug) UE_LOG(LogTemp, Warning, TEXT("[Hyper3DObjects]   - %s (PRIMARY)"), Hyper3DObjectsImport::ProceduralMeshTextureMaterialPath);
	if(debug) UE_LOG(LogTemp, Warning, TEXT("[Hyper3DObjects]   - %s"), Hyper3DObjectsImport::ProceduralMeshTextureMaterialPathAlt);
	if(debug) UE_LOG(LogTemp, Warning, TEXT("[Hyper3DObjects]   - %s"), Hyper3DObjectsImport::ProceduralMeshTextureMaterialPathAlt2);
	if(debug) UE_LOG(LogTemp, Warning, TEXT("[Hyper3DObjects] Falling back to default materials..."));
	
	// Fallback to other materials (only if primary material not found)
	// Note: These fallbacks are kept for compatibility but should rarely be needed
	BaseMaterial = LoadObject<UMaterialInterface>(nullptr, Hyper3DObjectsImport::VertexColorMaterialPathA);
	if (!BaseMaterial)
	{
		BaseMaterial = LoadObject<UMaterialInterface>(nullptr, Hyper3DObjectsImport::VertexColorMaterialPathB);
	}
	if (!BaseMaterial)
	{
		BaseMaterial = LoadObject<UMaterialInterface>(nullptr, Hyper3DObjectsImport::EditorVertexColorMaterialPath);
	}

#if WITH_EDITOR
	// If no material found, try to create a simple material with texture parameters
	if (!BaseMaterial)
	{
		BaseMaterial = CreateMaterialWithTextureParameters();
	}
#endif

	return BaseMaterial;
}

#if WITH_EDITOR
UMaterial* UHyper3DObjectsSubsystem::CreateMaterialWithTextureParameters() const
{
	// Material creation programmatically is complex in UE5
	// For now, return nullptr and log a message
	// The user should create a material manually with texture parameters:
	// - BaseColor (Texture2D Parameter)
	// - Normal (Texture2D Parameter) 
	// - Metallic (Texture2D Parameter)
	// - Roughness (Texture2D Parameter)
	
	if(debug) UE_LOG(LogTemp, Warning, TEXT("[Hyper3DObjects] Material creation not implemented. Please create a material manually with texture parameters: BaseColor, Normal, Metallic, Roughness"));
	return nullptr;
}
#endif

void UHyper3DObjectsSubsystem::ApplyMaterial(
	UProceduralMeshComponent* MeshComp,
	AActor* Owner,
	const FTextureSet& Textures,
	const TArray<FColor>& VertexColors) const
{
	if (!MeshComp)
	{
		return;
	}

	// Try to get a base material
	UMaterialInterface* BaseMaterial = GetOrCreateBaseMaterial();

	if (!BaseMaterial)
	{
		if(debug) UE_LOG(LogTemp, Warning, TEXT("[Hyper3DObjects] Could not find any base material. Trying to use default engine material."));
		// Last resort: try to find any material in the engine
		BaseMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineMaterials/DefaultMaterial"));
		
		if (!BaseMaterial)
		{
			if(debug) UE_LOG(LogTemp, Error, TEXT("[Hyper3DObjects] No materials available. Mesh will be untextured."));
			return;
		}
	}

	UObject* MaterialOuter = Owner ? static_cast<UObject*>(Owner) : static_cast<UObject*>(MeshComp);
	UMaterialInstanceDynamic* DynamicMaterial = UMaterialInstanceDynamic::Create(BaseMaterial, MaterialOuter);
	if (!DynamicMaterial)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Hyper3DObjects] Failed to create dynamic material instance"));
		return;
	}

	// Get all available texture parameters from the material
	TArray<FMaterialParameterInfo> AvailableTextureParams;
	TArray<FGuid> ParamIds;
	DynamicMaterial->GetAllTextureParameterInfo(AvailableTextureParams, ParamIds);
	
	if (AvailableTextureParams.Num() == 0)
	{
		if(debug) UE_LOG(LogTemp, Error, TEXT("[Hyper3DObjects] WARNING: The base material has no texture parameters! Textures cannot be applied."));
		if(debug) UE_LOG(LogTemp, Error, TEXT("[Hyper3DObjects] Please create a material with texture parameters (BaseColor, Normal, Metallic, Roughness) and set it as the base material."));
		if(debug) UE_LOG(LogTemp, Error, TEXT("[Hyper3DObjects] The material path being used is: %s"), BaseMaterial ? *BaseMaterial->GetPathName() : TEXT("None"));
		if(debug) UE_LOG(LogTemp, Error, TEXT("[Hyper3DObjects] Material name: %s"), BaseMaterial ? *BaseMaterial->GetName() : TEXT("None"));
	}

	// Helper function to find and set texture parameter
	auto SetTextureParameter = [&](UTexture2D* Texture, const TArray<FName>& ParameterNames, const FString& TextureType) -> bool
	{
		if (!Texture)
		{
			return false;
		}

		// First try to find an existing parameter
		for (const FName& ParamName : ParameterNames)
		{
			if (AvailableTextureParams.ContainsByPredicate([&ParamName](const FMaterialParameterInfo& Info)
			{
				return Info.Name == ParamName;
			}))
			{
				DynamicMaterial->SetTextureParameterValue(ParamName, Texture);
				return true;
			}
		}

		// If no parameter found, try SetTextureParameterValueByInfo (might work even without parameter)
		for (const FName& ParamName : ParameterNames)
		{
			FMaterialParameterInfo ParamInfo(ParamName);
			DynamicMaterial->SetTextureParameterValueByInfo(ParamInfo, Texture);
			// Just try to set it - if the parameter doesn't exist, it will silently fail
			// Note: We can't verify if it worked without GetTextureParameterValueByInfo, but we'll try anyway
			return true; // Assume it worked
		}

		if(debug) UE_LOG(LogTemp, Warning, TEXT("[Hyper3DObjects] Could not apply %s texture - no matching parameter found"), *TextureType);
		return false;
	};

	// Apply Diffuse/BaseColor texture (use Shaded if no Diffuse)
	UTexture2D* BaseColorTexture = Textures.Diffuse ? Textures.Diffuse : Textures.Shaded;
	if (BaseColorTexture)
	{
		static const FName BaseColorParams[] = {
			TEXT("BaseColor"), TEXT("BaseColorTexture"), TEXT("Diffuse"), TEXT("DiffuseTexture"),
			TEXT("Texture"), TEXT("ColorTexture"), TEXT("MainTexture"), TEXT("Albedo"), TEXT("AlbedoTexture")
		};
		SetTextureParameter(BaseColorTexture, TArray<FName>(BaseColorParams, UE_ARRAY_COUNT(BaseColorParams)), TEXT("BaseColor/Diffuse"));
	}

	// Apply Normal map
	if (Textures.Normal)
	{
		static const FName NormalParams[] = {
			TEXT("Normal"), TEXT("NormalMap"), TEXT("NormalTexture"), TEXT("BumpMap")
		};
		SetTextureParameter(Textures.Normal, TArray<FName>(NormalParams, UE_ARRAY_COUNT(NormalParams)), TEXT("Normal"));
	}

	// Apply Metallic texture
	if (Textures.Metallic)
	{
		static const FName MetallicParams[] = {
			TEXT("Metallic"), TEXT("MetallicTexture"), TEXT("Metalness"), TEXT("MetalnessTexture")
		};
		SetTextureParameter(Textures.Metallic, TArray<FName>(MetallicParams, UE_ARRAY_COUNT(MetallicParams)), TEXT("Metallic"));
	}

	// Apply Roughness texture
	if (Textures.Roughness)
	{
		static const FName RoughnessParams[] = {
			TEXT("Roughness"), TEXT("RoughnessTexture"), TEXT("Rough"), TEXT("RoughTexture")
		};
		SetTextureParameter(Textures.Roughness, TArray<FName>(RoughnessParams, UE_ARRAY_COUNT(RoughnessParams)), TEXT("Roughness"));
	}

	// Apply PBR texture (could be used for combined metallic/roughness or other PBR maps)
	if (Textures.PBR)
	{
		static const FName PBRParams[] = {
			TEXT("PBR"), TEXT("PBRTexture"), TEXT("MetallicRoughness"), TEXT("MetallicRoughnessTexture")
		};
		SetTextureParameter(Textures.PBR, TArray<FName>(PBRParams, UE_ARRAY_COUNT(PBRParams)), TEXT("PBR"));
	}

	MeshComp->SetMaterial(0, DynamicMaterial);
}

FString UHyper3DObjectsSubsystem::GetImportDirectory() const
{
	const FString PluginDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectPluginsDir() / TEXT("RealityStream/MeshImport"));

	if (FPaths::DirectoryExists(PluginDir))
	{
		return PluginDir;
	}

	// Fallback to explicit path provided by the user (absolute path)
	return Null;
}

void UHyper3DObjectsSubsystem::DestroyAllObjects()
{
	for (FObjectInstance& Instance : ObjectInstances)
	{
		if (AActor* Actor = Instance.Actor.Get())
		{
			Actor->Destroy();
		}

		if (UTexture2D* Texture = Instance.DiffuseTexture.Get())
		{
			LoadedTextures.Remove(Texture);
		}
	}

	ObjectInstances.Empty();
	LoadedTextures.Empty();
	MeshDataCache.Empty(); // Clear cache when destroying all objects
}


