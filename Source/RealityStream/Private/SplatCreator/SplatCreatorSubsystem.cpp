#include "SplatCreator/SplatCreatorSubsystem.h"
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


// ============================================================
// Initialize
// ============================================================

void USplatCreatorSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    UE_LOG(LogTemp, Display, TEXT("[SplatCreator] Subsystem initialized"));
}


// ============================================================
// PointCloud
// ============================================================

void USplatCreatorSubsystem::StartPointCloudSystem()
{
	if (bIsInitialized)
	{
		UE_LOG(LogTemp, Warning, TEXT("[SplatCreator] Already initialized"));
		return;
	}
	
	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Error, TEXT("[SplatCreator] Cannot start - no world available"));
		return;
	}
	
	UE_LOG(LogTemp, Display, TEXT("[SplatCreator] Starting point cloud system..."));
	
	ScanForPLYFiles();
	
	if (PlyFiles.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[SplatCreator] No PLY files found in %s"), *GetSplatCreatorFolder());
		return;
	}
	
	UE_LOG(LogTemp, Display, TEXT("[SplatCreator] Found %d PLY files"), PlyFiles.Num());
	
	// Load first PLY file
	FString FirstPLYPath = GetSplatCreatorFolder() / PlyFiles[0];
	LoadPLYFile(FirstPLYPath);
	
	// Start cycle timer (45 seconds)
	World->GetTimerManager().SetTimer(
		CycleTimer,
		this,
		&USplatCreatorSubsystem::CycleToNextPLY,
		45.0f,
		true
	);
	UE_LOG(LogTemp, Display, TEXT("[SplatCreator] Cycle timer started - will change PLY every 45 seconds"));
	
	bIsInitialized = true;
}

