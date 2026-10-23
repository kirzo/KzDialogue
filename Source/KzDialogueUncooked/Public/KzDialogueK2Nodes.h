// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "Textures/SlateIcon.h"

class UEdGraphNode;
class UEdGraphPin;
class UKzDialogueAsset;

/** What the dialogue K2 nodes share. The line-picking ones differ only in a hard or soft Asset pin. */
namespace KzDialogueK2Nodes
{
	inline const FName PN_Asset(TEXT("Asset"));
	inline const FName PN_LineId(TEXT("LineId"));

	/** The literal asset set on Node's unconnected Asset pin, or null. A soft pin loads its path synchronously, so editor use only. */
	KZDIALOGUEUNCOOKED_API UKzDialogueAsset* GetLiteralAsset(const UEdGraphNode* Node);

	/** True when Pin is a line pin of a line-picking node: LineId, or a Line List's Line_N. */
	KZDIALOGUEUNCOOKED_API bool IsLineIdPin(const UEdGraphPin* Pin);

	/** After ChangedPin changed on Node: when it is the Asset pin, clears line pins holding ids the new literal asset lacks (all of them once the asset is connected). */
	KZDIALOGUEUNCOOKED_API void ClearStaleLineIds(UEdGraphNode* Node, const UEdGraphPin* ChangedPin);

	/** The dialogue asset icon and tint every dialogue node shows. */
	KZDIALOGUEUNCOOKED_API FSlateIcon GetIcon(FLinearColor& OutColor);
}