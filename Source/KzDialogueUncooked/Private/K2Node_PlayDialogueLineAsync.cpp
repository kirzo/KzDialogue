// Copyright 2026 kirzo

#include "K2Node_PlayDialogueLineAsync.h"
#include "KzDialogueK2Nodes.h"
#include "KzDialogueAsyncActions.h"

#include "Kismet/BlueprintAsyncActionBase.h"

#define LOCTEXT_NAMESPACE "K2Node_PlayDialogueLineAsync"

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

	if (UEdGraphPin* LineIdPin = FindPin(KzDialogueK2Nodes::PN_LineId))
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
	return KzDialogueK2Nodes::GetIcon(OutColor);
}

FText UK2Node_PlayDialogueLineAsync::GetMenuCategory() const
{
	return LOCTEXT("MenuCategory", "Dialogue");
}

#undef LOCTEXT_NAMESPACE