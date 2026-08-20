// Copyright 2026 kirzo

#include "Customizations/KzDialogueLineCustomization.h"
#include "KzDialogueTypes.h"
#include "KzDialogueTimeline.h"
#include "KzDialogueAsset.h"
#include "KzNamedAsset.h"
#include "Settings/KzDialogueSettings.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "DetailWidgetRow.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "IDetailChildrenBuilder.h"
#include "IDetailGroup.h"
#include "IDetailPropertyRow.h"
#include "IPropertyUtilities.h"
#include "PropertyHandle.h"
#include "ScopedTransaction.h"
#include "Utils/KzEditorUtils.h"

#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "InputCoreTypes.h"
#include "Styling/AppStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SMenuAnchor.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SKzAudioRangeStrip.h"
#include "Widgets/SKzDialogueTimeline.h"
#include "Widgets/SKzTokenPicker.h"

#include "Components/AudioComponent.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundWave.h"

#define LOCTEXT_NAMESPACE "KzDialogueLineCustomization"


TSharedRef<IPropertyTypeCustomization> FKzDialogueLineCustomization::MakeInstance()
{
	return MakeShared<FKzDialogueLineCustomization>();
}

FKzDialogueLineCustomization::~FKzDialogueLineCustomization()
{
	StopRangeAudition();
}

void FKzDialogueLineCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& /*StructCustomizationUtils*/)
{
	HeaderRow
		.NameContent()
		[
			StructPropertyHandle->CreatePropertyNameWidget()
		];
}

void FKzDialogueLineCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	StructHandle = StructPropertyHandle;
	PropertyUtilities = StructCustomizationUtils.GetPropertyUtilities();

	// Editable line fields (the runtime-only Timeline pointer is not among them). Fields with
	// a third category segment (Audio, Timing, Playback, Localization) render as groups.
	// The range fields are skipped here and re-added by AddAudioRangeRow so they sit at the
	// bottom of the Audio group, right above the visual strip they drive. The Text row is
	// claimed to gain the named-asset token picker without moving from its place.
	TMap<FString, IDetailGroup*> Groups;
	const TSet<FName> RangeFields{ GET_MEMBER_NAME_CHECKED(FKzDialogueLine, AudioStartTime), GET_MEMBER_NAME_CHECKED(FKzDialogueLine, AudioEndTime) };
	FKzPropertyHandleUtils::AddChildrenGroupedByCategory(StructBuilder, StructPropertyHandle, RangeFields, &Groups,
		[this](IDetailChildrenBuilder& Builder, TSharedRef<IPropertyHandle> Child)
		{
			if (Child->GetProperty() && Child->GetProperty()->GetFName() == GET_MEMBER_NAME_CHECKED(FKzDialogueLine, Text))
			{
				AddTextRowWithTokenPicker(Builder, Child);
				return true;
			}
			return false;
		});

	// Visual playback-range editor inside the Audio group (plain waves only: a cue or
	// metasound wrapper has no single waveform to draw).
	if (IDetailGroup* const* AudioGroup = Groups.Find(TEXT("Audio")))
	{
		AddAudioRangeRow(**AudioGroup);
	}

	// The strip caches the wave it was built from: rebuild the rows when the audio changes.
	if (TSharedPtr<IPropertyHandle> AudioHandle = StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FKzDialogueLine, Audio)))
	{
		AudioHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda([WeakUtilities = TWeakPtr<IPropertyUtilities>(PropertyUtilities)]()
		{
			if (TSharedPtr<IPropertyUtilities> Utilities = WeakUtilities.Pin())
			{
				Utilities->ForceRefresh();
			}
		}));
	}

	// Per-line timeline, authored on the asset and presented here by line id.
	UKzDialogueAsset* Asset = ResolveOwningAsset();
	const FGuid LineId = GetLineId();
	UKzDialogueTimeline* Timeline = (Asset && LineId.IsValid()) ? Asset->FindTimelineForLine(LineId) : nullptr;

	if (Timeline)
	{
		StructBuilder.AddCustomRow(LOCTEXT("TimelineRowFilter", "Timeline"))
			.NameContent()
			[
				SNew(STextBlock)
					.Text(LOCTEXT("TimelineLabel", "Timeline"))
			]
			.ValueContent()
			[
				SNew(SButton)
					.Text(LOCTEXT("DeleteTimeline", "Delete timeline"))
					.ToolTipText(LOCTEXT("DeleteTimelineTip", "Removes this line's notify timeline from the asset."))
					.OnClicked(FOnClicked::CreateSP(this, &FKzDialogueLineCustomization::OnDeleteTimelineClicked))
			];

		StructBuilder.AddCustomRow(LOCTEXT("TimelineRowFilter", "Timeline"))
			.WholeRowContent()
			[
				SNew(SKzDialogueTimeline, Timeline)
					.DisplayDuration_Lambda([this]() { return GetDisplayDuration(); })
					.OnModified_Lambda([this]()
					{
						if (UKzDialogueAsset* OwningAsset = ResolveOwningAsset()) { OwningAsset->MarkPackageDirty(); }
					})
					.OnRequestHostRefresh_Lambda([this]()
					{
						// RefreshTree only: re-measures the hosting details tree (fixing its scroll
						// after this row grows) without re-running customizations.
						if (PropertyUtilities.IsValid()) { PropertyUtilities->RequestRefresh(); }
					})
			];
	}
	else
	{
		StructBuilder.AddCustomRow(LOCTEXT("TimelineRowFilter", "Timeline"))
			.NameContent()
			[
				SNew(STextBlock)
					.Text(LOCTEXT("TimelineLabel", "Timeline"))
			]
			.ValueContent()
			[
				SNew(SButton)
					.Text(LOCTEXT("CreateTimeline", "Create timeline"))
					.ToolTipText(LOCTEXT("CreateTimelineTip", "Adds a notify timeline to this line (stored on the asset)."))
					.OnClicked(FOnClicked::CreateSP(this, &FKzDialogueLineCustomization::OnCreateTimelineClicked))
			];
	}
}

