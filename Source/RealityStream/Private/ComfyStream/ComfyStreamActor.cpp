#include "ComfyStream/ComfyStreamActor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "TimerManager.h"
#include "Engine/Texture2D.h"
#include "Math/UnrealMathUtility.h"

static bool debug = false;
//Actor that receives 3 texture maps from ComfyUI and applies to to a material 

AComfyStreamActor::AComfyStreamActor()
{
	PrimaryActorTick.bCanEverTick = true;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	DisplayMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DisplayMesh"));
	DisplayMesh->SetupAttachment(RootComponent);

	ComfyStreamComponent = CreateDefaultSubobject<UComfyStreamComponent>(TEXT("ComfyStreamComponent"));

	SegmentationChannelConfig.ChannelNumber = 1;
	SegmentationChannelConfig.ChannelType   = EComfyChannel::Segmentation;
}

void AComfyStreamActor::BeginPlay()
{
	Super::BeginPlay();

	//Create external helpers for pairing and placement 
	FrameBuffer   = NewObject<UComfyFrameBuffer>(this);

	//Create material
	if (BaseMaterial)
	{
		DynMat = UMaterialInstanceDynamic::Create(BaseMaterial, this);
		if (DynMat)
			DisplayMesh->SetMaterial(0, DynMat);
	}

	//Bind buffer completion
	if (FrameBuffer)
	{
		FrameBuffer->OnFullFrameReady.AddDynamic(this, &AComfyStreamActor::HandleFullFrame);
	}

	//Bind component events 
	if (ComfyStreamComponent)
	{
		ComfyStreamComponent->StreamConfig = SegmentationChannelConfig;

		ComfyStreamComponent->OnTextureReceived.AddDynamic(this, &AComfyStreamActor::HandleStreamTexture);
		ComfyStreamComponent->OnConnectionStatusChanged.AddDynamic(this, &AComfyStreamActor::HandleConnectionChanged);
		ComfyStreamComponent->OnError.AddDynamic(this, &AComfyStreamActor::HandleStreamError);

		if (SegmentationChannelConfig.bAutoReconnect)
		{
			ConnectSegmentationChannel();
		}
	}

	//Hide display mesh - actors will be spawned instead
	if (DisplayMesh)
	{
		DisplayMesh->SetVisibility(false);
	}
}

void AComfyStreamActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Process interpolation queue
	if (bEnableInterpolation && InterpolationQueue.Num() > 0)
	{
		InterpolationTimer += DeltaTime;
		
		// Apply interpolated frames based on timing
		for (int32 i = InterpolationQueue.Num() - 1; i >= 0; --i)
		{
			FInterpolatedFrame& InterpFrame = InterpolationQueue[i];
			InterpFrame.TimeRemaining -= DeltaTime;
			
			if (InterpFrame.TimeRemaining <= 0.0f)
			{
				// Time to apply this interpolated frame
				ApplyInterpolatedFrame(InterpFrame.Frame);
				InterpolationQueue.RemoveAt(i);
			}
		}
		
		// If queue is empty, interpolation is complete
		// The final frame should have already been applied via the queue
	}
	else
	{
		// No interpolation - apply latest frame directly if available
		if (LatestFrame.IsComplete())
		{
			ApplyTexturesToMaterial(LatestFrame);
			FVector FixedPosition = GetActorLocation();
			SpawnTextureActor(LatestFrame, FixedPosition);
		}
	}

	//Update lerp and opacity fade for all actors
	for (int32 i = ActorData.Num() - 1; i >= 0; i--)
	{
		FActorLerpData& Data = ActorData[i];
		
		// Skip if actor is invalid
		if (!IsValid(Data.Actor))
		{
			GetWorld()->GetTimerManager().ClearTimer(Data.DestroyTimer);
			GetWorld()->GetTimerManager().ClearTimer(Data.LerpTimer);
			ActorData.RemoveAt(i);
			continue;
		}
		
		//Update texture lerp
		if (Data.bIsLerping && IsValid(Data.Material))
		{
			UpdateActorLerp(Data, LatestFrame, DeltaTime);
		}

		// Opacity fade-out disabled - actors stay up permanently until replaced by new frame
		// (Removed fade-out logic so images persist)
	}
}

void AComfyStreamActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	for (AActor* Actor : SpawnedTextureActors)
	{
		Actor->Destroy();
	}
	SpawnedTextureActors.Empty();
	
	DisconnectAll();
	Super::EndPlay(EndPlayReason);
}

void AComfyStreamActor::ConnectSegmentationChannel()
{
	if (ComfyStreamComponent)
		ComfyStreamComponent->Connect();
}

void AComfyStreamActor::DisconnectAll()
{
	if (ComfyStreamComponent)
		ComfyStreamComponent->Disconnect();
}

void AComfyStreamActor::HandleStreamTexture(UTexture2D* Texture)
{
	if (!Texture || !FrameBuffer)
		return;

	// ComfyImageFetcher broadcasts textures in order: RGB (0), Depth (1), Mask (2)
	// Assign to first available slot based on what we've received
	// This handles out-of-order arrival gracefully
	
	int32 Index = INDEX_NONE;
	FString ChannelName;
	
	if (!bHasRGB)
	{
		// First texture received - always RGB
		Index = 0;
		ChannelName = TEXT("RGB");
		bHasRGB = true;
	}
	else if (!bHasDepth)
	{
		// Second texture - assign to Depth slot
		Index = 1;
		ChannelName = TEXT("Depth");
		bHasDepth = true;
	}
	else if (!bHasMask)
	{
		// Third texture - assign to Mask slot
		Index = 2;
		ChannelName = TEXT("Mask");
		bHasMask = true;
	}
	else
	{
		// All slots filled - shouldn't happen, skip
		if(debug) UE_LOG(LogTemp, Warning, TEXT("[ComfyStreamActor] Received texture but all slots already filled (SeqIndex=%d)"), SeqIndex);
		return;
	}
	
	int32 CurrentSeqIndex = SeqIndex; // Log before incrementing
	FrameBuffer->PushTexture(Texture, Index);
	SeqIndex++;
	
	if(debug) UE_LOG(LogTemp, Display, TEXT("[ComfyStreamActor] Received %s texture (SeqIndex=%d, FrameBuffer index=%d, HasRGB=%d, HasDepth=%d, HasMask=%d)"), 
		*ChannelName, CurrentSeqIndex, Index, bHasRGB ? 1 : 0, bHasDepth ? 1 : 0, bHasMask ? 1 : 0);

	//Notify blueprint
	OnTextureReceived(Texture);
}



void AComfyStreamActor::HandleConnectionChanged(bool bConnected)
{
	OnConnectionStatusChanged(bConnected);
}

void AComfyStreamActor::HandleStreamError(const FString& Error)
{
	OnError(Error);
}


void AComfyStreamActor::ApplyTexturesToMaterial(const FComfyFrame& Frame)
{
	static const FName RGBParam  = TEXT("RGB_Map");
	static const FName DepthParam= TEXT("Depth_Map_Object");
	static const FName MaskParam = TEXT("Mask_Map");

	if (!DynMat)
	{
		return;
	}

	// Set required textures (RGB and Mask) - both must be valid
	if (!IsValid(Frame.RGB) || !IsValid(Frame.Mask))
	{
		if(debug) UE_LOG(LogTemp, Warning, TEXT("[ComfyStreamActor] ApplyTexturesToMaterial called with invalid frame - RGB=%s, Mask=%s"), 
			IsValid(Frame.RGB) ? TEXT("valid") : TEXT("NULL"),
			IsValid(Frame.Mask) ? TEXT("valid") : TEXT("NULL"));
		return;
	}
	
	DynMat->SetTextureParameterValue(RGBParam, Frame.RGB);
	DynMat->SetTextureParameterValue(MaskParam, Frame.Mask);
	
	// Set Depth if available (optional)
	if (IsValid(Frame.Depth))
	{
		DynMat->SetTextureParameterValue(DepthParam, Frame.Depth);
	}
	else
	{
		DynMat->SetTextureParameterValue(DepthParam, nullptr);
	}
}

