// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "K2Node_KzCancellableAsyncTask.h"
#include "K2Node_PlayDialogueLineAsync.generated.h"

/**
 * Async variant of UK2Node_PlayDialogueLine: same inline line/alias dropdown (driven by the
 * shared pin factory), but expands to UKzAsyncPlayDialogueLineInline, so the node exposes
 * Started / Finished / Cancelled pins and completes when the picked line ends. Its Asset and
 * LineId pins come from the factory function's parameters.
 */
UCLASS()
class KZDIALOGUEUNCOOKED_API UK2Node_PlayDialogueLineAsync : public UK2Node_KzCancellableAsyncTask
{
	GENERATED_BODY()

public:
	UK2Node_PlayDialogueLineAsync(const FObjectInitializer& ObjectInitializer);

	virtual void AllocateDefaultPins() override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;
	virtual FSlateIcon GetIconAndTint(FLinearColor& OutColor) const override;
	virtual FText GetMenuCategory() const override;
};