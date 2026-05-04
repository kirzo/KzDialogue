// Copyright 2026 kirzo

#include "KzDialogue.h"
#include "Debug/KzDialogueDebugCommands.h"

#define LOCTEXT_NAMESPACE "FKzDialogueModule"

void FKzDialogueModule::StartupModule()
{
#if !UE_BUILD_SHIPPING
	DebugCommands = MakeUnique<FKzDialogueDebugCommands>();
#endif
}

void FKzDialogueModule::ShutdownModule()
{
#if !UE_BUILD_SHIPPING
	DebugCommands.Reset();
#endif
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FKzDialogueModule, KzDialogue)