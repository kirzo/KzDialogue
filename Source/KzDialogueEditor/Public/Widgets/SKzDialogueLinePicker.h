// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Widgets/SCompoundWidget.h"
#include "KzDialogueTypes.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

class UKzDialogueAsset;
class SSearchBox;
class SComboButton;
template<typename ItemType> class SListView;

namespace KzDialoguePickerInternal
{
	/** A single row in the picker. Can represent a line or an alias. */
	struct FEntry
	{
		bool bIsHeader = false;
		bool bIsAlias = false;
		FGuid Id; // line GUID or alias GUID
		FString DisplayText;
		FString SearchHaystack;
		float DefaultDuration = 2.0f;

		FGameplayTag SpeakerTag;
		FGameplayTagContainer LineTags;
		bool bHasAudio = false;
	};
	using FEntryPtr = TSharedPtr<FEntry>;
}

/**
 * A searchable, filterable drop-down/picker widget that lists all lines inside a
 * UKzDialogueAsset. Used to quickly select a dialogue line by its text or speaker.
 *
 * Filters are combined with the search text using AND semantics.
 */
class KZDIALOGUEEDITOR_API SKzDialogueLinePicker : public SCompoundWidget
{
public:
	DECLARE_DELEGATE_TwoParams(FOnEntryPicked, FKzDialogueAssetReference /*Reference*/, float /*DefaultDuration*/);

	SLATE_BEGIN_ARGS(SKzDialogueLinePicker)
		: _bShowAliases(true)
		{
		}
		/** The dialogue asset to pull lines from. */
		SLATE_ARGUMENT(TWeakObjectPtr<UKzDialogueAsset>, Asset)
		/** Optional set of line ids that are already used elsewhere (e.g. by a Sequencer
		 *  track being edited). When non-empty, an extra "Hide already used" filter is
		 *  shown and enabled by default. */
		SLATE_ARGUMENT(TSet<FGuid>, AlreadyUsedLineIds)
		SLATE_ARGUMENT(FGameplayTag, RequiredSpeaker)  // empty = no constraint
		SLATE_ARGUMENT(bool, bRequireExactSpeakerMatch) // if RequiredSpeaker is empty, require lines without speaker
		SLATE_ARGUMENT(bool, bShowAliases)
		/** Fired when the user clicks a line or hits Enter while focused on the search. */
		SLATE_EVENT(FOnEntryPicked, OnEntryPicked)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	// Construction helpers
	void BuildAllItems();
	void GatherDistinctSpeakers();
	void GatherDistinctLineTags();

	// Slate event handlers
	EActiveTimerReturnType FocusSearchBoxOnce(double, float);
	void OnSearchTextChanged(const FText& NewText);
	void OnSearchTextCommitted(const FText& Text, ETextCommit::Type Type);
	TSharedRef<ITableRow> OnGenerateRow(KzDialoguePickerInternal::FEntryPtr Item, const TSharedRef<STableViewBase>& Owner);
	void OnItemClicked(KzDialoguePickerInternal::FEntryPtr Item);

	// Filter UI
	TSharedRef<SWidget> BuildFilterMenu();
	void ToggleSpeakerFilter(FGameplayTag SpeakerTag);
	void ToggleLineTagFilter(FGameplayTag LineTag);
	void ClearAllFilters();
	int32 GetActiveFilterCount() const;
	FText GetSpeakerDisplayLabel(FGameplayTag SpeakerTag) const;

	// Apply current search + filter state to VisibleItems.
	void RebuildVisibleItems();

private:
	TWeakObjectPtr<UKzDialogueAsset> Asset;
	FOnEntryPicked OnEntryPicked;
	TSet<FGuid> AlreadyUsedLineIds;

	TArray<KzDialoguePickerInternal::FEntryPtr>	  AllItems;
	TArray<KzDialoguePickerInternal::FEntryPtr>	  VisibleItems;

	// Distinct values gathered from the asset, used to populate the filter popup.
	TArray<FGameplayTag> DistinctSpeakerTags;	 // includes an invalid tag for "narration"
	TMap<FGameplayTag, FString> SpeakerDisplayNames;	 // representative display name for each speaker
	TArray<FGameplayTag> DistinctLineTags;

	// Speaker constraint (passed by argument, immutable for the lifetime of the picker).
	FGameplayTag RequiredSpeaker;
	bool bRequireExactSpeakerMatch = false;

	bool bShowAliases = true;

	// Active filter state.
	FString CurrentSearchText;	     // lowercase
	TSet<FGameplayTag> SelectedSpeakerFilters;	 // empty = no speaker filter
	TSet<FGameplayTag> SelectedLineTagFilters;	 // empty = no tag filter
	bool bHideAlreadyUsed = false;

	TSharedPtr<SSearchBox> SearchBox;
	TSharedPtr<SComboButton> FilterButton;
	TSharedPtr<SListView<KzDialoguePickerInternal::FEntryPtr>> ListView;
};