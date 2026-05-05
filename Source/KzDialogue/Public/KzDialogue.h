// Copyright 2026 kirzo

#pragma once

#include "Modules/ModuleManager.h"

class FKzDialogueModule : public IModuleInterface
{
private:
#if !UE_BUILD_SHIPPING
	TUniquePtr<class FKzDialogueDebugCommands> DebugCommands;
	TUniquePtr<class FKzDialogueDebugOverlay>  DebugOverlay;
#endif

public:
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};