#include "ComfyStream/ComfyStreamActor.h"
#include "ComfyStream/ComfyReconstruction.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "TimerManager.h"

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
	Reconstruction = NewObject<UComfyReconstruction>(this);

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

		//Update opacity fade-out
		if (Data.bIsFadingOut && IsValid(Data.Material))
		{
			Data.OpacityAlpha = FMath::Max(0.0f, Data.OpacityAlpha - (DeltaTime / FadeOutDuration));
			Data.Material->SetScalarParameterValue(TEXT("Opacity"), Data.OpacityAlpha);
			
			//When fade completes, destroy actor
			if (Data.OpacityAlpha <= 0.0f)
			{
				if (IsValid(Data.Actor))
				{
					AActor* ActorToDestroy = Data.Actor;
					ActorToDestroy->Destroy();
					SpawnedTextureActors.Remove(ActorToDestroy);
				}
				
				//Remove from ActorData
				GetWorld()->GetTimerManager().ClearTimer(Data.DestroyTimer);
				GetWorld()->GetTimerManager().ClearTimer(Data.LerpTimer);
				ActorData.RemoveAt(i);
				continue;
			}
		}
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

	const int32 Index = SeqIndex % 3;
	FrameBuffer->PushTexture(Texture, Index);
	SeqIndex++;

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

void AComfyStreamActor::HandleFullFrame(const FComfyFrame& Frame)
{
	LatestFrame = Frame;
	ApplyTexturesToMaterial(Frame);
	UpdatePlacementFromDepth(Frame);
}

void AComfyStreamActor::ApplyTexturesToMaterial(const FComfyFrame& Frame)
{
	static const FName RGBParam  = TEXT("RGB_Map");
	static const FName DepthParam= TEXT("Depth_Map");
	static const FName MaskParam = TEXT("Mask_Map");

	DynMat->SetTextureParameterValue(RGBParam, Frame.RGB);
	DynMat->SetTextureParameterValue(DepthParam, Frame.Depth);
	DynMat->SetTextureParameterValue(MaskParam, Frame.Mask);
}

void AComfyStreamActor::UpdatePlacementFromDepth(const FComfyFrame& Frame)
{
	//1) average depth in masked area
	float d01 = 0.5f;
	UTexture2D* DepthTex = Frame.Depth;
	UTexture2D* MaskTex = Frame.Mask;
	
	Reconstruction->AverageNormalizedDepth(DepthTex, MaskTex, d01, 8);

	//2) map to world-units 
	//for DepthAnything: d01~1.0 near, ~0.0 far
	const float Far = Reconstruction->DepthScaleUnits;
	const float Near = Far * 0.05f; //avoid zero depth 
	const float Zdist = FMath::Lerp(Far, Near, FMath::Clamp(d01, 0.0f, 1.0f));

	//3) estimate intrinsics from RGB size or depth based on what is available
	UTexture2D* SizeTex = Frame.RGB ? Frame.RGB : Frame.Depth;
	if (!SizeTex) return;
	
	const int32 W = SizeTex->GetSizeX();
	const int32 H = SizeTex->GetSizeY();

	float fx, fy, cx, cy;
	Reconstruction->EstimateIntrinsics(W, H, fx, fy, cx, cy);

	//4) pick a representative pixel (image center)
	const int32 Px = (W - 1) / 2;
	const int32 Py = (H - 1) / 2;

	//5) back-project to world-relative vector
	const FVector Local = Reconstruction->DepthToWorld(Px, Py, Zdist, W, H, fx, fy, cx, cy);
	const FVector WorldPosition = GetActorLocation() + Local;

	//Spawn actor at calculated world position
	SpawnTextureActor(Frame, WorldPosition);
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

	if (!ActorDataPtr)
	{
		FActorLerpData NewData;
		NewData.Actor = Actor;
		NewData.Position = WorldPosition;
		ActorData.Add(NewData);
		ActorDataPtr = &ActorData[ActorData.Num() - 1];
	}

	//Update textures with lerp if material exists
	if (ActorDataPtr->Material)
	{
		//Set new textures - update directly (smooth transition handled by material if it supports it)
		ActorDataPtr->Material->SetTextureParameterValue(TEXT("RGB_Map"), Frame.RGB);
		ActorDataPtr->Material->SetTextureParameterValue(TEXT("Depth_Map"), Frame.Depth);
		ActorDataPtr->Material->SetTextureParameterValue(TEXT("Mask_Map"), Frame.Mask);

		//If material supports lerp alpha parameter, enable lerping
		float LerpParam = 0.0f;
		if (ActorDataPtr->Material->GetScalarParameterValue(TEXT("LerpAlpha"), LerpParam))
		{
			ActorDataPtr->LerpAlpha = 0.0f;
			ActorDataPtr->bIsLerping = true;
			//Set new textures as target for lerp
			ActorDataPtr->Material->SetTextureParameterValue(TEXT("RGB_Map_New"), Frame.RGB);
			ActorDataPtr->Material->SetTextureParameterValue(TEXT("Depth_Map_New"), Frame.Depth);
			ActorDataPtr->Material->SetTextureParameterValue(TEXT("Mask_Map_New"), Frame.Mask);
		}
	}
	else
	{
			//First time - apply textures directly
			UStaticMeshComponent* MeshComp = Actor->FindComponentByClass<UStaticMeshComponent>();
			if (MeshComp && BaseMaterial)
			{
				ActorDataPtr->Material = UMaterialInstanceDynamic::Create(BaseMaterial, Actor);
				ActorDataPtr->Material->SetTextureParameterValue(TEXT("RGB_Map"), Frame.RGB);
				ActorDataPtr->Material->SetTextureParameterValue(TEXT("Depth_Map"), Frame.Depth);
				ActorDataPtr->Material->SetTextureParameterValue(TEXT("Mask_Map"), Frame.Mask);
				ActorDataPtr->Material->SetScalarParameterValue(TEXT("Opacity"), 1.0f);
				MeshComp->SetMaterial(0, ActorDataPtr->Material);
				ActorDataPtr->LerpAlpha = 1.0f;
				ActorDataPtr->OpacityAlpha = 1.0f;
			}
	}

	//Reset destroy timer and opacity
	GetWorld()->GetTimerManager().ClearTimer(ActorDataPtr->DestroyTimer);
	ActorDataPtr->OpacityAlpha = 1.0f;
	ActorDataPtr->bIsFadingOut = false;
	if (ActorDataPtr->Material)
	{
		ActorDataPtr->Material->SetScalarParameterValue(TEXT("Opacity"), 1.0f);
	}
	DestroyActorDelayed(Actor);
}

