#include "SplatCreator/SplatCreatorSubsystem.h"
#include "ComfyStream/ComfyImageSender.h"
#include "Engine/World.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "GameFramework/Actor.h"
#include "Engine/StaticMesh.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Engine.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/LocalPlayer.h"
#include "Math/RotationMatrix.h"
#include "Math/RandomStream.h"
#include "Math/Box.h"

int debug = 0; //0 = off, 1 = on


// ============================================================
// Initialize
// ============================================================

void USplatCreatorSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
   	if(debug) UE_LOG(LogTemp, Display, TEXT("[SplatCreator] Subsystem initialized"));
}


// ============================================================
// PointCloud
// ============================================================

void USplatCreatorSubsystem::StartPointCloudSystem()
{
	if (bIsInitialized)
	{
		if(debug) UE_LOG(LogTemp, Display, TEXT("[SplatCreator] Already initialized"));
		return;
	}
	
	UWorld* World = GetWorld();
	if (!World)
	{
		if(debug) UE_LOG(LogTemp, Error, TEXT("[SplatCreator] Cannot start - no world available"));
		return;
	}
	
	if(debug) UE_LOG(LogTemp, Display, TEXT("[SplatCreator] Starting point cloud system..."));
	
	ScanForPLYFiles();
	
	if (PlyFiles.Num() == 0)
	{
		if(debug) UE_LOG(LogTemp, Warning, TEXT("[SplatCreator] No PLY files found in %s"), *GetSplatCreatorFolder());
		return;
	}
	
	if(debug) UE_LOG(LogTemp, Display, TEXT("[SplatCreator] Found %d PLY files"), PlyFiles.Num());
	
	// Load first PLY file
	FString FirstPLYPath = GetSplatCreatorFolder() / PlyFiles[0];
	LoadPLYFile(FirstPLYPath);
	
	// Start cycle timer (45 seconds)
	// Note: Timer may be delayed if game thread is busy, but optimizations should prevent that
	World->GetTimerManager().SetTimer(
		CycleTimer,
		this,
		&USplatCreatorSubsystem::CycleToNextPLY,
		45.0f,
		true
	);
	if(debug) UE_LOG(LogTemp, Display, TEXT("[SplatCreator] Cycle timer started - will change PLY every 45 seconds"));
	
	bIsInitialized = true;
}

void USplatCreatorSubsystem::Deinitialize()
{
	UWorld* World = GetWorld();
	//make GetWorld() pointer 
	if (CycleTimer.IsValid() && World)
	{
		World->GetTimerManager().ClearTimer(CycleTimer);
	}
	if (MorphTimer.IsValid() && World)
	{
		World->GetTimerManager().ClearTimer(MorphTimer);
	}
	if (BobbingTimer.IsValid() && World)
	{
		World->GetTimerManager().ClearTimer(BobbingTimer);
	}
	if (RandomMovementTimer.IsValid() && World)
	{
		World->GetTimerManager().ClearTimer(RandomMovementTimer);
	}
	if (CurrentPointCloudActor)
	{
		CurrentPointCloudActor->Destroy();
	}
	if (ComfyImageSender)
	{
		ComfyImageSender->Disconnect();
	}
	Super::Deinitialize();
}

// ============================================================
// FIND PLYs
// ============================================================

FString USplatCreatorSubsystem::GetSplatCreatorFolder() const
{
	FString PluginDir = FPaths::ProjectPluginsDir() / TEXT("RealityStream");
	FString SplatCreatorDir = PluginDir / TEXT("SplatCreatorOutputs");
	return SplatCreatorDir;
}

void USplatCreatorSubsystem::TrySendImageToComfyUI(const FString& PLYPath)
{
	if (!bSendImageToComfyUIOnPlyChange)
	{
		UE_LOG(LogTemp, Verbose, TEXT("[SplatCreator] Image send disabled (bSendImageToComfyUIOnPlyChange=false)"));
		return;
	}
	if (ComfyUIWebSocketHost.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("[SplatCreator] ComfyUIWebSocketHost is empty - cannot send image. Set it (e.g. 'localhost') in Blueprint or code."));
		return;
	}

	// Get base name without extension (e.g. "scene" from "scene.ply")
	FString BaseName = FPaths::GetBaseFilename(PLYPath);
	FString Dir = FPaths::GetPath(PLYPath);

	// Try .jpg first, then .png
	FString ImagePath;
	if (FPaths::FileExists(Dir / (BaseName + TEXT(".jpg"))))
	{
		ImagePath = Dir / (BaseName + TEXT(".jpg"));
	}
	else if (FPaths::FileExists(Dir / (BaseName + TEXT(".png"))))
	{
		ImagePath = Dir / (BaseName + TEXT(".png"));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[SplatCreator] No matching image (.jpg/.png) for PLY '%s' in %s - ensure image has same name as PLY"), *BaseName, *Dir);
		return;
	}

	TArray<uint8> ImageData;
	if (!FFileHelper::LoadFileToArray(ImageData, *ImagePath))
	{
		UE_LOG(LogTemp, Warning, TEXT("[SplatCreator] Failed to load image: %s"), *ImagePath);
		return;
	}

	if (!ComfyImageSender)
	{
		ComfyImageSender = NewObject<UComfyImageSender>(this);
	}

	FString ServerURL = FString::Printf(TEXT("ws://%s:8001"), *ComfyUIWebSocketHost);
	UE_LOG(LogTemp, Display, TEXT("[SplatCreator] Sending image %s (%d bytes) to ComfyUI %s channel %d"), *FPaths::GetCleanFilename(ImagePath), ImageData.Num(), *ServerURL, ComfyUIImageChannel);
	ComfyImageSender->ConfigureAndSend(ServerURL, ComfyUIImageChannel, ImageData);
}

void USplatCreatorSubsystem::ScanForPLYFiles()
{
	FString SplatCreatorDir = GetSplatCreatorFolder();
	PlyFiles.Empty();
	
	// Convert to absolute path and normalize
	FString AbsolutePath = FPaths::ConvertRelativePathToFull(SplatCreatorDir);
	FPaths::NormalizeDirectoryName(AbsolutePath);
	
	if(debug) UE_LOG(LogTemp, Display, TEXT("[SplatCreator] Scanning for PLY files in: %s"), *AbsolutePath);
	
	// Check if directory exists
	if (!FPaths::DirectoryExists(AbsolutePath))
	{
		if(debug) UE_LOG(LogTemp, Error, TEXT("[SplatCreator] Directory does not exist: %s"), *AbsolutePath);
		return;
	}
	
	IFileManager::Get().FindFiles(PlyFiles, *(AbsolutePath / TEXT("*.ply")), true, false);
	PlyFiles.Sort();
	
	if(debug) UE_LOG(LogTemp, Display, TEXT("[SplatCreator] Found %d PLY files in %s"), PlyFiles.Num(), *AbsolutePath);
}

// ============================================================
// CYCLE SPLATS
// ============================================================

void USplatCreatorSubsystem::CycleToNextPLY()
{
	if (PlyFiles.Num() == 0)
	{
		ScanForPLYFiles(); // Re-scan in case files were added
		if (PlyFiles.Num() == 0) return;
	}
	
	CurrentFileIndex = (CurrentFileIndex + 1) % PlyFiles.Num();
	FString PLYPath = GetSplatCreatorFolder() / PlyFiles[CurrentFileIndex];
	
	if(debug) UE_LOG(LogTemp, Display, TEXT("[SplatCreator] Cycling to PLY: %s"), *PlyFiles[CurrentFileIndex]);
	LoadPLYFile(PLYPath);
}

// ============================================================
// READ PLY FILES
// ============================================================

void USplatCreatorSubsystem::LoadPLYFile(const FString& PLYPath)
{
	// Reset all transformations to normal when loading a new PLY file
	ResetToNormal();
	
	TArray<FVector> Positions;
	TArray<FColor> Colors;
	
	if (!ParsePLYFile(PLYPath, Positions, Colors))
	{
		if(debug) UE_LOG(LogTemp, Error, TEXT("[SplatCreator] Failed to parse PLY file: %s"), *PLYPath);
            return;
        }

	// Send matching image to ComfyUI on channel 2 when PLY changes (only sent when PLY changes)
	TrySendImageToComfyUI(PLYPath);

	if(debug) UE_LOG(LogTemp, Display, TEXT("[SplatCreator] Parsed %d points from %s"), Positions.Num(), *PLYPath);
	
	// Uniformly sample points to limit count for performance
	TArray<FVector> FilteredPositions;
	TArray<FColor> FilteredColors;
	SamplePointsUniformly(Positions, Colors, FilteredPositions, FilteredColors);
	
	if (FilteredPositions.Num() > 0)
	{
		Positions = FilteredPositions;
		Colors = FilteredColors;
		if(debug) UE_LOG(LogTemp, Display, TEXT("[SplatCreator] After filtering: %d points"), Positions.Num());
        }
        else
        {
		if(debug) UE_LOG(LogTemp, Warning, TEXT("[SplatCreator] Filtering removed all points, using original"));
	}

	// Create or morph point cloud
	if (PointCloudComponent && bIsMorphing == false)
	{
		// Start smooth morphing between point clouds
		OldPositions.Empty();
		OldColors.Empty();
		
		// Get current positions
		int32 InstanceCount = PointCloudComponent->GetInstanceCount();
		for (int32 i = 0; i < InstanceCount; i++)
		{
			FTransform Transform;
			if (PointCloudComponent->GetInstanceTransform(i, Transform))
			{
				OldPositions.Add(Transform.GetLocation());
				OldColors.Add(FColor::White); // Use white as default, colors will interpolate
			}
		}
		
		// Scale positions for display (PLY coordinates are typically small)
		NewPositions.Empty();
		NewPositions.Reserve(Positions.Num());
		const FVector DownOffset = FVector(0.0f, 0.0f, 0.0f);
		for (const FVector& Pos : Positions)
		{
			NewPositions.Add(Pos * 125.0f + DownOffset); // Scale by 125x and apply offset
		}
		NewColors = Colors;
		
		// Calculate adaptive sphere sizes for new positions (already scaled)
		CalculateAdaptiveSphereSizes(NewPositions, SphereSizes);
		
		MorphProgress = 0.0f;
		MorphUpdateIndex = 0;
		bIsMorphing = true;
		
		if (UWorld* World = GetWorld())
		{
			MorphStartTime = World->GetTimeSeconds();
    		World->GetTimerManager().SetTimer(MorphTimer,this,&USplatCreatorSubsystem::UpdateMorph,
				0.033f, // ~30fps update rate for better performance
				true);
        }
    }
    else
    {
		// Create new point cloud
		CreatePointCloud(Positions, Colors);
	}
}

