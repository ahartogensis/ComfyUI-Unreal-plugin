#include "ComfyStream/ComfyFrameBuffer.h"
#include "HAL/UnrealMemory.h"
#include "Misc/AssertionMacros.h"

//receives textures and pairs them into a single frame
// Expected order from ComfyUI: RGB (Index 0), Depth (Index 1, optional), Mask (Index 2)
// Frame is complete when RGB and Mask are present (Depth is optional)
void UComfyFrameBuffer::PushTexture(UTexture2D* Tex, int Index)
{
	if (!Tex) return;

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

	if (Frame.IsComplete())
	{
		OnFullFrameReady.Broadcast(Frame);
		Reset();
	}
}

void UComfyFrameBuffer::Reset()
{
	Frame = {};
	NextIndex = 0;
}