void AComfyStreamActor::SpawnTextureActor(const FComfyFrame& Frame, const FVector& WorldPosition)
{
	AActor* Actor = FindOrSpawnActorAtLocation(WorldPosition);
	if (!Actor) return;

	//Find or create actor data entry
	FActorLerpData* ActorDataPtr = nullptr;
	for (FActorLerpData& Data : ActorData)
	{
		if (Data.Actor == Actor)
		{
			ActorDataPtr = &Data;
			break;
		}
	}

	bool bIsNewActor = false;
	if (!ActorDataPtr)
	{
		FActorLerpData NewData;
		NewData.Actor = Actor;
		NewData.Position = WorldPosition;
		ActorData.Add(NewData);
		ActorDataPtr = &ActorData[ActorData.Num() - 1];
		bIsNewActor = true;
	}
	
	// Scale actor to match texture size (only for new actors)
	if (bIsNewActor)
	{
		ScaleActorToTextureSize(Actor, Frame);
	}

	//Update textures with lerp if material exists
	// Always update textures - the frame comparison in HandleFullFrame ensures we only get new frames
	if (ActorDataPtr->Material)
	{
		// Check if textures are different from what's currently set (for logging)
		UTexture* CurrentRGB = nullptr;
		UTexture* CurrentDepth = nullptr;
		UTexture* CurrentMask = nullptr;
		bool bMaterialHasRGB = ActorDataPtr->Material->GetTextureParameterValue(TEXT("RGB_Map"), CurrentRGB);
		bool bMaterialHasDepth = ActorDataPtr->Material->GetTextureParameterValue(TEXT("Depth_Map_Object"), CurrentDepth);
		bool bMaterialHasMask = ActorDataPtr->Material->GetTextureParameterValue(TEXT("Mask_Map"), CurrentMask);
		
		// Check if textures changed (RGB and Mask are required, Depth is optional)
		bool bTexturesChanged = !bMaterialHasRGB || !bMaterialHasMask || 
		                        (CurrentRGB != Frame.RGB || CurrentMask != Frame.Mask);
		// Also check if Depth changed (if it exists)
		if (IsValid(Frame.Depth) && (!bMaterialHasDepth || CurrentDepth != Frame.Depth))
		{
			bTexturesChanged = true;
		}
		
		if (bTexturesChanged)
		{
			// Ensure required textures (RGB and Mask) are valid before setting
			if (IsValid(Frame.RGB) && IsValid(Frame.Mask))
			{
				//Set required textures
				ActorDataPtr->Material->SetTextureParameterValue(TEXT("RGB_Map"), Frame.RGB);
				ActorDataPtr->Material->SetTextureParameterValue(TEXT("Mask_Map"), Frame.Mask);
				
				//Set Depth if available (optional)
				if (IsValid(Frame.Depth))
				{
					ActorDataPtr->Material->SetTextureParameterValue(TEXT("Depth_Map_Object"), Frame.Depth);
				}
				else
				{
					// Clear depth texture if not present
					ActorDataPtr->Material->SetTextureParameterValue(TEXT("Depth_Map_Object"), nullptr);
				}

				//If material supports lerp alpha parameter, enable lerping
				// Only use material lerp if interpolation is disabled (interpolation handles blending itself)
				if (!bEnableInterpolation)
				{
					float LerpParam = 0.0f;
					if (ActorDataPtr->Material->GetScalarParameterValue(TEXT("LerpAlpha"), LerpParam))
					{
						ActorDataPtr->LerpAlpha = 0.0f;
						ActorDataPtr->bIsLerping = true;
						//Set new textures as target for lerp
						ActorDataPtr->Material->SetTextureParameterValue(TEXT("RGB_Map_New"), Frame.RGB);
						ActorDataPtr->Material->SetTextureParameterValue(TEXT("Mask_Map_New"), Frame.Mask);
						if (IsValid(Frame.Depth))
						{
							ActorDataPtr->Material->SetTextureParameterValue(TEXT("Depth_Map_New"), Frame.Depth);
						}
						else
						{
							ActorDataPtr->Material->SetTextureParameterValue(TEXT("Depth_Map_New"), nullptr);
						}
					}
				}
				else
				{
					// When interpolation is enabled, disable material lerp to avoid conflicts
					ActorDataPtr->bIsLerping = false;
					ActorDataPtr->LerpAlpha = 1.0f;
				}
				
				if(debug) UE_LOG(LogTemp, Display, TEXT("[ComfyStreamActor] Updated textures on actor: RGB=%s, Depth=%s, Mask=%s"), 
					IsValid(Frame.RGB) ? *Frame.RGB->GetName() : TEXT("NULL"),
					IsValid(Frame.Depth) ? *Frame.Depth->GetName() : TEXT("NULL"),
					IsValid(Frame.Mask) ? *Frame.Mask->GetName() : TEXT("NULL"));
			}
			else
			{
				if(debug) UE_LOG(LogTemp, Warning, TEXT("[ComfyStreamActor] Frame missing required textures - RGB=%s, Mask=%s"), 
					IsValid(Frame.RGB) ? TEXT("valid") : TEXT("NULL"),
					IsValid(Frame.Mask) ? TEXT("valid") : TEXT("NULL"));
			}
		}
		else
		{
			// Textures haven't changed - don't update actor
			if(debug) UE_LOG(LogTemp, Verbose, TEXT("[ComfyStreamActor] Textures unchanged, skipping actor update"));
		}
	}
	else
	{
		//First time - create material and apply textures directly
		UStaticMeshComponent* MeshComp = Actor->FindComponentByClass<UStaticMeshComponent>();
		if (MeshComp && BaseMaterial)
		{
			ActorDataPtr->Material = UMaterialInstanceDynamic::Create(BaseMaterial, Actor);
			if (ActorDataPtr->Material)
			{
				// Ensure required textures (RGB and Mask) are valid before setting
				if (IsValid(Frame.RGB) && IsValid(Frame.Mask))
				{
					ActorDataPtr->Material->SetTextureParameterValue(TEXT("RGB_Map"), Frame.RGB);
					ActorDataPtr->Material->SetTextureParameterValue(TEXT("Mask_Map"), Frame.Mask);
					
					// Set Depth if available (optional)
					if (IsValid(Frame.Depth))
					{
						ActorDataPtr->Material->SetTextureParameterValue(TEXT("Depth_Map_Object"), Frame.Depth);
					}
					else
					{
						ActorDataPtr->Material->SetTextureParameterValue(TEXT("Depth_Map_Object"), nullptr);
					}
					
					ActorDataPtr->Material->SetScalarParameterValue(TEXT("Opacity"), 1.0f);
					MeshComp->SetMaterial(0, ActorDataPtr->Material);
					ActorDataPtr->LerpAlpha = 1.0f;
					ActorDataPtr->OpacityAlpha = 1.0f;
					
					if(debug) UE_LOG(LogTemp, Display, TEXT("[ComfyStreamActor] Created material and set textures on new actor: RGB=%s, Depth=%s, Mask=%s"), 
						IsValid(Frame.RGB) ? *Frame.RGB->GetName() : TEXT("NULL"),
						IsValid(Frame.Depth) ? *Frame.Depth->GetName() : TEXT("NULL"),
						IsValid(Frame.Mask) ? *Frame.Mask->GetName() : TEXT("NULL"));
				}
				else
				{
					UE_LOG(LogTemp, Error, TEXT("[ComfyStreamActor] Failed to set textures on new actor - Frame missing required textures: RGB=%s, Mask=%s"), 
						IsValid(Frame.RGB) ? TEXT("valid") : TEXT("NULL"),
						IsValid(Frame.Mask) ? TEXT("valid") : TEXT("NULL"));
				}
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("[ComfyStreamActor] Failed to create material instance dynamic"));
			}
		}
		else
		{
			if(debug) UE_LOG(LogTemp, Warning, TEXT("[ComfyStreamActor] Cannot create material - MeshComp=%s, BaseMaterial=%s"), 
				MeshComp ? TEXT("valid") : TEXT("NULL"),
				BaseMaterial ? TEXT("valid") : TEXT("NULL"));
		}
	}

	//Reset opacity to ensure actor is visible (no auto-destruction - actors stay up permanently)
	ActorDataPtr->OpacityAlpha = 1.0f;
	ActorDataPtr->bIsFadingOut = false;
	if (ActorDataPtr->Material)
	{
		ActorDataPtr->Material->SetScalarParameterValue(TEXT("Opacity"), 1.0f);
	}
	// Clear any existing destroy timer (actors should not auto-destroy)
	GetWorld()->GetTimerManager().ClearTimer(ActorDataPtr->DestroyTimer);
}