bool USplatCreatorSubsystem::ParsePLYFile(const FString& PLYPath, TArray<FVector>& OutPositions, TArray<FColor>& OutColors)
{
	TArray<uint8> FileData;
	if (!FFileHelper::LoadFileToArray(FileData, *PLYPath))
	{
		return false;
	}
	
	FString FileContent;
	FFileHelper::BufferToString(FileContent, FileData.GetData(), FileData.Num());
	
	TArray<FString> Lines;
	FileContent.ParseIntoArrayLines(Lines);
	
	OutPositions.Empty();
	OutColors.Empty();
	
	bool bIsBinary = false;
	int32 VertexCount = 0;
	int32 HeaderEndOffset = 0;
	TArray<FString> PropertyNames;
	
	// Parse header
	for (int32 i = 0; i < Lines.Num(); i++)
	{
		FString Line = Lines[i].TrimStartAndEnd();
		
		if (Line.StartsWith(TEXT("format")))
		{
			bIsBinary = Line.Contains(TEXT("binary"));
		}
		else if (Line.StartsWith(TEXT("element vertex")))
		{
			TArray<FString> Parts;
			Line.ParseIntoArray(Parts, TEXT(" "));
			if (Parts.Num() >= 3)
			{
				VertexCount = FCString::Atoi(*Parts[2]);
			}
		}
		else if (Line.StartsWith(TEXT("property")))
		{
			TArray<FString> Parts;
			Line.ParseIntoArray(Parts, TEXT(" "));
			if (Parts.Num() >= 3)
			{
				PropertyNames.Add(Parts[2]);
			}
		}
		else if (Line == TEXT("end_header"))
		{
			int32 HeaderEndStrPos = FileContent.Find(TEXT("end_header"));
			if (HeaderEndStrPos != INDEX_NONE)
			{
				int32 NewlinePos = FileContent.Find(TEXT("\n"), ESearchCase::CaseSensitive, ESearchDir::FromStart, HeaderEndStrPos);
				HeaderEndOffset = NewlinePos != INDEX_NONE ? NewlinePos + 1 : HeaderEndStrPos + 10;
			}
			break;
		}
	}
	
	if (bIsBinary)
	{
		// Binary PLY parsing
		const uint8* DataPtr = FileData.GetData() + HeaderEndOffset;
		int32 DataOffset = 0;
		
		int32 XIdx = PropertyNames.IndexOfByKey(TEXT("x"));
		int32 YIdx = PropertyNames.IndexOfByKey(TEXT("y"));
		int32 ZIdx = PropertyNames.IndexOfByKey(TEXT("z"));
		int32 RedIdx = PropertyNames.IndexOfByKey(TEXT("red"));
		int32 GreenIdx = PropertyNames.IndexOfByKey(TEXT("green"));
		int32 BlueIdx = PropertyNames.IndexOfByKey(TEXT("blue"));
		// Gaussian splat spherical harmonics
		int32 Fdc0Idx = PropertyNames.IndexOfByKey(TEXT("f_dc_0"));
		int32 Fdc1Idx = PropertyNames.IndexOfByKey(TEXT("f_dc_1"));
		int32 Fdc2Idx = PropertyNames.IndexOfByKey(TEXT("f_dc_2"));
		
		// Calculate vertex size (simplified - assume all floats)
		int32 VertexSize = PropertyNames.Num() * 4;
		
		for (int32 i = 0; i < VertexCount && DataOffset + VertexSize <= FileData.Num() - HeaderEndOffset; i++)
		{
			float X = 0, Y = 0, Z = 0;
			uint8 R = 255, G = 255, B = 255;
			
			if (XIdx >= 0) X = *reinterpret_cast<const float*>(DataPtr + DataOffset + XIdx * 4);
			if (YIdx >= 0) Y = *reinterpret_cast<const float*>(DataPtr + DataOffset + YIdx * 4);
			if (ZIdx >= 0) Z = *reinterpret_cast<const float*>(DataPtr + DataOffset + ZIdx * 4);
			
			// Check for Gaussian splat spherical harmonics first, then fall back to RGB
			if (Fdc0Idx >= 0 && Fdc1Idx >= 0 && Fdc2Idx >= 0)
			{
				// Convert spherical harmonics to RGB
				float SH0 = *reinterpret_cast<const float*>(DataPtr + DataOffset + Fdc0Idx * 4);
				float SH1 = *reinterpret_cast<const float*>(DataPtr + DataOffset + Fdc1Idx * 4);
				float SH2 = *reinterpret_cast<const float*>(DataPtr + DataOffset + Fdc2Idx * 4);
				
				// Spherical harmonics to RGB conversion (simplified - using DC component)
				// SH coefficients are typically in range, convert to [0, 1] then to [0, 255]
				// Apply sigmoid activation: 0.5 + 0.28209479177387814 * SH (standard SH normalization)
				float Rf = FMath::Clamp(0.5f + 0.28209479177387814f * SH0, 0.0f, 1.0f);
				float Gf = FMath::Clamp(0.5f + 0.28209479177387814f * SH1, 0.0f, 1.0f);
				float Bf = FMath::Clamp(0.5f + 0.28209479177387814f * SH2, 0.0f, 1.0f);
				
				R = FMath::RoundToInt(Rf * 255.0f);
				G = FMath::RoundToInt(Gf * 255.0f);
				B = FMath::RoundToInt(Bf * 255.0f);
			}
			else if (RedIdx >= 0 && GreenIdx >= 0 && BlueIdx >= 0)
			{
				// Standard RGB values - read as float (common in PLY)
				float Rf = *reinterpret_cast<const float*>(DataPtr + DataOffset + RedIdx * 4);
				float Gf = *reinterpret_cast<const float*>(DataPtr + DataOffset + GreenIdx * 4);
				float Bf = *reinterpret_cast<const float*>(DataPtr + DataOffset + BlueIdx * 4);
				
				// If values are > 1, they're likely already in 0-255 range, otherwise 0-1 range
				if (Rf > 1.0f || Gf > 1.0f || Bf > 1.0f)
				{
					R = FMath::Clamp(FMath::RoundToInt(Rf), 0, 255);
					G = FMath::Clamp(FMath::RoundToInt(Gf), 0, 255);
					B = FMath::Clamp(FMath::RoundToInt(Bf), 0, 255);
				}
				else
				{
					R = FMath::Clamp(FMath::RoundToInt(Rf * 255.0f), 0, 255);
					G = FMath::Clamp(FMath::RoundToInt(Gf * 255.0f), 0, 255);
					B = FMath::Clamp(FMath::RoundToInt(Bf * 255.0f), 0, 255);
				}
			}
			
			// Convert coordinates: PLY (X, Y, Z) -> Unreal (X, Z, -Y)
			OutPositions.Add(FVector(X, Z, -Y));
			OutColors.Add(FColor(R, G, B, 255));
			
			DataOffset += VertexSize;
		}
	}
	else
	{
		// ASCII PLY parsing
		int32 LineIndex = 0;
		for (int32 i = 0; i < Lines.Num(); i++)
		{
			if (Lines[i].TrimStartAndEnd() == TEXT("end_header"))
			{
				LineIndex = i + 1;
				break;
			}
		}
		
		for (int32 i = 0; i < VertexCount && LineIndex < Lines.Num(); i++, LineIndex++)
		{
			FString Line = Lines[LineIndex].TrimStartAndEnd();
			TArray<FString> Parts;
			Line.ParseIntoArray(Parts, TEXT(" "));
			
			if (Parts.Num() >= 3)
			{
				float X = FCString::Atof(*Parts[0]);
				float Y = FCString::Atof(*Parts[1]);
				float Z = FCString::Atof(*Parts[2]);
				
				uint8 R = 255, G = 255, B = 255;
				if (Parts.Num() >= 6)
				{
					R = FCString::Atoi(*Parts[3]);
					G = FCString::Atoi(*Parts[4]);
					B = FCString::Atoi(*Parts[5]);
				}
				
				// Convert coordinates: PLY (X, Y, Z) -> Unreal (X, Z, -Y)
				OutPositions.Add(FVector(X, Z, -Y));
				OutColors.Add(FColor(R, G, B, 255));
			}
		}
	}
	
	return OutPositions.Num() > 0;
}


// ============================================================
// DOWNSCALE POINTS
// ============================================================

void USplatCreatorSubsystem::SamplePointsUniformly(const TArray<FVector>& InPositions, const TArray<FColor>& InColors, TArray<FVector>& OutPositions, TArray<FColor>& OutColors)
{
	OutPositions.Empty();
	OutColors.Empty();
	
	if (InPositions.Num() == 0) return;
	
	// Uniformly sample points to limit total count for performance
	// Use uniform sampling to reduce from large point counts to manageable number while maintaining mesh-like appearance
	// Reduced significantly to avoid HISM internal culling issues
	const int32 MaxPoints = 100000;
	
	if (InPositions.Num() <= MaxPoints)
	{
		// Keep all points if under limit
		OutPositions = InPositions;
		OutColors = InColors;
		if(debug) UE_LOG(LogTemp, Display, TEXT("[SamplePoints] Keeping all %d points (under limit)"), InPositions.Num());
		return;
	}
	
	// Uniform sampling: keep every Nth point to reach target count
	// This maintains the overall shape while reducing point count
	float SampleRate = (float)InPositions.Num() / (float)MaxPoints;
	int32 Step = FMath::Max(1, FMath::RoundToInt(SampleRate));
	
	OutPositions.Reserve(MaxPoints);
	OutColors.Reserve(MaxPoints);
	
	for (int32 i = 0; i < InPositions.Num(); i += Step)
	{
		if (OutPositions.Num() >= MaxPoints)
		{
			break;
		}
		
		OutPositions.Add(InPositions[i]);
		if (i < InColors.Num())
		{
			OutColors.Add(InColors[i]);
		}
		else
		{
			OutColors.Add(FColor::White);
		}
	}
	
	if (debug) UE_LOG(LogTemp, Display, TEXT("[SamplePoints] Uniform sampling: %d -> %d points (step: %d)"), 
		InPositions.Num(), OutPositions.Num(), Step);
}


