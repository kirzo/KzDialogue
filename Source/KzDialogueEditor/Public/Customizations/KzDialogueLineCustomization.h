// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "IPropertyTypeCustomization.h"

class IDetailGroup;
class IPropertyUtilities;
class UKzDialogueAsset;

/**
 * Customizes the expanded FKzDialogueLine details. Adds the per-line notify timeline, which lives on
 * the owning asset (keyed by line id) rather than on the line struct: lines are edited as external
 * structures, where an instanced subobject can't be created.
 */
class FKzDialogueLineCustomization : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;

private:
	UKzDialogueAsset* ResolveOwningAsset() const;
	FGuid GetLineId() const;
	float GetDisplayDuration() const;
	FReply OnCreateTimelineClicked();
	FReply OnDeleteTimelineClicked();

	/** Waveform strip + play button inside the Audio group, when the line's audio is a plain wave. */
	void AddAudioRangeRow(IDetailGroup& AudioGroup);

	/** The Text row hosting SKzTokenTextEditor (multiline editor, "{}" browser, "{" autocomplete, localization toggle). */
	void AddTextRowWithTokenPicker(class IDetailChildrenBuilder& StructBuilder, TSharedRef<class IPropertyHandle> TextHandle);

	FReply OnPlayRangeClicked();
	bool IsAuditioningRange() const;

	TSharedPtr<IPropertyHandle> StructHandle;
	TSharedPtr<IPropertyUtilities> PropertyUtilities;
};