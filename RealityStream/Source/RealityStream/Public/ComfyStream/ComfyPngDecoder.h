#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Engine/Texture2D.h"
#include "ComfyPngDecoder.generated.h"


// Decodes PNG images into UTexture2D (used by ComfyStreamActor for received images)
UCLASS()
class REALITYSTREAM_API UComfyPngDecoder : public UObject
{
	GENERATED_BODY()

public:
	UTexture2D* DecodePNGToTexture(const TArray<uint8>& PNGData);
	UTexture2D* DecodePNGToTextureWithFormat(const TArray<uint8>& PNGData, TEnumAsByte<EPixelFormat> PixelFormat);

	bool IsValidPNGData(const TArray<uint8>& PNGData);

private:
	UTexture2D* CreateTextureFromData(const TArray<uint8>& UncompressedData, int32 Width, int32 Height, EPixelFormat PixelFormat);
};