// ============================================================
// ADD SPHERES
// ============================================================
void USplatCreatorSubsystem::CalculateAdaptiveSphereSizes(const TArray<FVector>& Positions, TArray<float>& OutSphereSizes)
{
	const int32 NumPoints = Positions.Num();
	OutSphereSizes.Empty(NumPoints);
	OutSphereSizes.Reserve(NumPoints);
	
		// Min and max cube sizes (in scale units)
		// Increased sizes slightly to create visible radius while still minimizing z-fighting
		const float MinCubeSize = 0.03f;   // Increased to create visible radius
		const float MaxCubeSize = 0.10f;   // Increased to create visible radius (3.3x ratio maintained)
		const float BaseCubeSize = 0.05f;  // Default size for dense clusters
	
	// Search radius for finding nearest neighbors (in scaled world units)
	// Increased to account for scaled positions (125x scaling)
	const float SearchRadius = 10.0f;
	
	if (debug) UE_LOG(LogTemp, Display, TEXT("[SplatCreator] Calculating adaptive sphere sizes for %d points (SearchRadius=%.1f)..."), NumPoints, SearchRadius);
	
	// For each point, find nearest neighbor and calculate adaptive size
	for (int32 i = 0; i < NumPoints; i++)
	{
		const FVector& CurrentPos = Positions[i];
		float NearestDistance = MAX_flt;
		
		// Find nearest neighbor within search radius
		// Use spatial search: check nearby points (within reasonable range)
		const int32 SearchRange = FMath::Min(1000, NumPoints); // Check up to 1000 nearby points for performance
		const int32 StartIdx = FMath::Max(0, i - SearchRange / 2);
		const int32 EndIdx = FMath::Min(NumPoints, i + SearchRange / 2);
		
		for (int32 j = StartIdx; j < EndIdx; j++)
		{
			if (i == j) continue;
			
			const float Distance = FVector::Dist(CurrentPos, Positions[j]);
			if (Distance < NearestDistance)
			{
				NearestDistance = Distance;
			}
		}
		
		// Calculate adaptive size based on nearest neighbor distance
		// High density (close neighbors) = smaller spheres (0.1)
		// Sparse areas (far neighbors) = larger spheres (0.3)
		// Small NearestDistance (high density) -> small sphere (0.1)
		// Large NearestDistance (sparse) -> large sphere (0.3)
		float SphereSize;
		
		// If no neighbor found or very far, use large sphere for sparse areas
		if (NearestDistance == MAX_flt)
		{
			SphereSize = MaxCubeSize; // Large sphere for isolated points
		}
		else
		{
			// Map distance: small distance (high density) -> small sphere, large distance (sparse) -> large sphere
			// Adjusted thresholds for smoother blending
			const float DenseThreshold = 40.0f;  // Below this = dense
			const float SparseThreshold = 120.0f; // Above this = sparse
			
			if (NearestDistance <= DenseThreshold)
			{
				SphereSize = MinCubeSize; // Dense area = small sphere
			}
			else if (NearestDistance >= SparseThreshold)
			{
				SphereSize = MaxCubeSize; // Sparse area = large sphere
			}
			else
			{
				// Smooth interpolation with ease-in-out curve for better blending
				float T = (NearestDistance - DenseThreshold) / (SparseThreshold - DenseThreshold);
				// Apply ease-in-out curve: smooth start and end, faster in middle
				float EasedT = T < 0.5f 
					? 2.0f * T * T 
					: 1.0f - FMath::Pow(-2.0f * T + 2.0f, 2.0f) / 2.0f;
				SphereSize = FMath::Lerp(MinCubeSize, MaxCubeSize, EasedT);
			}
		}
		
		// No multiplier - use increased base sizes to avoid z-fighting while maintaining density illusion
		OutSphereSizes.Add(FMath::Clamp(SphereSize, MinCubeSize, MaxCubeSize));
	}
	
	if (debug) UE_LOG(LogTemp, Display, TEXT("[SplatCreator] Calculated adaptive sphere sizes: min=%.3f, max=%.3f"), 
		MinCubeSize, MaxCubeSize);
}

