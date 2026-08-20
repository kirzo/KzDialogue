// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "IPropertyTypeCustomization.h"
#include "UObject/WeakObjectPtr.h"

class IDetailGroup;
class IPropertyUtilities;
class UAudioComponent;
class UKzDialogueAsset;

/**
 * Customizes the expanded FKzDialogueLine details. Adds a per-line notify timeline that
 * lives on the owning asset (keyed by line id) rather than on the line struct: lines are
 * edited as external structures, where an instanced subobject can't be created. The line's
 * runtime Timeline pointer is filled by the provider at resolve time. This is also the
 * future host for the visual timeline strip.
 */
class FKzDialogueLineCustomization : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();
	virtual ~FKzDialogueLineCustomization() override;

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

	/** Auditions exactly what the game will play: Play(AudioStartTime) plus a cut at AudioEndTime. */
	FReply OnPlayRangeClicked();
	bool IsAuditioningRange() const;
	void StopRangeAudition();

	TSharedPtr<IPropertyHandle> StructHandle;
	TSharedPtr<IPropertyUtilities> PropertyUtilities;

	/** Editor preview of the audio range; the ticker cuts it at AudioEndTime. */
	TWeakObjectPtr<UAudioComponent> RangePreviewAudio;
	FTSTicker::FDelegateHandle RangeStopTicker;
};