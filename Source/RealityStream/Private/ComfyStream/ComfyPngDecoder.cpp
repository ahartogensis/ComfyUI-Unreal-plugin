#include "ComfyStream/ComfyPngDecoder.h"
#include "IImageWrapperModule.h"
#include "IImageWrapper.h"
#include "Engine/Texture2D.h"
#include "Modules/ModuleManager.h"

//Decodes PNG and JPEG images into UTexture2D
//Assume depth images are DepthAnything PNGs
//Use UE's built-in image wrapper module for decoding
UTexture2D* UComfyPngDecoder::DecodePNGToTexture(const TArray<uint8>& PNGData)
{
	return DecodePNGToTextureWithFormat(PNGData, PF_R8G8B8A8);
}

// ============================================================
// Decoder
// ============================================================

UTexture2D* UComfyPngDecoder::DecodePNGToTextureWithFormat(const TArray<uint8>& PNGData, TEnumAsByte<EPixelFormat> PixelFormat)
{	
	//use UE's built-in image wrapper 
	IImageWrapperModule& ImageWrapperModule = 
		FModuleManager::LoadModuleChecked<IImageWrapperModule>("ImageWrapper");

		//detect either PNG or JPEG
	EImageFormat Format =
		IsValidPNGData(PNGData) ? EImageFormat::PNG :
		(IsValidJPEGData(PNGData) ? EImageFormat::JPEG : EImageFormat::Invalid);
	
	if (Format == EImageFormat::Invalid)
		return nullptr;

	//create wrapper for format then parse data 
	TSharedPtr<IImageWrapper> Wrapper = ImageWrapperModule.CreateImageWrapper(Format);
	if (!Wrapper.IsValid() || !Wrapper->SetCompressed(PNGData.GetData(), PNGData.Num()))
		return nullptr;

	int32 W = Wrapper->GetWidth();
	int32 H = Wrapper->GetHeight();

	TArray<uint8> Raw;
	if (!Wrapper->GetRaw(ERGBFormat::RGBA, 8, Raw)) return nullptr;
	
	return CreateTextureFromData(Raw, W, H, PixelFormat);
}

// ============================================================
// Texture Creator
// ============================================================

UTexture2D* UComfyPngDecoder::CreateTextureFromData(const TArray<uint8>& Data, int32 W, int32 H, EPixelFormat Format)
{
	// Downscale by half
	int32 ScaledW = W / 2;
	int32 ScaledH = H / 2;
	
	// Ensure minimum size
	if (ScaledW < 1) ScaledW = 1;
	if (ScaledH < 1) ScaledH = 1;
	
	TArray<uint8> ScaledData;
	
	// Only downscale if the image is large enough and we have RGBA format (4 bytes per pixel)
	if (W > 1 && H > 1 && Format == PF_R8G8B8A8)
	{
		const int32 BytesPerPixel = 4;
		ScaledData.SetNum(ScaledW * ScaledH * BytesPerPixel);
		
		// Simple box filter downsampling (average of 2x2 pixels)
		for (int32 Y = 0; Y < ScaledH; Y++)
		{
			for (int32 X = 0; X < ScaledW; X++)
			{
				// Source pixel coordinates (top-left of 2x2 block)
				int32 SrcX = X * 2;
				int32 SrcY = Y * 2;
				
				// Clamp to source image bounds
				SrcX = FMath::Min(SrcX, W - 1);
				SrcY = FMath::Min(SrcY, H - 1);
				
				// Get 2x2 block (clamped to image bounds)
				int32 X1 = FMath::Min(SrcX + 1, W - 1);
				int32 Y1 = FMath::Min(SrcY + 1, H - 1);
				
				// Sample 4 pixels and average
				uint32 R = 0, G = 0, B = 0, A = 0;
				for (int32 Dy = 0; Dy <= 1; Dy++)
				{
					for (int32 Dx = 0; Dx <= 1; Dx++)
					{
						int32 Px = FMath::Min(SrcX + Dx, W - 1);
						int32 Py = FMath::Min(SrcY + Dy, H - 1);
						int32 SrcIdx = (Py * W + Px) * BytesPerPixel;
						
						if (SrcIdx + 3 < Data.Num())
						{
							R += Data[SrcIdx + 0];
							G += Data[SrcIdx + 1];
							B += Data[SrcIdx + 2];
							A += Data[SrcIdx + 3];
						}
					}
				}
				
				// Average and write to scaled data
				int32 DstIdx = (Y * ScaledW + X) * BytesPerPixel;
				ScaledData[DstIdx + 0] = R / 4;
				ScaledData[DstIdx + 1] = G / 4;
				ScaledData[DstIdx + 2] = B / 4;
				ScaledData[DstIdx + 3] = A / 4;
			}
		}
	}
	else
	{
		// No downscaling for small images or non-RGBA formats
		ScaledData = Data;
		ScaledW = W;
		ScaledH = H;
	}
	
	UTexture2D* Texture = UTexture2D::CreateTransient(ScaledW, ScaledH, Format);
	if (!Texture) return nullptr;

	//for depth maps to attain full color fidelity 
	Texture->CompressionSettings = TC_VectorDisplacementmap; //prevent color compression 
	Texture->SRGB = true; //DepthAnything mask uses grayscale but RGB should be gamma
	Texture->Filter = TF_Bilinear;

	//copy scaled data into texture
	void* TexData = Texture->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(TexData, ScaledData.GetData(), ScaledData.Num());
	Texture->GetPlatformData()->Mips[0].BulkData.Unlock();

	Texture->UpdateResource();
	return Texture;
}

bool UComfyPngDecoder::IsValidPNGData(const TArray<uint8>& Data) {
	return Data.Num() >= 8 && FMemory::Memcmp(Data.GetData(), "\x89PNG\r\n\x1A\n", 8) == 0;
}

bool UComfyPngDecoder::IsValidJPEGData(const TArray<uint8>& Data) {
	return Data.Num() >= 3 && Data[0] == 0xFF && Data[1] == 0xD8 && Data[2] == 0xFF;
}
