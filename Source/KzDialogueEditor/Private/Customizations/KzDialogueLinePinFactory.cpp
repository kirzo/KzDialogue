// Copyright 2026 kirzo

#include "Customizations/KzDialogueLinePinFactory.h"
#include "Widgets/SKzDialogueLinePicker.h"

#include "K2Node_PlayDialogueLine.h"
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
	UK2Node_PlayDialogueLine* GetOwningNode() const
	{
		return GraphPinObj ? Cast<UK2Node_PlayDialogueLine>(GraphPinObj->GetOwningNode()) : nullptr;
	}

	UKzDialogueAsset* GetCurrentAsset() const
	{
		const UK2Node_PlayDialogueLine* Node = GetOwningNode();
		return Node ? Node->GetLiteralAsset() : nullptr;
	}

	bool IsDropdownAvailable() const
	{
		const UK2Node_PlayDialogueLine* Node = GetOwningNode();
		// Disabled when:
		//   - Asset pin is connected (LineId must come from caller).
		//   - LineId pin itself is connected (caller is providing the GUID).
		//   - No literal asset is selected.
		if (!Node || !Node->ShouldShowLineDropdown()) { return false; }
		if (GraphPinObj && GraphPinObj->LinkedTo.Num() > 0) { return false; }
		return true;
	}

	FText GetCurrentLineLabel() const
	{
		if (!GraphPinObj) { return LOCTEXT("InvalidPin", "(invalid)"); }

		const UK2Node_PlayDialogueLine* Node = GetOwningNode();
		if (!Node) { return LOCTEXT("InvalidNode", "(invalid)"); }

		if (!Node->ShouldShowLineDropdown())
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

		UKzDialogueAsset* Asset = GetCurrentAsset();
		if (!Asset || !Id.IsValid())
		{
			return LOCTEXT("PickLineOrAlias", "Select line or alias...");
		}

		// Prefer line lookup (line GUIDs are typically more numerous).
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

		// The pin is FGuid-typed; we only persist the id. The runtime decides whether
		// the GUID maps to a line or an alias.
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
	if (!InPin) { return nullptr; }

	// Only intercept the LineId pin of our specific K2Node.
	UK2Node_PlayDialogueLine* Node = Cast<UK2Node_PlayDialogueLine>(InPin->GetOwningNode());
	if (!Node) { return nullptr; }
	if (InPin->PinName != UK2Node_PlayDialogueLine::PN_LineId) { return nullptr; }

	return SNew(SKzDialogueLineGraphPin, InPin);
}

#undef LOCTEXT_NAMESPACE