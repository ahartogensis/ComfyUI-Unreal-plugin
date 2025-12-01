#include "ComfyStream/ComfyFrameBuffer.h"
#include "HAL/UnrealMemory.h"
#include "Misc/AssertionMacros.h"

//recives the textures in batches of 3 and pairs the textures into a single frame
// Expected order from ComfyUI: RGB (Index 0), Depth (Index 1), Mask (Index 2)
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
