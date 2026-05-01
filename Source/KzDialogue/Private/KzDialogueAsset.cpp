// Copyright 2026 kirzo

#include "KzDialogueAsset.h"

UE_DISABLE_OPTIMIZATION

UKzDialogueAsset::UKzDialogueAsset()
{
}

int32 UKzDialogueAsset::IndexOfLine(const FGuid& LineId) const
{
	return Lines.IndexOfByPredicate([&LineId](const FKzDialogueLine& L) { return L.LineId == LineId; });
}

bool UKzDialogueAsset::TryGetLineById(const FGuid& LineId, FKzDialogueLine& OutLine) const
{
	const int32 Index = IndexOfLine(LineId);
	if (Index == INDEX_NONE)
	{
		return false;
	}
	OutLine = Lines[Index];
	return true;
}

FPrimaryAssetId UKzDialogueAsset::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(TEXT("KzDialogue"), GetFName());
}

#if WITH_EDITOR

void UKzDialogueAsset::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	EnsureLineGuids();
}

void UKzDialogueAsset::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent)
{
	Super::PostEditChangeChainProperty(PropertyChangedEvent);
	EnsureLineGuids();
}

void UKzDialogueAsset::PostDuplicate(bool bDuplicateForPIE)
{
	Super::PostDuplicate(bDuplicateForPIE);
	if (!bDuplicateForPIE)
	{
		// Force regeneration after duplication so duplicated lines don't share GUIDs
		// with the originals (which would break Sequencer references in copies).
		for (FKzDialogueLine& Line : Lines)
		{
			Line.LineId = FGuid::NewGuid();
		}
	}
}

void UKzDialogueAsset::PostLoad()
{
	Super::PostLoad();
	EnsureLineGuids();
}

void UKzDialogueAsset::EnsureLineGuids()
{
	bool bDirty = false;
	TSet<FGuid> Seen;
	Seen.Reserve(Lines.Num());
	for (FKzDialogueLine& Line : Lines)
	{
		if (!Line.LineId.IsValid() || Seen.Contains(Line.LineId))
		{
			Line.LineId = FGuid::NewGuid();
			bDirty = true;
		}
		Seen.Add(Line.LineId);
	}
	if (bDirty)
	{
		MarkPackageDirty();
	}
}

#endif

UE_ENABLE_OPTIMIZATION