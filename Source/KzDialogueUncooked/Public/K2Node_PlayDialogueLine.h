// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "K2Node.h"
#include "K2Node_PlayDialogueLine.generated.h"

class UKzDialogueAsset;

/**
 * Custom Blueprint node that plays a single line from a UKzDialogueAsset.
 *
 * The Asset pin can either be connected (runtime asset, falls back to a generic
 * function call that resolves the line by GUID at runtime) or left unconnected with
 * a literal asset selected via the details panel — in which case a custom dropdown
 * appears in the LineId pin allowing the user to pick a line at design time.
 */
UCLASS()
class KZDIALOGUEUNCOOKED_API UK2Node_PlayDialogueLine : public UK2Node
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
	virtual bool IsNodePure() const override { return false; }

	/** Pin name constants. */
	static const FName PN_Asset;
	static const FName PN_LineId;
	static const FName PN_Channel;
	static const FName PN_Priority;
	static const FName PN_StartImmediately;
	static const FName PN_WorldContext;
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

private:
	/** Resolve the GUID currently stored in the LineId pin's default value. */
	FGuid GetLineIdValue() const;
};