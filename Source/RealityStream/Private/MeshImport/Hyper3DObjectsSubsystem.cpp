#include "MeshImport/Hyper3DObjectsSubsystem.h"

#include "Engine/World.h"
#include "TimerManager.h"
#include "Engine/Engine.h"
#include "GameFramework/Actor.h"
#include "Components/SceneComponent.h"
#include "ProceduralMeshComponent.h"
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
#include "AssetRegistry/AssetRegistryModule.h"

namespace Hyper3DObjectsImport
{
	// User's material with texture parameters (try this first!)
	static constexpr TCHAR ProceduralMeshTextureMaterialPath[] = TEXT("/Game/M_ProceduralMeshTexture.M_ProceduralMeshTexture");
	static constexpr TCHAR ProceduralMeshTextureMaterialPathAlt[] = TEXT("/Game/ImportedTextures/M_ProceduralMeshTexture.M_ProceduralMeshTexture");
	// Try to use materials that exist in UE5.6
	static constexpr TCHAR VertexColorMaterialPathA[] = TEXT("/Game/_GENERATED/Materials/M_VertexColor.M_VertexColor");
	static constexpr TCHAR VertexColorMaterialPathB[] = TEXT("/Game/M_VertexColor.M_VertexColor");
	static constexpr TCHAR EditorVertexColorMaterialPath[] = TEXT("/Engine/EditorMaterials/WidgetVertexColorMaterial");
	// Try common UE5 material paths
	static constexpr TCHAR BasicMaterialPath[] = TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial");
	static constexpr TCHAR DefaultMaterialPath[] = TEXT("/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial");

	static float DegsPerRad(float Radians)
	{
		return Radians * 57.29577951308232f;
	}
}

void UHyper3DObjectsSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	UE_LOG(LogTemp, Display, TEXT("[Hyper3DObjects] Subsystem initialized"));

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

	UE_LOG(LogTemp, Display, TEXT("[Hyper3DObjects] Subsystem deinitialized"));

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
		UE_LOG(LogTemp, Warning, TEXT("[Hyper3DObjects] Cannot activate imports - no valid world"));
		return;
	}

	StartTimers(World);
	RefreshObjects();
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

void UHyper3DObjectsSubsystem::SetReferenceLocation(const FVector& InReferenceLocation)
{
	ReferenceLocation = InReferenceLocation;
	UE_LOG(LogTemp, Display, TEXT("[Hyper3DObjects] Reference location set to: %s"), *ReferenceLocation.ToString());
	
	// Update object positions immediately if objects are already spawned
	if (bImportsActive)
	{
		UpdateObjectMotion();
	}
}

