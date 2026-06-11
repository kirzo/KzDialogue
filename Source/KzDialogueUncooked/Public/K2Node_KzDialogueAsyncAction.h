// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "K2Node_BaseAsyncTask.h"
#include "K2Node_KzDialogueAsyncAction.generated.h"

/**
 * Async node for the dialogue async actions that don't need the inline line dropdown
 * (Play Dialogue Line (Async) by ref and Play Dialogue Line List (Async)). Exists instead of
 * the auto-generated UK2Node_AsyncAction so the nodes carry the dialogue icon and styling;
 * the proxy factory is assigned per spawner in GetMenuActions.
 */
UCLASS()
class KZDIALOGUEUNCOOKED_API UK2Node_KzDialogueAsyncAction : public UK2Node_BaseAsyncTask
{
	GENERATED_BODY()

public:
	UK2Node_KzDialogueAsyncAction(const FObjectInitializer& ObjectInitializer);

	virtual void AllocateDefaultPins() override;
	virtual FSlateIcon GetIconAndTint(FLinearColor& OutColor) const override;
	virtual FText GetMenuCategory() const override;
	virtual void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;
};