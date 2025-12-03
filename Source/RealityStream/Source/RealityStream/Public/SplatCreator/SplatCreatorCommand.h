#pragma once

#include "CoreMinimal.h"

#if WITH_EDITOR

class FSplatCreatorCommand
{
public:
    static void RegisterMenus();
    static void ExecuteGenerateSplat();

private:
    static FString OpenFileDialog();
};

#endif // WITH_EDITOR

