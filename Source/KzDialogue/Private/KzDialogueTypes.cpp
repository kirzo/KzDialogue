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

bool FKzDialogueLineList::TryResolve(int32 Index, FKzDialogueLine& OutLine) const
{
	if (!LineIds.IsValidIndex(Index)) { return false; }

	UKzDialogueAsset* Loaded = Asset.LoadSynchronous();
	if (!Loaded) { return false; }

	return Loaded->TryResolveLineOrAlias(LineIds[Index], OutLine);
}

bool FKzDialogueLineList::TryResolveAll(TArray<FKzDialogueLine>& OutLines) const
{
	OutLines.Reset();
	if (!Asset.IsValid() && Asset.IsNull()) { return false; }

	UKzDialogueAsset* Loaded = Asset.LoadSynchronous();
	if (!Loaded) { return false; }

	OutLines.Reserve(LineIds.Num());
	for (const FGuid& Id : LineIds)
	{
		FKzDialogueLine Line;
		if (Loaded->TryResolveLineOrAlias(Id, Line))
		{
			OutLines.Add(MoveTemp(Line));
		}
	}
	return OutLines.Num() > 0;
}

void FKzDialogueLineList::GetLineRefs(TArray<FKzDialogueLineRef>& OutRefs) const
{
	OutRefs.Reset();
	OutRefs.Reserve(LineIds.Num());
	for (const FGuid& Id : LineIds)
	{
		FKzDialogueLineRef Ref;
		Ref.Asset = Asset;
		Ref.LineId = Id;
		OutRefs.Add(MoveTemp(Ref));
	}
}