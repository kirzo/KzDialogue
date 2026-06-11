// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "K2Node_BaseAsyncTask.h"
#include "K2Node_PlayDialogueLineAsync.generated.h"

class UKzDialogueAsset;

/**
 * Async variant of UK2Node_PlayDialogueLine: same inline line/alias dropdown (driven by the
 * shared pin factory), but expands to UKzAsyncPlayDialogueLineInline, so the node exposes
 * Started / Finished / Cancelled pins and completes when the picked line ends.
 */
UCLASS()
class KZDIALOGUEUNCOOKED_API UK2Node_PlayDialogueLineAsync : public UK2Node_BaseAsyncTask
{
	GENERATED_BODY()

public:
	UK2Node_PlayDialogueLineAsync(const FObjectInitializer& ObjectInitializer);

	virtual void AllocateDefaultPins() override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;
	virtual FSlateIcon GetIconAndTint(FLinearColor& OutColor) const override;
	virtual FText GetMenuCategory() const override;

	virtual void PinConnectionListChanged(UEdGraphPin* Pin) override;
	virtual void PinDefaultValueChanged(UEdGraphPin* Pin) override;

	/** Pin names, matching the factory function's parameter names. */
	static const FName PN_Asset;
	static const FName PN_LineId;
	static const FName PN_Channel;

	/** Queries used by the shared line pin factory (mirrors UK2Node_PlayDialogueLine). */
	bool ShouldShowLineDropdown() const;
	UKzDialogueAsset* GetLiteralAsset() const;
	UEdGraphPin* GetAssetPin() const;
	UEdGraphPin* GetLineIdPin() const;
};