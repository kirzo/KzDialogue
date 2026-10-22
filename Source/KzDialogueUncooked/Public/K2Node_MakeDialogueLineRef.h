// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "K2Node.h"
#include "K2Node_MakeDialogueLineRef.generated.h"

/**
 * Custom Blueprint node that builds an FKzDialogueLineRef at design time.
 *
 * The Asset pin can either be connected (runtime soft reference, the LineId pin must
 * also be cabled) or left unconnected with a literal asset selected via the details
 * panel, in which case a custom dropdown appears in the LineId pin allowing the user
 * to pick a line or alias at design time.
 */
UCLASS()
class KZDIALOGUEUNCOOKED_API UK2Node_MakeDialogueLineRef : public UK2Node
{
	GENERATED_BODY()

public:
	//~ UEdGraphNode
	virtual void AllocateDefaultPins() override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;
	virtual FSlateIcon GetIconAndTint(FLinearColor& OutColor) const override;

	//~ UK2Node
	virtual void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;
	virtual FText GetMenuCategory() const override;
	virtual void ExpandNode(class FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph) override;
	virtual bool IsNodePure() const override { return true; }

	/** Pin name constant; Asset and LineId live in KzDialogueK2Nodes. */
	static const FName PN_ReturnValue;
};