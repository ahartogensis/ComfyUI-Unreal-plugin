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
		UE_LOG(LogTemp, Warning, TEXT("[ComfyFrameBuffer] Assigned RGB texture (Index 0), size: %dx%d"), Tex->GetSizeX(), Tex->GetSizeY());
	}
	if (Index == 1) 
	{
		Frame.Depth = Tex;
		UE_LOG(LogTemp, Warning, TEXT("[ComfyFrameBuffer] Assigned Depth texture (Index 1), size: %dx%d"), Tex->GetSizeX(), Tex->GetSizeY());
	}
	if (Index == 2) 
	{
		Frame.Mask = Tex;
		UE_LOG(LogTemp, Warning, TEXT("[ComfyFrameBuffer] Assigned Mask texture (Index 2), size: %dx%d"), Tex->GetSizeX(), Tex->GetSizeY());
	}

	NextIndex = (NextIndex + 1) % 3;

	if (Frame.IsComplete())
	{
		UE_LOG(LogTemp, Warning, TEXT("[ComfyFrameBuffer] Full frame complete! RGB: %s, Depth: %s, Mask: %s"), 
			Frame.RGB ? TEXT("Valid") : TEXT("NULL"),
			Frame.Depth ? TEXT("Valid") : TEXT("NULL"),
			Frame.Mask ? TEXT("Valid") : TEXT("NULL"));
		OnFullFrameReady.Broadcast(Frame);
		Reset();
	}
}

void UComfyFrameBuffer::Reset()
{
	Frame = {};
	NextIndex = 0;
}
