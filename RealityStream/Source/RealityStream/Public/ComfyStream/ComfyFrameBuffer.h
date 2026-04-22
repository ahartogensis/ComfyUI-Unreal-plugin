#pragma once
#include "CoreMinimal.h"
#include "ComfyFrameBundle.h"
#include "ComfyFrameBuffer.generated.h"

//Fires event when complete frame arrives (RGB and Mask required, Depth optional)

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnFullFrameReady, const FComfyFrame&, Frame);

UCLASS()
class REALITYSTREAM_API UComfyFrameBuffer : public UObject
{
    GENERATED_BODY()
public:
    //Event triggered when complete frame is ready (RGB and Mask required, Depth optional) 
    UPROPERTY(BlueprintAssignable)
    FOnFullFrameReady OnFullFrameReady;

    void PushTexture(UTexture2D* Tex, int Index);
    void Reset();

private:
    FComfyFrame Frame;
    int NextIndex = 0; //loop through textures (0=RGB, 1=Depth, 2=Mask) - Depth is optional
    int32 TextureCount = 0; // Track how many textures have been received in current frame
};
