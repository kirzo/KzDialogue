// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class UKzDialogueAsset;
class SVerticalBox;

/**
 * Per-culture localization status of a set of dialogue assets. Shared by the asset editor's
 * Localization tab (one asset) and the Dialogue Dashboard (every asset in the project), so
 * both views stay identical: one card per culture (native first, showing recording state),
 * text/audio progress bars, and the line list with per-line status chips, per-culture take
 * audition and jump-to-line navigation. With more than one asset the lists group lines under
 * asset header rows (magnifier to the Content Browser, double-click opens the asset).
 * Header actions export/import translation CSVs; hosts append their own toolbar buttons via
 * the ToolbarExtension slot (Open Dashboard in the asset editor, Gather/Compile in the
 * dashboard).
 */
class SKzDialogueCoveragePanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SKzDialogueCoveragePanel)
		: _bIncludeProjectTexts(false)
		{}
		/** Extra host-specific buttons appended at the right end of the toolbar. */
		SLATE_NAMED_SLOT(FArguments, ToolbarExtension)
		/** Also show/export the target's NON-dialogue gathered texts (UI, menus...) per culture. Project-scope hosts (the dashboard) enable it; the per-asset tab does not. */
		SLATE_ARGUMENT(bool, bIncludeProjectTexts)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TArray<UKzDialogueAsset*>& InAssets);

private:
	TArray<TWeakObjectPtr<UKzDialogueAsset>> Assets;
	TSharedPtr<SVerticalBox> Rows;

	// Filters (mirroring both hosts' toolbars).
	/** Only rows whose source text contains this (lines and project texts; "" = no search). */
	FString TextFilter;
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

	/** Show the target's non-dialogue texts as an extra per-culture section (dashboard host). */
	bool bIncludeProjectTexts = false;

	/** Eye toggle over the dialogue line areas and their bars; off leaves only the Other texts (dashboard host). */
	bool bShowDialogueLines = true;
	/** Eye toggle over the Other texts area and its progress bar. */
	bool bShowOtherTexts = true;
	/** Other texts show only collapsed identical-source groups: the Merge candidates. */
	bool bOnlyMergeableTexts = false;

	/** Export menu check: the scope exports also carry the project texts (asset-less CSV rows, _Other.po files). */
	bool bExportProjectTexts = true;

	/** "Namespace,Key" identities merged away or made non-localizable from this panel: the manifest still lists them until the next Gather, so Refresh hides them optimistically. */
	TSet<FString> HandledIdentities;

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
		/** This culture's current translation (foreign cards), shown dimmed in the row's right half. */
		FText Translation;

		/** Inline translation editing: card culture plus the identities sharing this row's source. Empty culture = the row has no translation column (native card). */
		FString TranslationCulture;
		TArray<TPair<FString, FString>> TranslationIdentities;
		FString TranslationSource;
		bool bPending = false;
		/** The pending work involves audio; the Missing voice filter keeps only these. */
		bool bAudioWork = false;
		/** Stale take on the native card: shows the Accept button (take valid despite the text change). */
		bool bAcknowledgeable = false;

		/** Localizable text that can be turned culture invariant from here (dialogue line or project text). */
		bool bCanMakeNonLocalizable = false;
		/** This culture's playable take (localized variant on foreign cards, source take on the native one). */
		FSoftObjectPath AudioPath;

		/** Identical-source project texts collapsed behind this row (namespace/key each); more than one enables Merge. */
		TArray<TPair<FString, FString>> GroupIdentities;
		FString GroupSource;

		/** Where the project text is authored (manifest source locations: asset object paths or code sites); clicking the row navigates there. */
		TArray<FString> SourceLocations;
	};

	/** One asset's rows inside a culture card; multi-asset hosts render a header row per group. */
	struct FAssetLines
	{
		TWeakObjectPtr<UKzDialogueAsset> Asset;
		TArray<FLineRow> Lines;
	};

	/** Editor audio preview, one at a time. Key = card prefix + line id. */
	TWeakObjectPtr<class UAudioComponent> PreviewAudio;
	FString PreviewKey;

	void Refresh();

	/** Auto-refresh: the panel follows edits to any of its assets instead of relying on the Refresh button. */
	void OnObjectPropertyChanged(UObject* Object, struct FPropertyChangedEvent& Event);

	/** Opens the asset's editor (or focuses it) and selects the line, same navigation as validation issues. */
	void NavigateToLine(TWeakObjectPtr<UKzDialogueAsset> InAsset, FGuid LineId);

	/** Accepts the current text for a stale take: re-stamps RecordedTextHash without re-recording. */
	void AcknowledgeRecordedText(TWeakObjectPtr<UKzDialogueAsset> InAsset, FGuid LineId);

	/** Writes an inline-edited translation into the culture's archive (every identity of the row) and refreshes on the next tick. */
	void CommitTranslation(FString Culture, TArray<TPair<FString, FString>> Identities, FString Source, FString NewTranslation);

	/** Rewrites the identical-source occurrences to one shared namespace/key (MergeIdenticalTexts) and reports the result. */
	void MergeOtherTexts(FString Source, TArray<TPair<FString, FString>> Identities);

	/** Opens where a project text is authored: its asset editor (one asset), the content browser (several), or the code site in the IDE. */
	void NavigateToTextSource(const TArray<FString>& Locations);

	/** Turns the row's text culture invariant: the dialogue line's Text on its asset, or every authored occurrence of a project text. */
	void MakeRowNonLocalizable(TWeakObjectPtr<UKzDialogueAsset> InAsset, FGuid LineId, FString Source, TArray<TPair<FString, FString>> Identities);

	TSharedRef<SWidget> MakeCultureCard(const FText& Title, TArray<TSharedRef<SWidget>> ProgressRows, const TArray<FAssetLines>& Groups, const FString& AudioKeyPrefix);
	TSharedRef<SWidget> MakeProgressRow(const FText& Label, int32 Done, int32 Total, int32 StaleCount, int32 PendingWords = 0);

	/** Asset separator row inside a multi-asset line list: display name, magnifier, double-click opens. */
	TSharedRef<SWidget> MakeAssetHeaderRow(TWeakObjectPtr<UKzDialogueAsset> InAsset);

	/** One line cell: status chip, play button for this culture's take (when any), navigable label. */
	TSharedRef<SWidget> MakeLineRowWidget(const FLineRow& Entry, TWeakObjectPtr<UKzDialogueAsset> InAsset, const FString& AudioKeyPrefix);

	/** Export entries: CSV and PO submenus, each with the same scopes (all / filtered / pending text). */
	TSharedRef<SWidget> BuildExportMenu();

	/** Import entries: CSV and PO submenus, each listing the target's cultures; imports refresh the panel. */
	TSharedRef<SWidget> BuildImportMenu();

	/** Culture filter entries: All Cultures, the native culture, each foreign culture. */
	TSharedRef<SWidget> BuildCultureFilterMenu();

	/** Speaker filter entries: All Speakers, narration, each speaker used by the assets' lines. */
	TSharedRef<SWidget> BuildSpeakerFilterMenu();

	/** Row filters (missing voice, only incomplete), mirroring the dashboard's funnel menu. */
	TSharedRef<SWidget> BuildFiltersMenu();

	/** View options (the localized-audio toggle). */
	TSharedRef<SWidget> BuildViewOptionsMenu();
};