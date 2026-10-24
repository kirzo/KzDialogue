// Copyright 2026 kirzo

#include "Customizations/KzDialogueLineCustomization.h"
#include "KzDialogueAudition.h"
#include "KzDialogueTypes.h"
#include "KzDialogueTimeline.h"
#include "KzDialogueAsset.h"
#include "Settings/KzDialogueSettings.h"

#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "IDetailGroup.h"
#include "IDetailPropertyRow.h"
#include "IPropertyUtilities.h"
#include "PropertyHandle.h"
#include "ScopedTransaction.h"
#include "Utils/KzEditorUtils.h"

#include "Styling/AppStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SKzAudioRangeStrip.h"
#include "Widgets/SKzDialogueTimeline.h"
#include "Widgets/SKzTokenTextEditor.h"

#include "AssetRegistry/IAssetRegistry.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundWave.h"

#define LOCTEXT_NAMESPACE "KzDialogueLineCustomization"


TSharedRef<IPropertyTypeCustomization> FKzDialogueLineCustomization::MakeInstance()
{
	return MakeShared<FKzDialogueLineCustomization>();
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
	const TSharedPtr<IPropertyHandle> MaxCharactersHandle = StructHandle.IsValid() ? StructHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FKzDialogueLine, MaxCharacters)) : nullptr;

	StructBuilder.AddProperty(TextHandle).CustomWidget()
		.NameContent()
		[
			TextHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		.MinDesiredWidth(325.0f)
		.MaxDesiredWidth(600.0f)
		[
			SNew(SKzTokenTextEditor, TextHandle)
				.bManagedLocalizationKeys(true)
				.MaxCharacters_Lambda([MaxCharactersHandle]()
				{
					int32 Value = 0;
					if (MaxCharactersHandle.IsValid()) { MaxCharactersHandle->GetValue(Value); }
					return Value;
				})
		];
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
	return KzDialogueAudition::IsPlayingLine(GetLineId());
}

FReply FKzDialogueLineCustomization::OnPlayRangeClicked()
{
	void* RawData = nullptr;
	if (StructHandle.IsValid() && StructHandle->GetValueData(RawData) == FPropertyAccess::Success && RawData)
	{
		KzDialogueAudition::ToggleLine(*reinterpret_cast<const FKzDialogueLine*>(RawData));
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
	if (StructHandle.IsValid())
	{
		void* RawData = nullptr;
		if (StructHandle->GetValueData(RawData) == FPropertyAccess::Success && RawData)
		{
			const FKzDialogueLine* Line = reinterpret_cast<const FKzDialogueLine*>(RawData);
			// The registry tag unless the wave is loaded: the timeline reads this every tick.
			float AudioDuration = 0.f;
			if (const USoundBase* Sound = Line->Audio.Get()) { AudioDuration = Sound->GetDuration(); }
			else if (!Line->Audio.IsNull()) { IAssetRegistry::GetChecked().GetAssetByObjectPath(Line->Audio.ToSoftObjectPath()).GetTagValue(GET_MEMBER_NAME_CHECKED(USoundBase, Duration), AudioDuration); }
			return FMath::Max(0.1f, Line->ResolveTotalDuration(AudioDuration));
		}
	}

	const UKzDialogueSettings* Settings = UKzDialogueSettings::Get();
	return Settings ? Settings->DefaultDuration : 2.5f;
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