// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "AssetRegistry/AssetData.h"
#include "Localization/KzDialogueTranslationCsv.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SHeaderRow.h"

class ITableRow;
class STableViewBase;
class SVerticalBox;
template<typename ItemType> class STreeView;

/**
 * Project-wide dialogue overview: every UKzDialogueAsset in the project, listed from the
 * Asset Registry without loading a single asset (label, counts and speakers come from the
 * tags UKzDialogueAsset exports at save time; assets saved before the tags existed show "?").
 *
 * Asset rows expand into their lines (the asset loads lazily at that point). Selecting a
 * culture in the toolbar reframes the whole view to that language: per-line text/audio
 * states, per-asset "done/total" rollups (computed by loading the filtered set once, with
 * progress, cached per culture) and an "only incomplete" filter: the exact to-do list of
 * what is left to localize. Batch actions (translation CSV export by Selection/Filtered/All,
 * per-culture import, coverage) reuse the shared localization flows.
 * Opened from Tools > Dialogue Dashboard.
 */
class SKzDialogueDashboard : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SKzDialogueDashboard) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SKzDialogueDashboard() override;

	/** Nomad tab id the dashboard is registered under; TryInvokeTab with it to open/focus the dashboard. */
	static const FName TabId;

	/** One tree row: a dialogue asset (registry data only) or one of its lines (loaded lazily). */
	struct FRow
	{
		bool bIsLine = false;

		/** Owning asset; for line rows, the parent asset's data. */
		FAssetData Asset;

		// Asset rows. Count -1 / empty strings = tag missing (asset predates the tag export).
		FString DisplayName;
		int32 LineCount = -1;
		int32 VoicedCount = -1;
		FString Speakers;
		TArray<TSharedPtr<FRow>> Children;
		bool bChildrenLoaded = false;

		/** Per-culture rollups filled by AnalyzeCulture: X = done, Y = total. */
		TMap<FString, FIntPoint> TextRollup;
		TMap<FString, FIntPoint> AudioRollup;

		// Line rows.
		FGuid LineId;
		FString LineText;
		FString LineSpeaker;
		FString LineSpeakerAssetName;
		bool bLineVoiced = false;
		/** Source text changed after the audio was assigned: the recording needs a re-take. */
		bool bLineAudioStale = false;
		FString LineAudioPackage;
		FSoftObjectPath LineAudioPath;
		FString LineNamespace;
		FString LineKey;

		/** Per-culture states filled by AnalyzeCulture. Absent culture = not analyzed yet. */
		TMap<FString, EKzTranslationState> LineTextState;
		TMap<FString, bool> LineAudioLocalized;

		/** Asset label: DisplayName when authored, the asset name otherwise. */
		FString AssetLabel() const { return DisplayName.IsEmpty() ? Asset.AssetName.ToString() : DisplayName; }
	};
	using FRowPtr = TSharedPtr<FRow>;

	/** Shared audition state (one editor preview sound at a time), handed to row widgets. */
	struct FAudition
	{
		TWeakObjectPtr<class UAudioComponent> Component;
		FGuid LineId;
	};

