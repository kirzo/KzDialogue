// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class UKzDialogueAsset;
class SVerticalBox;
struct FKzCultureCoverage;

/**
 * Per-culture localization status of one dialogue asset, shown as an extra tab in the
 * asset editor. One card per culture (native first, showing recording state): text and
 * audio progress bars plus an expandable list of the exact lines with work left, each
 * entry navigating to its line. Header actions export/import this asset's translation
 * CSV without leaving the editor.
 */
class SKzDialogueCoveragePanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SKzDialogueCoveragePanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UKzDialogueAsset* InAsset);

	/** Renders the per-culture coverage table (header row + one row per culture) into Rows. Used by the dialogue dashboard's batch coverage. */
	static void FillCoverageRows(SVerticalBox& Rows, const TArray<FKzCultureCoverage>& Cultures);

private:
	TWeakObjectPtr<UKzDialogueAsset> Asset;
	TSharedPtr<SVerticalBox> Rows;

	// Filters (mirroring the dialogue dashboard).
	/** Only this culture's card ("" = every culture; may be the native culture). */
	FString CultureFilter;
	/** When active, only lines of this speaker asset name count ("" = narration lines). */
	bool bSpeakerFilterActive = false;
	FString SpeakerFilter;
	/** Show the foreign cards' localized-audio state. Off silences missing-localized-audio warnings (projects that do not localize audio); the native card's recording state (missing and stale takes) is unaffected. */
	bool bShowLocalizedAudio = true;

	/** Line lists show only rows with audio work (the recording to-do list). */
	bool bOnlyMissingVoice = false;

	/** Line lists hide "ok" rows; fully complete cultures lose their card. */
	bool bOnlyIncomplete = false;

	/** One line inside a culture card: its status for that culture, its playable take and its label. */
	struct FLineRow
	{
		FGuid LineId;
		FText Label;
		/** Status chip: "ok", or what is pending ("text", "stale", "audio", combos...). */
		FText State;
		FLinearColor StateColor;
		/** Optional detail (e.g. the stale diff); replaces the default row tooltip when set. */
		FText Tooltip;
		bool bPending = false;
		/** The pending work involves audio; the Missing voice filter keeps only these. */
		bool bAudioWork = false;
		/** This culture's playable take (localized variant on foreign cards, source take on the native one). */
		FSoftObjectPath AudioPath;
	};

	/** Editor audio preview, one at a time. Key = card prefix + line id. */
	TWeakObjectPtr<class UAudioComponent> PreviewAudio;
	FString PreviewKey;

	void Refresh();

	/** Auto-refresh: the panel follows edits to the asset instead of relying on the Refresh button. */
	void OnObjectPropertyChanged(UObject* Object, struct FPropertyChangedEvent& Event);

	/** Selects the line in the editor's Lines tab (same navigation as validation issues). */
	void NavigateToLine(FGuid LineId);

	TSharedRef<SWidget> MakeCultureCard(const FText& Title, TArray<TSharedRef<SWidget>> ProgressRows, const TArray<FLineRow>& Lines, const FString& AudioKeyPrefix);
	TSharedRef<SWidget> MakeProgressRow(const FText& Label, int32 Done, int32 Total, int32 StaleCount, int32 PendingWords = 0);

	/** Export scope entries: all lines, the filtered lines, or only the pending text. */
	TSharedRef<SWidget> BuildExportMenu();

	/** Per-culture import entries; imports refresh the panel afterwards. */
	TSharedRef<SWidget> BuildImportMenu();

	/** Culture filter entries: All Cultures, the native culture, each foreign culture. */
	TSharedRef<SWidget> BuildCultureFilterMenu();

	/** Speaker filter entries: All Speakers, narration, each speaker used by this asset's lines. */
	TSharedRef<SWidget> BuildSpeakerFilterMenu();

	/** Row filters (missing voice, only incomplete), mirroring the dashboard's funnel menu. */
	TSharedRef<SWidget> BuildFiltersMenu();

	/** View options (the localized-audio toggle), mirroring the dashboard's eye menu. */
	TSharedRef<SWidget> BuildViewOptionsMenu();
};