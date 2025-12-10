#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/StaticMeshComponent.h"
#include "ComfyStreamComponent.h"
#include "ComfyFrameBuffer.h"
#include "ComfyStreamActor.generated.h"

//receives 3 texture maps from ComfyUI and applies to to a material 
UCLASS()
class REALITYSTREAM_API AComfyStreamActor : public AActor
{
	GENERATED_BODY()

public:
	AComfyStreamActor();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaTime) override;

public:
	//visualize streamed textures on display mesh 
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	UStaticMeshComponent* DisplayMesh;

	//Comfy stream component
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	UComfyStreamComponent* ComfyStreamComponent;

	//base material made with parameters RGB, Mask, and Depth
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	UMaterialInterface* BaseMaterial;

	//network config for segmentation channel
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FComfyStreamConfig SegmentationChannelConfig;

	//Actor lifetime and lerp settings
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Actor Spawning")
	float ActorLifetimeSeconds = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Actor Spawning")
	float LerpSpeed = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Actor Spawning")
	float LocationThreshold = 50.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Actor Spawning")
	float FadeOutDuration = 0.5f;

	//Blueprint events 
	UFUNCTION(BlueprintImplementableEvent)
	void OnTextureReceived(UTexture2D* Texture);

	UFUNCTION(BlueprintImplementableEvent)
	void OnConnectionStatusChanged(bool bConnected);

	UFUNCTION(BlueprintImplementableEvent)
	void OnError(const FString& ErrorMessage);

	//connect and disconnect functions 
	UFUNCTION(BlueprintCallable) void ConnectSegmentationChannel();
	UFUNCTION(BlueprintCallable) void DisconnectAll();

private:
	//External helper functions 
	UPROPERTY() UComfyFrameBuffer*    FrameBuffer = nullptr;
	UPROPERTY() UMaterialInstanceDynamic* DynMat = nullptr;

	//once buffer completes, last frame 
	FComfyFrame LatestFrame;

	//Last frame that was actually applied to an actor (to prevent duplicate updates)
	FComfyFrame LastAppliedFrame;

	//sequence index for textures 
	int32 SeqIndex = 0;
	
	// Track which channels have been received in the current frame
	bool bHasRGB = false;
	bool bHasDepth = false;
	bool bHasMask = false;

	//Spawned actors from textures
	UPROPERTY() TArray<AActor*> SpawnedTextureActors;
	
	//Track actor positions and lerp states
	struct FActorLerpData
	{
		AActor* Actor = nullptr;
		FVector Position;
		UMaterialInstanceDynamic* Material = nullptr;
		float LerpAlpha = 1.0f;
		bool bIsLerping = false;
		float OpacityAlpha = 1.0f;
		bool bIsFadingOut = false;
		FTimerHandle DestroyTimer;
		FTimerHandle LerpTimer;
	};
	TArray<FActorLerpData> ActorData;

	//Internal helper functions (must be UFUNCTION for dynamic delegate binding)
	UFUNCTION() void HandleStreamTexture(UTexture2D* Texture);
	UFUNCTION() void HandleConnectionChanged(bool bConnected);
	UFUNCTION() void HandleStreamError(const FString& Error);

	//When FrameBuffer emits a complete triplet
	UFUNCTION() void HandleFullFrame(const FComfyFrame& Frame);

	//Apply textures to the material
	void ApplyTexturesToMaterial(const FComfyFrame& Frame);
	//Spawn actor at specified world position
	void SpawnTextureActor(const FComfyFrame& Frame, const FVector& WorldPosition);
	
	//Find existing actor at location or spawn new
	AActor* FindOrSpawnActorAtLocation(const FVector& WorldPosition);
	
	//Update material lerp
	void UpdateActorLerp(FActorLerpData& ActorData, const FComfyFrame& Frame, float DeltaTime);
	
	//Destroy actor after delay
	void DestroyActorDelayed(AActor* Actor);
	
	//Scale actor to match texture dimensions
	void ScaleActorToTextureSize(AActor* Actor, const FComfyFrame& Frame);
};