#pragma once
#include "Engine/Texture2D.h"
#include "ComfyFrameBundle.generated.h"

//The Blueprint stuct for RGB, Depth, and Mask Maps
USTRUCT(BlueprintType)
struct FComfyFrame
{
    GENERATED_BODY()

    UPROPERTY() UTexture2D* RGB = nullptr;
    UPROPERTY() UTexture2D* Depth = nullptr;
    UPROPERTY() UTexture2D* Mask = nullptr;

    bool IsComplete() const
    {
        // Frame is complete if we have RGB and Mask (Depth is optional)
        return RGB && Mask;
    }
    
    bool HasDepth() const
    {
        return Depth != nullptr;
    }
};