void FKzDialogueLineCustomization::AddTextRowWithTokenPicker(IDetailChildrenBuilder& StructBuilder, TSharedRef<IPropertyHandle> TextHandle)
{
	FText InitialText;
	TextHandle->GetValue(InitialText);
	LineTextSnapshot = InitialText.ToString();

	// Our own multiline editor instead of the stock value widget: it owns the caret (picker
	// inserts land at the cursor) and hosts the inline "{" autocomplete. Commits on focus
	// loss, like the stock multiline editor; Enter inside the text inserts a newline.
	// The text is UNBOUND on purpose: with a bound attribute, InsertTextAtCursor edits get
	// wiped by the attribute refresh and GetText reads the stale binding. External changes
	// (undo, other views) re-sync through the property change notify below.
	LineTextEditor = SNew(SMultiLineEditableTextBox)
		.Text(InitialText)
		.AutoWrapText(true)
		.OnTextChanged_Lambda([this](const FText& NewText) { OnLineTextChanged(NewText); })
		// The commit must NOT cancel the autocomplete session: clicking a popup row moves the
		// focus (committing) BEFORE the click lands; the session ends via OnMenuOpenChanged.
		.OnTextCommitted_Lambda([this, TextHandle](const FText& NewText, ETextCommit::Type)
		{
			FText Current;
			TextHandle->GetValue(Current);
			if (!NewText.ToString().Equals(Current.ToString(), ESearchCase::CaseSensitive))
			{
				TextHandle->SetValue(NewText);
			}
		})
		.OnKeyDownHandler_Lambda([this](const FGeometry&, const FKeyEvent& KeyEvent) { return OnLineTextKeyDown(KeyEvent); });

	TextHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSPLambda(this, [this, TextHandle]()
	{
		if (LineTextEditor.IsValid())
		{
			FText Value;
			TextHandle->GetValue(Value);
			if (!Value.ToString().Equals(LineTextEditor->GetText().ToString(), ESearchCase::CaseSensitive))
			{
				LineTextEditor->SetText(Value);
			}
		}
	}));

	// The autocomplete popup anchors under the editor; it never takes the keyboard focus,
	// the editor routes the keyboard to it while a session is active.
	TokenMenuAnchor = SNew(SMenuAnchor)
		.Placement(MenuPlacement_BelowAnchor)
		// Whatever closes the popup (outside click, accept, Esc) also ends the session state.
		.OnMenuOpenChanged_Lambda([this](bool bOpen)
		{
			if (!bOpen)
			{
				AutocompleteBraceIndex = INDEX_NONE;
				AutocompleteFragmentLen = 0;
			}
		})
		.OnGetMenuContent_Lambda([this, TextHandle]()
		{
			return SAssignNew(AutocompletePicker, SKzTokenPicker)
				.bAutocompleteMode(true)
				.OnTokenChosen_Lambda([this, TextHandle](const FString& TokenText) { AcceptTokenAutocomplete(TokenText, TextHandle); });
		})
		[
			LineTextEditor.ToSharedRef()
		];

	// The "{}" button opens the full browser; the holder wires its search box as the focus target.
	TSharedRef<TWeakPtr<SComboButton>> ComboHolder = MakeShared<TWeakPtr<SComboButton>>();
	TSharedRef<SComboButton> Combo = SNew(SComboButton)
		.ToolTipText(LOCTEXT("TokenPickerTip", "Insert a named-asset token at the cursor: it resolves to the thing's localized name when the line plays. Typing '{' in the text offers the same list inline."))
		.OnGetMenuContent_Lambda([this, TextHandle, ComboHolder]()
		{
			TSharedRef<SKzTokenPicker> Picker = SNew(SKzTokenPicker)
				.OnTokenChosen_Lambda([this, TextHandle](const FString& TokenText)
				{
					if (LineTextEditor.IsValid())
					{
						LineTextEditor->InsertTextAtCursor(TokenText);
						TextHandle->SetValue(LineTextEditor->GetText());
					}
					FSlateApplication::Get().DismissAllMenus();
				});
			if (TSharedPtr<SComboButton> Pinned = ComboHolder->Pin())
			{
				Pinned->SetMenuContentWidgetToFocus(Picker->GetWidgetToFocus());
			}
			return StaticCastSharedRef<SWidget>(Picker);
		})
		.ButtonContent()
		[
			SNew(STextBlock).Text(INVTEXT("{}"))
		];
	*ComboHolder = Combo;

	// The custom text editor replaced the stock FText widget, so the localization toggle it
	// carried comes back as a globe button. Keys are plugin-managed: localizable texts are
	// re-anchored to their stable key by the asset's metadata refresh, so the toggle only
	// flips culture invariance. Reads go through the SOURCE string: the display string of a
	// keyed text can be a resolved translation.
	TSharedRef<SCheckBox> LocToggle = SNew(SCheckBox)
		.Style(FAppStyle::Get(), "ToggleButtonCheckbox")
		.ToolTipText_Lambda([TextHandle]()
		{
			FText Current;
			TextHandle->GetValue(Current);
			return Current.IsCultureInvariant()
				? LOCTEXT("LineNotLocalizableTip", "Not localizable: this line is skipped by the localization gather. Click to make it localizable.")
				: LOCTEXT("LineLocalizableTip", "Localizable: this line is gathered for translation. Click to exclude it.");
		})
		.IsChecked_Lambda([TextHandle]()
		{
			FText Current;
			TextHandle->GetValue(Current);
			return Current.IsCultureInvariant() ? ECheckBoxState::Unchecked : ECheckBoxState::Checked;
		})
		.OnCheckStateChanged_Lambda([TextHandle](ECheckBoxState NewState)
		{
			FText Current;
			TextHandle->GetValue(Current);
			const FString* Source = FTextInspector::GetSourceString(Current);
			const FString SourceString = Source ? *Source : FString();
			TextHandle->SetValue(NewState == ECheckBoxState::Checked ? FText::FromString(SourceString) : FText::AsCultureInvariant(SourceString));
		})
		[
			SNew(SImage)
				.Image(FAppStyle::GetBrush("Icons.Localization"))
				.ColorAndOpacity(FSlateColor::UseForeground())
		];

	StructBuilder.AddProperty(TextHandle).CustomWidget()
		.NameContent()
		[
			TextHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		.MinDesiredWidth(325.0f)
		.MaxDesiredWidth(600.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f)
			[
				TokenMenuAnchor.ToSharedRef()
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(4.0f, 0.0f, 0.0f, 0.0f)
			[
				Combo
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(4.0f, 0.0f, 0.0f, 0.0f)
			[
				LocToggle
			]
		];
}

void FKzDialogueLineCustomization::OnLineTextChanged(const FText& NewText)
{
	const FString New = NewText.ToString();
	const FString Old = LineTextSnapshot;
	LineTextSnapshot = New;

	if (AutocompleteBraceIndex == INDEX_NONE)
	{
		// A session opens on a single typed "{".
		if (New.Len() == Old.Len() + 1)
		{
			int32 DiffIndex = 0;
			while (DiffIndex < Old.Len() && Old[DiffIndex] == New[DiffIndex]) { ++DiffIndex; }
			if (New[DiffIndex] == TEXT('{'))
			{
				AutocompleteBraceIndex = DiffIndex;
				AutocompleteFragmentLen = 0;
				if (TokenMenuAnchor.IsValid())
				{
					TokenMenuAnchor->SetIsOpen(true, /*bFocusMenu=*/false);
				}
				if (AutocompletePicker.IsValid())
				{
					AutocompletePicker->SetFilter(FString());
				}
			}
		}
		return;
	}

	// Session active: only single-character edits inside the fragment keep it alive; anything
	// unusual (paste, selection edits, typing elsewhere, closing the brace by hand) ends it.
	const int32 FragmentEnd = AutocompleteBraceIndex + 1 + AutocompleteFragmentLen;
	if (New.Len() == Old.Len() + 1)
	{
		int32 DiffIndex = 0;
		while (DiffIndex < Old.Len() && Old[DiffIndex] == New[DiffIndex]) { ++DiffIndex; }
		const TCHAR Typed = New[DiffIndex];
		const bool bInFragment = DiffIndex > AutocompleteBraceIndex && DiffIndex <= FragmentEnd;
		const bool bTokenChar = FChar::IsAlnum(Typed) || Typed == TEXT('_') || Typed == TEXT(':') || Typed == TEXT('-');
		if (!bInFragment || !bTokenChar)
		{
			CancelTokenAutocomplete();
			return;
		}
		++AutocompleteFragmentLen;
	}
	else if (New.Len() == Old.Len() - 1)
	{
		int32 DiffIndex = 0;
		while (DiffIndex < New.Len() && Old[DiffIndex] == New[DiffIndex]) { ++DiffIndex; }
		const bool bInFragment = DiffIndex > AutocompleteBraceIndex && DiffIndex <= FragmentEnd && AutocompleteFragmentLen > 0;
		if (!bInFragment)
		{
			CancelTokenAutocomplete();
			return;
		}
		--AutocompleteFragmentLen;
	}
	else
	{
		CancelTokenAutocomplete();
		return;
	}

	if (AutocompletePicker.IsValid() && New.IsValidIndex(AutocompleteBraceIndex) && New[AutocompleteBraceIndex] == TEXT('{'))
	{
		AutocompletePicker->SetFilter(New.Mid(AutocompleteBraceIndex + 1, AutocompleteFragmentLen));
	}
	else
	{
		CancelTokenAutocomplete();
	}
}

FReply FKzDialogueLineCustomization::OnLineTextKeyDown(const FKeyEvent& KeyEvent)
{
	if (AutocompleteBraceIndex == INDEX_NONE) { return FReply::Unhandled(); }

	const FKey Key = KeyEvent.GetKey();
	if (Key == EKeys::Down || Key == EKeys::Up)
	{
		if (AutocompletePicker.IsValid()) { AutocompletePicker->MoveSelection(Key == EKeys::Down ? 1 : -1); }
		return FReply::Handled();
	}
	if (Key == EKeys::Right || Key == EKeys::Left)
	{
		// Expand/collapse the selected base to reach its parts without the mouse; the caret
		// stays put during the session (Esc first to move it).
		if (AutocompletePicker.IsValid()) { AutocompletePicker->SetSelectionExpanded(Key == EKeys::Right); }
		return FReply::Handled();
	}
	if (Key == EKeys::Enter || Key == EKeys::Tab)
	{
		if (AutocompletePicker.IsValid() && AutocompletePicker->AcceptSelection())
		{
			return FReply::Handled();
		}
		// No match to accept: end the session and let the key act normally.
		CancelTokenAutocomplete();
		return FReply::Unhandled();
	}
	if (Key == EKeys::Escape)
	{
		CancelTokenAutocomplete();
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

void FKzDialogueLineCustomization::AcceptTokenAutocomplete(const FString& TokenText, TSharedRef<IPropertyHandle> TextHandle)
{
	if (AutocompleteBraceIndex == INDEX_NONE || !LineTextEditor.IsValid())
	{
		CancelTokenAutocomplete();
		return;
	}

	const FString Full = LineTextEditor->GetText().ToString();
	if (!Full.IsValidIndex(AutocompleteBraceIndex) || Full[AutocompleteBraceIndex] != TEXT('{'))
	{
		CancelTokenAutocomplete();
		return;
	}

	// Replace "{fragment" with the full token, put the caret right after it, commit.
	const FString Result = Full.Left(AutocompleteBraceIndex) + TokenText + Full.Mid(AutocompleteBraceIndex + 1 + AutocompleteFragmentLen);
	const int32 CaretOffset = AutocompleteBraceIndex + TokenText.Len();
	CancelTokenAutocomplete();

	LineTextSnapshot = Result;
	LineTextEditor->SetText(FText::FromString(Result));

	int32 LineIndex = 0;
	int32 LineStart = 0;
	for (int32 i = 0; i < CaretOffset && i < Result.Len(); ++i)
	{
		if (Result[i] == TEXT('\n'))
		{
			++LineIndex;
			LineStart = i + 1;
		}
	}
	LineTextEditor->GoTo(FTextLocation(LineIndex, CaretOffset - LineStart));
	FSlateApplication::Get().SetKeyboardFocus(LineTextEditor);

	TextHandle->SetValue(FText::FromString(Result));
}

void FKzDialogueLineCustomization::CancelTokenAutocomplete()
{
	AutocompleteBraceIndex = INDEX_NONE;
	AutocompleteFragmentLen = 0;
	if (TokenMenuAnchor.IsValid() && TokenMenuAnchor->IsOpen())
	{
		TokenMenuAnchor->SetIsOpen(false);
	}
}

void FKzDialogueLineCustomization::AddAudioRangeRow(IDetailGroup& AudioGroup)
{
	if (!StructHandle.IsValid())
	{
		return;
	}

	// The range fields skipped by the grouped walk, placed here right above their strip.
	TSharedPtr<IPropertyHandle> StartHandle = StructHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FKzDialogueLine, AudioStartTime));
	TSharedPtr<IPropertyHandle> EndHandle = StructHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FKzDialogueLine, AudioEndTime));
	if (StartHandle.IsValid()) { AudioGroup.AddPropertyRow(StartHandle.ToSharedRef()); }
	if (EndHandle.IsValid()) { AudioGroup.AddPropertyRow(EndHandle.ToSharedRef()); }

	void* RawData = nullptr;
	if (StructHandle->GetValueData(RawData) != FPropertyAccess::Success || !RawData)
	{
		return;
	}
	const FKzDialogueLine* Line = reinterpret_cast<const FKzDialogueLine*>(RawData);
	USoundWave* Wave = Cast<USoundWave>(Line->Audio.LoadSynchronous());
	if (!Wave)
	{
		return;
	}

	AudioGroup.AddWidgetRow()
		.WholeRowContent()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 4.f, 0.f)
			[
				SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "SimpleButton")
					.ContentPadding(FMargin(2.f))
					.ToolTipText(LOCTEXT("PlayRangeTip", "Preview exactly what the game will play: the custom range when set, the whole audio otherwise."))
					.OnClicked(FOnClicked::CreateSP(this, &FKzDialogueLineCustomization::OnPlayRangeClicked))
					[
						SNew(SBox).WidthOverride(16.f).HeightOverride(16.f)
						[
							SNew(SImage)
								.Image_Lambda([this]() { return FAppStyle::GetBrush(IsAuditioningRange() ? "Icons.Toolbar.Stop" : "Icons.Toolbar.Play"); })
								.ColorAndOpacity(FSlateColor::UseForeground())
						]
					]
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(SKzAudioRangeStrip, Wave)
					.StartHandle(StartHandle)
					.EndHandle(EndHandle)
			]
		];
}

