// Copyright 2026 kirzo

#include "KzDialogueAsset.h"

UKzDialogueAsset::UKzDialogueAsset()
{
}

int32 UKzDialogueAsset::IndexOfLine(const FGuid& LineId) const
{
	return Lines.IndexOfByPredicate([&LineId](const FKzDialogueLine& L) { return L.LineId == LineId; });
}

bool UKzDialogueAsset::TryGetLineById(const FGuid& InLineId, FKzDialogueLine& OutLine) const
{
	for (const FKzDialogueLine& Line : Lines)
	{
		if (Line.LineId == InLineId)
		{
			OutLine = Line;
			return true;
		}
	}
	return false;
}

bool UKzDialogueAsset::TryGetAliasByName(FName AliasName, FKzDialogueAlias& OutAlias) const
{
	if (AliasName.IsNone()) { return false; }
	for (const FKzDialogueAlias& Alias : Aliases)
	{
		if (Alias.AliasName == AliasName)
		{
			OutAlias = Alias;
			return true;
		}
	}
	return false;
}

bool UKzDialogueAsset::TryGetAliasById(const FGuid& InAliasId, FKzDialogueAlias& OutAlias) const
{
	if (!InAliasId.IsValid()) { return false; }
	for (const FKzDialogueAlias& Alias : Aliases)
	{
		if (Alias.AliasId == InAliasId)
		{
			OutAlias = Alias;
			return true;
		}
	}
	return false;
}

bool UKzDialogueAsset::TryResolveAlias(const FGuid& InAliasId, FKzDialogueLine& OutLine) const
{
	FKzDialogueAlias Alias;
	if (!TryGetAliasById(InAliasId, Alias)) { return false; }

	// Filter to LineIds that still exist in the asset (to gracefully ignore stale ids).
	TArray<int32> ValidIndices;
	ValidIndices.Reserve(Alias.LineIds.Num());
	for (int32 i = 0; i < Alias.LineIds.Num(); ++i)
	{
		for (const FKzDialogueLine& Line : Lines)
		{
			if (Line.LineId == Alias.LineIds[i])
			{
				ValidIndices.Add(i);
				break;
			}
		}
	}

	if (ValidIndices.Num() == 0) { return false; }

	const int32 PickIdx = FMath::RandRange(0, ValidIndices.Num() - 1);
	const FGuid PickedId = Alias.LineIds[ValidIndices[PickIdx]];
	return TryGetLineById(PickedId, OutLine);
}

bool UKzDialogueAsset::TryResolveAlias(FName AliasName, FKzDialogueLine& OutLine) const
{
	FKzDialogueAlias Alias;
	if (!TryGetAliasByName(AliasName, Alias)) { return false; }
	return TryResolveAlias(Alias.AliasId, OutLine);
}

bool UKzDialogueAsset::TryResolveLineOrAlias(const FGuid& Id, FKzDialogueLine& OutLine) const
{
	if (!Id.IsValid()) { return false; }

	// Try lines first (cheaper than alias resolution which loops twice).
	if (TryGetLineById(Id, OutLine)) { return true; }

	// Fall back to alias resolution. EnsureLineGuids guarantees Lines and Aliases
	// don't share GUIDs, so a hit here is unambiguous.
	return TryResolveAlias(Id, OutLine);
}

bool UKzDialogueAsset::TryResolveReference(const FKzDialogueAssetReference& Reference, FKzDialogueLine& OutLine) const
{
	if (!Reference.IsValid()) { return false; }
	return Reference.bIsAlias
		? TryResolveAlias(Reference.Id, OutLine)
		: TryGetLineById(Reference.Id, OutLine);
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

	// Lines
	{
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
	}

	// Aliases
	{
		TSet<FGuid> Seen;
		Seen.Reserve(Aliases.Num());
		for (FKzDialogueAlias& Alias : Aliases)
		{
			if (!Alias.AliasId.IsValid() || Seen.Contains(Alias.AliasId))
			{
				Alias.AliasId = FGuid::NewGuid();
				bDirty = true;
			}
			Seen.Add(Alias.AliasId);
		}
	}

	if (bDirty)
	{
		MarkPackageDirty();
	}
}

#endif