void USplatCreatorSubsystem::CreatePointCloud(const TArray<FVector>& Positions, const TArray<FColor>& Colors)
{
	UWorld* World = GetWorld();
	if (!World) return;
	
	// Stop all animations before creating new point cloud
	if (bIsBobbing)
	{
		StopBobbing();
	}
	if (bIsRandomMoving)
	{
		StopRandomMovement();
	}
	
	// Destroy old actor
	if (CurrentPointCloudActor)
	{
		CurrentPointCloudActor->Destroy();
	}
	
	// Create new actor
	CurrentPointCloudActor = World->SpawnActor<AActor>();
	if (!CurrentPointCloudActor) return;
	
	// Load sphere mesh
	UStaticMesh* SphereMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (!SphereMesh) return;
	
	// Create HISM component
	PointCloudComponent = NewObject<UHierarchicalInstancedStaticMeshComponent>(CurrentPointCloudActor);
	PointCloudComponent->SetStaticMesh(SphereMesh);
	PointCloudComponent->SetNumCustomDataFloats(4);
	PointCloudComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PointCloudComponent->SetCastShadow(false);
	PointCloudComponent->SetVisibility(true);
	PointCloudComponent->SetHiddenInGame(false);
	
	// Disable all culling to prevent points from disappearing
	PointCloudComponent->SetCullDistances(0, 0); // No near cull, no far cull
	PointCloudComponent->SetCanEverAffectNavigation(false);
	PointCloudComponent->SetReceivesDecals(false);
	
	// Force the component to always render
	PointCloudComponent->SetVisibility(true);
	PointCloudComponent->SetHiddenInGame(false);
	
	// HISM-specific settings to reduce aggressive culling
	// These help prevent instances from being culled when looking straight on
	PointCloudComponent->bDisableCollision = true;
	PointCloudComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	
	// Enable depth sorting and adjust rendering to reduce z-fighting
	PointCloudComponent->SetDepthPriorityGroup(SDPG_World);
	PointCloudComponent->SetRenderCustomDepth(false);
	// Disable depth testing for overlapping instances to reduce z-fighting
	// Note: This may cause some rendering order issues but eliminates z-fighting
	PointCloudComponent->bUseAsOccluder = false;
	PointCloudComponent->SetTranslucentSortPriority(1); // Higher priority for better sorting
	
	// Set material to see colors
	UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/_GENERATED/Materials/M_VertexColor.M_VertexColor"));
	if (!Material)
	{
		if(debug) UE_LOG(LogTemp, Warning, TEXT("[SplatCreator] Material M_VertexColor not found at /Game/_GENERATED/Materials/, trying fallback"));
		Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	}
	if (Material)
	{
		// Try to create a Material Instance Dynamic to enhance visibility and contrast
		UMaterialInstanceDynamic* DynamicMaterial = UMaterialInstanceDynamic::Create(Material, CurrentPointCloudActor);
		if (DynamicMaterial)
		{
			// Lower emissive intensity while maintaining radius (sphere sizes)
			// Try common emissive parameter names
			DynamicMaterial->SetVectorParameterValue(TEXT("EmissiveColor"), FLinearColor(0.4f, 0.4f, 0.4f, 1.0f));
			DynamicMaterial->SetScalarParameterValue(TEXT("EmissiveIntensity"), 1.0f);
			DynamicMaterial->SetScalarParameterValue(TEXT("Emissive"), 1.0f);
			// Try bloom/glow radius parameters if they exist
			DynamicMaterial->SetScalarParameterValue(TEXT("BloomIntensity"), 1.0f);
			DynamicMaterial->SetScalarParameterValue(TEXT("GlowRadius"), 4.0f); // Keep radius
			DynamicMaterial->SetScalarParameterValue(TEXT("GlowIntensity"), 1.0f);
			DynamicMaterial->SetScalarParameterValue(TEXT("BloomScale"), 2.0f);
			DynamicMaterial->SetScalarParameterValue(TEXT("GlowScale"), 2.0f);
			
			// Add contrast and color enhancement parameters
			// Try common parameter names - these will only apply if the material has these parameters exposed
			DynamicMaterial->SetScalarParameterValue(TEXT("Contrast"), 1.5f); // Increase contrast (1.0 = no change, >1.0 = more contrast)
			DynamicMaterial->SetScalarParameterValue(TEXT("Saturation"), 1.3f); // Increase saturation for more vibrant colors
			DynamicMaterial->SetScalarParameterValue(TEXT("Brightness"), 1.1f); // Slight brightness boost
			DynamicMaterial->SetScalarParameterValue(TEXT("ColorMultiplier"), 1.2f); // Color intensity multiplier
			
			// Try alternative parameter names that might exist in the material
			DynamicMaterial->SetScalarParameterValue(TEXT("ContrastAmount"), 1.5f);
			DynamicMaterial->SetScalarParameterValue(TEXT("SaturationAmount"), 1.3f);
			DynamicMaterial->SetScalarParameterValue(TEXT("ColorIntensity"), 1.2f);
			DynamicMaterial->SetScalarParameterValue(TEXT("Intensity"), 1.2f);
			DynamicMaterial->SetScalarParameterValue(TEXT("Vibrance"), 1.3f);
			
			PointCloudComponent->SetMaterial(0, DynamicMaterial);
			if(debug) UE_LOG(LogTemp, Display, TEXT("[SplatCreator] Created Material Instance Dynamic with emissive and contrast properties"));
		}
		else
		{
			PointCloudComponent->SetMaterial(0, Material);
			if(debug) UE_LOG(LogTemp, Display, TEXT("[SplatCreator] Set material: %s (failed to create MID)"), *Material->GetName());
		}
	}
	else
	{
		if(debug) UE_LOG(LogTemp, Error, TEXT("[SplatCreator] Failed to load material"));
	}
	
	// Set component as root BEFORE registering (critical for proper bounds calculation)
	CurrentPointCloudActor->SetRootComponent(PointCloudComponent);
	
	CurrentPointCloudActor->SetActorLocation(FVector(0.0f, 0.0f, -150.0f));
	CurrentPointCloudActor->SetActorRotation(FRotator(0.0f, 0.0f, 180.0f)); 
	
	FVector ActorLoc = CurrentPointCloudActor->GetActorLocation();
	FRotator ActorRot = CurrentPointCloudActor->GetActorRotation();

	// Register component (must be done after setting as root component)
	PointCloudComponent->RegisterComponent();
	
	// Force HISM to always render (disable all forms of culling)
	PointCloudComponent->SetCanEverAffectNavigation(false);
	
	// Calculate adaptive sphere sizes based on point density
	// Scale positions first (same scaling as used for rendering: 125.0f)
	TArray<FVector> ScaledPositions;
	ScaledPositions.Reserve(Positions.Num());
	for (const FVector& Pos : Positions)
	{
		ScaledPositions.Add(Pos * 125.0f);
	}
	CalculateAdaptiveSphereSizes(ScaledPositions, SphereSizes);
	if(debug) UE_LOG(LogTemp, Display, TEXT("[SplatCreator] Calculated %d sphere sizes for dense region detection"), SphereSizes.Num());
	
	// Add instances in batches
	int32 NumInstances = FMath::Min(Positions.Num(), Colors.Num());
	const int32 BatchSize = 5000;
	
	if(debug) UE_LOG(LogTemp, Display, TEXT("[SplatCreator] Adding %d instances in batches of %d..."), NumInstances, BatchSize);
	
	for (int32 BatchStart = 0; BatchStart < NumInstances; BatchStart += BatchSize)
	{
		int32 BatchEnd = FMath::Min(BatchStart + BatchSize, NumInstances);
		TArray<FTransform> Transforms;
		Transforms.Reserve(BatchEnd - BatchStart);
		
		// Offset to move PLY down (negative Z in Unreal = down)
		const FVector DownOffset = FVector(0.0f, 0.0f, 0.0f);
		
		for (int32 i = BatchStart; i < BatchEnd; i++)
		{
			FTransform Transform;
			// Scale positions and apply downward offset (no jitter)
			Transform.SetLocation(Positions[i] * 125.0f + DownOffset);
			// Use adaptive sphere size, fallback to default if not calculated
			float SphereSize = (i < SphereSizes.Num()) ? SphereSizes[i] : 0.06f;
			Transform.SetScale3D(FVector(SphereSize));
			Transforms.Add(Transform);
		}
		
		PointCloudComponent->AddInstances(Transforms, false, false);
		
		if (BatchEnd % 50000 == 0 || BatchEnd >= NumInstances)
		{
			if(debug) UE_LOG(LogTemp, Display, TEXT("[SplatCreator] Added %d / %d instances..."), BatchEnd, NumInstances);
		}
	}
	
	// Store current point positions (scaled and offset) for dense region detection BEFORE broadcasting
	// Apply the same offset as used for rendering
	const FVector DownOffset = FVector(0.0f, 0.0f, 200.0f);
	CurrentPointPositions.Empty();
	CurrentPointPositions.Reserve(ScaledPositions.Num());
	for (const FVector& ScaledPos : ScaledPositions)
	{
		CurrentPointPositions.Add(ScaledPos + DownOffset);
	}
	
	// Store base positions for bobbing animation
	BasePointPositions = CurrentPointPositions;
	
	// Verify SphereSizes is populated (it should be from CalculateAdaptiveSphereSizes call above)
	if(debug) UE_LOG(LogTemp, Display, TEXT("[SplatCreator] Stored %d point positions and %d sphere sizes for dense region detection"), 
		CurrentPointPositions.Num(), SphereSizes.Num());
	
	// Force update bounds after all instances are added (critical for preventing culling)
	// Calculate explicit bounds from all positions (use CurrentPointPositions which already has offset applied)
	if (CurrentPointPositions.Num() > 0)
	{
		FBox BoundingBox(ForceInit);
		for (const FVector& Pos : CurrentPointPositions)
		{
			BoundingBox += Pos;
		}
		
		// Expand bounds to account for sphere sizes
		float MaxSphereSize = 0.1f; // Max cube size from adaptive sizing
		BoundingBox = BoundingBox.ExpandBy(MaxSphereSize * 50.0f); // Expand by max sphere radius
		
		// Force HISM to use these bounds
		PointCloudComponent->UpdateBounds();
		PointCloudComponent->MarkRenderStateDirty();
		
		// Store bounds for external access
		CurrentSplatBounds = BoundingBox;
		bHasSplatBounds = true;
		
		// Store center for scaling operations
		SplatCenter = BoundingBox.GetCenter();
		bHasSplatCenter = true;
		SplatScaleMultiplier = 1.0f; // Reset scale when creating new point cloud
		
		FVector BoxSize = BoundingBox.GetSize();
		FVector BoxCenter = BoundingBox.GetCenter();
		if(debug) UE_LOG(LogTemp, Display, TEXT("[SplatCreator] Set explicit bounds: Min=(%.1f, %.1f, %.1f), Max=(%.1f, %.1f, %.1f)"), 
			BoundingBox.Min.X, BoundingBox.Min.Y, BoundingBox.Min.Z,
			BoundingBox.Max.X, BoundingBox.Max.Y, BoundingBox.Max.Z);
		if(debug) UE_LOG(LogTemp, Display, TEXT("[SplatCreator] Bounds size: X=%.1f, Y=%.1f, Z=%.1f, Center: (%.1f, %.1f, %.1f)"), 
			BoxSize.X, BoxSize.Y, BoxSize.Z, BoxCenter.X, BoxCenter.Y, BoxCenter.Z);
		
		// Notify other subsystems that bounds have been updated (CurrentPointPositions is now stored)
		OnSplatBoundsUpdated.Broadcast(BoundingBox);
	}
	else
	{
		if(debug) UE_LOG(LogTemp, Warning, TEXT("[SplatCreator] Cannot calculate bounds - CurrentPointPositions is empty"));
	}
	
	// Final visibility and culling overrides
	PointCloudComponent->SetVisibility(true);
	PointCloudComponent->SetHiddenInGame(false);
	PointCloudComponent->SetCullDistances(0, 0); // Ensure no distance culling
	PointCloudComponent->MarkRenderStateDirty();
	
	// Set colors
	for (int32 i = 0; i < NumInstances && i < Colors.Num(); i++)
	{
		PointCloudComponent->SetCustomDataValue(i, 0, Colors[i].R / 255.0f);
		PointCloudComponent->SetCustomDataValue(i, 1, Colors[i].G / 255.0f);
		PointCloudComponent->SetCustomDataValue(i, 2, Colors[i].B / 255.0f);
		PointCloudComponent->SetCustomDataValue(i, 3, Colors[i].A / 255.0f);
	}
	
	PointCloudComponent->MarkRenderStateDirty();
	
	// Store sphere sizes for morphing (already calculated above)
	
	if(debug) UE_LOG(LogTemp, Display, TEXT("[SplatCreator] Created point cloud with %d spheres"), NumInstances);
}


// ============================================================
// MORPH BETWEEN SPLATS
// ============================================================

void USplatCreatorSubsystem::UpdateMorph()
{
	if (!bIsMorphing || !PointCloudComponent) return;

	UWorld* World = GetWorld();
	if (!World) return;
	
	float CurrentTime = World->GetTimeSeconds();
	float ElapsedTime = CurrentTime - MorphStartTime;
	MorphProgress = ElapsedTime / MorphDuration;
	
	if (MorphProgress >= 1.0f)
	{
		CompleteMorph();
		return;
	}
	
	// Smooth easing function (ease-in-out cubic for smoother animation)
	float EasedProgress = MorphProgress < 0.5f
		? 4.0f * MorphProgress * MorphProgress * MorphProgress
		: 1.0f - FMath::Pow(-2.0f * MorphProgress + 2.0f, 3.0f) / 2.0f;
	
	// Update instances in batches
	int32 MaxInstances = FMath::Max(OldPositions.Num(), NewPositions.Num());
	int32 BatchSize = 5000; // Larger batch for better performance
	int32 BatchEnd = FMath::Min(MorphUpdateIndex + BatchSize, MaxInstances);
	
	// Ensure component has enough instances
	if (PointCloudComponent->GetInstanceCount() < MaxInstances)
	{
		TArray<FTransform> PlaceholderTransforms;
		for (int32 i = PointCloudComponent->GetInstanceCount(); i < MaxInstances; i++)
		{
			FTransform Transform;
			// Start at old position if available, otherwise new position
			FVector StartPos = (i < OldPositions.Num()) ? OldPositions[i] : (i < NewPositions.Num() ? NewPositions[i] : FVector::ZeroVector);
			Transform.SetLocation(StartPos);
			// Use adaptive sphere size, fallback to default
			float SphereSize = (i < SphereSizes.Num()) ? SphereSizes[i] : 0.06f;
			Transform.SetScale3D(FVector(SphereSize));
			PlaceholderTransforms.Add(Transform);
		}
		if (PlaceholderTransforms.Num() > 0)
		{
			PointCloudComponent->AddInstances(PlaceholderTransforms, false, true);
		}
	}
	
	// Update batch - smooth interpolation between old and new positions
	for (int32 i = MorphUpdateIndex; i < BatchEnd; i++)
	{
		FVector InterpPos = FVector::ZeroVector;
		FColor InterpColor = FColor::White;
		
		if (i < OldPositions.Num() && i < NewPositions.Num())
		{
			// Smooth interpolation between old and new positions
			InterpPos = FMath::Lerp(OldPositions[i], NewPositions[i], EasedProgress);
			
			// Smooth color interpolation
			FLinearColor OldLinear = OldColors[i].ReinterpretAsLinear();
			FLinearColor NewLinear = NewColors[i].ReinterpretAsLinear();
			FLinearColor InterpLinear = FMath::Lerp(OldLinear, NewLinear, EasedProgress);
			InterpColor = InterpLinear.ToFColor(true);
		}
		else if (i < NewPositions.Num())
		{
			// New point - fade in from old position or start position
			FVector StartPos = (i < OldPositions.Num()) ? OldPositions[i] : NewPositions[i];
			InterpPos = FMath::Lerp(StartPos, NewPositions[i], EasedProgress);
			InterpColor = NewColors[i];
			InterpColor.A = FMath::RoundToInt(EasedProgress * 255.0f); // Fade in
		}
		else if (i < OldPositions.Num())
		{
			// Old point fading out
			InterpPos = OldPositions[i];
			InterpColor = OldColors[i];
			InterpColor.A = FMath::RoundToInt((1.0f - EasedProgress) * 255.0f); // Fade out
		}
		
		if (i < PointCloudComponent->GetInstanceCount())
		{
			FTransform Transform;
			Transform.SetLocation(InterpPos);
			// Use adaptive sphere size, fallback to default
			float SphereSize = (i < SphereSizes.Num()) ? SphereSizes[i] : 0.06f;
			Transform.SetScale3D(FVector(SphereSize));
			PointCloudComponent->UpdateInstanceTransform(i, Transform, false, false);
			
			PointCloudComponent->SetCustomDataValue(i, 0, FMath::Clamp(InterpColor.R / 255.0f, 0.0f, 1.0f));
			PointCloudComponent->SetCustomDataValue(i, 1, FMath::Clamp(InterpColor.G / 255.0f, 0.0f, 1.0f));
			PointCloudComponent->SetCustomDataValue(i, 2, FMath::Clamp(InterpColor.B / 255.0f, 0.0f, 1.0f));
			PointCloudComponent->SetCustomDataValue(i, 3, FMath::Clamp(InterpColor.A / 255.0f, 0.0f, 1.0f));
		}
	}
	
	PointCloudComponent->MarkRenderStateDirty();
	
	MorphUpdateIndex = BatchEnd;
	if (MorphUpdateIndex >= MaxInstances)
	{
		MorphUpdateIndex = 0;
	}
}

