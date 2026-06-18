// Copyright 2026 kirzo

#include "Customizations/KzDialogueTimelineCustomizations.h"
#include "KzDialogueTimeline.h"
#include "KzDialogueNotify.h"

#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "PropertyHandle.h"

#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "KzDialogueTimelineCustomizations"

namespace
{
	const FKzDialogueNotifyEvent* ResolveEvent(const TSharedPtr<IPropertyHandle>& Handle)
	{
		if (!Handle.IsValid()) { return nullptr; }
		void* RawData = nullptr;
		if (Handle->GetValueData(RawData) != FPropertyAccess::Success || !RawData)
		{
			return nullptr;
		}
		return reinterpret_cast<const FKzDialogueNotifyEvent*>(RawData);
	}

	FText MakeEventSummary(const FKzDialogueNotifyEvent& Event)
	{
		const FText NotifyName = Event.Notify ? Event.Notify->GetNotifyName() : LOCTEXT("EmptyNotify", "(no notify)");

		FString TimeStr;
		if (const FKzDialogueTimeSource_Relative* Rel = Event.TimeSource.GetPtr<FKzDialogueTimeSource_Relative>())
		{
			const TCHAR* Unit = Rel->bNormalized ? TEXT("n") : TEXT("s");
			if (Rel->Duration > KINDA_SMALL_NUMBER)
			{
				TimeStr = FString::Printf(TEXT("[%.2f - %.2f]%s"), Rel->Time, Rel->Time + Rel->Duration, Unit);
			}
			else
			{
				TimeStr = FString::Printf(TEXT("@ %.2f%s"), Rel->Time, Unit);
			}
		}

		if (TimeStr.IsEmpty())
		{
			return NotifyName;
		}
		return FText::Format(INVTEXT("{0}    {1}"), NotifyName, FText::FromString(TimeStr));
	}

	void AddAllChildrenDefault(TSharedRef<IPropertyHandle> StructPropertyHandle, IDetailChildrenBuilder& StructBuilder)
	{
		uint32 NumChildren = 0;
		StructPropertyHandle->GetNumChildren(NumChildren);
		for (uint32 i = 0; i < NumChildren; ++i)
		{
			if (TSharedPtr<IPropertyHandle> ChildHandle = StructPropertyHandle->GetChildHandle(i))
			{
				StructBuilder.AddProperty(ChildHandle.ToSharedRef());
			}
		}
	}
}

TSharedRef<IPropertyTypeCustomization> FKzDialogueNotifyEventCustomization::MakeInstance()
{
	return MakeShared<FKzDialogueNotifyEventCustomization>();
}

void FKzDialogueNotifyEventCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& /*StructCustomizationUtils*/)
{
	StructHandle = StructPropertyHandle;

	HeaderRow
		.NameContent()
		[
			StructPropertyHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		.MinDesiredWidth(260.f)
		[
			SNew(STextBlock)
				.Text_Lambda([this]() -> FText
					{
						const FKzDialogueNotifyEvent* Event = ResolveEvent(StructHandle);
						return Event ? MakeEventSummary(*Event) : FText::GetEmpty();
					})
				.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		];
}

void FKzDialogueNotifyEventCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& /*StructCustomizationUtils*/)
{
	AddAllChildrenDefault(StructPropertyHandle, StructBuilder);
}

TSharedRef<IPropertyTypeCustomization> FKzDialogueNotifyTrackCustomization::MakeInstance()
{
	return MakeShared<FKzDialogueNotifyTrackCustomization>();
}

void FKzDialogueNotifyTrackCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& /*StructCustomizationUtils*/)
{
	StructHandle = StructPropertyHandle;

	HeaderRow
		.NameContent()
		[
			StructPropertyHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		.MinDesiredWidth(220.f)
		[
			SNew(STextBlock)
				.Text_Lambda([this]() -> FText
					{
						if (!StructHandle.IsValid()) { return FText::GetEmpty(); }
						void* RawData = nullptr;
						if (StructHandle->GetValueData(RawData) != FPropertyAccess::Success || !RawData)
						{
							return FText::GetEmpty();
						}
						const FKzDialogueNotifyTrack* Track = reinterpret_cast<const FKzDialogueNotifyTrack*>(RawData);
						if (!Track) { return FText::GetEmpty(); }
						const FText Name = Track->Name.IsNone() ? LOCTEXT("UnnamedTrack", "(unnamed)") : FText::FromName(Track->Name);
						return FText::Format(INVTEXT("{0}  ({1})"), Name, FText::AsNumber(Track->Events.Num()));
					})
				.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		];
}

void FKzDialogueNotifyTrackCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& /*StructCustomizationUtils*/)
{
	AddAllChildrenDefault(StructPropertyHandle, StructBuilder);
}

#undef LOCTEXT_NAMESPACE