AActor* AComfyStreamActor::FindOrSpawnActorAtLocation(const FVector& WorldPosition)
{
	UWorld* World = GetWorld();

	//Check for existing actor at same location
	for (FActorLerpData& Data : ActorData)
	{
		if (Data.Actor && FVector::Dist(Data.Position, WorldPosition) < LocationThreshold)
		{
			return Data.Actor;
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

void AComfyStreamActor::UpdateActorLerp(FActorLerpData& Data, const FComfyFrame& Frame, float DeltaTime)
{
	if (!Data.bIsLerping || !IsValid(Data.Material)) return;

	Data.LerpAlpha = FMath::Clamp(Data.LerpAlpha + (DeltaTime * LerpSpeed), 0.0f, 1.0f);
	Data.Material->SetScalarParameterValue(TEXT("LerpAlpha"), Data.LerpAlpha);

	if (Data.LerpAlpha >= 1.0f)
	{
		//Lerp complete - swap textures
		UTexture* NewRGB = nullptr;
		UTexture* NewDepth = nullptr;
		UTexture* NewMask = nullptr;
		Data.Material->GetTextureParameterValue(TEXT("RGB_Map_New"), NewRGB);
		Data.Material->GetTextureParameterValue(TEXT("Depth_Map_New"), NewDepth);
		Data.Material->GetTextureParameterValue(TEXT("Mask_Map_New"), NewMask);

		if (NewRGB) Data.Material->SetTextureParameterValue(TEXT("RGB_Map"), NewRGB);
		if (NewDepth) Data.Material->SetTextureParameterValue(TEXT("Depth_Map"), NewDepth);
		if (NewMask) Data.Material->SetTextureParameterValue(TEXT("Mask_Map"), NewMask);

		Data.bIsLerping = false;
	}
}

void AComfyStreamActor::DestroyActorDelayed(AActor* Actor)
{
	//Find actor data
	FActorLerpData* ActorDataPtr = nullptr;
	for (FActorLerpData& Data : ActorData)
	{
		if (Data.Actor == Actor)
		{
			ActorDataPtr = &Data;
			break;
		}
	}

	//Start fade-out timer - after lifetime expires, begin opacity fade
	GetWorld()->GetTimerManager().SetTimer(
		ActorDataPtr->DestroyTimer,
		[this, ActorDataPtr]()
		{
			if (ActorDataPtr->Actor && ActorDataPtr->Material)
			{
				//Start opacity fade-out
				ActorDataPtr->bIsFadingOut = true;
				ActorDataPtr->OpacityAlpha = 1.0f;
			}
		},
		ActorLifetimeSeconds - FadeOutDuration,
		false
	);
}