void UHyper3DObjectsSubsystem::HandlePostWorldInit(UWorld* World, const UWorld::InitializationValues IVS)
{
	if (!World || !World->IsGameWorld())
	{
		return;
	}

	CachedWorld = World;

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
		UE_LOG(LogTemp, Warning, TEXT("[Hyper3DObjects] MeshImport directory not found: %s"), *ImportDir);
		return;
	}

	auto RemoveObjectAt = [this](int32 Index)
	{
		if (!ObjectInstances.IsValidIndex(Index))
		{
			return;
		}

		if (AActor* Actor = ObjectInstances[Index].Actor.Get())
		{
			Actor->Destroy();
		}

		if (UTexture2D* Texture = ObjectInstances[Index].DiffuseTexture.Get())
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

	// Remove stale objects
	for (int32 Index = ObjectInstances.Num() - 1; Index >= 0; --Index)
	{
		FObjectInstance& Instance = ObjectInstances[Index];
		if (!DesiredPathSet.Contains(Instance.SourceObjPath))
		{
			RemoveObjectAt(Index);
		}
		else if (!Instance.Actor.IsValid())
		{
			RemoveObjectAt(Index);
		}
	}

	// Spawn new objects
	for (const FString& FullPath : DesiredPathList)
	{
		bool bAlreadySpawned = ObjectInstances.ContainsByPredicate(
			[&FullPath](const FObjectInstance& Instance)
			{
				return Instance.SourceObjPath == FullPath;
			});

		if (!bAlreadySpawned)
		{
			if (!SpawnObjectFromOBJ(FullPath))
			{
				UE_LOG(LogTemp, Warning, TEXT("[Hyper3DObjects] Failed to spawn object for %s"), *FullPath);
			}
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

	FRandomStream Stream(12345); // deterministic layout
	const float BoxSize = 200.0f; // 200x200 box
	const float HalfBoxSize = BoxSize * 0.5f; // -100 to +100

	for (int32 Index = 0; Index < Count; ++Index)
	{
		FObjectInstance& Instance = ObjectInstances[Index];
		// Randomly place objects in a 50x50 box centered at origin
		Instance.BaseX = Stream.FRandRange(-HalfBoxSize, HalfBoxSize);
		Instance.BaseY = Stream.FRandRange(-HalfBoxSize, HalfBoxSize);
		// No rotation speed needed since we're not orbiting
		Instance.RotationSpeed = 0.0f;
		// More dramatic bobbing with varied frequencies
		Instance.BobFrequency = BaseBobFrequency + Stream.FRandRange(-BobFrequencyVariance, BobFrequencyVariance);
		Instance.BobAmplitude = BaseBobAmplitude + Stream.FRandRange(-BobAmplitudeVariance, BobAmplitudeVariance);
		// Random height variation for each object
		Instance.BaseHeight = BaseHeight + Stream.FRandRange(-HeightVariance, HeightVariance);
	}
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

		// Use random positions from 100x100 box (already set in UpdateObjectLayout)
		const float X = Instance.BaseX;
		const float Y = Instance.BaseY;
		// Bob up and down (Z axis) with varied amplitude
		const float Height = Instance.BaseHeight + FMath::Sin(Time * PI * Instance.BobFrequency * 2.0f) * Instance.BobAmplitude;

		// Position relative to reference location
		const FVector Location = ReferenceLocation + FVector(X, Y, Height);
		Actor->SetActorLocation(Location);

		// Keep base rotation, no facing rotation since we're not orbiting
		Actor->SetActorRotation(BaseMeshRotation);
	}
}

bool UHyper3DObjectsSubsystem::SpawnObjectFromOBJ(const FString& ObjPath)
{
	if (!FPaths::FileExists(ObjPath))
	{
		return false;
	}

	FString MtlFile;
	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FColor> Colors;

	if (!LoadOBJ(ObjPath, Vertices, Triangles, Normals, UVs, Colors, MtlFile))
	{
		return false;
	}

	// Resolve all textures for this OBJ
	FTextureSet TextureSet = ResolveAllTexturesForOBJ(ObjPath, MtlFile);
	
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

	return true;
}

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

		UE_LOG(LogTemp, Display, TEXT("[Hyper3DObjects] Loading texture from: %s"), *TexturePath);
		
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
			UE_LOG(LogTemp, Display, TEXT("[Hyper3DObjects] Successfully loaded texture: %s (Size: %dx%d)"), 
				*Texture->GetName(), Texture->GetSizeX(), Texture->GetSizeY());
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[Hyper3DObjects] Failed to load texture from: %s"), *TexturePath);
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
					UE_LOG(LogTemp, Warning, TEXT("[Hyper3DObjects] Invalid position index in OBJ: %s"), *ObjPath);
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
	
	FString PBRPath = FindTextureInDirectory(Directory, TEXT("texture_pbr"));
	if (PBRPath.IsEmpty()) PBRPath = FindTextureInDirectory(Directory, TEXT("pbr"));
	if (!PBRPath.IsEmpty()) TextureSet.PBR = LoadTextureFromFile(PBRPath);
	
	FString ShadedPath = FindTextureInDirectory(Directory, TEXT("shaded"));
	if (!ShadedPath.IsEmpty()) TextureSet.Shaded = LoadTextureFromFile(ShadedPath);
	
	return TextureSet;
}