AActor* AComfyStreamActor::FindOrSpawnActorAtLocation(const FVector& WorldPosition)
{
	UWorld* World = GetWorld();

	//Check for existing actor at same location
	for (FActorLerpData& Data : ActorData)
	{
		if (Data.Actor && IsValid(Data.Actor))
		{
			// Check distance from stored position
			float Distance = FVector::Dist(Data.Position, WorldPosition);
			if (Distance < LocationThreshold)
			{
				// Update position smoothly to prevent drift
				// Only update if there's a significant change to avoid micro-movements
				if (Distance > 1.0f)
				{
					// Smoothly lerp to new position
					FVector CurrentActorPos = Data.Actor->GetActorLocation();
					FVector LerpedPosition = FMath::Lerp(CurrentActorPos, WorldPosition, 0.1f);
					Data.Actor->SetActorLocation(LerpedPosition);
					Data.Position = LerpedPosition;
				}
				return Data.Actor;
			}
		}
	}

	//No existing actor - spawn new one
	FRotator DisplayRotation = DisplayMesh ? DisplayMesh->GetComponentRotation() : FRotator(90, 0, -90);
	AActor* SpawnedActor = World->SpawnActor<AActor>(AActor::StaticClass(), WorldPosition, DisplayRotation);

	UStaticMeshComponent* MeshComp = NewObject<UStaticMeshComponent>(SpawnedActor);
	MeshComp->SetStaticMesh(DisplayMesh->GetStaticMesh());
	
	MeshComp->RegisterComponent();
	SpawnedActor->SetRootComponent(MeshComp);
	SpawnedActor->SetActorLocation(WorldPosition);
	SpawnedActor->SetActorRotation(DisplayRotation);
	MeshComp->SetVisibility(true);

	SpawnedTextureActors.Add(SpawnedActor);
	return SpawnedActor;
}

