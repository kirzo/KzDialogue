// Copyright 2026 kirzo

#include "KzDialogueProvider.h"
#include "KzDialogueAsset.h"

// =======================================================================================
// UKzAssetDialogueProvider
// =======================================================================================

UKzAssetDialogueProvider* UKzAssetDialogueProvider::Create(UObject* Outer, UKzDialogueAsset* InAsset)
{
	if (!IsValid(Outer) || !IsValid(InAsset))
	{
		return nullptr;
	}
	UKzAssetDialogueProvider* Provider = NewObject<UKzAssetDialogueProvider>(Outer);
	Provider->Asset = InAsset;
	return Provider;
}

bool UKzAssetDialogueProvider::HasNext_Implementation() const
{
	if (!IsValid(Asset) || Asset->Lines.Num() == 0)
	{
		return false;
	}
	const int32 NextIndex = (CursorIndex == INDEX_NONE) ? ResolveStartIndex() : CursorIndex + 1;
	return Asset->Lines.IsValidIndex(NextIndex) && NextIndex <= ResolveEndIndex();
}

FKzDialogueLine UKzAssetDialogueProvider::Advance_Implementation()
{
	if (!IsValid(Asset)) { return {}; }
	CursorIndex = (CursorIndex == INDEX_NONE) ? ResolveStartIndex() : CursorIndex + 1;
	return Asset->Lines.IsValidIndex(CursorIndex) ? Asset->Lines[CursorIndex] : FKzDialogueLine{};
}

FKzDialogueLine UKzAssetDialogueProvider::Current_Implementation() const
{
	if (!IsValid(Asset) || !Asset->Lines.IsValidIndex(CursorIndex)) { return {}; }
	return Asset->Lines[CursorIndex];
}

void UKzAssetDialogueProvider::Reset_Implementation()
{
	Super::Reset_Implementation();
}

int32 UKzAssetDialogueProvider::ResolveStartIndex() const
{
	if (!IsValid(Asset)) { return 0; }
	if (StartLineId.IsValid())
	{
		const int32 Index = Asset->IndexOfLine(StartLineId);
		if (Index != INDEX_NONE) { return Index; }
	}
	return 0;
}

int32 UKzAssetDialogueProvider::ResolveEndIndex() const
{
	if (!IsValid(Asset)) { return 0; }
	if (EndLineId.IsValid())
	{
		const int32 Index = Asset->IndexOfLine(EndLineId);
		if (Index != INDEX_NONE) { return Index; }
	}
	return Asset->Lines.Num() - 1;
}

// =======================================================================================
// UKzManualDialogueProvider
// =======================================================================================

UKzManualDialogueProvider* UKzManualDialogueProvider::Create(UObject* Outer, const FKzDialogueLine& InLine)
{
	if (!IsValid(Outer)) { return nullptr; }
	UKzManualDialogueProvider* Provider = NewObject<UKzManualDialogueProvider>(Outer);
	Provider->Line = InLine;
	return Provider;
}

void UKzManualDialogueProvider::SetLine(const FKzDialogueLine& InLine)
{
	Line = InLine;
	bConsumed = false;
	CursorIndex = INDEX_NONE;
}

FKzDialogueLine UKzManualDialogueProvider::Advance_Implementation()
{
	bConsumed = true;
	CursorIndex = 0;
	return Line;
}

// =======================================================================================
// UKzLineListDialogueProvider
// =======================================================================================

UKzLineListDialogueProvider* UKzLineListDialogueProvider::Create(UObject* Outer, const TArray<FKzDialogueLine>& InLines)
{
	if (!IsValid(Outer)) { return nullptr; }
	UKzLineListDialogueProvider* Provider = NewObject<UKzLineListDialogueProvider>(Outer);
	Provider->Lines = InLines;
	return Provider;
}

FKzDialogueLine UKzLineListDialogueProvider::Advance_Implementation()
{
	CursorIndex = (CursorIndex == INDEX_NONE) ? 0 : CursorIndex + 1;
	return Lines.IsValidIndex(CursorIndex) ? Lines[CursorIndex] : FKzDialogueLine{};
}

FKzDialogueLine UKzLineListDialogueProvider::Current_Implementation() const
{
	return Lines.IsValidIndex(CursorIndex) ? Lines[CursorIndex] : FKzDialogueLine{};
}