bool FKzDialogueLineCustomization::IsAuditioningRange() const
{
	return RangePreviewAudio.IsValid() && RangePreviewAudio->IsPlaying();
}

void FKzDialogueLineCustomization::StopRangeAudition()
{
	if (RangeStopTicker.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(RangeStopTicker);
		RangeStopTicker.Reset();
	}
	if (GEditor)
	{
		GEditor->ResetPreviewAudioComponent();
	}
	RangePreviewAudio = nullptr;
}

FReply FKzDialogueLineCustomization::OnPlayRangeClicked()
{
	// Toggle off when this row's preview is the one playing.
	if (IsAuditioningRange())
	{
		StopRangeAudition();
		return FReply::Handled();
	}

	StopRangeAudition();

	void* RawData = nullptr;
	if (!StructHandle.IsValid() || StructHandle->GetValueData(RawData) != FPropertyAccess::Success || !RawData || !GEditor)
	{
		return FReply::Handled();
	}
	const FKzDialogueLine* Line = reinterpret_cast<const FKzDialogueLine*>(RawData);
	USoundBase* Sound = Line->Audio.LoadSynchronous();
	if (!Sound)
	{
		return FReply::Handled();
	}

	UAudioComponent* Preview = GEditor->PlayPreviewSound(Sound);
	if (!Preview)
	{
		return FReply::Handled();
	}
	RangePreviewAudio = Preview;

	// Same behavior as the runtime player: start at the offset, cut at the end time.
	const float Start = FMath::Max(0.0f, Line->AudioStartTime);
	if (Start > 0.0f)
	{
		Preview->Stop();
		Preview->Play(Start);
	}
	if (Line->AudioEndTime > 0.0f)
	{
		const float StopIn = FMath::Max(0.05f, Line->AudioEndTime - Start);
		RangeStopTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakAudio = RangePreviewAudio](float)
		{
			if (UAudioComponent* Audio = WeakAudio.Get())
			{
				Audio->FadeOut(0.1f, 0.0f);
			}
			return false;   // one-shot
		}), StopIn);
	}

	return FReply::Handled();
}