void USplatCreatorSubsystem::CompleteMorph()
{
	UWorld* World = GetWorld();
	if (MorphTimer.IsValid() && World)
	{
		World->GetTimerManager().ClearTimer(MorphTimer);
	}
	
	bIsMorphing = false;
	MorphProgress = 0.0f;
	MorphUpdateIndex = 0;
	
	// Ensure final state
	int32 NumInstances = NewPositions.Num();
	if (PointCloudComponent && NumInstances > 0)
	{
		for (int32 i = 0; i < NumInstances && i < PointCloudComponent->GetInstanceCount(); i++)
		{
			FTransform Transform;
			Transform.SetLocation(NewPositions[i]);
			// Use adaptive sphere size, fallback to default
			float SphereSize = (i < SphereSizes.Num()) ? SphereSizes[i] : 0.06f;
			Transform.SetScale3D(FVector(SphereSize));
			PointCloudComponent->UpdateInstanceTransform(i, Transform, false, false);
			
			if (i < NewColors.Num())
			{
				PointCloudComponent->SetCustomDataValue(i, 0, NewColors[i].R / 255.0f);
				PointCloudComponent->SetCustomDataValue(i, 1, NewColors[i].G / 255.0f);
				PointCloudComponent->SetCustomDataValue(i, 2, NewColors[i].B / 255.0f);
				PointCloudComponent->SetCustomDataValue(i, 3, NewColors[i].A / 255.0f);
			}
		}
		
		PointCloudComponent->MarkRenderStateDirty();
		
		// Update bounds and store positions after morph completes
		if (NewPositions.Num() > 0)
		{
			FBox BoundingBox(ForceInit);
			for (const FVector& Pos : NewPositions)
			{
				BoundingBox += Pos;
			}
			CurrentSplatBounds = BoundingBox;
			bHasSplatBounds = true;
			
			// Store current positions for dense region detection
			CurrentPointPositions = NewPositions;
			
			// Store base positions for bobbing animation
			BasePointPositions = NewPositions;
			
			// Update center for scaling
			SplatCenter = BoundingBox.GetCenter();
			bHasSplatCenter = true;
			SplatScaleMultiplier = 1.0f; // Reset scale when morph completes
			
			if(debug) UE_LOG(LogTemp, Display, TEXT("[SplatCreator] Updated bounds after morph: Min=(%.1f, %.1f, %.1f), Max=(%.1f, %.1f, %.1f)"), 
				BoundingBox.Min.X, BoundingBox.Min.Y, BoundingBox.Min.Z,
				BoundingBox.Max.X, BoundingBox.Max.Y, BoundingBox.Max.Z);
			
			// Notify other subsystems that bounds have been updated
			OnSplatBoundsUpdated.Broadcast(BoundingBox);
		}
	}
}

// ============================================================
// SPLAT SCALING SYSTEM
// ============================================================

void USplatCreatorSubsystem::ScaleSplat(float NewScaleMultiplier)
{
	if (!PointCloudComponent || BasePointPositions.Num() == 0 || !bHasSplatCenter)
	{
		UE_LOG(LogTemp, Warning, TEXT("[SplatCreator] Cannot scale splat - no point cloud loaded or center not calculated"));
		return;
	}
	
	SplatScaleMultiplier = FMath::Clamp(NewScaleMultiplier, 0.1f, 5.0f);
	UpdateSplatScale();
}

void USplatCreatorSubsystem::UpdateSplatScale()
{
	if (!PointCloudComponent || BasePointPositions.Num() == 0 || !bHasSplatCenter)
	{
		return;
	}
	
	// If bobbing is active, the bobbing update will handle the scaling
	// Otherwise, update positions directly
	if (!bIsBobbing)
	{
		int32 NumInstances = FMath::Min(PointCloudComponent->GetInstanceCount(), BasePointPositions.Num());
		
		// Update all instances with scaled positions and sizes
		for (int32 i = 0; i < NumInstances; i++)
		{
			// Scale position relative to center
			FVector BasePosition = BasePointPositions[i];
			FVector OffsetFromCenter = BasePosition - SplatCenter;
			FVector ScaledOffset = OffsetFromCenter * SplatScaleMultiplier;
			FVector NewPosition = SplatCenter + ScaledOffset;
			
			// Scale sphere size proportionally
			float BaseSphereSize = (i < SphereSizes.Num()) ? SphereSizes[i] : 0.06f;
			float ScaledSphereSize = BaseSphereSize * SplatScaleMultiplier;
			
			FTransform Transform;
			if (PointCloudComponent->GetInstanceTransform(i, Transform))
			{
				Transform.SetLocation(NewPosition);
				Transform.SetScale3D(FVector(ScaledSphereSize));
				PointCloudComponent->UpdateInstanceTransform(i, Transform, false, false);
			}
		}
		
		// Update current positions
		CurrentPointPositions.Empty();
		CurrentPointPositions.Reserve(BasePointPositions.Num());
		for (const FVector& BasePos : BasePointPositions)
		{
			FVector OffsetFromCenter = BasePos - SplatCenter;
			FVector ScaledOffset = OffsetFromCenter * SplatScaleMultiplier;
			CurrentPointPositions.Add(SplatCenter + ScaledOffset);
		}
		
		PointCloudComponent->MarkRenderStateDirty();
	}
	// If bobbing is active, the next UpdateBobbing call will apply the new scale
	
	if(debug) UE_LOG(LogTemp, Display, TEXT("[SplatCreator] Splat scaled to %.2fx (center: %s)"), 
		SplatScaleMultiplier, *SplatCenter.ToString());
}

void USplatCreatorSubsystem::ResetToNormal()
{
	// Stop all animations if active (no smooth interpolation when resetting)
	if (bIsBobbing)
	{
		// Temporarily set scale to 1.0 so StopBobbing restores unscaled positions
		float SavedScale = SplatScaleMultiplier;
		SplatScaleMultiplier = 1.0f;
		StopBobbing(false); // No interpolation when resetting
		SplatScaleMultiplier = SavedScale; // Will be reset below
	}
	if (bIsRandomMoving)
	{
		StopRandomMovement(false); // No interpolation when resetting
	}
	
	// Stop any active interpolation
	if (bIsInterpolatingToBase)
	{
		bIsInterpolatingToBase = false;
		InterpolationTime = 0.0f;
		InterpolationStartPositions.Empty();
		
		UWorld* World = GetWorld();
		if (World)
		{
			if (BobbingTimer.IsValid())
			{
				World->GetTimerManager().ClearTimer(BobbingTimer);
			}
			if (RandomMovementTimer.IsValid())
			{
				World->GetTimerManager().ClearTimer(RandomMovementTimer);
			}
		}
	}
	
	// Reset all multipliers to normal
	BobbingSpeedMultiplier = 1.0f;
	SplatScaleMultiplier = 1.0f;
	
	// Restore positions to original unscaled positions if point cloud exists
	if (PointCloudComponent && BasePointPositions.Num() > 0)
	{
		int32 NumInstances = FMath::Min(PointCloudComponent->GetInstanceCount(), BasePointPositions.Num());
		
		for (int32 i = 0; i < NumInstances; i++)
		{
			FTransform Transform;
			if (PointCloudComponent->GetInstanceTransform(i, Transform))
			{
				// Restore to base position (unscaled, unbobbed)
				Transform.SetLocation(BasePointPositions[i]);
				
				// Restore to base sphere size
				float BaseSphereSize = (i < SphereSizes.Num()) ? SphereSizes[i] : 0.06f;
				Transform.SetScale3D(FVector(BaseSphereSize));
				
				PointCloudComponent->UpdateInstanceTransform(i, Transform, false, false);
			}
		}
		
		// Update current positions to match base positions
		CurrentPointPositions = BasePointPositions;
		
		PointCloudComponent->MarkRenderStateDirty();
	}
	
	if(debug) UE_LOG(LogTemp, Display, TEXT("[SplatCreator] Reset to normal - all transformations cleared (scale: %.2f, speed: %.2f)"), 
		SplatScaleMultiplier, BobbingSpeedMultiplier);
}

// ============================================================
// GET DIMENSIONS FOR HYPER3DOBJECTS
// ============================================================

