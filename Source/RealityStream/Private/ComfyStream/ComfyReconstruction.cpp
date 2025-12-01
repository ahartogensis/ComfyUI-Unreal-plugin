#include "ComfyStream/ComfyReconstruction.h"
#include "Engine/Texture2D.h"

// Converts depth to world projection 
// Assumes DepthAnything webcam depth format
// Auto intrinsics: fx=fy = width * FocalScale, cx,cy = image center
// Depth mapping: d_norm in [0..1], treat 1.0 ≈ near, 0.0 ≈ far
// Converts (px,py,depth) to UE coordinates (Z forward, X right, Y up)

void UComfyReconstruction::EstimateIntrinsics(int32 Width, int32 Height,
	float& OutFx, float& OutFy, float& OutCx, float& OutCy) const
{
	OutFx = FMath::Max(1.0f, Width * FocalScale);
	OutFy = OutFx;
	OutCx = (Width  - 1) * 0.5f;
	OutCy = (Height - 1) * 0.5f;
}

FVector UComfyReconstruction::DepthToWorld(
	int32 Px, int32 Py, float DepthUnits,
	int32 Width, int32 Height, float Fx, float Fy, float Cx, float Cy) const
{
	const float Z = FMath::Max(DepthUnits, DepthEpsilon);
	const float X = (Px - Cx) * Z / FMath::Max(Fx, DepthEpsilon);
	const float Y = (Py - Cy) * Z / FMath::Max(Fy, DepthEpsilon);

	// UE convention: Z forward, X right, Y up
	return FVector(-Z, X, -Y);
}

bool UComfyReconstruction::AverageNormalizedDepth(
	UTexture2D* DepthTex, UTexture2D* MaskTex, float& OutAvgDepth01, int32 Step) const
{
	OutAvgDepth01 = 0.5f;
	if (!DepthTex) return false;

	const FColor* DepthPtr = nullptr;
	int32 W=0, H=0;
	if (!LockReadFColor(DepthTex, DepthPtr, W, H))
	{
		return false;
	}

	bool bMaskLocked = false;
	const FColor* MaskPtr = nullptr;
	int32 MW=0, MH=0;
	if (MaskTex)
	{
		if (!LockReadFColor(MaskTex, MaskPtr, MW, MH))
		{
			Unlock(DepthTex);
			return false;
		}
		bMaskLocked = true;
	}

	if (!DepthPtr) 
	{
		if (bMaskLocked && MaskTex) Unlock(MaskTex);
		return false;
	}

	double Sum = 0.0;
	int64 Count = 0;

	for (int32 y = 0; y < H; y += Step)
	{
		for (int32 x = 0; x < W; x += Step)
		{
			const int32 i = y * W + x;

			if (MaskPtr)
			{
				// Ensure we don't go out of bounds
				if (i >= MW * MH)
					continue;
					
				const FColor M = MaskPtr[i];
				if (M.R < 8 && M.G < 8 && M.B < 8)
					continue;
			}

			const FColor D = DepthPtr[i];
			const float d = FMath::Max3(D.R, D.G, D.B) / 255.0f;
			Sum += d;
			++Count;
		}
	}

	// Unlock in reverse order
	if (bMaskLocked && MaskTex)
		Unlock(MaskTex);

	Unlock(DepthTex);

	if (Count == 0) return false;
	OutAvgDepth01 = static_cast<float>(Sum / Count);
	return true;
}

bool UComfyReconstruction::LockReadFColor(UTexture2D* Tex,
	const FColor*& OutPtr, int32& OutW, int32& OutH)
{
	if (!Tex) return false;
	
	// Ensure we're on the game thread for texture operations
	if (!IsInGameThread())
	{
		return false;
	}

	FTexturePlatformData* PlatformData = Tex->GetPlatformData();
	if (!PlatformData || PlatformData->Mips.Num() == 0) return false;
	
	FTexture2DMipMap& Mip = PlatformData->Mips[0];
	if (!Mip.BulkData.GetBulkDataSize()) return false;

	OutPtr = static_cast<const FColor*>(Mip.BulkData.LockReadOnly());
	if (!OutPtr) return false;
	
	OutW = Tex->GetSizeX();
	OutH = Tex->GetSizeY();
	return true;
}

void UComfyReconstruction::Unlock(UTexture2D* Tex)
{
	if (!Tex) return;
	
	// Ensure we're on the game thread for texture operations
	if (!IsInGameThread())
	{
		return;
	}
	
	FTexturePlatformData* PlatformData = Tex->GetPlatformData();
	if (!PlatformData || PlatformData->Mips.Num() == 0) return;
	PlatformData->Mips[0].BulkData.Unlock();
}