UKzDialogueAsset* FKzDialogueLineCustomization::ResolveOwningAsset() const
{
	if (!PropertyUtilities.IsValid()) { return nullptr; }
	for (const TWeakObjectPtr<UObject>& WeakObj : PropertyUtilities->GetSelectedObjects())
	{
		if (UObject* Obj = WeakObj.Get())
		{
			if (UKzDialogueAsset* Direct = Cast<UKzDialogueAsset>(Obj)) { return Direct; }
			if (UKzDialogueAsset* Typed = Obj->GetTypedOuter<UKzDialogueAsset>()) { return Typed; }
		}
	}
	return nullptr;
}

FGuid FKzDialogueLineCustomization::GetLineId() const
{
	if (!StructHandle.IsValid()) { return FGuid(); }
	void* RawData = nullptr;
	if (StructHandle->GetValueData(RawData) != FPropertyAccess::Success || !RawData)
	{
		return FGuid();
	}
	return reinterpret_cast<const FKzDialogueLine*>(RawData)->LineId;
}

float FKzDialogueLineCustomization::GetDisplayDuration() const
{
	// Mirror the runtime: resolve the audio length, then apply the line's DurationMode
	// (UKzDialoguePlayer::ResolveLineDuration goes through the same FKzDialogueLine::ResolveDuration).
	const UKzDialogueSettings* Settings = UKzDialogueSettings::Get();
	const float Default = Settings ? Settings->DefaultDuration : 2.5f;

	if (StructHandle.IsValid())
	{
		void* RawData = nullptr;
		if (StructHandle->GetValueData(RawData) == FPropertyAccess::Success && RawData)
		{
			const FKzDialogueLine* Line = reinterpret_cast<const FKzDialogueLine*>(RawData);
			float AudioLength = 0.f;
			if (USoundBase* Sound = Line->Audio.LoadSynchronous())
			{
				AudioLength = Line->ResolveAudioLength(Sound->GetDuration());
			}
			return FMath::Max(0.1f, Line->ResolveDuration(AudioLength, Default));
		}
	}

	return Default;
}