void AComfyStreamActor::ScaleActorToTextureSize(AActor* Actor, const FComfyFrame& Frame)
{
	if (!Actor || !DisplayMesh) return;
	
	UStaticMeshComponent* MeshComp = Actor->FindComponentByClass<UStaticMeshComponent>();
	if (!MeshComp) return;
	
	// Get the DisplayMesh's scale
	FVector DisplayMeshScale = DisplayMesh->GetComponentScale();
	
	// Apply the same scale to the spawned actor's mesh component
	MeshComp->SetWorldScale3D(DisplayMeshScale);
	
	if(debug) UE_LOG(LogTemp, Verbose, TEXT("[ComfyStreamActor] Scaled actor to match DisplayMesh scale: (%.2f, %.2f, %.2f)"), 
		DisplayMeshScale.X, DisplayMeshScale.Y, DisplayMeshScale.Z);
}

void AComfyStreamActor::UpdateActorLerp(FActorLerpData& Data, const FComfyFrame& Frame, float DeltaTime)
{
	if (!Data.bIsLerping || !IsValid(Data.Material)) return;
	UMaterialInstanceDynamic* material = Cast<UMaterialInstanceDynamic>(Data.Material);
	if (!material) return;
	
	Data.LerpAlpha = FMath::Clamp(Data.LerpAlpha + (DeltaTime * LerpSpeed), 0.0f, 1.0f);
	material->SetScalarParameterValue(TEXT("LerpAlpha"), Data.LerpAlpha);

		if (Data.LerpAlpha >= 1.0f)
		{
			//Lerp complete - swap textures
			UTexture* NewRGB = nullptr;
			UTexture* NewDepth = nullptr;
			UTexture* NewMask = nullptr;
			material->GetTextureParameterValue(TEXT("RGB_Map_New"), NewRGB);
			material->GetTextureParameterValue(TEXT("Depth_Map_New"), NewDepth);
			material->GetTextureParameterValue(TEXT("Mask_Map_New"), NewMask);

			// Swap required textures (RGB and Mask)
			if (IsValid(NewRGB)) material->SetTextureParameterValue(TEXT("RGB_Map"), NewRGB);
			if (IsValid(NewMask)) material->SetTextureParameterValue(TEXT("Mask_Map"), NewMask);
			
			// Swap Depth if available (optional)
			if (IsValid(NewDepth))
			{
				material->SetTextureParameterValue(TEXT("Depth_Map_Object"), NewDepth);
			}
			else
			{
				material->SetTextureParameterValue(TEXT("Depth_Map_Object"), nullptr);
			}

			Data.bIsLerping = false;
		}
}

