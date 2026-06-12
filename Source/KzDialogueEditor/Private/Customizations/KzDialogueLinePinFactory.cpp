// Copyright 2026 kirzo

#include "Customizations/KzDialogueLinePinFactory.h"
#include "Widgets/SKzDialogueLinePicker.h"

#include "K2Node_PlayDialogueLine.h"
#include "K2Node_PlayDialogueLineAsync.h"
#include "K2Node_MakeDialogueLineRef.h"
#include "K2Node_MakeDialogueLineList.h"
#include "KzDialogueAsset.h"

#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "SGraphPin.h"

#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "KzDialogueLinePinFactory"

namespace KzDialogueLinePinFactoryInternal
{
	/**
	 * Resolves a node into its dialogue-aware accessors. Returns true if the node is
	 * one of our K2Nodes and fills the out parameters; false otherwise.
	 *
	 * We pattern-match here instead of introducing a shared interface because UE's
	 * K2Node hierarchy doesn't play well with multiple inheritance, and the surface
	 * area we need is tiny (two queries).
	 */
	static bool TryResolveDialogueNode(UEdGraphNode* Node, bool& bOutShowDropdown, UKzDialogueAsset*& OutLiteralAsset)
	{
		if (UK2Node_PlayDialogueLine* Play = Cast<UK2Node_PlayDialogueLine>(Node))
		{
			bOutShowDropdown = Play->ShouldShowLineDropdown();
			OutLiteralAsset = Play->GetLiteralAsset();
			return true;
		}

		if (UK2Node_MakeDialogueLineRef* Make = Cast<UK2Node_MakeDialogueLineRef>(Node))
		{
			bOutShowDropdown = Make->ShouldShowLineDropdown();
			OutLiteralAsset = Make->GetLiteralAsset();
			return true;
		}

		if (UK2Node_PlayDialogueLineAsync* PlayAsync = Cast<UK2Node_PlayDialogueLineAsync>(Node))
		{
			bOutShowDropdown = PlayAsync->ShouldShowLineDropdown();
			OutLiteralAsset = PlayAsync->GetLiteralAsset();
			return true;
		}

		if (UK2Node_MakeDialogueLineList* MakeList = Cast<UK2Node_MakeDialogueLineList>(Node))
		{
			bOutShowDropdown = MakeList->ShouldShowLineDropdown();
			OutLiteralAsset = MakeList->GetLiteralAsset();
			return true;
		}

		return false;
	}

	/** True when Pin is a line-id pin of one of our K2Nodes (some nodes carry several). */
	static bool IsLineIdPin(UEdGraphNode* Node, const UEdGraphPin* Pin)
	{
		if (!Pin) { return false; }

		if (Cast<UK2Node_PlayDialogueLine>(Node)) { return Pin->PinName == UK2Node_PlayDialogueLine::PN_LineId; }
		if (Cast<UK2Node_MakeDialogueLineRef>(Node)) { return Pin->PinName == UK2Node_MakeDialogueLineRef::PN_LineId; }
		if (Cast<UK2Node_PlayDialogueLineAsync>(Node)) { return Pin->PinName == UK2Node_PlayDialogueLineAsync::PN_LineId; }
		if (const UK2Node_MakeDialogueLineList* MakeList = Cast<UK2Node_MakeDialogueLineList>(Node)) { return MakeList->IsLinePin(Pin); }

		return false;
	}
}

// =======================================================================================
// Custom SGraphPin
// =======================================================================================

class SKzDialogueLineGraphPin : public SGraphPin
{
public:
	SLATE_BEGIN_ARGS(SKzDialogueLineGraphPin) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UEdGraphPin* InPin)
	{
		SGraphPin::Construct(SGraphPin::FArguments(), InPin);
	}

protected:
	virtual TSharedRef<SWidget> GetDefaultValueWidget() override
	{
		return SNew(SBox)
			.WidthOverride(220.f)
			[
				SNew(SComboButton)
					.OnGetMenuContent(this, &SKzDialogueLineGraphPin::BuildPickerContent)
					.ContentPadding(FMargin(2.f))
					.IsEnabled(this, &SKzDialogueLineGraphPin::IsDropdownAvailable)
					.ButtonContent()
					[
						SNew(STextBlock)
							.Text(this, &SKzDialogueLineGraphPin::GetCurrentLineLabel)
							.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
					]
			];
	}