FVector2D USplatCreatorSubsystem::GetSplatDimensions() const
{
	if (!bHasSplatBounds)
	{
		if(debug) UE_LOG(LogTemp, Warning, TEXT("[SplatCreator] No splat bounds available, returning default (200x200)"));
		return FVector2D(200.0f, 200.0f);
	}
	
	// Return world-space dimensions (transform local bounds by actor transform)
	FBox WorldBounds = GetSplatBounds();
	FVector BoxSize = WorldBounds.GetSize();
	FVector2D Dimensions(BoxSize.X, BoxSize.Y);
	
	if(debug) UE_LOG(LogTemp, Display, TEXT("[SplatCreator] Splat dimensions: X=%.1f, Y=%.1f"), Dimensions.X, Dimensions.Y);
	return Dimensions;
}

FVector USplatCreatorSubsystem::GetSplatCenter() const
{
	if (!bHasSplatBounds)
	{
		if(debug) UE_LOG(LogTemp, Warning, TEXT("[SplatCreator] No splat bounds available, returning origin"));
		return FVector::ZeroVector;
	}
	
	// Transform local center to world space (includes actor rotation)
	FVector LocalCenter = CurrentSplatBounds.GetCenter();
	if (CurrentPointCloudActor)
	{
		FVector WorldCenter = CurrentPointCloudActor->GetActorTransform().TransformPosition(LocalCenter);
		if(debug) UE_LOG(LogTemp, Display, TEXT("[SplatCreator] Splat center: %s (world)"), *WorldCenter.ToString());
		return WorldCenter;
	}
	if(debug) UE_LOG(LogTemp, Display, TEXT("[SplatCreator] Splat center: %s (local, no actor)"), *LocalCenter.ToString());
	return LocalCenter;
}

FBox USplatCreatorSubsystem::GetSplatBounds() const
{
	if (!bHasSplatBounds)
	{
		if(debug) UE_LOG(LogTemp, Warning, TEXT("[SplatCreator] No splat bounds available, returning empty box"));
		return FBox(ForceInit);
	}
	
	// Transform local bounds to world space (includes actor rotation)
	if (CurrentPointCloudActor)
	{
		return CurrentSplatBounds.TransformBy(CurrentPointCloudActor->GetActorTransform());
	}
	return CurrentSplatBounds;
}

TArray<FVector> USplatCreatorSubsystem::GetDensePointRegions(float DensityThreshold) const
{
	TArray<FVector> DenseRegions;
	
	if (CurrentPointPositions.Num() == 0 || SphereSizes.Num() == 0)
	{
		if(debug) UE_LOG(LogTemp, Warning, TEXT("[SplatCreator] No point positions or sphere sizes available (Positions: %d, SphereSizes: %d)"), 
			CurrentPointPositions.Num(), SphereSizes.Num());
		return DenseRegions;
	}
	
	const FTransform ActorTransform = CurrentPointCloudActor ? CurrentPointCloudActor->GetActorTransform() : FTransform::Identity;
	
	// Points with small sphere sizes indicate dense regions
	const float MaxDenseSphereSize = DensityThreshold;
	int32 DenseCount = 0;
	for (int32 i = 0; i < CurrentPointPositions.Num() && i < SphereSizes.Num(); i++)
	{
		if (SphereSizes[i] <= MaxDenseSphereSize)
		{
			// Transform local position to world space
			DenseRegions.Add(ActorTransform.TransformPosition(CurrentPointPositions[i]));
			DenseCount++;
		}
	}
	
	if(debug) UE_LOG(LogTemp, Display, TEXT("[SplatCreator] Found %d dense points out of %d total (sphere size threshold: %.3f)"), 
		DenseCount, CurrentPointPositions.Num(), MaxDenseSphereSize);
	
	return DenseRegions;
}

bool USplatCreatorSubsystem::IsPositionTooCloseToSplatPoints(const FVector& Position, float MinDistance, bool bCheckHorizontalOnly) const
{
	if (CurrentPointPositions.Num() == 0)
	{
		return false;
	}

	// Position is in world space; transform splat points to world for comparison
	const FTransform ActorTransform = CurrentPointCloudActor ? CurrentPointCloudActor->GetActorTransform() : FTransform::Identity;
	const float MinDistanceSquared = MinDistance * MinDistance;
	
	for (const FVector& LocalSplatPoint : CurrentPointPositions)
	{
		FVector WorldSplatPoint = ActorTransform.TransformPosition(LocalSplatPoint);
		float DistanceSquared;
		if (bCheckHorizontalOnly)
		{
			FVector2D Pos2D(Position.X, Position.Y);
			FVector2D Splat2D(WorldSplatPoint.X, WorldSplatPoint.Y);
			DistanceSquared = FVector2D::DistSquared(Pos2D, Splat2D);
		}
		else
		{
			DistanceSquared = FVector::DistSquared(Position, WorldSplatPoint);
		}
		
		if (DistanceSquared < MinDistanceSquared)
		{
			return true;
		}
	}
	
	return false;
}

// ============================================================
// OSC MESSAGE HANDLING AND BOBBING ANIMATION
// ============================================================

void USplatCreatorSubsystem::HandleOSCMessage(const FString& Message)
{
	if (!PointCloudComponent || CurrentPointPositions.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[SplatCreator] Cannot handle OSC message - no point cloud loaded"));
		return;
	}
	
	// Convert message to lowercase for case-insensitive matching
	FString LowerMessage = Message.ToLower().TrimStartAndEnd();
	
	// Check for stop keyword first (stops all animations)
	if (LowerMessage.Contains(TEXT("stop")))
	{
		StopBobbing(true); // Smooth interpolation
		StopRandomMovement(true); // Smooth interpolation
		if(debug) UE_LOG(LogTemp, Display, TEXT("[SplatCreator] OSC message received: '%s' -> Stopping all animations"), *Message);
		return;
	}
	// Check for random movement
	else if (LowerMessage.Contains(TEXT("random")))
	{
		// Reset first when switching from bobbing to random
		if (bIsBobbing)
		{
			StopBobbing(false); // No smooth interpolation when switching
		}
		StartRandomMovement();
		if(debug) UE_LOG(LogTemp, Display, TEXT("[SplatCreator] OSC message received: '%s' -> Starting random movement"), *Message);
		return;
	}
	else if (LowerMessage.Contains(TEXT("faster")))
	{
		BobbingSpeedMultiplier = FMath::Clamp(BobbingSpeedMultiplier * 1.5f, 0.1f, 5.0f);
		RandomMovementSpeedMultiplier = FMath::Clamp(RandomMovementSpeedMultiplier * 1.5f, 0.1f, 5.0f);
		if(debug) UE_LOG(LogTemp, Display, TEXT("[SplatCreator] OSC message received: '%s' -> Speed multiplier: %.2f (bobbing), %.2f (random)"), 
			*Message, BobbingSpeedMultiplier, RandomMovementSpeedMultiplier);
		return;
	}
	else if (LowerMessage.Contains(TEXT("slower")))
	{
		BobbingSpeedMultiplier = FMath::Clamp(BobbingSpeedMultiplier * 0.67f, 0.1f, 5.0f);
		RandomMovementSpeedMultiplier = FMath::Clamp(RandomMovementSpeedMultiplier * 0.67f, 0.1f, 5.0f);
		if(debug) UE_LOG(LogTemp, Display, TEXT("[SplatCreator] OSC message received: '%s' -> Speed multiplier: %.2f (bobbing), %.2f (random)"), 
			*Message, BobbingSpeedMultiplier, RandomMovementSpeedMultiplier);
		return;
	}
	else if (LowerMessage.Contains(TEXT("normal")))
	{
		BobbingSpeedMultiplier = 1.0f;
		RandomMovementSpeedMultiplier = 1.0f;
		if(debug) UE_LOG(LogTemp, Display, TEXT("[SplatCreator] OSC message received: '%s' -> Speed reset to normal (%.2f)"), 
			*Message, BobbingSpeedMultiplier);
		return;
	}
	
	// Parse message for direction keywords
	EBobbingDirection NewDirection = EBobbingDirection::None;
	
	if (LowerMessage.Contains(TEXT("up")))
	{
		NewDirection = EBobbingDirection::Up;
	}
	else if (LowerMessage.Contains(TEXT("down")))
	{
		NewDirection = EBobbingDirection::Down;
	}
	else if (LowerMessage.Contains(TEXT("left")))
	{
		NewDirection = EBobbingDirection::Left;
	}
	else if (LowerMessage.Contains(TEXT("right")))
	{
		NewDirection = EBobbingDirection::Right;
	}
	
	if (NewDirection != EBobbingDirection::None)
	{
		// Reset first when switching from random to bobbing
		if (bIsRandomMoving)
		{
			StopRandomMovement(false); // No smooth interpolation when switching
		}
		StartBobbing(NewDirection);
		if(debug) UE_LOG(LogTemp, Display, TEXT("[SplatCreator] OSC message received: '%s' -> Starting bobbing direction: %d (speed: %.2f)"), 
			*Message, (int32)NewDirection, BobbingSpeedMultiplier);
	}
	else
	{
		// If message doesn't contain recognized keywords, ignore it (don't stop animations)
		if(debug) UE_LOG(LogTemp, Verbose, TEXT("[SplatCreator] OSC message received: '%s' -> No recognized keywords, ignoring"), *Message);
	}
}

void USplatCreatorSubsystem::StartBobbing(EBobbingDirection Direction)
{
	if (!PointCloudComponent || BasePointPositions.Num() == 0)
	{
		if(debug) UE_LOG(LogTemp, Warning, TEXT("[SplatCreator] Cannot start bobbing - no base positions stored"));
		return;
	}
	
	// BasePointPositions should already be set from CreatePointCloud
	// It always contains unscaled positions, which is what we want for bobbing
	
	CurrentBobbingDirection = Direction;
	bIsBobbing = true;
	BobbingTime = 0.0f;
	BobbingSpeedMultiplier = 1.0f; // Reset speed multiplier when starting
	
	UWorld* World = GetWorld();
	if (World)
	{
		// Update bobbing at 60fps for good performance (120fps was too expensive)
		World->GetTimerManager().SetTimer(
			BobbingTimer,
			this,
			&USplatCreatorSubsystem::UpdateBobbing,
			0.016f, // ~60fps - good balance between smoothness and performance
			true
		);
	}
}

