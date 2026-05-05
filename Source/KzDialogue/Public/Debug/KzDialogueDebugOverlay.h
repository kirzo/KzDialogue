// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "HAL/IConsoleManager.h"

#if !UE_BUILD_SHIPPING

class UCanvas;
class APlayerController;

/**
 * On-screen debug overlay for the dialogue system.
 *
 * Driven by the console variable Kz.Dialogue.Debug:
 *   0 -> hidden
 *   1 -> shown (top-left of the active viewport)
 *
 * Renders the state of every active channel (player state, priority, time scale,
 * current line) plus the list of speakers registered in the world. Hooked via
 * UDebugDrawService so it works in PIE, standalone game, and the editor's "Play in
 * Viewport" mode without any extra setup.
 */
class FKzDialogueDebugOverlay
{
public:
	FKzDialogueDebugOverlay();
	~FKzDialogueDebugOverlay();

private:
	/** Hooked into UDebugDrawService::Register("Game", ...). */
	void DrawDebug(UCanvas* Canvas, APlayerController* PC);

	/**
	 * Resolve the most relevant world for debug rendering. Mirrors the logic in
	 * FKzDialogueDebugCommands so console + overlay stay consistent.
	 */
	class UWorld* ResolveActiveWorld() const;

	FDelegateHandle DrawHandle;
	TUniquePtr<FAutoConsoleVariableRef> CVarRef;

	/** Backing storage for the cvar. */
	int32 bDebugEnabled = 0;
};

#endif // !UE_BUILD_SHIPPING