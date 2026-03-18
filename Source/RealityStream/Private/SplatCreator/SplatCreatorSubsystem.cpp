#include "SplatCreator/SplatCreatorSubsystem.h"
#include "ComfyStream/ComfyImageSender.h"
#include "ComfyStream/ComfyPngDecoder.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "Engine/Canvas.h"
#include "Engine/CanvasRenderTarget2D.h"
#include "Engine/Texture2D.h"
#include "Engine/Engine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "GameFramework/Actor.h"
#include "Engine/StaticMesh.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/LocalPlayer.h"
#include "Math/RotationMatrix.h"
#include "Math/RandomStream.h"
#include "Math/Box.h"
int debug = 0; // 0 = off, 1 = on

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
	
	// Load a random PLY file (not alphabetical)
	CurrentFileIndex = FMath::RandRange(0, PlyFiles.Num() - 1);
	FString FirstPLYPath = GetSplatCreatorFolder() / PlyFiles[CurrentFileIndex];
	LoadPLYFile(FirstPLYPath);
	
	// Start cycle timer unless cycling on ComfyUI frame receipt
	if (!bCycleSplatOnComfyFrame)
	{
		World->GetTimerManager().SetTimer(
			CycleTimer,
			this,
			&USplatCreatorSubsystem::CycleToNextPLY,
			CycleIntervalSeconds,
			true
		);
		if(debug) UE_LOG(LogTemp, Display, TEXT("[SplatCreator] Cycle timer started - will change PLY every %.1f seconds"), CycleIntervalSeconds);
	}
	else
	{
		if(debug) UE_LOG(LogTemp, Display, TEXT("[SplatCreator] Splat will cycle when new frame received from ComfyUI (timer disabled)"));
	}
	
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
	if (PlaneMorphTimer.IsValid() && World)
	{
		World->GetTimerManager().ClearTimer(PlaneMorphTimer);
	}
	if (MorphStartDelayTimer.IsValid() && World)
	{
		World->GetTimerManager().ClearTimer(MorphStartDelayTimer);
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
	if (World)
	{
		World->GetTimerManager().ClearTimer(ImagePreviewOpacityFadeTimer);
	}
	Super::Deinitialize();
}

// ============================================================
// FIND PLYs
// ============================================================

void USplatCreatorSubsystem::SetImagePreviewTarget(UPrimitiveComponent* PlaneComponent, UMaterialInterface* Material)
{
	ImagePreviewPlaneComponent = PlaneComponent;
	ImagePreviewMaterial = Material; // nullptr = will load from ImagePreviewMaterialPath when needed
	ImagePreviewMID = nullptr; // Will be created when we first display an image
	if (debug && PlaneComponent) UE_LOG(LogTemp, Display, TEXT("[SplatCreator] Image preview target set - plane will display ComfyUI-bound image"));
}

void USplatCreatorSubsystem::SetSendCurrentSplatImageToComfyUI(bool bSendCurrent)
{
	bSendCurrentSplatImageToComfyUI = bSendCurrent;
}

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
		if (debug) UE_LOG(LogTemp, Verbose, TEXT("[SplatCreator] Image send disabled (bSendImageToComfyUIOnPlyChange=false)"));
		return;
	}
	if (ComfyUIWebSocketHost.IsEmpty())
	{
		if (debug) UE_LOG(LogTemp, Warning, TEXT("[SplatCreator] ComfyUIWebSocketHost is empty - cannot send image. Set it (e.g. 'localhost') in Blueprint or code."));
		return;
	}

	// Get base name without extension (e.g. "scene" from "scene.ply")
	FString BaseName = FPaths::GetBaseFilename(PLYPath);
	FString Dir = FPaths::GetPath(PLYPath);

	FString ImagePath = Dir / (BaseName + TEXT(".png"));
	if (!FPaths::FileExists(ImagePath))
	{
		if (debug) UE_LOG(LogTemp, Warning, TEXT("[SplatCreator] No matching .png image for PLY '%s' in %s - ensure image has same name as PLY"), *BaseName, *Dir);
		return;
	}

	TArray<uint8> ImageData;
	if (!FFileHelper::LoadFileToArray(ImageData, *ImagePath))
	{
		if (debug) UE_LOG(LogTemp, Warning, TEXT("[SplatCreator] Failed to load image: %s"), *ImagePath);
		return;
	}

	if (!ComfyImageSender)
	{
		ComfyImageSender = NewObject<UComfyImageSender>(this);
	}

	FString ServerURL = FString::Printf(TEXT("ws://%s:8001"), *ComfyUIWebSocketHost);
	if (debug) UE_LOG(LogTemp, Display, TEXT("[SplatCreator] Sending image to ComfyUI %s channel %d (%d bytes)"), *ServerURL, ComfyUIImageChannel, ImageData.Num());
	ComfyImageSender->ConfigureAndSend(ServerURL, ComfyUIImageChannel, ImageData);
}

