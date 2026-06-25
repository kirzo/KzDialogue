// Copyright 2026 kirzo

#include "Customizations/KzDialogueLineCustomization.h"
#include "KzDialogueTypes.h"
#include "KzDialogueTimeline.h"
#include "KzDialogueAsset.h"
#include "Settings/KzDialogueSettings.h"

#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "IPropertyUtilities.h"
#include "PropertyHandle.h"
#include "ScopedTransaction.h"

#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SKzDialogueTimeline.h"

#include "Sound/SoundBase.h"

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

	// Editable line fields (the runtime-only Timeline pointer is not among them).
	uint32 NumChildren = 0;
	StructPropertyHandle->GetNumChildren(NumChildren);
	for (uint32 i = 0; i < NumChildren; ++i)
	{
		if (TSharedPtr<IPropertyHandle> ChildHandle = StructPropertyHandle->GetChildHandle(i))
		{
			StructBuilder.AddProperty(ChildHandle.ToSharedRef());
		}
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
				AudioLength = Sound->GetDuration();
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