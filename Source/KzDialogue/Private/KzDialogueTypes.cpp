// Copyright 2026 kirzo

#include "KzDialogueTypes.h"
#include "KzDialogueAsset.h"
#include "KzSpeakerAsset.h"

FText FKzDialogueLine::GetFormattedText() const
{
	return FormatArguments.Num() > 0 ? FText::Format(FTextFormat(Text), FormatArguments) : Text;
}

FText FKzDialogueSpeaker::GetDisplayLabel() const
{
	if (!Asset)
	{
		return NSLOCTEXT("KzDialogue", "Narration", "<Narration>");
	}

	const FText Resolved = Asset->GetResolvedDisplayName();
	if (!Resolved.IsEmpty())
	{
		return Resolved;
	}

	// Editor fallback for a freshly created asset with no name authored yet.
	return FText::FromString(FName::NameToDisplayString(Asset->GetName(), false));
}

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