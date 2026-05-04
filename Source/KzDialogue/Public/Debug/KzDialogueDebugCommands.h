// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "HAL/IConsoleManager.h"

#if !UE_BUILD_SHIPPING

/**
 * Registers Kz.Dialogue.* console commands. Owned and destroyed by the KzDialogue
 * runtime module. Commands are gated behind !UE_BUILD_SHIPPING so they don't ship to
 * end users.
 *
 * The class itself only stores the auto-console-command handles; all actual logic
 * lives in static helpers that resolve the active world's UKzDialogueSubsystem.
 */
class FKzDialogueDebugCommands
{
public:
	FKzDialogueDebugCommands();
	~FKzDialogueDebugCommands();

private:
	TArray<TUniquePtr<FAutoConsoleCommand>> Commands;
};

#endif // !UE_BUILD_SHIPPING