void USplatCreatorSubsystem::UpdateImagePreview(const FString& PLYPath)
{
	UPrimitiveComponent* Plane = ImagePreviewPlaneComponent.Get();
	if (!Plane || !IsValid(Plane))
	{
		return;
	}

	UMaterialInterface* Mat = ImagePreviewMaterial.Get();
	if (!Mat && ImagePreviewMaterialPath.IsValid())
	{
		Mat = Cast<UMaterialInterface>(ImagePreviewMaterialPath.TryLoad());
		if (Mat) ImagePreviewMaterial = Mat;
	}
	if (!Mat || !IsValid(Mat))
	{
		return;
	}

	FString BaseName = FPaths::GetBaseFilename(PLYPath);
	FString Dir = FPaths::GetPath(PLYPath);
	FString ImagePath = Dir / (BaseName + TEXT(".png"));
	if (!FPaths::FileExists(ImagePath))
	{
		return;
	}

	TArray<uint8> ImageData;
	if (!FFileHelper::LoadFileToArray(ImageData, *ImagePath))
	{
		return;
	}

	if (!ImageDecoder)
	{
		ImageDecoder = NewObject<UComfyPngDecoder>(this);
	}
	if (!ImageDecoder)
	{
		return;
	}

	UTexture2D* DecodedTexture = ImageDecoder->DecodePNGToTexture(ImageData);
	if (!DecodedTexture || !IsValid(DecodedTexture))
	{
		return;
	}

	UTexture* FinalTexture = DecodedTexture;
	if (bAddTextToImagePreview)
	{
		UWorld* World = GetWorld();
		if (World)
		{
			int32 TexW = DecodedTexture->GetSurfaceWidth();
			int32 TexH = DecodedTexture->GetSurfaceHeight();
			if (TexW > 0 && TexH > 0)
			{
				if (!CanvasRenderTargetForText || CanvasRenderTargetForText->SizeX != TexW || CanvasRenderTargetForText->SizeY != TexH)
				{
					CanvasRenderTargetForText = UCanvasRenderTarget2D::CreateCanvasRenderTarget2D(World, UCanvasRenderTarget2D::StaticClass(), TexW, TexH);
					if (CanvasRenderTargetForText)
					{
						CanvasRenderTargetForText->ClearColor = FLinearColor(1.0f, 1.0f, 1.0f, 1.0f);  // Opaque white clear - avoids translucent result when material multiplies by texture alpha
						CanvasRenderTargetForText->OnCanvasRenderTargetUpdate.AddDynamic(this, &USplatCreatorSubsystem::OnCanvasRenderTargetUpdate);
					}
				}
				if (CanvasRenderTargetForText)
				{
					FString DisplayText = ImagePreviewTextFormat;
					DisplayText.ReplaceInline(TEXT("{0}"), *BaseName);
					DisplayText.ReplaceInline(TEXT("{1}"), *FString::FromInt(CurrentFileIndex + 1));
					DisplayText.ReplaceInline(TEXT("{2}"), *FString::FromInt(PlyFiles.Num()));
					TextOverlaySourceTexture = DecodedTexture;
					TextOverlayDisplayText = DisplayText;
					CanvasRenderTargetForText->UpdateResource();
					FinalTexture = CanvasRenderTargetForText;
					TextOverlaySourceTexture = nullptr;
				}
			}
		}
	}

	if (!ImagePreviewMID)
	{
		ImagePreviewMID = UMaterialInstanceDynamic::Create(Mat, this);
	}
	if (ImagePreviewMID)
	{
		ImagePreviewMID->SetTextureParameterValue(TEXT("Image"), FinalTexture);
		ImagePreviewMID->SetScalarParameterValue(TEXT("Opacity"), 1.0f);
		Plane->SetMaterial(0, ImagePreviewMID);

		if (bFadeImagePreviewOpacity)
		{
			UWorld* World = GetWorld();
			if (World)
			{
				World->GetTimerManager().ClearTimer(ImagePreviewOpacityFadeTimer);
				ImagePreviewOpacityFadeStartTime = World->GetTimeSeconds();
				World->GetTimerManager().SetTimer(ImagePreviewOpacityFadeTimer, this, &USplatCreatorSubsystem::UpdateImagePreviewOpacityFade, 1.0f / 30.0f, true);
			}
		}

		if (debug) UE_LOG(LogTemp, Display, TEXT("[SplatCreator] Updated preview plane with current image: %s"), *BaseName);
	}
}

