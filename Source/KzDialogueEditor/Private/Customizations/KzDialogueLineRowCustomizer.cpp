// Copyright 2026 kirzo

#include "Customizations/KzDialogueLineRowCustomizer.h"
#include "KzDialogueAudition.h"

#include "KzDialogueAsset.h"

#include "AssetRegistry/IAssetRegistry.h"
#include "PropertyHandle.h"
#include "Sound/SoundBase.h"

#include "Styling/AppStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"

#define LOCTEXT_NAMESPACE "KzDialogueLineRowCustomizer"

namespace
{
	// A registry lookup, not a load: the row asks every frame, and a missing asset would hit the disk each time.
	bool IsAudioMissing(const FKzDialogueLine& Line)
	{
		return !Line.Audio.IsNull() && !Line.Audio.Get() && !IAssetRegistry::GetChecked().GetAssetByObjectPath(Line.Audio.ToSoftObjectPath()).IsValid();
	}
}

TSharedRef<SWidget> FKzDialogueLineRowCustomizer::BuildLeadingWidget(TSharedPtr<IPropertyHandle> Handle)
{
	// The box keeps the width when the icon hides, so lines without audio read as such by absence.
	return SNew(SBox)
		.WidthOverride(16.f).HeightOverride(16.f)
		[
			SNew(SImage)
				.Image_Lambda([this, Handle]() { return GetAudioStatusBrush(Handle); })
				.Visibility_Lambda([this, Handle]()
					{
						const FKzDialogueLine* Line = ResolveLine(Handle);
						return Line && !Line->Audio.IsNull() ? EVisibility::Visible : EVisibility::Hidden;
					})
				.ToolTipText_Lambda([this, Handle]()
					{
						const FKzDialogueLine* Line = ResolveLine(Handle);
						if (!Line || Line->Audio.IsNull()) { return FText::GetEmpty(); }
						return IsAudioMissing(*Line)
							? LOCTEXT("MissingAudioTip", "The audio asset does not exist.")
							: LOCTEXT("HasAudioTip", "This line has a valid audio asset.");
					})
		];
}

TSharedRef<SWidget> FKzDialogueLineRowCustomizer::BuildTrailingWidget(TSharedPtr<IPropertyHandle> Handle)
{
	return SNew(SButton)
		.ButtonStyle(FAppStyle::Get(), "SimpleButton")
		.ContentPadding(FMargin(2.f))
		.IsEnabled_Lambda([this, Handle]()
			{
				const FKzDialogueLine* Line = ResolveLine(Handle);
				return Line && !Line->Audio.IsNull();
			})
		.OnClicked(FOnClicked::CreateSP(this, &FKzDialogueLineRowCustomizer::OnPlayClicked, Handle))
		.ToolTipText_Lambda([this, Handle]()
			{
				return IsAuditioning(Handle)
					? LOCTEXT("StopAudition", "Stop preview")
					: LOCTEXT("PlayAudition", "Preview this line's audio");
			})
		.Content()
		[
			SNew(SBox)
				.WidthOverride(16.f).HeightOverride(16.f)
				[
					SNew(SImage)
						.Image_Lambda([this, Handle]() { return FAppStyle::GetBrush(IsAuditioning(Handle) ? "Icons.Toolbar.Stop" : "Icons.Toolbar.Play"); })
						.ColorAndOpacity(FSlateColor::UseForeground())
				]
		];
}

FText FKzDialogueLineRowCustomizer::GetDisplayText(TSharedPtr<IPropertyHandle> Handle) const
{
	const FKzDialogueLine* Line = ResolveLine(Handle);
	return Line ? Line->GetDisplayLabel() : FText::GetEmpty();
}

FText FKzDialogueLineRowCustomizer::GetTooltipText(TSharedPtr<IPropertyHandle> Handle) const
{
	return GetDisplayText(Handle);
}

bool FKzDialogueLineRowCustomizer::TryResolveContextId(const FGuid& ContextId, const TArray<TSharedPtr<IPropertyHandle>>& Handles, TSharedPtr<IPropertyHandle>& OutHandle) const
{
	if (!ContextId.IsValid()) { return false; }
	for (const TSharedPtr<IPropertyHandle>& Handle : Handles)
	{
		if (FKzDialogueLine* Line = ResolveStruct<FKzDialogueLine>(Handle))
		{
			if (Line->LineId == ContextId)
			{
				OutHandle = Handle;
				return true;
			}
		}
	}
	return false;
}

bool FKzDialogueLineRowCustomizer::IsAuditioning(TSharedPtr<IPropertyHandle> Handle) const
{
	const FKzDialogueLine* Line = ResolveLine(Handle);
	return Line && KzDialogueAudition::IsPlayingLine(Line->LineId);
}

FReply FKzDialogueLineRowCustomizer::OnPlayClicked(TSharedPtr<IPropertyHandle> Handle)
{
	if (const FKzDialogueLine* Line = ResolveLine(Handle))
	{
		KzDialogueAudition::ToggleLine(*Line);
	}
	return FReply::Handled();
}

const FSlateBrush* FKzDialogueLineRowCustomizer::GetAudioStatusBrush(TSharedPtr<IPropertyHandle> Handle) const
{
	const FKzDialogueLine* Line = ResolveLine(Handle);
	const bool bBroken = Line && IsAudioMissing(*Line);
	return FAppStyle::GetBrush(bBroken ? "Icons.WarningWithColor" : "ClassIcon.SoundCue");
}

FKzDialogueLine* FKzDialogueLineRowCustomizer::ResolveLine(TSharedPtr<IPropertyHandle> Handle) const
{
	return ResolveStruct<FKzDialogueLine>(Handle);
}

#undef LOCTEXT_NAMESPACE