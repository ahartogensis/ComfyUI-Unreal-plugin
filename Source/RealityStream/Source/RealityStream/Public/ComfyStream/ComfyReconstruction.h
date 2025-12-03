#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Engine/Texture2D.h"

// Converts depth to world projection 
// Assumes DepthAnything webcam depth format
// Auto intrinsics: fx=fy = width * FocalScale, cx,cy = image center
// Depth mapping: d_norm in [0..1], treat 1.0 ≈ near, 0.0 ≈ far
// Converts (px,py,depth) to UE coordinates (Z forward, X right, Y up)

#include "ComfyReconstruction.generated.h"

UCLASS(BlueprintType)
class REALITYSTREAM_API UComfyReconstruction : public UObject
{
	GENERATED_BODY()

public:

	// Focal length scale factor
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Reconstruction")
	float FocalScale = 1.2f; // default based on webcams

	// Depth scale in world units (1.0 = 1 cm) 
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Reconstruction")
	float DepthScaleUnits = 500.0f; // default scene size 

	// Avoid division and near-zero depth issues 
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Reconstruction")
	float DepthEpsilon = 1e-3f;

public:

	// Estimate intrinsics from image size and focal scale 
	UFUNCTION(BlueprintCallable, Category="Reconstruction")
	void EstimateIntrinsics(int32 Width, int32 Height,
		float& OutFx, float& OutFy, float& OutCx, float& OutCy) const;

	// Convert pixel + depth to world space 
	// returns vector in UE coords: Z forward, X right, Y up 
	UFUNCTION(BlueprintCallable, Category="Reconstruction")
	FVector DepthToWorld(int32 Px, int32 Py, float DepthUnits,
		int32 Width, int32 Height, float Fx, float Fy, float Cx, float Cy) const;

	// Compute normalized depth for masked pixels for speed 
	UFUNCTION(BlueprintCallable, Category="Reconstruction")
	bool AverageNormalizedDepth(UTexture2D* DepthTex, UTexture2D* MaskTex,
		float& OutAvgDepth01, int32 Step = 8) const;

private:

	static bool LockReadFColor(UTexture2D* Tex,
		const FColor*& OutPtr, int32& OutW, int32& OutH);

	static void Unlock(UTexture2D* Tex);
};