void USplatCreatorSubsystem::UpdateImagePreviewOpacityFade()
{
	UWorld* World = GetWorld();
	if (!World || !ImagePreviewMID) return;

	float Elapsed = World->GetTimeSeconds() - ImagePreviewOpacityFadeStartTime;
	float OpacityValue = 1.0f;
	// Hold at full opacity for ImagePreviewOpacityHoldDuration, then fade over ImagePreviewOpacityFadeDuration
	if (Elapsed < ImagePreviewOpacityHoldDuration)
	{
		OpacityValue = 1.0f;
		ImagePreviewMID->SetScalarParameterValue(TEXT("Opacity"), OpacityValue);
	}
	else
	{
		float FadeElapsed = Elapsed - ImagePreviewOpacityHoldDuration;
		if (FadeElapsed >= ImagePreviewOpacityFadeDuration)
		{
			OpacityValue = 0.0f;
			ImagePreviewMID->SetScalarParameterValue(TEXT("Opacity"), OpacityValue);
			World->GetTimerManager().ClearTimer(ImagePreviewOpacityFadeTimer);
			return;
		}
		OpacityValue = 1.0f - (FadeElapsed / ImagePreviewOpacityFadeDuration);
		ImagePreviewMID->SetScalarParameterValue(TEXT("Opacity"), OpacityValue);
	}
	// Force render update so opacity changes apply immediately
	if (UPrimitiveComponent* Plane = ImagePreviewPlaneComponent.Get())
	{
		Plane->MarkRenderStateDirty();
	}
}

void USplatCreatorSubsystem::OnCanvasRenderTargetUpdate(UCanvas* Canvas, int32 Width, int32 Height)
{
	if (!Canvas) return;
	UTexture2D* SourceTex = TextOverlaySourceTexture;
	if (SourceTex && IsValid(SourceTex))
	{
		float TexW = SourceTex->GetSurfaceWidth();
		float TexH = SourceTex->GetSurfaceHeight();
		if (TexW > 0 && TexH > 0)
		{
			Canvas->DrawTile(SourceTex, 0, 0, Width, Height, 0, 0, TexW, TexH, BLEND_Opaque);
		}
	}
	if (!TextOverlayDisplayText.IsEmpty())
	{
		UFont* Font = GEngine ? GEngine->GetLargeFont() : nullptr;
		if (!Font && GEngine) Font = GEngine->GetMediumFont();
		if (Font)
		{
			const float X = ImagePreviewTextPosition.X;
			const float Y = ImagePreviewTextPosition.Y;
			const float S = ImagePreviewTextScale;
			FFontRenderInfo RenderInfo;
			Canvas->DrawColor = FColor::Black;
			Canvas->DrawText(Font, TextOverlayDisplayText, X + 2.0f, Y + 2.0f, S, S, RenderInfo);  // shadow
			{
				const float O = 1.5f;
				Canvas->DrawText(Font, TextOverlayDisplayText, X - O, Y, S, S, RenderInfo);
				Canvas->DrawText(Font, TextOverlayDisplayText, X + O, Y, S, S, RenderInfo);
				Canvas->DrawText(Font, TextOverlayDisplayText, X, Y - O, S, S, RenderInfo);
				Canvas->DrawText(Font, TextOverlayDisplayText, X, Y + O, S, S, RenderInfo);
			}  // outline
			Canvas->DrawColor = FColor::White;
			Canvas->DrawText(Font, TextOverlayDisplayText, X, Y, S, S, RenderInfo);
		}
	}
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
	// No sort - splats are chosen randomly, not alphabetically
	
	if(debug) UE_LOG(LogTemp, Display, TEXT("[SplatCreator] Found %d PLY files in %s"), PlyFiles.Num(), *AbsolutePath);
}

// ============================================================
// CYCLE SPLATS
// ============================================================

void USplatCreatorSubsystem::CycleToNextSplat()
{
	// Only cycle when Comfy-frame-triggered mode is enabled (called by ComfyStreamActor)
	if (bCycleSplatOnComfyFrame)
	{
		CycleToNextPLY();
	}
}

void USplatCreatorSubsystem::CycleToNextPLY()
{
	if (PlyFiles.Num() == 0)
	{
		ScanForPLYFiles(); // Re-scan in case files were added
		if (PlyFiles.Num() == 0) return;
	}
	
	// Use pre-chosen next index so the splat we show matches the image already sent to ComfyUI
	if (NextFileIndex >= 0 && NextFileIndex < PlyFiles.Num())
	{
		CurrentFileIndex = NextFileIndex;
	}
	else
	{
		// Fallback if no next was set (e.g. first cycle or re-scan) - pick a different splat
		if (PlyFiles.Num() <= 1)
		{
			CurrentFileIndex = 0;
		}
		else
		{
			int32 PrevIndex = CurrentFileIndex;
			do { CurrentFileIndex = FMath::RandRange(0, PlyFiles.Num() - 1); } while (CurrentFileIndex == PrevIndex);
		}
	}
	
	FString PLYPath = GetSplatCreatorFolder() / PlyFiles[CurrentFileIndex];
	
	if(debug) UE_LOG(LogTemp, Display, TEXT("[SplatCreator] Cycling to PLY: %s"), *PlyFiles[CurrentFileIndex]);
	LoadPLYFile(PLYPath);
}

