// Copyright 2026 kirzo

#include "Customizations/KzDialogueLineFromAssetRowCustomizer.h"
#include "Widgets/SKzDialogueLinePicker.h"

#include "KzDialogueAsset.h"
#include "KzDialogueTypes.h"

#include "Editor.h"
#include "Editors/KzArrayAssetEditor.h"
#include "PropertyHandle.h"
#include "ScopedTransaction.h"
#include "Subsystems/AssetEditorSubsystem.h"

#include "Components/AudioComponent.h"
#include "Framework/Application/SlateApplication.h"
#include "Sound/SoundBase.h"
#include "Styling/AppStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "KzDialogueLineFromAssetRowCustomizer"

FKzDialogueLineFromAssetRowCustomizer::~FKzDialogueLineFromAssetRowCustomizer()
{
	StopActiveAudition();
}

TSharedRef<SWidget> FKzDialogueLineFromAssetRowCustomizer::BuildLeadingWidget(TSharedPtr<IPropertyHandle> Handle)
{
	return SNew(SBox)
		.WidthOverride(16.f).HeightOverride(16.f)
		[
			SNew(SImage)
				.Image_Lambda([this, Handle]()
					{
						UKzDialogueAsset* Asset = ResolveAsset();
						const FGuid Id = ReadGuid(Handle);
						if (!Asset || !Id.IsValid()) { return FAppStyle::GetBrush("Icons.WarningWithColor"); }

						FKzDialogueLine Line;
						if (Asset->TryGetLineById(Id, Line)) { return FAppStyle::GetBrush("ClassIcon.SoundCue"); }

						FKzDialogueAlias Alias;
						if (Asset->TryGetAliasById(Id, Alias)) { return FAppStyle::GetBrush("Sequencer.KeyDiamond"); }

						return FAppStyle::GetBrush("Icons.WarningWithColor");
					})
				.ColorAndOpacity(FSlateColor::UseForeground())
		];
}

TSharedRef<SWidget> FKzDialogueLineFromAssetRowCustomizer::BuildTrailingWidget(TSharedPtr<IPropertyHandle> Handle)
{
	// Play/stop button, mirroring the Lines tab rows. Collapsed for alias entries and
	// stale ids; disabled while the referenced line has no audio.
	return SNew(SButton)
		.ButtonStyle(FAppStyle::Get(), "SimpleButton")
		.ContentPadding(FMargin(2.f))
		.Visibility_Lambda([this, Handle]()
			{
				FKzDialogueLine Line;
				return TryResolveLine(Handle, Line) ? EVisibility::Visible : EVisibility::Collapsed;
			})
		.IsEnabled_Lambda([this, Handle]()
			{
				FKzDialogueLine Line;
				return TryResolveLine(Handle, Line) && !Line.Audio.IsNull();
			})
		.OnClicked(FOnClicked::CreateSP(this, &FKzDialogueLineFromAssetRowCustomizer::OnPlayClicked, Handle))
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
						.Image_Lambda([this, Handle]()
							{
								return FAppStyle::GetBrush(IsAuditioning(Handle) ? "Icons.Toolbar.Stop" : "Icons.Toolbar.Play");
							})
						.ColorAndOpacity(FSlateColor::UseForeground())
				]
		];
}

FText FKzDialogueLineFromAssetRowCustomizer::GetDisplayText(TSharedPtr<IPropertyHandle> Handle) const
{
	UKzDialogueAsset* Asset = ResolveAsset();
	const FGuid Id = ReadGuid(Handle);

	if (!Asset || !Id.IsValid())
	{
		return FText::Format(LOCTEXT("Missing", "(missing entry: {0})"),
			FText::FromString(Id.ToString(EGuidFormats::Digits)));
	}

	FKzDialogueLine Line;
	if (Asset->TryGetLineById(Id, Line)) { return Line.GetDisplayLabel(80); }

	FKzDialogueAlias Alias;
	if (Asset->TryGetAliasById(Id, Alias)) { return Alias.GetDisplayLabel(); }

	return FText::Format(LOCTEXT("MissingUnknown", "(missing entry: {0})"),
		FText::FromString(Id.ToString(EGuidFormats::Digits)));
}

FText FKzDialogueLineFromAssetRowCustomizer::GetTooltipText(TSharedPtr<IPropertyHandle> Handle) const
{
	return GetDisplayText(Handle);
}

bool FKzDialogueLineFromAssetRowCustomizer::TryResolveContextId(const FGuid& ContextId,
	const TArray<TSharedPtr<IPropertyHandle>>& Handles,
	TSharedPtr<IPropertyHandle>& OutHandle) const
{
	if (!ContextId.IsValid()) { return false; }
	for (const TSharedPtr<IPropertyHandle>& Handle : Handles)
	{
		if (ReadGuid(Handle) == ContextId)
		{
			OutHandle = Handle;
			return true;
		}
	}
	return false;
}

