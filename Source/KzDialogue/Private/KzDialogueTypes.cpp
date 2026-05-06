// Copyright 2026 kirzo

#include "KzDialogueTypes.h"
#include "KzDialogueAsset.h"

bool FKzDialogueLineRef::TryResolve(FKzDialogueLine& OutLine) const
{
	if (!IsValid()) { return false; }

	UKzDialogueAsset* Loaded = Asset.LoadSynchronous();
	if (!Loaded) { return false; }

	return Loaded->TryResolveLineOrAlias(LineId, OutLine);
}