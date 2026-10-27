// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "K2Node_BaseAsyncTask.h"
#include "K2Node_KzDialogueGraphAsyncAction.generated.h"

/**
 * Play Dialogue Graph and Resume Dialogue Graph, with the dialogue graph's icon instead of the auto-generated
 * UK2Node_AsyncAction's. No Stop or Interrupt pins: a graph play stops through its instance.
 */
UCLASS()
class KZDIALOGUEUNCOOKED_API UK2Node_KzDialogueGraphAsyncAction : public UK2Node_BaseAsyncTask
{
	GENERATED_BODY()

public:
	UK2Node_KzDialogueGraphAsyncAction(const FObjectInitializer& ObjectInitializer);

	virtual FSlateIcon GetIconAndTint(FLinearColor& OutColor) const override;
	virtual FText GetMenuCategory() const override;
	virtual void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;

	/** One node class serves both factories: the factory tells the palette entries (favorites) apart. */
	virtual FBlueprintNodeSignature GetSignature() const override;
};