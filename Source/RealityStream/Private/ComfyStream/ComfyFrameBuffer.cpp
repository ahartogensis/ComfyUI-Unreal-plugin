#include "ComfyStream/ComfyFrameBuffer.h"
#include "HAL/UnrealMemory.h"
#include "Misc/AssertionMacros.h"

int debug = 0; 
//receives textures and pairs them into a single frame
// Expected order from ComfyUI: RGB (Index 0), Depth (Index 1, optional), Mask (Index 2)
// Frame is complete when RGB and Mask are present (Depth is optional)
void UComfyFrameBuffer::PushTexture(UTexture2D* Tex, int Index)
{
	// Validate texture is valid before assigning
	if (!Tex || !IsValid(Tex))
	{
		if(debug) UE_LOG(LogTemp, Warning, TEXT("[ComfyFrameBuffer] Received invalid texture at index %d"), Index);
		return;
	}

	//textures are assigned based on order of arrival
	if (Index == 0) 
	{
		Frame.RGB = Tex;
	}
	if (Index == 1) 
	{
		Frame.Depth = Tex;
	}
	if (Index == 2) 
	{
		Frame.Mask = Tex;
	}

	NextIndex = (NextIndex + 1) % 3;

	// Track how many textures we've received in this frame
	TextureCount++;
	
	// Check if frame is complete (RGB + Mask required, Depth optional)
	// Both RGB and Mask must be valid (not null) for frame to be complete
	if (Frame.RGB && Frame.Mask && IsValid(Frame.RGB) && IsValid(Frame.Mask))
	{
		if(debug) UE_LOG(LogTemp, Display, TEXT("[ComfyFrameBuffer] Frame complete (RGB + Mask). TextureCount=%d, Index=%d"), TextureCount, Index);
		FComfyFrame CompleteFrame = Frame; // Copy frame before reset
		Reset(); // Reset immediately so next frame starts clean
		OnFullFrameReady.Broadcast(CompleteFrame); // Broadcast after reset
	}
	// If we have all three textures, frame should be complete
	else if (Frame.RGB && Frame.Depth && Frame.Mask)
	{
		if (Frame.IsComplete())
		{
			if(debug) UE_LOG(LogTemp, Display, TEXT("[ComfyFrameBuffer] Frame complete (RGB + Depth + Mask). TextureCount=%d"), TextureCount);
			OnFullFrameReady.Broadcast(Frame);
			Reset();
			TextureCount = 0;
		}
	}
}

void UComfyFrameBuffer::Reset()
{
	Frame = {};
	NextIndex = 0;
	TextureCount = 0; // Reset texture count when frame completes
}
