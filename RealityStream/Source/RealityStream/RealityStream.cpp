#include "Modules/ModuleManager.h"

class FRealityStreamModule : public IModuleInterface
{
public:
	virtual void StartupModule() override {}
	virtual void ShutdownModule() override {}
};

IMPLEMENT_MODULE(FRealityStreamModule, RealityStream);
