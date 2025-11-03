#include "ComfyStream/ComfyStreamComponent.h"
#include "ComfyStream/ComfyImageFetcher.h"
#include "ComfyStream/ComfyPngDecoder.h"
#include "Engine/World.h"
#include "TimerManager.h"

//Connects to one ComfyUI websocket channel and sets up texture broadcasting 
//pairs textures as they arrive 
UComfyStreamComponent::UComfyStreamComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true; //using lerp between frames 
}

void UComfyStreamComponent::BeginPlay()
{
	Super::BeginPlay();

	PngDecoder   = NewObject<UComfyPngDecoder>(this);
	ImageFetcher = NewObject<UComfyImageFetcher>(this);
	ImageFetcher->Config = StreamConfig;

	//bind textures to internal handlers
	ImageFetcher->OnTextureReceived.AddDynamic(this, &UComfyStreamComponent::OnTextureReceivedInternal);
	ImageFetcher->OnConnectionStatusChanged.AddDynamic(this, &UComfyStreamComponent::OnConnectionStatusChangedInternal);
	ImageFetcher->OnError.AddDynamic(this, &UComfyStreamComponent::OnErrorInternal);

	if (StreamConfig.bAutoReconnect)
	{
		Connect();
	}
}

void UComfyStreamComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (ImageFetcher) ImageFetcher->StopPolling();
	Super::EndPlay(EndPlayReason);
}

void UComfyStreamComponent::TickComponent(float Delta, ELevelTick Type, FActorComponentTickFunction* Fn)
{
	Super::TickComponent(Delta, Type, Fn);
	//material-lerp 
	if (StreamConfig.bEnableLerpSmoothing) UpdateLerpTransition(Delta);
}

void UComfyStreamComponent::Connect()
{
	if (ImageFetcher)
		ImageFetcher->StartPolling(StreamConfig.ServerURL, StreamConfig.ChannelNumber);
}

void UComfyStreamComponent::Disconnect()
{
	if (ImageFetcher)
		ImageFetcher->StopPolling();
}

bool UComfyStreamComponent::IsConnected() const
{
	return ImageFetcher ? ImageFetcher->IsPolling() : false;
}

EComfyConnectionStatus UComfyStreamComponent::GetConnectionStatus() const
{
	return ConnectionStatus;
}

void UComfyStreamComponent::OnTextureReceivedInternal(UTexture2D* Texture)
{
	if (!Texture) return;

	// If lerp smoothing is disabled, broadcast immediately
	if (!StreamConfig.bEnableLerpSmoothing)
	{
		OnTextureReceived.Broadcast(Texture);
	}
	else
	{
		// Lerp smoothing enabled - will broadcast in UpdateLerpTransition
		SetComponentTickEnabled(true);
	}
}

void UComfyStreamComponent::OnConnectionStatusChangedInternal(bool bConnected)
{
	ConnectionStatus = bConnected ? EComfyConnectionStatus::Connected : EComfyConnectionStatus::Disconnected;
	OnConnectionStatusChanged.Broadcast(bConnected);

	//attempt reconnect
	if (!bConnected && StreamConfig.bAutoReconnect)
	{
		GetWorld()->GetTimerManager().SetTimer(
			ReconnectTimer, this, &UComfyStreamComponent::AttemptReconnect, StreamConfig.ReconnectDelay, false);
	}
}

void UComfyStreamComponent::OnErrorInternal(const FString& Msg)
{
	ConnectionStatus = EComfyConnectionStatus::Error;
	OnError.Broadcast(Msg);
}

void UComfyStreamComponent::AttemptReconnect()
{
	if (!IsConnected())
	{
		Connect();
	}
}

void UComfyStreamComponent::UpdateLerpTransition(float DeltaTime)
{
	//Update each texture lerp channel
	RGBLerpState.UpdateLerp(DeltaTime, StreamConfig.LerpSpeed, StreamConfig.LerpThreshold);
	DepthLerpState.UpdateLerp(DeltaTime, StreamConfig.LerpSpeed, StreamConfig.LerpThreshold);
	MaskLerpState.UpdateLerp(DeltaTime, StreamConfig.LerpSpeed, StreamConfig.LerpThreshold);

	//Broadcast updated textures
	if (RGBLerpState.CurrentTexture) OnTextureReceived.Broadcast(RGBLerpState.CurrentTexture);
	if (DepthLerpState.CurrentTexture) OnTextureReceived.Broadcast(DepthLerpState.CurrentTexture);
	if (MaskLerpState.CurrentTexture) OnTextureReceived.Broadcast(MaskLerpState.CurrentTexture);

	//Stop ticking if no lerp is happening
	if (!RGBLerpState.bIsLerping &&
		!DepthLerpState.bIsLerping &&
		!MaskLerpState.bIsLerping)
	{
		SetComponentTickEnabled(false);
	}
}

