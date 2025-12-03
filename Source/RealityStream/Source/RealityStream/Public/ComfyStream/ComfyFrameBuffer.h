#pragma once
#include "CoreMinimal.h"
#include "ComfyFrameBundle.h"
#include "ComfyFrameBuffer.generated.h"

//Fires event when all 3 textures arrive (RGB, Depth, Mask)

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnFullFrameReady, const FComfyFrame&, Frame);

UCLASS()
class REALITYSTREAM_API UComfyFrameBuffer : public UObject
{
    GENERATED_BODY()
public:
    //Event triggered when all 3 textures have been recieved 
    UPROPERTY(BlueprintAssignable)
    FOnFullFrameReady OnFullFrameReady;

    void PushTexture(UTexture2D* Tex, int Index);
    void Reset();

private:
    FComfyFrame Frame;
    int NextIndex = 0; //loop through the 3 textures (0=RGB, 1=Depth, 2=Mask)
};
