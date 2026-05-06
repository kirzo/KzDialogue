// Copyright 2026 kirzo

#include "Customizations/KzDialogueLineRowCustomizer.h"

#include "KzDialogueAsset.h"

#include "PropertyHandle.h"
#include "Editor.h"
#include "Sound/SoundBase.h"
#include "Components/AudioComponent.h"

#include "Styling/AppStyle.h"
#include "Styling/SlateIconFinder.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"

#define LOCTEXT_NAMESPACE "KzDialogueLineRowCustomizer"

// =======================================================================================
// Lifecycle
// =======================================================================================

FKzDialogueLineRowCustomizer::~FKzDialogueLineRowCustomizer()
{
	StopActiveAudition();
}

void FKzDialogueLineRowCustomizer::OnRegister(FAssetEditorToolkit* InHostEditor)
{
	// Nothing to capture for now; the editor reference is unused at this point but the
	// hook is here so future features (e.g. selecting the row that finished playing)
	// can subscribe to editor events without changes to the base class.
}

void FKzDialogueLineRowCustomizer::OnUnregister()
{
	StopActiveAudition();
}

// =======================================================================================
// Slot widgets
// =======================================================================================

TSharedRef<SWidget> FKzDialogueLineRowCustomizer::BuildLeadingWidget(TSharedPtr<IPropertyHandle> Handle)
{
	// 16x16 reserved slot. The image inside is collapsed when the line has no audio,
	// so the row reads as "no audio" by absence rather than by a placeholder icon.
	return SNew(SBox)
		.WidthOverride(16.f).HeightOverride(16.f)
		[
			SNew(SImage)
				.Image_Lambda([this, Handle]() { return GetAudioStatusBrush(Handle); })
				.Visibility_Lambda([this, Handle]()
					{
						// Hide the slot entirely for lines without audio. Keeps the layout
						// stable (the SBox preserves width) but no icon is drawn.
						if (FKzDialogueLine* Line = ResolveLine(Handle))
						{
							return Line->Audio.IsNull() ? EVisibility::Hidden : EVisibility::Visible;
						}
						return EVisibility::Hidden;
					})
				.ToolTipText_Lambda([this, Handle]()
					{
						if (FKzDialogueLine* Line = ResolveLine(Handle))
						{
							if (Line->Audio.IsNull())
							{
								return FText::GetEmpty();
							}
							if (!Line->Audio.LoadSynchronous())
							{
								return LOCTEXT("BrokenAudioTip", "Audio asset failed to load.");
							}
							return LOCTEXT("HasAudioTip", "This line has a valid audio asset.");
						}
						return FText::GetEmpty();
					})
		];
}

TSharedRef<SWidget> FKzDialogueLineRowCustomizer::BuildTrailingWidget(TSharedPtr<IPropertyHandle> Handle)
{
	// Play/stop button. Disabled when the line has no audio.
	return SNew(SButton)
		.ButtonStyle(FAppStyle::Get(), "SimpleButton")
		.ContentPadding(FMargin(2.f))
		.IsEnabled_Lambda([this, Handle]() { return IsPlayButtonEnabled(Handle); })
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
						.Image_Lambda([this, Handle]() { return GetPlayButtonBrush(Handle); })
						.ColorAndOpacity(FSlateColor::UseForeground())
				]
		];
}

// =======================================================================================
// Text overrides
// =======================================================================================

FText FKzDialogueLineRowCustomizer::GetDisplayText(TSharedPtr<IPropertyHandle> Handle) const
{
	if (FKzDialogueLine* Line = ResolveLine(Handle))
	{
		return Line->GetDisplayLabel();
	}
	return FText::GetEmpty();
}

FText FKzDialogueLineRowCustomizer::GetTooltipText(TSharedPtr<IPropertyHandle> Handle) const
{
	if (FKzDialogueLine* Line = ResolveLine(Handle))
	{
		// Untruncated speaker + text for the tooltip, useful for long lines that get
		// cut off in the row label.
		return Line->GetDisplayLabel();
	}
	return FText::GetEmpty();
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

// =======================================================================================
// Audition
// =======================================================================================

bool FKzDialogueLineRowCustomizer::IsPlayButtonEnabled(TSharedPtr<IPropertyHandle> Handle) const
{
	if (FKzDialogueLine* Line = ResolveLine(Handle))
	{
		return !Line->Audio.IsNull();
	}
	return false;
}

bool FKzDialogueLineRowCustomizer::IsAuditioning(TSharedPtr<IPropertyHandle> Handle) const
{
	if (!ActiveLineId.IsValid()) { return false; }
	if (FKzDialogueLine* Line = ResolveLine(Handle))
	{
		return Line->LineId == ActiveLineId && ActivePreviewAudio.IsValid() && ActivePreviewAudio->IsPlaying();
	}
	return false;
}

FReply FKzDialogueLineRowCustomizer::OnPlayClicked(TSharedPtr<IPropertyHandle> Handle)
{
	FKzDialogueLine* Line = ResolveLine(Handle);
	if (!Line) { return FReply::Handled(); }

	// Toggle: if this is the line currently playing, stop it.
	if (IsAuditioning(Handle))
	{
		StopActiveAudition();
		return FReply::Handled();
	}

	// Otherwise, stop whatever may be playing and start this one.
	StopActiveAudition();

	USoundBase* Sound = Line->Audio.LoadSynchronous();
	if (!Sound || !GEditor) { return FReply::Handled(); }

	// PlayPreviewSound spawns and manages the component itself.
	ActivePreviewAudio = GEditor->PlayPreviewSound(Sound);

	// Track which line is playing so the icon updates.
	ActiveLineId = Line->LineId;

	return FReply::Handled();
}

void FKzDialogueLineRowCustomizer::StopActiveAudition()
{
	if (GEditor)
	{
		// Kills any preview sound. Safe to call when nothing is playing.
		GEditor->ResetPreviewAudioComponent();
	}
	ActivePreviewAudio = nullptr;
	ActiveLineId = FGuid();
}

// =======================================================================================
// Brushes
// =======================================================================================

const FSlateBrush* FKzDialogueLineRowCustomizer::GetAudioStatusBrush(TSharedPtr<IPropertyHandle> Handle) const
{
	FKzDialogueLine* Line = ResolveLine(Handle);
	if (!Line || Line->Audio.IsNull())
	{
		// Slot is hidden in this case anyway, but return a sane brush so we never
		// dereference null inside the SImage during streaming.
		return FAppStyle::GetBrush("ClassIcon.SoundCue");
	}

	if (!Line->Audio.LoadSynchronous())
	{
		return FAppStyle::GetBrush("Icons.WarningWithColor");
	}

	return FAppStyle::GetBrush("ClassIcon.SoundCue");
}

const FSlateBrush* FKzDialogueLineRowCustomizer::GetPlayButtonBrush(TSharedPtr<IPropertyHandle> Handle) const
{
	// Switch between play and stop based on whether this row is the active one.
	if (IsAuditioning(Handle))
	{
		return FAppStyle::GetBrush("Icons.Toolbar.Stop");
	}

	// Default play icon for audio assets.
	return FAppStyle::GetBrush("Icons.Toolbar.Play");
}

// =======================================================================================
// Helpers
// =======================================================================================

FKzDialogueLine* FKzDialogueLineRowCustomizer::ResolveLine(TSharedPtr<IPropertyHandle> Handle) const
{
	return ResolveStruct<FKzDialogueLine>(Handle);
}

#undef LOCTEXT_NAMESPACE