private:
	TArray<FRowPtr> AllRows;
	TArray<FRowPtr> VisibleRows;

	// Filter + sort state.
	FString FilterText;
	FString SpeakerFilter;
	bool bShowDevelopers = true;
	bool bSearchInLines = false;
	FName SortColumn;
	EColumnSortMode::Type SortMode = EColumnSortMode::Ascending;

	/** Active culture ("" = all cultures / cheap registry view). */
	FString SelectedCulture;
	bool bOnlyIncomplete = false;

	/** The localization target's native culture, cached when the target is first read. */
	FString NativeCulture;

	/** Show only lines whose audio is missing or stale for the selected culture (source audio when none); the recording to-do list. */
	bool bOnlyMissingVoice = false;

	TSharedPtr<FAudition> Audition = MakeShared<FAudition>();

	/** View option: projects that do not localize audio can hide the Voiced column. */
	bool bShowVoicedColumn = true;

	/** Loaded on demand when a culture is first selected; reset by Refresh. */
	TUniquePtr<FKzLocQuery> LocQuery;

	TSharedPtr<STreeView<FRowPtr>> TreeView;
	TSharedPtr<SHeaderRow> HeaderRow;

	/** Batch-coverage output area, empty until Coverage runs. */
	TSharedPtr<SVerticalBox> CoverageRows;

	/** Registry query into AllRows (no asset loads); carries over loaded children and rollups by package, except InvalidatedPackage. */
	void RebuildFromRegistry(FName InvalidatedPackage = NAME_None);

	/** Filters + sorts AllRows into VisibleRows and rebuilds the tree rows. */
	void RebuildVisible();
	bool PassesFilters(const FRow& Row) const;

	/** True when the line row still needs work for the selected culture. */
	bool IsLineIncomplete(const FRow& Line) const;

	/** True when the line's audio is missing or stale for the selected culture (source audio when none). */
	bool IsLineMissingVoice(const FRow& Line) const;

	bool IsNativeSelected() const { return !SelectedCulture.IsEmpty() && SelectedCulture == NativeCulture; }

	/** True when the line row matches the in-line text search. */
	bool LineMatchesSearch(const FRow& Line) const;

	/** Loads the asset and builds its line rows. No-op when already loaded. */
	void LoadChildren(const FRowPtr& AssetRow);

	/** Computes line states + rollups of one loaded asset row for the selected culture. Cached per culture. */
	void ComputeCultureData(const FRowPtr& AssetRow);

	/** Loads children + culture data of every filtered asset row, with a progress dialog. */
	void AnalyzeFiltered(const FText& ProgressLabel);

	/** Lazily creates the localization query. False (with notification) when the target is unavailable. */
	bool EnsureLocQuery();

	TSharedRef<ITableRow> OnGenerateRow(FRowPtr Row, const TSharedRef<STableViewBase>& Owner);
	void OnGetChildren(FRowPtr Row, TArray<FRowPtr>& OutChildren);
	void OnExpansionChanged(FRowPtr Row, bool bExpanded);
	void OnRowDoubleClicked(FRowPtr Row);
	EColumnSortMode::Type GetColumnSortMode(FName Column) const;
	void OnColumnSort(EColumnSortPriority::Type Priority, const FName& Column, EColumnSortMode::Type Mode);

	/** Speaker filter menu, enumerating UKzSpeakerAsset names from the registry on open. */
	TSharedRef<SWidget> BuildSpeakerFilterMenu();

	/** Culture selector: All cultures + the localization target's foreign cultures. */
	TSharedRef<SWidget> BuildCultureMenu();

	/** Row filters menu (missing voice, only incomplete, developers). */
	TSharedRef<SWidget> BuildFiltersMenu();

	/** View options menu (column visibility toggles). */
	TSharedRef<SWidget> BuildViewOptionsMenu();

	/** Export scope entries: Selection / Filtered / All. */
	TSharedRef<SWidget> BuildExportMenu();

	/** Per-culture import entries, read from the localization target. */
	TSharedRef<SWidget> BuildImportCultureMenu();

	TArray<FAssetData> GetFilteredAssetData() const;
	TArray<FAssetData> GetSelectedAssetData() const;
	FReply OnCoverageClicked();

	/** Runs the engine's Gather Text / Compile Text commandlet for the plugin's localization target, without opening the Localization Dashboard. */
	FReply OnGatherClicked();
	FReply OnCompileClicked();

	/** Drops archive-derived caches (LocQuery, rollups, line text states) and re-analyzes the active culture. Call after anything that rewrites the archives. */
	void InvalidateLocData();

	void OnAssetRegistryChanged(const FAssetData& Data);
	void OnAssetRenamed(const FAssetData& Data, const FString& OldPath);
	void OnRegistryFilesLoaded();
};