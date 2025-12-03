#include "SplatCreator/SplatCreatorCommand.h"
#include "CoreMinimal.h"

void RegisterSplatCreatorMenus()
{
	// No menu registration needed - functions are Blueprint-callable
	UE_LOG(LogTemp, Display, TEXT("[SplatCreator] SplaceCreator subsystem ready for Blueprint use"));
}