void AComfyStreamActor::DestroyActorDelayed(AActor* Actor)
{
	// Actors now persist permanently - no auto-destruction
	// This function is kept for compatibility but does nothing
	// Actors will only be replaced when a new frame arrives
}

void AComfyStreamActor::GenerateInterpolatedFrames(const FComfyFrame& FromFrame, const FComfyFrame& ToFrame)
{
	if (!bEnableInterpolation || NumInterpolatedFrames <= 0)
	{
		return;
	}

	// Clear existing interpolation queue
	InterpolationQueue.Empty();

	// Generate interpolated frames with smooth easing
	for (int32 i = 1; i <= NumInterpolatedFrames; ++i)
	{
		// Calculate linear alpha (0.0 to 1.0)
		float LinearAlpha = float(i) / float(NumInterpolatedFrames + 1);
		
		// Apply smooth easing function (ease-in-out cubic) for more natural transitions
		float Alpha = LinearAlpha;
		if (bUseSmoothEasing)
		{
			// Smooth step (ease-in-out cubic): 3t^2 - 2t^3
			Alpha = LinearAlpha * LinearAlpha * (3.0f - 2.0f * LinearAlpha);
			// Alternative: ease-in-out cubic (even smoother)
			// Alpha = LinearAlpha < 0.5f ? 4.0f * LinearAlpha * LinearAlpha * LinearAlpha : 1.0f - FMath::Pow(-2.0f * LinearAlpha + 2.0f, 3.0f) / 2.0f;
		}
		
		FComfyFrame InterpolatedFrame;
		
		// Blend RGB textures
		if (IsValid(FromFrame.RGB) && IsValid(ToFrame.RGB))
		{
			InterpolatedFrame.RGB = BlendTextures(FromFrame.RGB, ToFrame.RGB, Alpha);
		}
		else if (IsValid(ToFrame.RGB))
		{
			InterpolatedFrame.RGB = ToFrame.RGB;
		}
		else if (IsValid(FromFrame.RGB))
		{
			InterpolatedFrame.RGB = FromFrame.RGB;
		}

		// Blend Mask textures
		if (IsValid(FromFrame.Mask) && IsValid(ToFrame.Mask))
		{
			InterpolatedFrame.Mask = BlendTextures(FromFrame.Mask, ToFrame.Mask, Alpha);
		}
		else if (IsValid(ToFrame.Mask))
		{
			InterpolatedFrame.Mask = ToFrame.Mask;
		}
		else if (IsValid(FromFrame.Mask))
		{
			InterpolatedFrame.Mask = FromFrame.Mask;
		}

		// Blend Depth textures (optional)
		if (IsValid(FromFrame.Depth) && IsValid(ToFrame.Depth))
		{
			InterpolatedFrame.Depth = BlendTextures(FromFrame.Depth, ToFrame.Depth, Alpha);
		}
		else if (IsValid(ToFrame.Depth))
		{
			InterpolatedFrame.Depth = ToFrame.Depth;
		}
		else if (IsValid(FromFrame.Depth))
		{
			InterpolatedFrame.Depth = FromFrame.Depth;
		}

		// Only add if frame is complete
		if (InterpolatedFrame.IsComplete())
		{
			FInterpolatedFrame QueueEntry;
			QueueEntry.Frame = InterpolatedFrame;
			QueueEntry.TimeRemaining = InterpolationDuration / float(NumInterpolatedFrames + 1); // +1 to include final frame timing
			InterpolationQueue.Add(QueueEntry);
		}
	}
	
	// Add the final frame (ToFrame) at the end of interpolation
	if (ToFrame.IsComplete())
	{
		FInterpolatedFrame FinalFrame;
		FinalFrame.Frame = ToFrame;
		FinalFrame.TimeRemaining = InterpolationDuration / float(NumInterpolatedFrames + 1);
		InterpolationQueue.Add(FinalFrame);
	}

	if(debug) UE_LOG(LogTemp, Display, TEXT("[ComfyStreamActor] Generated %d interpolated frames"), InterpolationQueue.Num());
}