// ============================================================
// READ PLY FILES
// ============================================================

void USplatCreatorSubsystem::LoadPLYFile(const FString& PLYPath)
{
	// Clear any pending morph delay (new cycle supersedes it)
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(MorphStartDelayTimer);
	}

	// Reset all transformations to normal when loading a new PLY file
	ResetToNormal();
	
	TArray<FVector> Positions;
	TArray<FColor> Colors;
	
	if (!ParsePLYFile(PLYPath, Positions, Colors))
	{
		if(debug) UE_LOG(LogTemp, Error, TEXT("[SplatCreator] Failed to parse PLY file: %s"), *PLYPath);
            return;
        }

	// Display current splat's image on preview plane (the one we're loading now)
	UpdateImagePreview(PLYPath);

	// Send image to ComfyUI: either current splat or next (based on bSendCurrentSplatImageToComfyUI)
	if (PlyFiles.Num() > 0)
	{
		// Pick next index for cycle logic (used when we cycle to next splat)
		if (PlyFiles.Num() <= 1)
		{
			NextFileIndex = 0;
		}
		else
		{
			do { NextFileIndex = FMath::RandRange(0, PlyFiles.Num() - 1); } while (NextFileIndex == CurrentFileIndex);
		}
		FString ImageToSendPath = bSendCurrentSplatImageToComfyUI ? PLYPath : (GetSplatCreatorFolder() / PlyFiles[NextFileIndex]);
		TrySendImageToComfyUI(ImageToSendPath);
	}

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

	// Create new point cloud with plane morph (GPU-based flat->3D transition)
	CreatePointCloud(Positions, Colors);
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
		if (PlaneMorphTimer.IsValid() && World)
		{
			World->GetTimerManager().ClearTimer(PlaneMorphTimer);
		}
		bIsPlaneMorphing = false;
		SplatMorphMID = nullptr;
		CurrentPointCloudActor->Destroy();
	}
	
	PointCloudComponent = nullptr;

	// Create new actor
	CurrentPointCloudActor = World->SpawnActor<AActor>();
	if (!CurrentPointCloudActor) return;
	
	// Load sphere mesh
	UStaticMesh* SphereMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (!SphereMesh) return;
	
	// Load plane morph material
	UMaterialInterface* Material = nullptr;
	bool bMorphMaterialLoaded = false;
	{
		UObject* Loaded = PlaneMorphMaterialPath.TryLoad();
		Material = Cast<UMaterialInterface>(Loaded);
		if (!Material) Material = LoadObject<UMaterialInterface>(nullptr, *PlaneMorphMaterialPath.ToString());
		bMorphMaterialLoaded = (Material != nullptr);
	}
	if (!Material) Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/_GENERATED/Materials/M_VertexColor.M_VertexColor"));
	if (!bMorphMaterialLoaded && debug)
		UE_LOG(LogTemp, Warning, TEXT("[SplatCreator] Plane morph material not found at %s - using direct 3D display."), *PlaneMorphMaterialPath.ToString());
	
	// Precompute scaled positions and sphere sizes (needed for both paths)
	TArray<FVector> ScaledPositions;
	ScaledPositions.Reserve(Positions.Num());
	for (const FVector& Pos : Positions) ScaledPositions.Add(Pos * 125.0f);
	CalculateAdaptiveSphereSizes(ScaledPositions, SphereSizes);
	const float FlatPlaneY = -PlaneMorphY;
	const FVector DownOffset = FVector(0.0f, 0.0f, 0.0f);
	const int32 NumInstances = FMath::Min(Positions.Num(), Colors.Num());
	
	// Create DynamicMaterial
	if (!Material)
	{
		if(debug) UE_LOG(LogTemp, Warning, TEXT("[SplatCreator] Material not found, trying fallback"));
		Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	}
	UMaterialInterface* MatToUse = nullptr;
	if (Material)
	{
		if (UMaterialInstanceDynamic* DynMat = UMaterialInstanceDynamic::Create(Material, CurrentPointCloudActor))
		{
			SplatMorphMID = DynMat;
			MatToUse = DynMat;
			if (bMorphMaterialLoaded) DynMat->SetScalarParameterValue(TEXT("MorphProgress"), 0.0f);  // 0 = flat at start
			DynMat->SetVectorParameterValue(TEXT("EmissiveColor"), FLinearColor(0.4f, 0.4f, 0.4f, 1.0f));
			DynMat->SetScalarParameterValue(TEXT("EmissiveIntensity"), 1.0f);
			DynMat->SetScalarParameterValue(TEXT("Emissive"), 1.0f);
			DynMat->SetScalarParameterValue(TEXT("BloomIntensity"), 1.0f);
			DynMat->SetScalarParameterValue(TEXT("GlowRadius"), 4.0f);
			DynMat->SetScalarParameterValue(TEXT("GlowIntensity"), 1.0f);
			DynMat->SetScalarParameterValue(TEXT("BloomScale"), 2.0f);
			DynMat->SetScalarParameterValue(TEXT("GlowScale"), 2.0f);
			DynMat->SetScalarParameterValue(TEXT("Contrast"), 1.5f);
			DynMat->SetScalarParameterValue(TEXT("Saturation"), 1.3f);
			DynMat->SetScalarParameterValue(TEXT("Brightness"), 1.1f);
			DynMat->SetScalarParameterValue(TEXT("ColorMultiplier"), 1.2f);
			DynMat->SetScalarParameterValue(TEXT("ContrastAmount"), 1.5f);
			DynMat->SetScalarParameterValue(TEXT("SaturationAmount"), 1.3f);
			DynMat->SetScalarParameterValue(TEXT("ColorIntensity"), 1.2f);
			DynMat->SetScalarParameterValue(TEXT("Intensity"), 1.2f);
			DynMat->SetScalarParameterValue(TEXT("Vibrance"), 1.3f);
		}
		else { SplatMorphMID = nullptr; MatToUse = Material; }
	}
	else { SplatMorphMID = nullptr; }
	
	// Create ISM component (plain ISM - bounds match geometry for visibility when camera is inside)
	// Create ISM component (plain ISM to avoid HISM's aggressive internal culling when camera is inside)
	PointCloudComponent = NewObject<UInstancedStaticMeshComponent>(CurrentPointCloudActor);
	PointCloudComponent->SetStaticMesh(SphereMesh);
	// Plane morph uses custom data slot 4 for Y offset (negated: FlatY - TargetY for 180° yaw / world-space WPO)
	PointCloudComponent->SetNumCustomDataFloats(5);  // slot 4 for plane morph Y offset
	PointCloudComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PointCloudComponent->SetCastShadow(false);
	PointCloudComponent->SetVisibility(true);
	PointCloudComponent->SetHiddenInGame(false);
	
	// Disable all distance culling - critical for visibility when camera is inside the cloud
	PointCloudComponent->SetCullDistances(0, 0);
	PointCloudComponent->InstanceMinDrawDistance = 0;  // Allow instances at any distance (including very close)
	PointCloudComponent->bNeverDistanceCull = true;   // Bypass primitive-level distance culling
	PointCloudComponent->bAllowCullDistanceVolume = false;  // Ignore Cull Distance Volumes in the level
	PointCloudComponent->SetCanEverAffectNavigation(false);
	PointCloudComponent->SetReceivesDecals(false);
	
	// Force the component to always render
	PointCloudComponent->SetVisibility(true);
	PointCloudComponent->SetHiddenInGame(false);
	
	// ISM settings
	PointCloudComponent->bDisableCollision = true;
	PointCloudComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	
	// Enable depth sorting and adjust rendering to reduce z-fighting
	PointCloudComponent->SetDepthPriorityGroup(SDPG_World);
	PointCloudComponent->SetRenderCustomDepth(false);
	// Disable depth testing for overlapping instances to reduce z-fighting
	// Note: This may cause some rendering order issues but eliminates z-fighting
	PointCloudComponent->bUseAsOccluder = false;
	PointCloudComponent->SetTranslucentSortPriority(1); // Higher priority for better sorting
	
	// Set material on single component (MatToUse created before branch)
	if (MatToUse) PointCloudComponent->SetMaterial(0, MatToUse);
	else if (Material) PointCloudComponent->SetMaterial(0, Material);
	if (Material && !Material->IsTwoSided() && bVisibleFromInside) PointCloudComponent->SetReverseCulling(true);
	
	// Set component as root BEFORE registering (critical for proper bounds calculation)
	CurrentPointCloudActor->SetRootComponent(PointCloudComponent);
	
	// Actor transform - location and rotation apply to the entire point cloud
	CurrentPointCloudActor->SetActorLocation(FVector(0.0f, -100.0f, -150.0f));
	CurrentPointCloudActor->SetActorRotation(FRotator(0.0f, 0.0f, 180.0f));

	// Register component (must be done after setting as root component)
	PointCloudComponent->RegisterComponent();
	
	// Force ISM to always render (disable all forms of culling)
	PointCloudComponent->SetCanEverAffectNavigation(false);
	
	if(debug) UE_LOG(LogTemp, Display, TEXT("[SplatCreator] Calculated %d sphere sizes for dense region detection"), SphereSizes.Num());
	
	// Add instances in batches
	const int32 BatchSize = 5000;
	
	if(debug) UE_LOG(LogTemp, Display, TEXT("[SplatCreator] Adding %d instances in batches of %d..."), NumInstances, BatchSize);
	
	for (int32 BatchStart = 0; BatchStart < NumInstances; BatchStart += BatchSize)
	{
		int32 BatchEnd = FMath::Min(BatchStart + BatchSize, NumInstances);
		TArray<FTransform> Transforms;
		Transforms.Reserve(BatchEnd - BatchStart);
		
		for (int32 i = BatchStart; i < BatchEnd; i++)
		{
			FVector ScaledPos = Positions[i] * 125.0f + DownOffset;
			FTransform Transform;
			// Always place at 3D - bounds must match geometry for visibility when camera is inside
			// Plane morph: material WPO uses (1-MorphProgress) to pull points to flat plane at start
			Transform.SetLocation(ScaledPos);
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
		if (CurrentPointPositions.Num() > 0)
		{
		FBox BoundingBox(ForceInit);
		for (const FVector& Pos : CurrentPointPositions)
		{
			BoundingBox += Pos;
		}
		// Include flat plane positions too - WPO moves vertices there during morph, so bounds must encompass both flat and 3D
		if (bMorphMaterialLoaded)
		{
			for (const FVector& Pos : ScaledPositions)
			{
				BoundingBox += FVector(Pos.X, FlatPlaneY, Pos.Z);
			}
		}
		
		// Expand bounds aggressively - must encompass both flat plane and full 3D extent for WPO morph.
		// Large expansion prevents clipping when camera/actor is close or inside the cloud.
		FVector Extent = BoundingBox.GetSize();
		BoundingBox = BoundingBox.ExpandBy(FMath::Max(200.0f, FMath::Max3(Extent.X, Extent.Y, Extent.Z) * 0.5f));
		
		// BoundsScale multiplies computed bounds - critical for WPO and when camera is inside/close.
		// Very large value prevents frustum culling when actor is moved closer to camera.
		PointCloudComponent->SetBoundsScale(10000.0f);
		
		PointCloudComponent->UpdateBounds();
		PointCloudComponent->MarkRenderStateDirty();
		
		// Store bounds for external access
		CurrentSplatBounds = BoundingBox;
		bHasSplatBounds = true;
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
	PointCloudComponent->SetCullDistances(0, 0);
	PointCloudComponent->MarkRenderStateDirty();
	
	// Set colors and (when plane morph) Z offset in custom data
	for (int32 i = 0; i < NumInstances && i < Colors.Num(); i++)
	{
		PointCloudComponent->SetCustomDataValue(i, 0, Colors[i].R / 255.0f);
		PointCloudComponent->SetCustomDataValue(i, 1, Colors[i].G / 255.0f);
		PointCloudComponent->SetCustomDataValue(i, 2, Colors[i].B / 255.0f);
		PointCloudComponent->SetCustomDataValue(i, 3, Colors[i].A / 255.0f);
		if (bMorphMaterialLoaded && i < ScaledPositions.Num())
		{
			// World-space Y delta: FlatPlaneY - TargetY (negated local offset for 180° yaw; WPO is world space)
			float YOffsetWorld = FlatPlaneY - ScaledPositions[i].Y;
			PointCloudComponent->SetCustomDataValue(i, 4, YOffsetWorld);
		}
	}
	
	PointCloudComponent->MarkRenderStateDirty();
	
	// Start plane morph (GPU-based: animates MorphProgress 0->1), optionally after delay
	if (bMorphMaterialLoaded && SplatMorphMID && World)
	{
		if (MorphStartDelaySeconds <= 0.0f)
		{
			bIsPlaneMorphing = true;
			PlaneMorphStartTime = World->GetTimeSeconds();
			World->GetTimerManager().SetTimer(PlaneMorphTimer, this, &USplatCreatorSubsystem::UpdatePlaneMorph, 0.016f, true);
			if(debug) UE_LOG(LogTemp, Display, TEXT("[SplatCreator] Started plane morph (%.2fs)"), PlaneMorphDuration);
		}
		else
		{
			World->GetTimerManager().SetTimer(MorphStartDelayTimer, this, &USplatCreatorSubsystem::StartDelayedMorph, MorphStartDelaySeconds, false);
			if(debug) UE_LOG(LogTemp, Display, TEXT("[SplatCreator] Plane morph delayed by %.2fs"), MorphStartDelaySeconds);
		}
	}
	
	if (debug) UE_LOG(LogTemp, Display, TEXT("[SplatCreator] Created point cloud with %d spheres"), NumInstances);
}


// ============================================================
// DELAYED MORPH START
// ============================================================

void USplatCreatorSubsystem::StartDelayedMorph()
{
	UWorld* World = GetWorld();
	if (!World) return;
	if (SplatMorphMID && PointCloudComponent)
	{
		bIsPlaneMorphing = true;
		PlaneMorphStartTime = World->GetTimeSeconds();
		World->GetTimerManager().SetTimer(PlaneMorphTimer, this, &USplatCreatorSubsystem::UpdatePlaneMorph, 0.016f, true);
		if(debug) UE_LOG(LogTemp, Display, TEXT("[SplatCreator] Started delayed plane morph (%.2fs)"), PlaneMorphDuration);
	}
}

// ============================================================
// PLANE-TO-3D MATERIAL MORPH (GPU-based)
// ============================================================

void USplatCreatorSubsystem::UpdatePlaneMorph()
{
	if (!bIsPlaneMorphing || !SplatMorphMID) return;
	
	UWorld* World = GetWorld();
	if (!World) return;
	
	float ElapsedTime = World->GetTimeSeconds() - PlaneMorphStartTime;
	float Progress = FMath::Clamp(ElapsedTime / PlaneMorphDuration, 0.0f, 1.0f);
	
	// Ease-in-out for smoother feel
	Progress = Progress < 0.5f
		? 4.0f * Progress * Progress * Progress
		: 1.0f - FMath::Pow(-2.0f * Progress + 2.0f, 3.0f) / 2.0f;
	
	// Pass Progress: 0 = flat at start, 1 = full 3D at end. Instances placed at 3D so bounds are correct.
	SplatMorphMID->SetScalarParameterValue(TEXT("MorphProgress"), Progress);
	
	if (Progress >= 1.0f)
	{
		CompletePlaneMorph();
	}
}

void USplatCreatorSubsystem::CompletePlaneMorph()
{
	UWorld* World = GetWorld();
	if (PlaneMorphTimer.IsValid() && World)
	{
		World->GetTimerManager().ClearTimer(PlaneMorphTimer);
	}
	
	bIsPlaneMorphing = false;
	
	if (SplatMorphMID)
	{
		SplatMorphMID->SetScalarParameterValue(TEXT("MorphProgress"), 1.0f);  // 1 = full 3D, no flattening
	}
	
	// Force bounds refresh - WPO morph can leave stale culling bounds; ensure full 3D extent is visible
	if (PointCloudComponent)
	{
		PointCloudComponent->UpdateBounds();
		PointCloudComponent->MarkRenderStateDirty();
	}
	
	if(debug) UE_LOG(LogTemp, Display, TEXT("[SplatCreator] Plane morph complete"));
}

// ============================================================
// SPLAT SCALING SYSTEM
// ============================================================

void USplatCreatorSubsystem::ScaleSplat(float NewScaleMultiplier)
{
	if (!PointCloudComponent || BasePointPositions.Num() == 0 || !bHasSplatBounds)
	{
		if (debug) UE_LOG(LogTemp, Warning, TEXT("[SplatCreator] Cannot scale splat - no point cloud loaded or center not calculated"));
		return;
	}
	
	SplatScaleMultiplier = FMath::Clamp(NewScaleMultiplier, 0.1f, 5.0f);
	UpdateSplatScale();
}

void USplatCreatorSubsystem::UpdateSplatScale()
{
	if (!PointCloudComponent || BasePointPositions.Num() == 0 || !bHasSplatBounds)
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
			if (i >= PointCloudComponent->GetInstanceCount()) continue;
			FVector BasePosition = BasePointPositions[i];
			FVector OffsetFromCenter = BasePosition - CurrentSplatBounds.GetCenter();
			FVector ScaledOffset = OffsetFromCenter * SplatScaleMultiplier;
			FVector NewPosition = CurrentSplatBounds.GetCenter() + ScaledOffset;
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
			FVector OffsetFromCenter = BasePos - CurrentSplatBounds.GetCenter();
			FVector ScaledOffset = OffsetFromCenter * SplatScaleMultiplier;
			CurrentPointPositions.Add(CurrentSplatBounds.GetCenter() + ScaledOffset);
		}
		
		PointCloudComponent->MarkRenderStateDirty();
	}
	// If bobbing is active, the next UpdateBobbing call will apply the new scale
	
	if(debug) UE_LOG(LogTemp, Display, TEXT("[SplatCreator] Splat scaled to %.2fx (center: %s)"), 
		SplatScaleMultiplier, *CurrentSplatBounds.GetCenter().ToString());
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
			if (i >= PointCloudComponent->GetInstanceCount()) continue;
			FTransform Transform;
			if (PointCloudComponent->GetInstanceTransform(i, Transform))
			{
				Transform.SetLocation(BasePointPositions[i]);
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
		if (debug) UE_LOG(LogTemp, Warning, TEXT("[SplatCreator] Cannot handle OSC message - no point cloud loaded"));
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
		
		if (bHasSplatBounds && SplatScaleMultiplier != 1.0f)
		{
			// Scale the bobbed position relative to center
			FVector OffsetFromCenter = BobbedBasePosition - CurrentSplatBounds.GetCenter();
			FVector ScaledOffset = OffsetFromCenter * SplatScaleMultiplier;
			FinalPosition = CurrentSplatBounds.GetCenter() + ScaledOffset;
			
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
		
		if (i >= PointCloudComponent->GetInstanceCount()) continue;
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
		int32 NumInstances = FMath::Min(PointCloudComponent->GetInstanceCount(), BasePointPositions.Num());
		InterpolationStartPositions.Empty();
		InterpolationStartPositions.Reserve(NumInstances);
		for (int32 i = 0; i < NumInstances; i++)
		{
			FTransform Transform;
			if (i < PointCloudComponent->GetInstanceCount() && PointCloudComponent->GetInstanceTransform(i, Transform))
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
		if (World)
		{
			World->GetTimerManager().SetTimer(
				BobbingTimer,
				this,
				&USplatCreatorSubsystem::UpdateInterpolationToBase,
				0.016f, // ~60fps
				true
			);
		}
	}
		else
		{
			BobbingSpeedMultiplier = 1.0f;
			if (PointCloudComponent && BasePointPositions.Num() > 0)
			{
				int32 NumInstances = FMath::Min(PointCloudComponent->GetInstanceCount(), BasePointPositions.Num());
				for (int32 i = 0; i < NumInstances; i++)
				{
					if (i >= PointCloudComponent->GetInstanceCount()) continue;
					FVector FinalPosition;
					float ScaledSphereSize;
					if (bHasSplatBounds && SplatScaleMultiplier != 1.0f)
					{
						FVector OffsetFromCenter = BasePointPositions[i] - CurrentSplatBounds.GetCenter();
						FinalPosition = CurrentSplatBounds.GetCenter() + OffsetFromCenter * SplatScaleMultiplier;
						ScaledSphereSize = (i < SphereSizes.Num() ? SphereSizes[i] : 0.06f) * SplatScaleMultiplier;
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
		
		if (bHasSplatBounds && SplatScaleMultiplier != 1.0f)
		{
			// Scale the random position relative to center
			FVector OffsetFromCenter = NewPosition - CurrentSplatBounds.GetCenter();
			FVector ScaledOffset = OffsetFromCenter * SplatScaleMultiplier;
			FinalPosition = CurrentSplatBounds.GetCenter() + ScaledOffset;
			
			float BaseSphereSize = (i < SphereSizes.Num()) ? SphereSizes[i] : 0.06f;
			ScaledSphereSize = BaseSphereSize * SplatScaleMultiplier;
		}
		else
		{
			FinalPosition = NewPosition;
			ScaledSphereSize = (i < SphereSizes.Num()) ? SphereSizes[i] : 0.06f;
		}
		
		if (i >= PointCloudComponent->GetInstanceCount()) continue;
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
		int32 NumInstances = FMath::Min(PointCloudComponent->GetInstanceCount(), BasePointPositions.Num());
		InterpolationStartPositions.Empty();
		InterpolationStartPositions.Reserve(NumInstances);
		for (int32 i = 0; i < NumInstances; i++)
		{
			FTransform Transform;
			if (i < PointCloudComponent->GetInstanceCount() && PointCloudComponent->GetInstanceTransform(i, Transform))
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
		if (World)
		{
			World->GetTimerManager().SetTimer(
				RandomMovementTimer,
				this,
				&USplatCreatorSubsystem::UpdateInterpolationToBase,
				0.016f, // ~60fps
				true
			);
		}
		
	}
	else
	{
		RandomMovementSpeedMultiplier = 1.0f;
		if (PointCloudComponent && BasePointPositions.Num() > 0)
		{
			int32 NumInstances = FMath::Min(PointCloudComponent->GetInstanceCount(), BasePointPositions.Num());
			for (int32 i = 0; i < NumInstances; i++)
			{
				if (i >= PointCloudComponent->GetInstanceCount()) continue;
				FVector FinalPosition;
				float ScaledSphereSize;
				if (bHasSplatBounds && SplatScaleMultiplier != 1.0f)
				{
					FVector OffsetFromCenter = BasePointPositions[i] - CurrentSplatBounds.GetCenter();
					FinalPosition = CurrentSplatBounds.GetCenter() + OffsetFromCenter * SplatScaleMultiplier;
					ScaledSphereSize = (i < SphereSizes.Num() ? SphereSizes[i] : 0.06f) * SplatScaleMultiplier;
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
			if (i >= InterpolationStartPositions.Num() || i >= PointCloudComponent->GetInstanceCount()) continue;
			FVector TargetPosition;
			float ScaledSphereSize;
			if (bHasSplatBounds && SplatScaleMultiplier != 1.0f)
			{
				FVector OffsetFromCenter = BasePointPositions[i] - CurrentSplatBounds.GetCenter();
				TargetPosition = CurrentSplatBounds.GetCenter() + OffsetFromCenter * SplatScaleMultiplier;
				ScaledSphereSize = (i < SphereSizes.Num() ? SphereSizes[i] : 0.06f) * SplatScaleMultiplier;
			}
			else
			{
				TargetPosition = BasePointPositions[i];
				ScaledSphereSize = (i < SphereSizes.Num()) ? SphereSizes[i] : 0.06f;
			}
			FVector InterpolatedPosition = FMath::Lerp(InterpolationStartPositions[i], TargetPosition, EasedAlpha);
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