TSharedPtr<SWidget> FKzDialogueLineFromAssetRowCustomizer::BuildAddMenu(TSharedPtr<IPropertyHandleArray> ArrayHandle)
{
	if (!ArrayHandle.IsValid() || !ResolveAsset()) { return nullptr; }

	UKzDialogueAsset* Asset = ResolveAsset();
	const FGameplayTag SpeakerTag = ResolveSpeaker();
	TSet<FGuid> AlreadyUsed = CollectAlreadyUsed(ArrayHandle);
	TWeakPtr<IPropertyHandleArray> WeakArrayHandle = ArrayHandle;

	return SNew(SBox).WidthOverride(320.f).HeightOverride(360.f)
		[
			SNew(SKzDialogueLinePicker)
				.Asset(Asset)
				.bShowAliases(bShowAliases)
				.RequiredSpeaker(SpeakerTag)
				.bRequireExactSpeakerMatch(SpeakerTag.IsValid() == false && ResolveSpeakerFn != nullptr)
				.AlreadyUsedLineIds(AlreadyUsed)
				.OnEntryPicked(SKzDialogueLinePicker::FOnEntryPicked::CreateSP(
					SharedThis(this), &FKzDialogueLineFromAssetRowCustomizer::OnLinePicked, WeakArrayHandle))
		];
}

void FKzDialogueLineFromAssetRowCustomizer::OnLinePicked(FKzDialogueAssetReference InRef, float /*Duration*/,
	TWeakPtr<IPropertyHandleArray> WeakArrayHandle)
{
	if (!InRef.IsValid()) { return; }
	if (!bShowAliases && InRef.bIsAlias) { return; }

	TSharedPtr<IPropertyHandleArray> ArrayPin = WeakArrayHandle.Pin();
	if (!ArrayPin.IsValid()) { return; }

	const FScopedTransaction Transaction(LOCTEXT("AddEntryTrans", "Add line / alias"));

	ArrayPin->AddItem();
	uint32 NumElements = 0;
	ArrayPin->GetNumElements(NumElements);
	if (NumElements == 0) { return; }

	TSharedPtr<IPropertyHandle> NewElement = ArrayPin->GetElement(NumElements - 1);
	if (NewElement.IsValid())
	{
		NewElement->SetValueFromFormattedString(InRef.Id.ToString(EGuidFormats::Digits));
	}

	FSlateApplication::Get().DismissAllMenus();
}

void FKzDialogueLineFromAssetRowCustomizer::OnRowDoubleClicked(TSharedPtr<IPropertyHandle> Handle)
{
	UKzDialogueAsset* Asset = ResolveAsset();
	const FGuid Id = ReadGuid(Handle);
	if (!Asset || !Id.IsValid() || !GEditor) { return; }

	// Jump to the referenced entry in its owning tab (Lines, or Aliases for alias entries),
	// through the same per-tab ContextId resolution the validation panel uses.
	IAssetEditorInstance* Instance = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->FindEditorForAsset(Asset, /*bFocusIfOpen=*/true);
	if (Instance && Instance->GetEditorName() == TEXT("KzArrayAssetEditor"))
	{
		static_cast<FKzArrayAssetEditor*>(Instance)->SelectElementById(Id);
	}
}

bool FKzDialogueLineFromAssetRowCustomizer::TryResolveLine(TSharedPtr<IPropertyHandle> Handle, FKzDialogueLine& OutLine) const
{
	UKzDialogueAsset* Asset = ResolveAsset();
	const FGuid Id = ReadGuid(Handle);
	return Asset && Id.IsValid() && Asset->TryGetLineById(Id, OutLine);
}

bool FKzDialogueLineFromAssetRowCustomizer::IsAuditioning(TSharedPtr<IPropertyHandle> Handle) const
{
	if (!ActiveEntryId.IsValid()) { return false; }
	return ReadGuid(Handle) == ActiveEntryId && ActivePreviewAudio.IsValid() && ActivePreviewAudio->IsPlaying();
}

FReply FKzDialogueLineFromAssetRowCustomizer::OnPlayClicked(TSharedPtr<IPropertyHandle> Handle)
{
	// Toggle: if this is the entry currently playing, stop it.
	if (IsAuditioning(Handle))
	{
		StopActiveAudition();
		return FReply::Handled();
	}

	StopActiveAudition();

	FKzDialogueLine Line;
	if (!TryResolveLine(Handle, Line) || !GEditor) { return FReply::Handled(); }

	USoundBase* Sound = Line.Audio.LoadSynchronous();
	if (!Sound) { return FReply::Handled(); }

	// PlayPreviewSound spawns and manages the component itself.
	ActivePreviewAudio = GEditor->PlayPreviewSound(Sound);
	ActiveEntryId = ReadGuid(Handle);

	return FReply::Handled();
}

void FKzDialogueLineFromAssetRowCustomizer::StopActiveAudition()
{
	if (GEditor)
	{
		// Kills any preview sound. Safe to call when nothing is playing.
		GEditor->ResetPreviewAudioComponent();
	}
	ActivePreviewAudio = nullptr;
	ActiveEntryId = FGuid();
}

FGuid FKzDialogueLineFromAssetRowCustomizer::ReadGuid(TSharedPtr<IPropertyHandle> Handle) const
{
	FGuid Result;
	if (Handle.IsValid())
	{
		FString AsString;
		Handle->GetValueAsFormattedString(AsString);
		FGuid::Parse(AsString, Result);
	}
	return Result;
}

TSet<FGuid> FKzDialogueLineFromAssetRowCustomizer::CollectAlreadyUsed(TSharedPtr<IPropertyHandleArray> ArrayHandle) const
{
	TSet<FGuid> Result;
	if (!ArrayHandle.IsValid()) { return Result; }
	uint32 N = 0;
	ArrayHandle->GetNumElements(N);
	for (uint32 i = 0; i < N; ++i)
	{
		const FGuid Id = ReadGuid(ArrayHandle->GetElement(i));
		if (Id.IsValid()) { Result.Add(Id); }
	}
	return Result;
}

#undef LOCTEXT_NAMESPACE