FReply FKzDialogueLineCustomization::OnCreateTimelineClicked()
{
	UKzDialogueAsset* Asset = ResolveOwningAsset();
	const FGuid LineId = GetLineId();
	if (!Asset || !LineId.IsValid())
	{
		return FReply::Handled();
	}

	const FScopedTransaction Transaction(LOCTEXT("CreateTimelineTransaction", "Create Dialogue Timeline"));
	Asset->Modify();

	UKzDialogueTimeline* NewTimeline = NewObject<UKzDialogueTimeline>(Asset, NAME_None, RF_Transactional);
	NewTimeline->OwningLineId = LineId;

	// Start with one track so the line has somewhere to drop notifies right away.
	FKzDialogueNotifyTrack DefaultTrack;
	DefaultTrack.Name = TEXT("1");
	NewTimeline->Tracks.Add(DefaultTrack);

	Asset->Timelines.Add(NewTimeline);

	if (PropertyUtilities.IsValid())
	{
		PropertyUtilities->ForceRefresh();
	}
	return FReply::Handled();
}

FReply FKzDialogueLineCustomization::OnDeleteTimelineClicked()
{
	UKzDialogueAsset* Asset = ResolveOwningAsset();
	const FGuid LineId = GetLineId();
	if (!Asset || !LineId.IsValid())
	{
		return FReply::Handled();
	}

	const FScopedTransaction Transaction(LOCTEXT("DeleteTimelineTransaction", "Delete Dialogue Timeline"));
	Asset->Modify();
	Asset->Timelines.RemoveAll([LineId](const UKzDialogueTimeline* Timeline)
	{
		return Timeline && Timeline->OwningLineId == LineId;
	});

	if (PropertyUtilities.IsValid())
	{
		PropertyUtilities->ForceRefresh();
	}
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