void USplatCreatorSubsystem::Deinitialize()
{
	if (CycleTimer.IsValid() && GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(CycleTimer);
	}
	if (MorphTimer.IsValid() && GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(MorphTimer);
	}
	if (CurrentPointCloudActor)
	{
		CurrentPointCloudActor->Destroy();
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

void USplatCreatorSubsystem::ScanForPLYFiles()
{
	FString SplatCreatorDir = GetSplatCreatorFolder();
	PlyFiles.Empty();
	
	// Convert to absolute path and normalize
	FString AbsolutePath = FPaths::ConvertRelativePathToFull(SplatCreatorDir);
	FPaths::NormalizeDirectoryName(AbsolutePath);
	
	UE_LOG(LogTemp, Display, TEXT("[SplatCreator] Scanning for PLY files in: %s"), *AbsolutePath);
	
	// Check if directory exists
	if (!FPaths::DirectoryExists(AbsolutePath))
	{
		UE_LOG(LogTemp, Error, TEXT("[SplatCreator] Directory does not exist: %s"), *AbsolutePath);
		return;
	}
	
	IFileManager::Get().FindFiles(PlyFiles, *(AbsolutePath / TEXT("*.ply")), true, false);
	PlyFiles.Sort();
	
	UE_LOG(LogTemp, Display, TEXT("[SplatCreator] Found %d PLY files in %s"), PlyFiles.Num(), *AbsolutePath);
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
	
	UE_LOG(LogTemp, Display, TEXT("[SplatCreator] Cycling to PLY: %s"), *PlyFiles[CurrentFileIndex]);
	LoadPLYFile(PLYPath);
}

// ============================================================
// READ PLY FILES
// ============================================================

void USplatCreatorSubsystem::LoadPLYFile(const FString& PLYPath)
{
	TArray<FVector> Positions;
	TArray<FColor> Colors;
	
	if (!ParsePLYFile(PLYPath, Positions, Colors))
	{
		UE_LOG(LogTemp, Error, TEXT("[SplatCreator] Failed to parse PLY file: %s"), *PLYPath);
            return;
        }

	UE_LOG(LogTemp, Display, TEXT("[SplatCreator] Parsed %d points from %s"), Positions.Num(), *PLYPath);
	
	// Log coordinate ranges for debugging
	if (Positions.Num() > 0)
	{
		FVector MinBounds = Positions[0];
		FVector MaxBounds = Positions[0];
		for (const FVector& Pos : Positions)
		{
			MinBounds = MinBounds.ComponentMin(Pos);
			MaxBounds = MaxBounds.ComponentMax(Pos);
		}
		UE_LOG(LogTemp, Display, TEXT("[SplatCreator] Coordinate range: Min(%s) Max(%s)"), 
			*MinBounds.ToString(), *MaxBounds.ToString());
	}
	
	// Uniformly sample points to limit count for performance
	TArray<FVector> FilteredPositions;
	TArray<FColor> FilteredColors;
	SamplePointsUniformly(Positions, Colors, FilteredPositions, FilteredColors);
	
	if (FilteredPositions.Num() > 0)
	{
		Positions = FilteredPositions;
		Colors = FilteredColors;
		UE_LOG(LogTemp, Display, TEXT("[SplatCreator] After filtering: %d points"), Positions.Num());
        }
        else
        {
		UE_LOG(LogTemp, Warning, TEXT("[SplatCreator] Filtering removed all points, using original"));
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
		const FVector DownOffset = FVector(0.0f, 0.0f, -100.0f); // Move down by 100 units (moved up from -200)
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
    World->GetTimerManager().SetTimer(
				MorphTimer,
        this,
				&USplatCreatorSubsystem::UpdateMorph,
				0.033f, // ~30fps update rate for better performance
        true
    );
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
		UE_LOG(LogTemp, Display, TEXT("[SamplePoints] Keeping all %d points (under limit)"), InPositions.Num());
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
	
	UE_LOG(LogTemp, Display, TEXT("[SamplePoints] Uniform sampling: %d -> %d points (step: %d)"), 
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
	
	UE_LOG(LogTemp, Display, TEXT("[SplatCreator] Calculating adaptive sphere sizes for %d points (SearchRadius=%.1f)..."), NumPoints, SearchRadius);
	
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
	
	UE_LOG(LogTemp, Display, TEXT("[SplatCreator] Calculated adaptive sphere sizes: min=%.3f, max=%.3f"), 
		MinCubeSize, MaxCubeSize);
}

void USplatCreatorSubsystem::CreatePointCloud(const TArray<FVector>& Positions, const TArray<FColor>& Colors)
{
	UWorld* World = GetWorld();
	if (!World) return;
	
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
	
	// Try to reduce HISM's internal culling aggressiveness
	// Note: HISM uses an octree structure internally, and reducing instance count helps
	
	// Set material to see colors
	UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/_GENERATED/Materials/M_VertexColor.M_VertexColor"));
	if (!Material)
	{
		UE_LOG(LogTemp, Warning, TEXT("[SplatCreator] Material M_VertexColor not found at /Game/_GENERATED/Materials/, trying fallback"));
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
			UE_LOG(LogTemp, Display, TEXT("[SplatCreator] Created Material Instance Dynamic with emissive and contrast properties"));
		}
		else
		{
			PointCloudComponent->SetMaterial(0, Material);
			UE_LOG(LogTemp, Display, TEXT("[SplatCreator] Set material: %s (failed to create MID)"), *Material->GetName());
		}
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[SplatCreator] Failed to load material"));
	}
	
	// Set component as root BEFORE registering (critical for proper bounds calculation)
	CurrentPointCloudActor->SetRootComponent(PointCloudComponent);
	
	// Position actor at origin
	CurrentPointCloudActor->SetActorLocation(FVector::ZeroVector);
	
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
	UE_LOG(LogTemp, Display, TEXT("[SplatCreator] Calculated %d sphere sizes for dense region detection"), SphereSizes.Num());
	
	// Add instances in batches
	int32 NumInstances = FMath::Min(Positions.Num(), Colors.Num());
	const int32 BatchSize = 5000;
	
	UE_LOG(LogTemp, Display, TEXT("[SplatCreator] Adding %d instances in batches of %d..."), NumInstances, BatchSize);
	
	for (int32 BatchStart = 0; BatchStart < NumInstances; BatchStart += BatchSize)
	{
		int32 BatchEnd = FMath::Min(BatchStart + BatchSize, NumInstances);
		TArray<FTransform> Transforms;
		Transforms.Reserve(BatchEnd - BatchStart);
		
		// Offset to move PLY down (negative Z in Unreal = down)
		const FVector DownOffset = FVector(0.0f, 0.0f, -100.0f);
		
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
			UE_LOG(LogTemp, Display, TEXT("[SplatCreator] Added %d / %d instances..."), BatchEnd, NumInstances);
		}
	}
	
	// Store current point positions (scaled and offset) for dense region detection BEFORE broadcasting
	// Apply the same offset as used for rendering
	const FVector DownOffset = FVector(0.0f, 0.0f, -100.0f);
	CurrentPointPositions.Empty();
	CurrentPointPositions.Reserve(ScaledPositions.Num());
	for (const FVector& ScaledPos : ScaledPositions)
	{
		CurrentPointPositions.Add(ScaledPos + DownOffset);
	}
	
	// Verify SphereSizes is populated (it should be from CalculateAdaptiveSphereSizes call above)
	UE_LOG(LogTemp, Display, TEXT("[SplatCreator] Stored %d point positions and %d sphere sizes for dense region detection"), 
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
		
		FVector BoxSize = BoundingBox.GetSize();
		FVector BoxCenter = BoundingBox.GetCenter();
		UE_LOG(LogTemp, Display, TEXT("[SplatCreator] Set explicit bounds: Min=(%.1f, %.1f, %.1f), Max=(%.1f, %.1f, %.1f)"), 
			BoundingBox.Min.X, BoundingBox.Min.Y, BoundingBox.Min.Z,
			BoundingBox.Max.X, BoundingBox.Max.Y, BoundingBox.Max.Z);
		UE_LOG(LogTemp, Display, TEXT("[SplatCreator] Bounds size: X=%.1f, Y=%.1f, Z=%.1f, Center: (%.1f, %.1f, %.1f)"), 
			BoxSize.X, BoxSize.Y, BoxSize.Z, BoxCenter.X, BoxCenter.Y, BoxCenter.Z);
		
		// Notify other subsystems that bounds have been updated (CurrentPointPositions is now stored)
		OnSplatBoundsUpdated.Broadcast(BoundingBox);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[SplatCreator] Cannot calculate bounds - CurrentPointPositions is empty"));
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
	
	UE_LOG(LogTemp, Display, TEXT("[SplatCreator] Created point cloud with %d spheres"), NumInstances);
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
	if (MorphTimer.IsValid() && GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(MorphTimer);
	}
	
	bIsMorphing = false;
	MorphProgress = 0.0f;
	MorphUpdateIndex = 0;
	
	// Ensure final state
	if (PointCloudComponent && NewPositions.Num() > 0)
	{
		for (int32 i = 0; i < NewPositions.Num() && i < PointCloudComponent->GetInstanceCount(); i++)
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
			
			UE_LOG(LogTemp, Display, TEXT("[SplatCreator] Updated bounds after morph: Min=(%.1f, %.1f, %.1f), Max=(%.1f, %.1f, %.1f)"), 
				BoundingBox.Min.X, BoundingBox.Min.Y, BoundingBox.Min.Z,
				BoundingBox.Max.X, BoundingBox.Max.Y, BoundingBox.Max.Z);
			
			// Notify other subsystems that bounds have been updated
			OnSplatBoundsUpdated.Broadcast(BoundingBox);
		}
	}
}

// ============================================================
// GET DIMENSIONS FOR HYPER3DOBJECTS
// ============================================================

FVector2D USplatCreatorSubsystem::GetSplatDimensions() const
{
	if (!bHasSplatBounds)
	{
		UE_LOG(LogTemp, Warning, TEXT("[SplatCreator] No splat bounds available, returning default (200x200)"));
		return FVector2D(200.0f, 200.0f);
	}
	
	FVector BoxSize = CurrentSplatBounds.GetSize();
	FVector2D Dimensions(BoxSize.X, BoxSize.Y);
	
	UE_LOG(LogTemp, Display, TEXT("[SplatCreator] Splat dimensions: X=%.1f, Y=%.1f"), Dimensions.X, Dimensions.Y);
	return Dimensions;
}

FVector USplatCreatorSubsystem::GetSplatCenter() const
{
	if (!bHasSplatBounds)
	{
		UE_LOG(LogTemp, Warning, TEXT("[SplatCreator] No splat bounds available, returning origin"));
		return FVector::ZeroVector;
	}
	
	FVector Center = CurrentSplatBounds.GetCenter();
	UE_LOG(LogTemp, Display, TEXT("[SplatCreator] Splat center: %s"), *Center.ToString());
	return Center;
}

FBox USplatCreatorSubsystem::GetSplatBounds() const
{
	if (!bHasSplatBounds)
	{
		UE_LOG(LogTemp, Warning, TEXT("[SplatCreator] No splat bounds available, returning empty box"));
		return FBox(ForceInit);
	}
	
	return CurrentSplatBounds;
}

TArray<FVector> USplatCreatorSubsystem::GetDensePointRegions(float DensityThreshold) const
{
	TArray<FVector> DenseRegions;
	
	if (CurrentPointPositions.Num() == 0 || SphereSizes.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[SplatCreator] No point positions or sphere sizes available (Positions: %d, SphereSizes: %d)"), 
			CurrentPointPositions.Num(), SphereSizes.Num());
		return DenseRegions;
	}
	
	// Points with small sphere sizes indicate dense regions
	// Small sphere sizes (like 0.1) indicate dense areas, large ones (0.3) indicate sparse areas
	// DensityThreshold is the maximum sphere size to consider as dense (default 0.15 means sphere size <= 0.15 is dense)
	const float MaxDenseSphereSize = DensityThreshold;
	
	int32 DenseCount = 0;
	for (int32 i = 0; i < CurrentPointPositions.Num() && i < SphereSizes.Num(); i++)
	{
		// Small sphere size = dense region
		if (SphereSizes[i] <= MaxDenseSphereSize)
		{
			DenseRegions.Add(CurrentPointPositions[i]);
			DenseCount++;
		}
	}
	
	UE_LOG(LogTemp, Display, TEXT("[SplatCreator] Found %d dense points out of %d total (sphere size threshold: %.3f)"), 
		DenseCount, CurrentPointPositions.Num(), MaxDenseSphereSize);
	
	return DenseRegions;
}

bool USplatCreatorSubsystem::IsPositionTooCloseToSplatPoints(const FVector& Position, float MinDistance, bool bCheckHorizontalOnly) const
{
	if (CurrentPointPositions.Num() == 0)
	{
		// No splat points loaded yet, so position is valid
		return false;
	}

	// Check distance to all splat points
	// For performance, we could optimize this with spatial partitioning, but for now check all points
	const float MinDistanceSquared = MinDistance * MinDistance;
	
	for (const FVector& SplatPoint : CurrentPointPositions)
	{
		float DistanceSquared;
		if (bCheckHorizontalOnly)
		{
			// Only check horizontal (X,Y) distance, ignore Z
			FVector2D Pos2D(Position.X, Position.Y);
			FVector2D Splat2D(SplatPoint.X, SplatPoint.Y);
			DistanceSquared = FVector2D::DistSquared(Pos2D, Splat2D);
		}
		else
		{
			// Check full 3D distance
			DistanceSquared = FVector::DistSquared(Position, SplatPoint);
		}
		
		if (DistanceSquared < MinDistanceSquared)
		{
			return true; // Position is too close to a splat point
		}
	}
	
	return false; // Position is far enough from all splat points
}
