// Copyright 2026 kirzo

#include "KzDialogueProvider.h"

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
}

FKzDialogueLine UKzManualDialogueProvider::Advance_Implementation()
{
	bConsumed = true;
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