void USplatCreatorSubsystem::UpdateBobbing()
{
	if (!bIsBobbing || !PointCloudComponent || BasePointPositions.Num() == 0)
	{
		return;
	}
	
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	
	// Update bobbing time with speed multiplier
	// Use fixed timestep for consistent animation regardless of frame rate
	float DeltaTime = World->GetDeltaSeconds();
	float CurrentSpeed = BaseBobbingSpeed * BobbingSpeedMultiplier;
	BobbingTime += DeltaTime * CurrentSpeed;
	
	// Use smooth sine wave for natural bobbing motion
	// Higher update rate (120fps) ensures smooth interpolation between frames
	float Offset = FMath::Sin(BobbingTime * 2.0f * UE_PI) * BobbingAmplitude;
	
	FVector DirectionVector = FVector::ZeroVector;
	switch (CurrentBobbingDirection)
	{
		case EBobbingDirection::Up:
			DirectionVector = FVector(0.0f, 0.0f, 1.0f); // Up in Unreal (positive Z)
			break;
		case EBobbingDirection::Down:
			DirectionVector = FVector(0.0f, 0.0f, -1.0f); // Down in Unreal (negative Z)
			break;
		case EBobbingDirection::Left:
			DirectionVector = FVector(-1.0f, 0.0f, 0.0f); // Left (negative X)
			break;
		case EBobbingDirection::Right:
			DirectionVector = FVector(1.0f, 0.0f, 0.0f); // Right (positive X)
			break;
		default:
			return;
	}
	
	FVector BobbingOffset = DirectionVector * Offset;
	
	// Optimized: Update all instances but only mark render state dirty once at the end
	int32 NumInstances = FMath::Min(PointCloudComponent->GetInstanceCount(), BasePointPositions.Num());
	
	// Update all instances - pass false to UpdateInstanceTransform to defer render state update
	for (int32 i = 0; i < NumInstances; i++)
	{
		// Calculate bobbed position from base position
		FVector BobbedBasePosition = BasePointPositions[i] + BobbingOffset;
		
		// Apply scaling relative to center if scale is not 1.0
		FVector FinalPosition;
		float ScaledSphereSize;
		
		if (bHasSplatCenter && SplatScaleMultiplier != 1.0f)
		{
			// Scale the bobbed position relative to center
			FVector OffsetFromCenter = BobbedBasePosition - SplatCenter;
			FVector ScaledOffset = OffsetFromCenter * SplatScaleMultiplier;
			FinalPosition = SplatCenter + ScaledOffset;
			
			// Scale sphere size
			float BaseSphereSize = (i < SphereSizes.Num()) ? SphereSizes[i] : 0.06f;
			ScaledSphereSize = BaseSphereSize * SplatScaleMultiplier;
		}
		else
		{
			// No scaling, use bobbed position directly
			FinalPosition = BobbedBasePosition;
			ScaledSphereSize = (i < SphereSizes.Num()) ? SphereSizes[i] : 0.06f;
		}
		
		FTransform Transform;
		if (PointCloudComponent->GetInstanceTransform(i, Transform))
		{
			Transform.SetLocation(FinalPosition);
			Transform.SetScale3D(FVector(ScaledSphereSize));
			// Pass false for bMarkRenderStateDirty - we'll do it once at the end
			PointCloudComponent->UpdateInstanceTransform(i, Transform, false, false);
		}
	}
	
	// Mark render state dirty once at the end (much more efficient than per-instance)
	PointCloudComponent->MarkRenderStateDirty();
}

void USplatCreatorSubsystem::StopBobbing(bool bSmoothInterpolation)
{
	if (!bIsBobbing)
	{
		return;
	}
	
	bIsBobbing = false;
	CurrentBobbingDirection = EBobbingDirection::None;
	BobbingTime = 0.0f;
	
	UWorld* World = GetWorld();
	if (World && BobbingTimer.IsValid())
	{
		World->GetTimerManager().ClearTimer(BobbingTimer);
	}
	
	if (bSmoothInterpolation && PointCloudComponent && BasePointPositions.Num() > 0)
	{
		// Start smooth interpolation back to base positions
		int32 NumInstances = FMath::Min(PointCloudComponent->GetInstanceCount(), BasePointPositions.Num());
		InterpolationStartPositions.Empty();
		InterpolationStartPositions.Reserve(NumInstances);
		
		// Store current positions as starting points for interpolation
		for (int32 i = 0; i < NumInstances; i++)
		{
			FTransform Transform;
			if (PointCloudComponent->GetInstanceTransform(i, Transform))
			{
				InterpolationStartPositions.Add(Transform.GetLocation());
			}
			else
			{
				InterpolationStartPositions.Add(BasePointPositions[i]);
			}
		}
		
		bIsInterpolatingToBase = true;
		InterpolationTime = 0.0f;
		
		// Start interpolation timer
		if (World)
		{
			World->GetTimerManager().SetTimer(
				BobbingTimer, // Reuse bobbing timer for interpolation
				this,
				&USplatCreatorSubsystem::UpdateInterpolationToBase,
				0.016f, // ~60fps
				true
			);
		}
	}
	else
	{
		// Immediate restore (no interpolation)
		BobbingSpeedMultiplier = 1.0f; // Reset speed to normal when stopping
		
		if (PointCloudComponent && BasePointPositions.Num() > 0)
		{
			int32 NumInstances = FMath::Min(PointCloudComponent->GetInstanceCount(), BasePointPositions.Num());
			
			for (int32 i = 0; i < NumInstances; i++)
			{
				FVector FinalPosition;
				float ScaledSphereSize;
				
				// Apply scaling if scale is not 1.0
				if (bHasSplatCenter && SplatScaleMultiplier != 1.0f)
				{
					FVector OffsetFromCenter = BasePointPositions[i] - SplatCenter;
					FVector ScaledOffset = OffsetFromCenter * SplatScaleMultiplier;
					FinalPosition = SplatCenter + ScaledOffset;
					
					float BaseSphereSize = (i < SphereSizes.Num()) ? SphereSizes[i] : 0.06f;
					ScaledSphereSize = BaseSphereSize * SplatScaleMultiplier;
				}
				else
				{
					FinalPosition = BasePointPositions[i];
					ScaledSphereSize = (i < SphereSizes.Num()) ? SphereSizes[i] : 0.06f;
				}
				
				FTransform Transform;
				if (PointCloudComponent->GetInstanceTransform(i, Transform))
				{
					Transform.SetLocation(FinalPosition);
					Transform.SetScale3D(FVector(ScaledSphereSize));
					PointCloudComponent->UpdateInstanceTransform(i, Transform, false, false);
				}
			}
			
			PointCloudComponent->MarkRenderStateDirty();
		}
		
	}
}

// ============================================================
// RANDOM MOVEMENT SYSTEM
// ============================================================

void USplatCreatorSubsystem::StartRandomMovement()
{
	if (!PointCloudComponent || BasePointPositions.Num() == 0)
	{
		if(debug) UE_LOG(LogTemp, Warning, TEXT("[SplatCreator] Cannot start random movement - no base positions stored"));
		return;
	}
	
	bIsRandomMoving = true;
	RandomChangeTimer = 0.0f;
	RandomMovementSpeedMultiplier = 1.0f; // Reset speed multiplier when starting
	
	// Initialize random velocities and targets for each sphere
	int32 NumSpheres = BasePointPositions.Num();

	RandomVelocities.Empty();
	RandomTargets.Empty();
	RandomCurrentPositions.Empty();
	RandomVelocities.Reserve(NumSpheres);
	RandomTargets.Reserve(NumSpheres);
	RandomCurrentPositions.Reserve(NumSpheres);
	
	FRandomStream RandomStream(FMath::Rand());
	
	for (int32 i = 0; i < NumSpheres; i++)
	{
		// Start from base position
		RandomCurrentPositions.Add(BasePointPositions[i]);
		
		// Generate random direction vector
		FVector RandomDirection = FVector(
			RandomStream.FRandRange(-1.0f, 1.0f),
			RandomStream.FRandRange(-1.0f, 1.0f),
			RandomStream.FRandRange(-1.0f, 1.0f)
		).GetSafeNormal();
		
		// Random target position within radius from base position
		float RandomDistance = RandomStream.FRandRange(0.0f, RandomMovementRadius);
		FVector RandomTarget = BasePointPositions[i] + (RandomDirection * RandomDistance);
		
		RandomTargets.Add(RandomTarget);
		
		// Initial velocity towards target
				FVector ToTarget = (RandomTarget - BasePointPositions[i]).GetSafeNormal();
		float CurrentSpeed = BaseRandomMovementSpeed * RandomMovementSpeedMultiplier;
		RandomVelocities.Add(ToTarget * CurrentSpeed);
	}
	
	UWorld* World = GetWorld();
	if (World)
	{
		// Update random movement at 60fps for smooth animation
		World->GetTimerManager().SetTimer(
			RandomMovementTimer,
			this,
			&USplatCreatorSubsystem::UpdateRandomMovement,
			0.016f, // ~60fps
			true
		);
	}
}

