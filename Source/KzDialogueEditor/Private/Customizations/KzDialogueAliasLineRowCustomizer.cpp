// Copyright 2026 kirzo

#include "Customizations/KzDialogueAliasLineRowCustomizer.h"
#include "Widgets/SKzDialogueLinePicker.h"

#include "KzDialogueAsset.h"
#include "KzDialogueTypes.h"

#include "PropertyHandle.h"
#include "ScopedTransaction.h"
#include "GameplayTagContainer.h"

#include "Framework/Application/SlateApplication.h"
#include "Styling/AppStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "KzDialogueAliasLineRowCustomizer"

void FKzDialogueAliasLineRowCustomizer::SetAliasHandle(TSharedPtr<IPropertyHandle> InAliasHandle)
{
	AliasHandle = InAliasHandle;
}

// =======================================================================================
// Display
// =======================================================================================

TSharedRef<SWidget> FKzDialogueAliasLineRowCustomizer::BuildLeadingWidget(TSharedPtr<IPropertyHandle> Handle)
{
	// Status icon: SoundCue brush if the line resolves cleanly, warning otherwise.
	return SNew(SBox)
		.WidthOverride(16.f).HeightOverride(16.f)
		[
			SNew(SImage)
				.Image_Lambda([this, Handle]()
					{
						UKzDialogueAsset* Asset = ResolveAsset();
						const FGuid Id = ReadGuid(Handle);
						FKzDialogueLine Line;
						const bool bResolves = Asset && Asset->TryGetLineById(Id, Line);
						return bResolves
							? FAppStyle::GetBrush("ClassIcon.SoundCue")
							: FAppStyle::GetBrush("Icons.WarningWithColor");
					})
				.ColorAndOpacity(FSlateColor::UseForeground())
		];
}

FText FKzDialogueAliasLineRowCustomizer::GetDisplayText(TSharedPtr<IPropertyHandle> Handle) const
{
	UKzDialogueAsset* Asset = ResolveAsset();
	const FGuid Id = ReadGuid(Handle);

	if (!Asset || !Id.IsValid())
	{
		return FText::Format(LOCTEXT("MissingNoAsset", "(missing line: {0})"),
			FText::FromString(Id.ToString(EGuidFormats::Digits)));
	}

	FKzDialogueLine Line;
	if (Asset->TryGetLineById(Id, Line))
	{
		return Line.GetDisplayLabel(80);
	}

	return FText::Format(LOCTEXT("MissingLine", "(missing line: {0})"),
		FText::FromString(Id.ToString(EGuidFormats::Digits)));
}

FText FKzDialogueAliasLineRowCustomizer::GetTooltipText(TSharedPtr<IPropertyHandle> Handle) const
{
	UKzDialogueAsset* Asset = ResolveAsset();
	const FGuid Id = ReadGuid(Handle);
	if (!Asset || !Id.IsValid()) { return GetDisplayText(Handle); }

	FKzDialogueLine Line;
	if (Asset->TryGetLineById(Id, Line))
	{
		return Line.GetDisplayLabel(/*MaxTextLength=*/0);
	}
	return GetDisplayText(Handle);
}

bool FKzDialogueAliasLineRowCustomizer::TryResolveContextId(const FGuid& ContextId,
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

// =======================================================================================
// Add widget (picker)
// =======================================================================================

TSharedPtr<SWidget> FKzDialogueAliasLineRowCustomizer::BuildAddMenu(TSharedPtr<IPropertyHandleArray> ArrayHandle)
{
	if (!ArrayHandle.IsValid() || !ResolveAsset()) { return nullptr; }

	UKzDialogueAsset* Asset = ResolveAsset();
	const FGameplayTag SpeakerTag = GetAliasSpeakerTag();
	TSet<FGuid> AlreadyUsed = CollectAlreadyUsed(ArrayHandle);

	TWeakPtr<IPropertyHandleArray> WeakArrayHandle = ArrayHandle;

	return SNew(SBox).WidthOverride(320.f).HeightOverride(360.f)
		[
			SNew(SKzDialogueLinePicker)
				.Asset(Asset)
				.bShowAliases(false)
				.RequiredSpeaker(SpeakerTag)
				.bRequireExactSpeakerMatch(true)
				.AlreadyUsedLineIds(AlreadyUsed)
				.OnEntryPicked(SKzDialogueLinePicker::FOnEntryPicked::CreateSP(
					SharedThis(this), &FKzDialogueAliasLineRowCustomizer::OnLinePicked, WeakArrayHandle))
		];
}

void FKzDialogueAliasLineRowCustomizer::OnLinePicked(FKzDialogueAssetReference InRef, float /*Duration*/,
	TWeakPtr<IPropertyHandleArray> WeakArrayHandle)
{
	if (!InRef.IsValid() || InRef.bIsAlias) { return; }

	TSharedPtr<IPropertyHandleArray> ArrayPin = WeakArrayHandle.Pin();
	if (!ArrayPin.IsValid()) { return; }

	const FScopedTransaction Transaction(LOCTEXT("AddLineTrans", "Add line to alias"));

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

// =======================================================================================
// Helpers
// =======================================================================================

UKzDialogueAsset* FKzDialogueAliasLineRowCustomizer::ResolveAsset() const
{
	return ResolveAssetFn ? ResolveAssetFn() : nullptr;
}

FGuid FKzDialogueAliasLineRowCustomizer::ReadGuid(TSharedPtr<IPropertyHandle> Handle) const
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

FGameplayTag FKzDialogueAliasLineRowCustomizer::GetAliasSpeakerTag() const
{
	TSharedPtr<IPropertyHandle> AliasHandlePin = AliasHandle.Pin();
	if (!AliasHandlePin.IsValid()) { return FGameplayTag(); }

	void* RawData = nullptr;
	if (AliasHandlePin->GetValueData(RawData) != FPropertyAccess::Success || !RawData)
	{
		return FGameplayTag();
	}

	const FKzDialogueAlias* Alias = reinterpret_cast<const FKzDialogueAlias*>(RawData);
	return Alias ? Alias->Speaker.SpeakerTag : FGameplayTag();
}

TSet<FGuid> FKzDialogueAliasLineRowCustomizer::CollectAlreadyUsed(TSharedPtr<IPropertyHandleArray> ArrayHandle) const
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