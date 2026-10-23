// Copyright 2026 kirzo

#include "Customizations/KzDialogueLinePinFactory.h"
#include "Widgets/SKzDialogueLinePicker.h"
#include "Widgets/SKzTokenPicker.h"

#include "K2Node_CallFunction.h"
#include "KzDialogueK2Nodes.h"
#include "KzDialogueAsset.h"
#include "KzNamedAsset.h"

#include "AssetRegistry/AssetRegistryModule.h"

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
	/** Named-token pin opt-in metadata: KzTokenPin names the token parameter, KzTokenPartPin a part parameter whose choices depend on the token pin's value. */
	static const FName MD_KzTokenPin(TEXT("KzTokenPin"));
	static const FName MD_KzTokenPartPin(TEXT("KzTokenPartPin"));

	/** The function called by the pin's owning node, or null when the node is not a call. */
	static const UFunction* GetCalledFunction(const UEdGraphPin* Pin)
	{
		const UK2Node_CallFunction* Call = Pin ? Cast<UK2Node_CallFunction>(Pin->GetOwningNode()) : nullptr;
		return Call ? Call->GetTargetFunction() : nullptr;
	}

	/** True when Pin is the parameter the called function's MetaKey metadata names. */
	static bool IsMetaMarkedPin(const UEdGraphPin* Pin, const FName MetaKey)
	{
		if (!Pin || Pin->Direction != EGPD_Input || Pin->ParentPin) { return false; }

		const UFunction* Function = GetCalledFunction(Pin);
		return Function && Function->GetMetaData(MetaKey) == Pin->PinName.ToString();
	}

	/** The named asset claiming Token, loaded, or null. Same registry discovery as the runtime lookup. */
	static const UKzNamedAsset* FindNamedAssetByToken(const FString& Token)
	{
		if (Token.IsEmpty()) { return nullptr; }

		const IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
		TArray<FAssetData> NamedAssets;
		Registry.GetAssetsByClass(UKzNamedAsset::StaticClass()->GetClassPathName(), NamedAssets, /*bSearchSubClasses=*/true);
		for (const FAssetData& Data : NamedAssets)
		{
			FName AssetToken;
			if (Data.GetTagValue(GET_MEMBER_NAME_CHECKED(UKzNamedAsset, Token), AssetToken) && AssetToken == FName(*Token))
			{
				return Cast<UKzNamedAsset>(Data.ToSoftObjectPath().TryLoad());
			}
		}
		return nullptr;
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
	/** The literal asset of the owning node, or null when the Asset pin is connected or empty. */
	UKzDialogueAsset* GetCurrentAsset() const
	{
		return GraphPinObj ? KzDialogueK2Nodes::GetLiteralAsset(GraphPinObj->GetOwningNode()) : nullptr;
	}

	/** Needs a literal asset and an unconnected line pin. */
	bool IsDropdownAvailable() const
	{
		return GetCurrentAsset() && GraphPinObj->LinkedTo.Num() == 0;
	}

	FText GetCurrentLineLabel() const
	{
		if (!GraphPinObj) { return LOCTEXT("InvalidPin", "(invalid)"); }

		UKzDialogueAsset* Asset = GetCurrentAsset();
		if (!Asset)
		{
			if (GraphPinObj->LinkedTo.Num() > 0)
			{
				return LOCTEXT("ConnectedHint", "Connected: set externally");
			}
			// Without a literal asset there is nothing to pick from: the id has to come wired in.
			const UEdGraphPin* AssetPin = GraphPinObj->GetOwningNode()->FindPin(KzDialogueK2Nodes::PN_Asset, EGPD_Input);
			return AssetPin && AssetPin->LinkedTo.Num() > 0
				? LOCTEXT("AssetConnectedHint", "Asset is connected: wire the line id")
				: LOCTEXT("NoAssetHint", "Pick an asset first");
		}

		FGuid Id;
		FGuid::Parse(GraphPinObj->GetDefaultAsString(), Id);

		if (!Id.IsValid())
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
// Named-token SGraphPin
// =======================================================================================

class SKzTokenGraphPin : public SGraphPin
{
public:
	SLATE_BEGIN_ARGS(SKzTokenGraphPin) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UEdGraphPin* InPin)
	{
		SGraphPin::Construct(SGraphPin::FArguments(), InPin);
	}

protected:
	virtual TSharedRef<SWidget> GetDefaultValueWidget() override
	{
		return SNew(SBox)
			.WidthOverride(150.f)
			[
				SAssignNew(ComboButton, SComboButton)
					.OnGetMenuContent(this, &SKzTokenGraphPin::BuildPickerContent)
					.ContentPadding(FMargin(2.f))
					.ButtonContent()
					[
						SNew(STextBlock)
							.Text(this, &SKzTokenGraphPin::GetCurrentTokenLabel)
							.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
					]
			];
	}

private:
	TSharedPtr<SComboButton> ComboButton;

	FText GetCurrentTokenLabel() const
	{
		const FString DefaultStr = GraphPinObj ? GraphPinObj->GetDefaultAsString() : FString();
		return DefaultStr.IsEmpty() ? LOCTEXT("PickToken", "Select token...") : FText::FromString(DefaultStr);
	}

	TSharedRef<SWidget> BuildPickerContent()
	{
		// Name pins hold a bare token, so parts would not fit; string pins take "Token:part".
		TSharedRef<SKzTokenPicker> Picker = SNew(SKzTokenPicker)
			.bBaseTokensOnly(!IsStringPin())
			.OnTokenChosen(FOnKzTokenChosen::CreateSP(this, &SKzTokenGraphPin::OnTokenChosen));
		if (ComboButton.IsValid())
		{
			ComboButton->SetMenuContentWidgetToFocus(Picker->GetWidgetToFocus());
		}

		// The picker sizes itself.
		return Picker;
	}

	void OnTokenChosen(const FString& TokenText)
	{
		if (!GraphPinObj) { return; }

		// The picker hands over the insertable form ("{Kirzo}" / "{Kirzo:given}").
		FString Inner = TokenText.TrimStartAndEnd();
		if (Inner.StartsWith(TEXT("{")) && Inner.EndsWith(TEXT("}"))) { Inner = Inner.Mid(1, Inner.Len() - 2); }

		const FScopedTransaction Transaction(LOCTEXT("PickTokenTransaction", "Select named token"));
		GraphPinObj->Modify();
		GraphPinObj->GetSchema()->TrySetDefaultValue(*GraphPinObj, Inner);

		FSlateApplication::Get().DismissAllMenus();
	}

	bool IsStringPin() const
	{
		return GraphPinObj && GraphPinObj->PinType.PinCategory == UEdGraphSchema_K2::PC_String;
	}
};

// =======================================================================================
// Named-token part SGraphPin
// =======================================================================================

class SKzTokenPartGraphPin : public SGraphPin
{
public:
	SLATE_BEGIN_ARGS(SKzTokenPartGraphPin) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UEdGraphPin* InPin)
	{
		SGraphPin::Construct(SGraphPin::FArguments(), InPin);
	}

protected:
	virtual TSharedRef<SWidget> GetDefaultValueWidget() override
	{
		return SNew(SBox)
			.WidthOverride(120.f)
			[
				SAssignNew(ComboButton, SComboButton)
					.OnGetMenuContent(this, &SKzTokenPartGraphPin::BuildPartsMenu)
					.ContentPadding(FMargin(2.f))
					.ButtonContent()
					[
						SNew(STextBlock)
							.Text(this, &SKzTokenPartGraphPin::GetCurrentPartLabel)
							.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
					]
			];
	}

private:
	FText GetCurrentPartLabel() const
	{
		const FString DefaultStr = GraphPinObj ? GraphPinObj->GetDefaultAsString() : FString();
		return DefaultStr.IsEmpty() ? LOCTEXT("PickPart", "Select part...") : FText::FromString(DefaultStr);
	}

	/** The sibling pin the function's KzTokenPin metadata names, holding the token this part belongs to. */
	UEdGraphPin* FindTokenPin() const
	{
		using namespace KzDialogueLinePinFactoryInternal;

		const UFunction* Function = GetCalledFunction(GraphPinObj);
		const FString TokenPinName = Function ? Function->GetMetaData(MD_KzTokenPin) : FString();
		return TokenPinName.IsEmpty() ? nullptr : GraphPinObj->GetOwningNode()->FindPin(FName(*TokenPinName), EGPD_Input);
	}

	TSharedPtr<SComboButton> ComboButton;

	/** The parts menu is built on open, so it always reflects the token pin's current value. It is the token picker itself in parts mode, so the two dropdowns share one look. */
	TSharedRef<SWidget> BuildPartsMenu()
	{
		using namespace KzDialogueLinePinFactoryInternal;

		auto Hint = [](const FText& Text) -> TSharedRef<SWidget>
		{
			return SNew(SBox).Padding(8.f)
				[
					SNew(STextBlock).Text(Text).ColorAndOpacity(FSlateColor::UseSubduedForeground())
				];
		};

		const UEdGraphPin* TokenPin = FindTokenPin();
		if (!TokenPin) { return Hint(LOCTEXT("NoTokenPin", "No token pin on this node.")); }
		if (TokenPin->LinkedTo.Num() > 0) { return Hint(LOCTEXT("TokenConnected", "Token is connected: type the part manually.")); }

		const FString Token = TokenPin->GetDefaultAsString();
		if (Token.IsEmpty()) { return Hint(LOCTEXT("NoTokenValue", "Pick a token first.")); }

		const UKzNamedAsset* Named = FindNamedAssetByToken(Token);
		if (!Named) { return Hint(FText::Format(LOCTEXT("UnknownToken", "No named asset claims '{0}'."), FText::FromString(Token))); }

		TSharedRef<SKzTokenPicker> Picker = SNew(SKzTokenPicker)
			.PartsAsset(Named)
			.OnTokenChosen(FOnKzTokenChosen::CreateSP(this, &SKzTokenPartGraphPin::OnPartChosen));
		if (ComboButton.IsValid())
		{
			ComboButton->SetMenuContentWidgetToFocus(Picker->GetWidgetToFocus());
		}
		return Picker;
	}

	void OnPartChosen(const FString& Part)
	{
		if (!GraphPinObj) { return; }

		const FScopedTransaction Transaction(LOCTEXT("PickPartTransaction", "Select token part"));
		GraphPinObj->Modify();
		GraphPinObj->GetSchema()->TrySetDefaultValue(*GraphPinObj, Part);

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

	// Line-id pins of our K2Nodes.
	if (KzDialogueK2Nodes::IsLineIdPin(InPin)) { return SNew(SKzDialogueLineGraphPin, InPin); }

	// Token and part parameters of functions marked with KzTokenPin / KzTokenPartPin metadata.
	if (IsMetaMarkedPin(InPin, MD_KzTokenPin)) { return SNew(SKzTokenGraphPin, InPin); }
	if (IsMetaMarkedPin(InPin, MD_KzTokenPartPin)) { return SNew(SKzTokenPartGraphPin, InPin); }

	return nullptr;
}

#undef LOCTEXT_NAMESPACE