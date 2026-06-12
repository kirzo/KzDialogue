// Copyright 2026 kirzo

#include "K2Node_PlayDialogueLineAsync.h"
#include "KzDialogueAsset.h"
#include "KzDialogueAsyncActions.h"

#include "Kismet/BlueprintAsyncActionBase.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "K2Node_PlayDialogueLineAsync"

const FName UK2Node_PlayDialogueLineAsync::PN_Asset(TEXT("Asset"));
const FName UK2Node_PlayDialogueLineAsync::PN_LineId(TEXT("LineId"));

UK2Node_PlayDialogueLineAsync::UK2Node_PlayDialogueLineAsync(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	ProxyFactoryFunctionName = GET_FUNCTION_NAME_CHECKED(UKzAsyncPlayDialogueLineInline, PlayDialogueLineFromAsset);
	ProxyFactoryClass = UKzAsyncPlayDialogueLineInline::StaticClass();
	ProxyClass = UKzAsyncPlayDialogueLineInline::StaticClass();

	// UK2Node_BaseAsyncTask only generates the Activate() call when this is set (it defaults to
	// None there; UK2Node_AsyncAction is who normally sets it). Without it the action never runs.
	ProxyActivateFunctionName = GET_FUNCTION_NAME_CHECKED(UBlueprintAsyncActionBase, Activate);
}

void UK2Node_PlayDialogueLineAsync::AllocateDefaultPins()
{
	Super::AllocateDefaultPins();

	if (UEdGraphPin* LineIdPin = GetLineIdPin())
	{
		LineIdPin->PinFriendlyName = LOCTEXT("LineIdPin", "Line / Alias");
	}
}

FText UK2Node_PlayDialogueLineAsync::GetNodeTitle(ENodeTitleType::Type /*TitleType*/) const
{
	return LOCTEXT("NodeTitle", "Play Dialogue Line (Async)");
}

FText UK2Node_PlayDialogueLineAsync::GetTooltipText() const
{
	return LOCTEXT("Tooltip",
		"Plays a single line from a Dialogue Asset and completes when that specific line "
		"finishes. Finished fires on natural completion; Cancelled fires when the dialogue "
		"ends first (stop, abort, interrupt) or the line cannot play.\n"
		"When the Asset pin is unconnected and a literal asset is selected, the Line pin "
		"shows a dropdown of the asset's lines.");
}

FSlateIcon UK2Node_PlayDialogueLineAsync::GetIconAndTint(FLinearColor& OutColor) const
{
	OutColor = FColor::FromHex(TEXT("#4A6EB6")); // Match the dialogue asset color.
	return FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Comment");
}

FText UK2Node_PlayDialogueLineAsync::GetMenuCategory() const
{
	return LOCTEXT("MenuCategory", "Dialogue");
}

void UK2Node_PlayDialogueLineAsync::PinConnectionListChanged(UEdGraphPin* Pin)
{
	Super::PinConnectionListChanged(Pin);

	if (Pin == GetAssetPin())
	{
		// The pin factory's widget reads ShouldShowLineDropdown() so a refresh is enough.
		GetGraph()->NotifyGraphChanged();
	}
}

void UK2Node_PlayDialogueLineAsync::PinDefaultValueChanged(UEdGraphPin* Pin)
{
	Super::PinDefaultValueChanged(Pin);

	if (Pin == GetAssetPin())
	{
		// Literal asset changed: the dropdown's option list depends on it.
		GetGraph()->NotifyGraphChanged();
	}
}

UEdGraphPin* UK2Node_PlayDialogueLineAsync::GetAssetPin() const { return FindPin(PN_Asset); }
UEdGraphPin* UK2Node_PlayDialogueLineAsync::GetLineIdPin() const { return FindPin(PN_LineId); }

bool UK2Node_PlayDialogueLineAsync::ShouldShowLineDropdown() const
{
	const UEdGraphPin* AssetPin = GetAssetPin();
	if (!AssetPin || AssetPin->LinkedTo.Num() > 0) { return false; }
	return GetLiteralAsset() != nullptr;
}

UKzDialogueAsset* UK2Node_PlayDialogueLineAsync::GetLiteralAsset() const
{
	const UEdGraphPin* AssetPin = GetAssetPin();
	if (!AssetPin || AssetPin->LinkedTo.Num() > 0) { return nullptr; }
	return Cast<UKzDialogueAsset>(AssetPin->DefaultObject);
}

#undef LOCTEXT_NAMESPACE