// Copyright 2026 kirzo

#include "Customizations/KzDialogueLineRowCustomizer.h"
#include "KzDialogueAudition.h"

#include "KzDialogueAsset.h"

#include "PropertyHandle.h"
#include "Sound/SoundBase.h"

#include "Styling/AppStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"

#define LOCTEXT_NAMESPACE "KzDialogueLineRowCustomizer"

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
						return Line->Audio.LoadSynchronous()
							? LOCTEXT("HasAudioTip", "This line has a valid audio asset.")
							: LOCTEXT("BrokenAudioTip", "Audio asset failed to load.");
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
	const bool bBroken = Line && !Line->Audio.IsNull() && !Line->Audio.LoadSynchronous();
	return FAppStyle::GetBrush(bBroken ? "Icons.WarningWithColor" : "ClassIcon.SoundCue");
}

FKzDialogueLine* FKzDialogueLineRowCustomizer::ResolveLine(TSharedPtr<IPropertyHandle> Handle) const
{
	return ResolveStruct<FKzDialogueLine>(Handle);
}

#undef LOCTEXT_NAMESPACE