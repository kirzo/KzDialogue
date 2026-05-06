// Copyright 2026 kirzo

#include "Customizations/KzDialogueAliasRowCustomizer.h"
#include "KzDialogueAsset.h"

#include "PropertyHandle.h"
#include "Styling/AppStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"

#define LOCTEXT_NAMESPACE "KzDialogueAliasRowCustomizer"

TSharedRef<SWidget> FKzDialogueAliasRowCustomizer::BuildLeadingWidget(TSharedPtr<IPropertyHandle> Handle)
{
	return SNew(SBox)
		.WidthOverride(16.f).HeightOverride(16.f)
		[
			SNew(SImage)
				.Image(FAppStyle::GetBrush("Sequencer.KeyDiamond"))
				.ColorAndOpacity(FSlateColor::UseForeground())
				.ToolTipText(LOCTEXT("AliasTip", "Alias — resolves to a random line at runtime."))
		];
}

FText FKzDialogueAliasRowCustomizer::GetDisplayText(TSharedPtr<IPropertyHandle> Handle) const
{
	if (FKzDialogueAlias* Alias = ResolveAlias(Handle))
	{
		return Alias->GetDisplayLabel();
	}
	return FText::GetEmpty();
}

FText FKzDialogueAliasRowCustomizer::GetTooltipText(TSharedPtr<IPropertyHandle> Handle) const
{
	if (FKzDialogueAlias* Alias = ResolveAlias(Handle))
	{
		const FString Speaker = Alias->Speaker.GetDisplayLabel().ToString();
		return FText::Format(LOCTEXT("AliasTooltip", "{0}\nSpeaker: {1}\n{2} line(s) referenced."),
			FText::FromName(Alias->AliasName),
			FText::FromString(Speaker.IsEmpty() ? TEXT("(none)") : Speaker),
			FText::AsNumber(Alias->LineIds.Num()));
	}
	return FText::GetEmpty();
}

bool FKzDialogueAliasRowCustomizer::TryResolveContextId(const FGuid& ContextId, const TArray<TSharedPtr<IPropertyHandle>>& Handles, TSharedPtr<IPropertyHandle>& OutHandle) const
{
	if (!ContextId.IsValid()) { return false; }
	for (const TSharedPtr<IPropertyHandle>& Handle : Handles)
	{
		if (FKzDialogueAlias* Alias = ResolveStruct<FKzDialogueAlias>(Handle))
		{
			if (Alias->AliasId == ContextId)
			{
				OutHandle = Handle;
				return true;
			}
		}
	}
	return false;
}

FKzDialogueAlias* FKzDialogueAliasRowCustomizer::ResolveAlias(TSharedPtr<IPropertyHandle> Handle) const
{
	return ResolveStruct<FKzDialogueAlias>(Handle);
}

#undef LOCTEXT_NAMESPACE