UTexture2D* AComfyStreamActor::BlendTextures(UTexture2D* TextureA, UTexture2D* TextureB, float Alpha)
{
	if (!IsValid(TextureA) || !IsValid(TextureB))
	{
		return Alpha >= 0.5f ? TextureB : TextureA;
	}

	// Clamp alpha
	Alpha = FMath::Clamp(Alpha, 0.0f, 1.0f);

	// Get texture dimensions - use the smaller dimensions
	int32 WidthA = TextureA->GetSizeX();
	int32 HeightA = TextureA->GetSizeY();
	int32 WidthB = TextureB->GetSizeX();
	int32 HeightB = TextureB->GetSizeY();

	int32 Width = FMath::Min(WidthA, WidthB);
	int32 Height = FMath::Min(HeightA, HeightB);

	if (Width <= 0 || Height <= 0)
	{
		return Alpha >= 0.5f ? TextureB : TextureA;
	}

	// Read pixel data from both textures
	TArray<FColor> PixelsA;
	TArray<FColor> PixelsB;
	
	bool bReadA = false;
	bool bReadB = false;

	// Try to read TextureA
	if (TextureA->GetPlatformData() && TextureA->GetPlatformData()->Mips.Num() > 0)
	{
		FTexture2DMipMap& MipA = TextureA->GetPlatformData()->Mips[0];
		if (MipA.BulkData.GetBulkDataSize() > 0)
		{
			const FColor* PixelsA_Raw = static_cast<const FColor*>(MipA.BulkData.LockReadOnly());
			if (PixelsA_Raw)
			{
				PixelsA.SetNumUninitialized(WidthA * HeightA);
				FMemory::Memcpy(PixelsA.GetData(), PixelsA_Raw, WidthA * HeightA * sizeof(FColor));
				MipA.BulkData.Unlock();
				bReadA = true;
			}
		}
	}

	// Try to read TextureB
	if (TextureB->GetPlatformData() && TextureB->GetPlatformData()->Mips.Num() > 0)
	{
		FTexture2DMipMap& MipB = TextureB->GetPlatformData()->Mips[0];
		if (MipB.BulkData.GetBulkDataSize() > 0)
		{
			const FColor* PixelsB_Raw = static_cast<const FColor*>(MipB.BulkData.LockReadOnly());
			if (PixelsB_Raw)
			{
				PixelsB.SetNumUninitialized(WidthB * HeightB);
				FMemory::Memcpy(PixelsB.GetData(), PixelsB_Raw, WidthB * HeightB * sizeof(FColor));
				MipB.BulkData.Unlock();
				bReadB = true;
			}
		}
	}

	if (!bReadA || !bReadB)
	{
		return Alpha >= 0.5f ? TextureB : TextureA;
	}

	// Create blended texture
	UTexture2D* BlendedTexture = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8);
	if (!BlendedTexture)
	{
		return Alpha >= 0.5f ? TextureB : TextureA;
	}

	BlendedTexture->SRGB = TextureA->SRGB;
	BlendedTexture->CompressionSettings = TextureA->CompressionSettings;
	BlendedTexture->Filter = TF_Bilinear;

	// Blend pixels
	TArray<FColor> BlendedPixels;
	BlendedPixels.SetNumUninitialized(Width * Height);

	for (int32 Y = 0; Y < Height; ++Y)
	{
		for (int32 X = 0; X < Width; ++X)
		{
			int32 IdxA = Y * WidthA + X;
			int32 IdxB = Y * WidthB + X;
			int32 IdxBlended = Y * Width + X;

			if (IdxA < PixelsA.Num() && IdxB < PixelsB.Num())
			{
				FColor PixelA = PixelsA[IdxA];
				FColor PixelB = PixelsB[IdxB];

				// Linear interpolation
				FColor Blended;
				Blended.R = FMath::Lerp(PixelA.R, PixelB.R, Alpha);
				Blended.G = FMath::Lerp(PixelA.G, PixelB.G, Alpha);
				Blended.B = FMath::Lerp(PixelA.B, PixelB.B, Alpha);
				Blended.A = FMath::Lerp(PixelA.A, PixelB.A, Alpha);

				BlendedPixels[IdxBlended] = Blended;
			}
		}
	}

	// Write blended pixels to texture
	if (BlendedTexture->GetPlatformData() && BlendedTexture->GetPlatformData()->Mips.Num() > 0)
	{
		FTexture2DMipMap& Mip = BlendedTexture->GetPlatformData()->Mips[0];
		void* TextureData = Mip.BulkData.Lock(LOCK_READ_WRITE);
		FMemory::Memcpy(TextureData, BlendedPixels.GetData(), Width * Height * sizeof(FColor));
		Mip.BulkData.Unlock();
		BlendedTexture->UpdateResource();
	}

	return BlendedTexture;
}