FString UHyper3DObjectsSubsystem::FindFallbackTexture(const FString& ObjPath) const
{
	const FString Directory = FPaths::GetPath(ObjPath);
	const FString BaseName = FPaths::GetBaseFilename(ObjPath);

	const TArray<FString> PossibleExtensions = { TEXT(".png"), TEXT(".jpg"), TEXT(".jpeg"), TEXT(".bmp"), TEXT(".tga") };
	
	// First, try textures with the same name as the OBJ file
	for (const FString& Extension : PossibleExtensions)
	{
		const FString Candidate = Directory / (BaseName + Extension);
		if (FPaths::FileExists(Candidate))
		{
			UE_LOG(LogTemp, Display, TEXT("[Hyper3DObjects] Found texture matching OBJ name: %s"), *Candidate);
			return Candidate;
		}
	}

	// Then try common texture names in the same directory
	const TArray<FString> CommonTextureNames = { 
		TEXT("texture_diffuse"), 
		TEXT("diffuse"), 
		TEXT("texture"), 
		TEXT("albedo"), 
		TEXT("base"), 
		TEXT("color"),
		TEXT("texture_pbr"),
		TEXT("texture_normal"),
		TEXT("texture_metallic"),
		TEXT("texture_roughness"),
		TEXT("shaded")
	};

	for (const FString& TextureName : CommonTextureNames)
	{
		for (const FString& Extension : PossibleExtensions)
		{
			const FString Candidate = Directory / (TextureName + Extension);
			if (FPaths::FileExists(Candidate))
			{
				UE_LOG(LogTemp, Display, TEXT("[Hyper3DObjects] Found texture with common name: %s"), *Candidate);
				return Candidate;
			}
		}
	}

	// Last resort: search for any image file in the directory
	UE_LOG(LogTemp, Display, TEXT("[Hyper3DObjects] Searching for any texture file in directory: %s"), *Directory);
	TArray<FString> FoundFiles;
	for (const FString& Extension : PossibleExtensions)
	{
		FString SearchPattern = TEXT("*") + Extension;
		IFileManager::Get().FindFiles(FoundFiles, *(Directory / SearchPattern), true, false);
		if (FoundFiles.Num() > 0)
		{
			// Prefer diffuse/albedo/base textures
			for (const FString& File : FoundFiles)
			{
				FString LowerFile = File.ToLower();
				if (LowerFile.Contains(TEXT("diffuse")) || LowerFile.Contains(TEXT("albedo")) || 
				    LowerFile.Contains(TEXT("base")) || LowerFile.Contains(TEXT("color")) ||
				    LowerFile.Contains(TEXT("texture")))
				{
					const FString Candidate = Directory / File;
					UE_LOG(LogTemp, Display, TEXT("[Hyper3DObjects] Found texture file: %s"), *Candidate);
					return Candidate;
				}
			}
			// If no preferred texture found, use the first one
			const FString Candidate = Directory / FoundFiles[0];
			UE_LOG(LogTemp, Display, TEXT("[Hyper3DObjects] Using first found texture: %s"), *Candidate);
			return Candidate;
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("[Hyper3DObjects] No texture found for OBJ: %s"), *ObjPath);
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
		UE_LOG(LogTemp, Display, TEXT("[Hyper3DObjects] Using existing texture asset: %s"), *TargetAssetPath);
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

			UE_LOG(LogTemp, Display, TEXT("[Hyper3DObjects] Successfully imported texture as asset: %s"), *TargetAssetPath);
			return ImportedTexture;
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("[Hyper3DObjects] Failed to import texture as asset: %s"), *TexturePath);
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
	
	// Search for materials with "ProceduralMeshTexture" in the name
	TArray<FAssetData> AssetDataList;
	FARFilter Filter;
	Filter.ClassPaths.Add(UMaterial::StaticClass()->GetClassPathName());
	Filter.ClassPaths.Add(UMaterialInstance::StaticClass()->GetClassPathName());
	Filter.bRecursivePaths = true;
	
	// Try specific paths first
	Filter.PackagePaths.Add(FName("/Game"));
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
				UE_LOG(LogTemp, Display, TEXT("[Hyper3DObjects] Found user's material via AssetRegistry: %s"), *BaseMaterial->GetPathName());
				return BaseMaterial;
			}
		}
	}
	
	// Try direct path loading
	BaseMaterial = LoadObject<UMaterialInterface>(nullptr, Hyper3DObjectsImport::ProceduralMeshTextureMaterialPath);
	if (!BaseMaterial)
	{
		BaseMaterial = LoadObject<UMaterialInterface>(nullptr, Hyper3DObjectsImport::ProceduralMeshTextureMaterialPathAlt);
	}
	if (BaseMaterial)
	{
		UE_LOG(LogTemp, Display, TEXT("[Hyper3DObjects] Found user's material via direct path: %s"), *BaseMaterial->GetPathName());
		return BaseMaterial;
	}
	
	UE_LOG(LogTemp, Warning, TEXT("[Hyper3DObjects] Could not find M_ProceduralMeshTexture material. Searched paths:"));
	UE_LOG(LogTemp, Warning, TEXT("[Hyper3DObjects]   - %s"), Hyper3DObjectsImport::ProceduralMeshTextureMaterialPath);
	UE_LOG(LogTemp, Warning, TEXT("[Hyper3DObjects]   - %s"), Hyper3DObjectsImport::ProceduralMeshTextureMaterialPathAlt);
	UE_LOG(LogTemp, Warning, TEXT("[Hyper3DObjects] Falling back to default materials..."));
	
	// Fallback to other materials
	BaseMaterial = LoadObject<UMaterialInterface>(nullptr, Hyper3DObjectsImport::VertexColorMaterialPathA);
	if (!BaseMaterial)
	{
		BaseMaterial = LoadObject<UMaterialInterface>(nullptr, Hyper3DObjectsImport::VertexColorMaterialPathB);
	}
	if (!BaseMaterial)
	{
		BaseMaterial = LoadObject<UMaterialInterface>(nullptr, Hyper3DObjectsImport::EditorVertexColorMaterialPath);
	}
	if (!BaseMaterial)
	{
		BaseMaterial = LoadObject<UMaterialInterface>(nullptr, Hyper3DObjectsImport::BasicMaterialPath);
	}
	if (!BaseMaterial)
	{
		BaseMaterial = LoadObject<UMaterialInterface>(nullptr, Hyper3DObjectsImport::DefaultMaterialPath);
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
	
	UE_LOG(LogTemp, Warning, TEXT("[Hyper3DObjects] Material creation not implemented. Please create a material manually with texture parameters: BaseColor, Normal, Metallic, Roughness"));
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
		UE_LOG(LogTemp, Warning, TEXT("[Hyper3DObjects] Could not find any base material. Trying to use default engine material."));
		// Last resort: try to find any material in the engine
		BaseMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineMaterials/DefaultMaterial"));
		
		if (!BaseMaterial)
		{
			UE_LOG(LogTemp, Error, TEXT("[Hyper3DObjects] No materials available. Mesh will be untextured."));
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

	UE_LOG(LogTemp, Display, TEXT("[Hyper3DObjects] Material has %d texture parameters"), AvailableTextureParams.Num());
	
	// Log all available texture parameters
	if (AvailableTextureParams.Num() > 0)
	{
		UE_LOG(LogTemp, Display, TEXT("[Hyper3DObjects] Available texture parameters:"));
		for (const FMaterialParameterInfo& ParamInfo : AvailableTextureParams)
		{
			UE_LOG(LogTemp, Display, TEXT("[Hyper3DObjects]   - %s"), *ParamInfo.Name.ToString());
		}
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[Hyper3DObjects] WARNING: The base material has no texture parameters! Textures cannot be applied."));
		UE_LOG(LogTemp, Error, TEXT("[Hyper3DObjects] Please create a material with texture parameters (BaseColor, Normal, Metallic, Roughness) and set it as the base material."));
		UE_LOG(LogTemp, Error, TEXT("[Hyper3DObjects] The material path being used is: %s"), BaseMaterial ? *BaseMaterial->GetPathName() : TEXT("None"));
		UE_LOG(LogTemp, Error, TEXT("[Hyper3DObjects] Material name: %s"), BaseMaterial ? *BaseMaterial->GetName() : TEXT("None"));
	}

	// Helper function to find and set texture parameter
	auto SetTextureParameter = [&](UTexture2D* Texture, const TArray<FName>& ParameterNames, const FString& TextureType) -> bool
	{
		if (!Texture)
		{
			return false;
		}

		UE_LOG(LogTemp, Display, TEXT("[Hyper3DObjects] Attempting to apply %s texture: %s (Size: %dx%d)"), 
			*TextureType, *Texture->GetName(), Texture->GetSizeX(), Texture->GetSizeY());

		// First try to find an existing parameter
		for (const FName& ParamName : ParameterNames)
		{
			if (AvailableTextureParams.ContainsByPredicate([&ParamName](const FMaterialParameterInfo& Info)
			{
				return Info.Name == ParamName;
			}))
			{
				DynamicMaterial->SetTextureParameterValue(ParamName, Texture);
				UE_LOG(LogTemp, Display, TEXT("[Hyper3DObjects] Successfully applied %s texture to parameter: %s"), *TextureType, *ParamName.ToString());
				return true;
			}
		}

		// If no parameter found, try SetTextureParameterValueByInfo (might work even without parameter)
		for (const FName& ParamName : ParameterNames)
		{
			FMaterialParameterInfo ParamInfo(ParamName);
			DynamicMaterial->SetTextureParameterValueByInfo(ParamInfo, Texture);
			// Just try to set it - if the parameter doesn't exist, it will silently fail
			UE_LOG(LogTemp, Display, TEXT("[Hyper3DObjects] Attempted to set %s texture using SetTextureParameterValueByInfo: %s"), *TextureType, *ParamName.ToString());
			// Note: We can't verify if it worked without GetTextureParameterValueByInfo, but we'll try anyway
			return true; // Assume it worked
		}

		UE_LOG(LogTemp, Warning, TEXT("[Hyper3DObjects] Could not apply %s texture - no matching parameter found"), *TextureType);
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
	UE_LOG(LogTemp, Display, TEXT("[Hyper3DObjects] Material applied to mesh component"));
}

FString UHyper3DObjectsSubsystem::GetImportDirectory() const
{
	const FString PluginDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectPluginsDir() / TEXT("RealityStream/MeshImport"));

	if (FPaths::DirectoryExists(PluginDir))
	{
		return PluginDir;
	}

	// Fallback to explicit path provided by the user (absolute path)
	return TEXT("C:/Users/alexi/OneDrive/Documents/Unreal Projects/Reconstruction_3D/Plugins/RealityStream/MeshImport");
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
}