void USplatCreatorSubsystem::UpdateRandomMovement()
{
	if (!bIsRandomMoving || !PointCloudComponent || BasePointPositions.Num() == 0)
	{
		return;
	}
	
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	
	float DeltaTime = World->GetDeltaSeconds();
	RandomChangeTimer += DeltaTime;
	
	int32 NumInstances = FMath::Min(PointCloudComponent->GetInstanceCount(), BasePointPositions.Num());
	
	FRandomStream RandomStream(FMath::Rand());
	
	// Periodically change random directions
	bool bShouldChangeDirections = (RandomChangeTimer >= RandomChangeInterval);
	if (bShouldChangeDirections)
	{
		RandomChangeTimer = 0.0f;
	}
	
	// Update all instances - pass false to UpdateInstanceTransform to defer render state update
	for (int32 i = 0; i < NumInstances; i++)
	{
		if (i >= RandomTargets.Num() || i >= RandomVelocities.Num() || i >= RandomCurrentPositions.Num())
		{
			continue;
		}
		
		FVector CurrentPosition = RandomCurrentPositions[i];
		FVector TargetPosition = RandomTargets[i];
		FVector Velocity = RandomVelocities[i];
		
		// Periodically pick new random target
		if (bShouldChangeDirections)
		{
			FVector RandomDirection = FVector(
				RandomStream.FRandRange(-1.0f, 1.0f),
				RandomStream.FRandRange(-1.0f, 1.0f),
				RandomStream.FRandRange(-1.0f, 1.0f)
			).GetSafeNormal();
			
			float RandomDistance = RandomStream.FRandRange(0.0f, RandomMovementRadius);
			TargetPosition = BasePointPositions[i] + (RandomDirection * RandomDistance);
			RandomTargets[i] = TargetPosition;
			
			FVector ToTarget = (TargetPosition - CurrentPosition).GetSafeNormal();
			float CurrentSpeed = BaseRandomMovementSpeed * RandomMovementSpeedMultiplier;
			Velocity = ToTarget * CurrentSpeed;
			RandomVelocities[i] = Velocity;
		}
		else
		{
			// Update velocity towards target
			FVector ToTarget = (TargetPosition - CurrentPosition);
			float DistanceToTarget = ToTarget.Size();
			float CurrentSpeed = BaseRandomMovementSpeed * RandomMovementSpeedMultiplier;
			
			if (DistanceToTarget > 1.0f)
			{
				Velocity = ToTarget.GetSafeNormal() * CurrentSpeed;
				RandomVelocities[i] = Velocity;
			}
			else
			{
				// Reached target, pick new random target
				FVector RandomDirection = FVector(
					RandomStream.FRandRange(-1.0f, 1.0f),
					RandomStream.FRandRange(-1.0f, 1.0f),
					RandomStream.FRandRange(-1.0f, 1.0f)
				).GetSafeNormal();
				
				float RandomDistance = RandomStream.FRandRange(0.0f, RandomMovementRadius);
				TargetPosition = BasePointPositions[i] + (RandomDirection * RandomDistance);
				RandomTargets[i] = TargetPosition;
				
				Velocity = (TargetPosition - CurrentPosition).GetSafeNormal() * CurrentSpeed;
				RandomVelocities[i] = Velocity;
			}
		}
		
		// Move sphere towards target
		FVector NewPosition = CurrentPosition + (Velocity * DeltaTime);
		
		// Clamp to radius from base position
		FVector OffsetFromBase = NewPosition - BasePointPositions[i];
		float DistanceFromBase = OffsetFromBase.Size();
		if (DistanceFromBase > RandomMovementRadius)
		{
			NewPosition = BasePointPositions[i] + (OffsetFromBase.GetSafeNormal() * RandomMovementRadius);
		}
		
		// Update current position
		RandomCurrentPositions[i] = NewPosition;
		
		// Apply scaling if scale is not 1.0
		FVector FinalPosition;
		float ScaledSphereSize;
		
		if (bHasSplatCenter && SplatScaleMultiplier != 1.0f)
		{
			// Scale the random position relative to center
			FVector OffsetFromCenter = NewPosition - SplatCenter;
			FVector ScaledOffset = OffsetFromCenter * SplatScaleMultiplier;
			FinalPosition = SplatCenter + ScaledOffset;
			
			float BaseSphereSize = (i < SphereSizes.Num()) ? SphereSizes[i] : 0.06f;
			ScaledSphereSize = BaseSphereSize * SplatScaleMultiplier;
		}
		else
		{
			FinalPosition = NewPosition;
			ScaledSphereSize = (i < SphereSizes.Num()) ? SphereSizes[i] : 0.06f;
		}
		
		FTransform Transform;
		if (PointCloudComponent->GetInstanceTransform(i, Transform))
		{
			Transform.SetLocation(FinalPosition);
			Transform.SetScale3D(FVector(ScaledSphereSize));
			// Pass false for bMarkRenderStateDirty - we'll do it once at the end
			PointCloudComponent->UpdateInstanceTransform(i, Transform, false, false);
		}
	}
	
	// Mark render state dirty once at the end (much more efficient than per-instance)
	PointCloudComponent->MarkRenderStateDirty();
}

void USplatCreatorSubsystem::StopRandomMovement(bool bSmoothInterpolation)
{
	if (!bIsRandomMoving)
	{
		return;
	}
	
	bIsRandomMoving = false;
	RandomChangeTimer = 0.0f;
	
	UWorld* World = GetWorld();
	if (World && RandomMovementTimer.IsValid())
	{
		World->GetTimerManager().ClearTimer(RandomMovementTimer);
	}
	
	if (bSmoothInterpolation && PointCloudComponent && BasePointPositions.Num() > 0)
	{
		// Start smooth interpolation back to base positions
		int32 NumInstances = FMath::Min(PointCloudComponent->GetInstanceCount(), BasePointPositions.Num());
		InterpolationStartPositions.Empty();
		InterpolationStartPositions.Reserve(NumInstances);
		
		// Store current positions as starting points for interpolation
		for (int32 i = 0; i < NumInstances; i++)
		{
			FTransform Transform;
			if (PointCloudComponent->GetInstanceTransform(i, Transform))
			{
				InterpolationStartPositions.Add(Transform.GetLocation());
			}
			else
			{
				InterpolationStartPositions.Add(BasePointPositions[i]);
			}
		}
		
		bIsInterpolatingToBase = true;
		InterpolationTime = 0.0f;
		
		// Start interpolation timer
		if (World)
		{
			World->GetTimerManager().SetTimer(
				RandomMovementTimer, // Reuse random movement timer for interpolation
				this,
				&USplatCreatorSubsystem::UpdateInterpolationToBase,
				0.016f, // ~60fps
				true
			);
		}
		
	}
	else
	{
		// Immediate restore (no interpolation)
		RandomMovementSpeedMultiplier = 1.0f; // Reset speed to normal when stopping
		
		if (PointCloudComponent && BasePointPositions.Num() > 0)
		{
			int32 NumInstances = FMath::Min(PointCloudComponent->GetInstanceCount(), BasePointPositions.Num());
			
			for (int32 i = 0; i < NumInstances; i++)
			{
				FVector FinalPosition;
				float ScaledSphereSize;
				
				// Apply scaling if scale is not 1.0
				if (bHasSplatCenter && SplatScaleMultiplier != 1.0f)
				{
					FVector OffsetFromCenter = BasePointPositions[i] - SplatCenter;
					FVector ScaledOffset = OffsetFromCenter * SplatScaleMultiplier;
					FinalPosition = SplatCenter + ScaledOffset;
					
					float BaseSphereSize = (i < SphereSizes.Num()) ? SphereSizes[i] : 0.06f;
					ScaledSphereSize = BaseSphereSize * SplatScaleMultiplier;
				}
				else
				{
					FinalPosition = BasePointPositions[i];
					ScaledSphereSize = (i < SphereSizes.Num()) ? SphereSizes[i] : 0.06f;
				}
				
				FTransform Transform;
				if (PointCloudComponent->GetInstanceTransform(i, Transform))
				{
					Transform.SetLocation(FinalPosition);
					Transform.SetScale3D(FVector(ScaledSphereSize));
					PointCloudComponent->UpdateInstanceTransform(i, Transform, false, false);
				}
			}
			
			PointCloudComponent->MarkRenderStateDirty();
		}
		
	}
	
	RandomVelocities.Empty();
	RandomTargets.Empty();
	RandomCurrentPositions.Empty();
}

void USplatCreatorSubsystem::UpdateInterpolationToBase()
{
	if (!bIsInterpolatingToBase || !PointCloudComponent || BasePointPositions.Num() == 0)
	{
		return;
	}
	
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	
	float DeltaTime = World->GetDeltaSeconds();
	InterpolationTime += DeltaTime;
	
	float Alpha = FMath::Clamp(InterpolationTime / InterpolationDuration, 0.0f, 1.0f);
	
	// Smooth easing function (ease-in-out cubic)
	float EasedAlpha = Alpha < 0.5f
		? 4.0f * Alpha * Alpha * Alpha
		: 1.0f - FMath::Pow(-2.0f * Alpha + 2.0f, 3.0f) / 2.0f;
	
	int32 NumInstances = FMath::Min(PointCloudComponent->GetInstanceCount(), BasePointPositions.Num());
	const int32 BatchSize = 5000;
	
	for (int32 BatchStart = 0; BatchStart < NumInstances; BatchStart += BatchSize)
	{
		int32 BatchEnd = FMath::Min(BatchStart + BatchSize, NumInstances);
		
		for (int32 i = BatchStart; i < BatchEnd; i++)
		{
			if (i >= InterpolationStartPositions.Num())
			{
				continue;
			}
			
			// Calculate target position (base position with scaling applied)
			FVector TargetPosition;
			float ScaledSphereSize;
			
			if (bHasSplatCenter && SplatScaleMultiplier != 1.0f)
			{
				FVector OffsetFromCenter = BasePointPositions[i] - SplatCenter;
				FVector ScaledOffset = OffsetFromCenter * SplatScaleMultiplier;
				TargetPosition = SplatCenter + ScaledOffset;
				
				float BaseSphereSize = (i < SphereSizes.Num()) ? SphereSizes[i] : 0.06f;
				ScaledSphereSize = BaseSphereSize * SplatScaleMultiplier;
			}
			else
			{
				TargetPosition = BasePointPositions[i];
				ScaledSphereSize = (i < SphereSizes.Num()) ? SphereSizes[i] : 0.06f;
			}
			
			// Interpolate from start position to target position
			FVector StartPosition = InterpolationStartPositions[i];
			FVector InterpolatedPosition = FMath::Lerp(StartPosition, TargetPosition, EasedAlpha);
			
			FTransform Transform;
			if (PointCloudComponent->GetInstanceTransform(i, Transform))
			{
				Transform.SetLocation(InterpolatedPosition);
				Transform.SetScale3D(FVector(ScaledSphereSize));
				PointCloudComponent->UpdateInstanceTransform(i, Transform, false, false);
			}
		}
	}
	
	PointCloudComponent->MarkRenderStateDirty();
	
	// Check if interpolation is complete
	if (Alpha >= 1.0f)
	{
		bIsInterpolatingToBase = false;
		InterpolationTime = 0.0f;
		InterpolationStartPositions.Empty();
		
		if (World)
		{
			// Clear both timers in case either was used for interpolation
			if (BobbingTimer.IsValid())
			{
				World->GetTimerManager().ClearTimer(BobbingTimer);
			}
			if (RandomMovementTimer.IsValid())
			{
				World->GetTimerManager().ClearTimer(RandomMovementTimer);
			}
		}
	}
}