private:
	bool ResolveNodeState(bool& bOutShowDropdown, UKzDialogueAsset*& OutLiteralAsset) const
	{
		using namespace KzDialogueLinePinFactoryInternal;
		if (!GraphPinObj) { return false; }
		return TryResolveDialogueNode(GraphPinObj->GetOwningNode(), bOutShowDropdown, OutLiteralAsset);
	}

	UKzDialogueAsset* GetCurrentAsset() const
	{
		bool bDropdown = false;
		UKzDialogueAsset* Asset = nullptr;
		ResolveNodeState(bDropdown, Asset);
		return Asset;
	}

	bool IsDropdownAvailable() const
	{
		bool bDropdown = false;
		UKzDialogueAsset* Asset = nullptr;
		if (!ResolveNodeState(bDropdown, Asset)) { return false; }

		// Disabled when:
		//   - Asset pin is connected (LineId must come from caller).
		//   - LineId pin itself is connected (caller is providing the GUID).
		//   - No literal asset is selected.
		if (!bDropdown) { return false; }
		if (GraphPinObj && GraphPinObj->LinkedTo.Num() > 0) { return false; }
		return true;
	}

	FText GetCurrentLineLabel() const
	{
		if (!GraphPinObj) { return LOCTEXT("InvalidPin", "(invalid)"); }

		bool bDropdown = false;
		UKzDialogueAsset* Asset = nullptr;
		if (!ResolveNodeState(bDropdown, Asset))
		{
			return LOCTEXT("InvalidNode", "(invalid)");
		}

		if (!bDropdown)
		{
			if (GraphPinObj->LinkedTo.Num() > 0)
			{
				return LOCTEXT("ConnectedHint", "Connected — set externally");
			}
			const FString DefaultStr = GraphPinObj->GetDefaultAsString();
			return DefaultStr.IsEmpty()
				? LOCTEXT("NoLineIdHint", "Set a Line / Alias")
				: FText::FromString(DefaultStr);
		}

		FGuid Id;
		FGuid::Parse(GraphPinObj->GetDefaultAsString(), Id);

		if (!Asset || !Id.IsValid())
		{
			return LOCTEXT("PickLineOrAlias", "Select line or alias...");
		}

		FKzDialogueLine Line;
		if (Asset->TryGetLineById(Id, Line))
		{
			return Line.GetDisplayLabel();
		}

		FKzDialogueAlias Alias;
		if (Asset->TryGetAliasById(Id, Alias))
		{
			return Alias.GetDisplayLabel();
		}

		return LOCTEXT("UnknownEntry", "(unknown)");
	}

	TSharedRef<SWidget> BuildPickerContent()
	{
		UKzDialogueAsset* Asset = GetCurrentAsset();
		if (!Asset)
		{
			return SNew(SBox).Padding(8.f)
				[
					SNew(STextBlock).Text(LOCTEXT("NoAsset", "No literal asset selected."))
				];
		}

		return SNew(SBox).WidthOverride(320.f).HeightOverride(360.f)
			[
				SNew(SKzDialogueLinePicker)
					.Asset(Asset)
					.OnEntryPicked(SKzDialogueLinePicker::FOnEntryPicked::CreateSP(
						this, &SKzDialogueLineGraphPin::OnEntryPicked))
			];
	}

	void OnEntryPicked(FKzDialogueAssetReference InRef, float /*Duration*/)
	{
		if (!GraphPinObj || !InRef.IsValid()) { return; }

		const FString NewValue = InRef.Id.ToString(EGuidFormats::Digits);
		if (NewValue == GraphPinObj->GetDefaultAsString()) { return; }

		const FScopedTransaction Transaction(LOCTEXT("PickLineTransaction", "Select dialogue line/alias"));
		GraphPinObj->Modify();
		GraphPinObj->GetSchema()->TrySetDefaultValue(*GraphPinObj, NewValue);

		FSlateApplication::Get().DismissAllMenus();
	}
};

// =======================================================================================
// Factory
// =======================================================================================

TSharedPtr<SGraphPin> FKzDialogueLinePinFactory::CreatePin(UEdGraphPin* InPin) const
{
	using namespace KzDialogueLinePinFactoryInternal;

	if (!InPin) { return nullptr; }

	// Only intercept the line-id pins of our K2Nodes.
	if (!IsLineIdPin(InPin->GetOwningNode(), InPin)) { return nullptr; }

	return SNew(SKzDialogueLineGraphPin, InPin);
}

#undef LOCTEXT_NAMESPACE