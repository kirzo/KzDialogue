// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "K2Node.h"
#include "K2Node_AddPinInterface.h"
#include "K2Node_MakeDialogueLineList.generated.h"

class UKzDialogueAsset;

/**
 * Custom Blueprint node that builds an FKzDialogueLineList at design time: one Asset plus a
 * variable number of line pins (add/remove via the node's context menu).
 *
 * Like UK2Node_MakeDialogueLineRef: when the Asset pin is unconnected and a literal asset is
 * selected, every line pin shows a dropdown of the asset's lines and aliases.
 */
UCLASS()
class KZDIALOGUEUNCOOKED_API UK2Node_MakeDialogueLineList : public UK2Node, public IK2Node_AddPinInterface
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
	virtual void GetNodeContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const override;

	//~ UK2Node
	virtual void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;
	virtual FText GetMenuCategory() const override;
	virtual void ExpandNode(class FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph) override;
	virtual bool IsNodePure() const override { return true; }

	//~ IK2Node_AddPinInterface
	virtual void AddInputPin() override;
	virtual bool CanAddPin() const override { return true; }

	/** Pin name constants. */
	static const FName PN_Asset;
	static const FName PN_ReturnValue;

	/** Name of the line pin at Index ("Line_0", "Line_1", ...). */
	static FName MakeLinePinName(int32 Index);

	/**
	 * True when the Asset pin has no connection and a valid literal asset is set,
	 * meaning the line pins should display the dropdown.
	 */
	bool ShouldShowLineDropdown() const;

	/** Returns the literal asset currently assigned to the Asset pin, or nullptr. */
	UKzDialogueAsset* GetLiteralAsset() const;

	/** True when Pin is one of this node's line input pins. */
	bool IsLinePin(const UEdGraphPin* Pin) const;

	/** Pin accessors used by the pin factory and the compiler. */
	UEdGraphPin* GetAssetPin() const;

	/** Collects the line pins in index order. */
	void GetLinePins(TArray<UEdGraphPin*>& OutPins) const;

	/** Removes one line pin and renumbers the rest. */
	void RemoveLinePin(UEdGraphPin* Pin);

private:
	/** Number of line input pins; serialized so reconstruction keeps the layout. */
	UPROPERTY()
	int32 NumLinePins = 1;
};