void AComfyStreamActor::ApplyInterpolatedFrame(const FComfyFrame& Frame)
{
	if (!Frame.IsComplete())
	{
		return;
	}

	ApplyTexturesToMaterial(Frame);
	FVector FixedPosition = GetActorLocation();
	SpawnTextureActor(Frame, FixedPosition);
}

void AComfyStreamActor::HandleFullFrame(const FComfyFrame& Frame)
{
	// Only process if frame is complete (has RGB and Mask at minimum)
	if (!Frame.IsComplete())
	{
		if(debug) UE_LOG(LogTemp, Warning, TEXT("[ComfyStreamActor] Received incomplete frame - RGB=%s, Mask=%s, Depth=%s. Waiting for complete frame."),
			IsValid(Frame.RGB) ? TEXT("valid") : TEXT("NULL"),
			IsValid(Frame.Mask) ? TEXT("valid") : TEXT("NULL"),
			IsValid(Frame.Depth) ? TEXT("valid") : TEXT("NULL"));
		return;
	}
	
	// Reset sequence index and channel flags FIRST, before processing frame
	// This ensures next frame's textures start with clean state
	SeqIndex = 0;
	bHasRGB = false;
	bHasDepth = false;
	bHasMask = false;
	
	LatestFrame = Frame;
	
	if(debug) UE_LOG(LogTemp, Display, TEXT("[ComfyStreamActor] Received complete frame - RGB=%s, Mask=%s, Depth=%s"), 
		IsValid(Frame.RGB) ? *Frame.RGB->GetName() : TEXT("NULL"),
		IsValid(Frame.Mask) ? *Frame.Mask->GetName() : TEXT("NULL"),
		IsValid(Frame.Depth) ? *Frame.Depth->GetName() : TEXT("NULL"));
	
	// Check if this is actually a new frame (textures are different from last applied frame)
	// Only update if we have a new complete frame ready to replace the current one
	bool bIsNewFrame = false;
	if (!LastAppliedFrame.IsComplete())
	{
		// First frame - always apply
		bIsNewFrame = true;
		if(debug) UE_LOG(LogTemp, Display, TEXT("[ComfyStreamActor] First frame received, applying to actor"));
	}
	else
	{
		// Compare textures to see if this is a new frame
		// RGB and Mask are required, Depth is optional
		if (Frame.RGB != LastAppliedFrame.RGB ||
			Frame.Mask != LastAppliedFrame.Mask ||
			Frame.Depth != LastAppliedFrame.Depth)  // Compare depth even if null
		{
			bIsNewFrame = true;
			if(debug) UE_LOG(LogTemp, Display, TEXT("[ComfyStreamActor] New frame detected (textures changed), applying to actor"));
		}
	}
	
	// Only update actor if this is a new complete frame ready to replace the current one
	if (bIsNewFrame)
	{
		// Generate interpolated frames if interpolation is enabled and we have a previous frame
		if (bEnableInterpolation && PreviousFrame.IsComplete() && NumInterpolatedFrames > 0)
		{
			GenerateInterpolatedFrames(PreviousFrame, Frame);
			InterpolationTimer = 0.0f;
		}
		else
		{
			// No interpolation - apply frame directly
			ApplyTexturesToMaterial(Frame);
			FVector FixedPosition = GetActorLocation();
			SpawnTextureActor(Frame, FixedPosition);
		}
		
		// Update previous frame for next interpolation
		PreviousFrame = Frame;
		
		// Update last applied frame
		LastAppliedFrame = Frame;
	}
	else
	{
		if(debug) UE_LOG(LogTemp, Verbose, TEXT("[ComfyStreamActor] Frame unchanged, skipping update"));
	}
}
