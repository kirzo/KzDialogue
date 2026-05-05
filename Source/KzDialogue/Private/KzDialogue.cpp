// Copyright 2026 kirzo

#include "KzDialogue.h"
#include "Debug/KzDialogueDebugCommands.h"
#include "Debug/KzDialogueDebugOverlay.h"

#define LOCTEXT_NAMESPACE "FKzDialogueModule"

void FKzDialogueModule::StartupModule()
{
#if !UE_BUILD_SHIPPING
	DebugCommands = MakeUnique<FKzDialogueDebugCommands>();
	DebugOverlay = MakeUnique<FKzDialogueDebugOverlay>();
#endif
}

void FKzDialogueModule::ShutdownModule()
{
#if !UE_BUILD_SHIPPING
	DebugCommands.Reset();
	DebugCommands.Reset();
#endif
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FKzDialogueModule, KzDialogue)