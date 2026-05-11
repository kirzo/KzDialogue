// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "K2Node.h"
#include "K2Node_MakeDialogueLineRef.generated.h"

class UKzDialogueAsset;

/**
 * Custom Blueprint node that builds an FKzDialogueLineRef at design time.
 *
 * The Asset pin can either be connected (runtime soft reference, the LineId pin must
 * also be cabled) or left unconnected with a literal asset selected via the details
 * panel — in which case a custom dropdown appears in the LineId pin allowing the user
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
	virtual void PinConnectionListChanged(UEdGraphPin* Pin) override;
	virtual void PinDefaultValueChanged(UEdGraphPin* Pin) override;

	//~ UK2Node
	virtual void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;
	virtual FText GetMenuCategory() const override;
	virtual void ExpandNode(class FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph) override;
	virtual bool IsNodePure() const override { return true; }

	/** Pin name constants. */
	static const FName PN_Asset;
	static const FName PN_LineId;
	static const FName PN_ReturnValue;

	/**
	 * True when the Asset pin has no connection and a valid literal asset is set,
	 * meaning the LineId pin should display the dropdown.
	 */
	bool ShouldShowLineDropdown() const;

	/** Returns the literal asset currently assigned to the Asset pin, or nullptr. */
	UKzDialogueAsset* GetLiteralAsset() const;

	/** Pin accessors used by the pin factory and the compiler. */
	UEdGraphPin* GetAssetPin() const;
	UEdGraphPin* GetLineIdPin() const;
};