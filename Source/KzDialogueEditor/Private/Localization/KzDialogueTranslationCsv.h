// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "AssetRegistry/AssetData.h"

class FLocTextHelper;
class UKzDialogueAsset;
struct FKzDialogueLine;

/** Optional per-line export filter: return false to skip a line's row. Speaker rows follow the included lines. */
using FKzExportLineFilter = TFunction<bool(const UKzDialogueAsset&, const FKzDialogueLine&)>;

/** Localization target layout resolved from the target's generated step ini (Config/Localization/<Target>_Gather.ini). */
struct FKzLocTargetInfo
{
	FString TargetName;
	FString TargetPath;
	FString ManifestName;
	FString ArchiveName;
	FString NativeCulture;
	TArray<FString> ForeignCultures;
};

/** Translation state of one localizable text in one culture. */
enum class EKzTranslationState : uint8
{
	Missing,
	Stale,
	Translated
};

/**
 * Localization target, manifest and archives opened once, answering per-text and per-audio
 * questions cheaply afterwards. Used by BuildCoverage and by the dialogue dashboard's
 * per-line culture states.
 */
class FKzLocQuery
{
public:
	/** Loads target info, the manifest (strict: gather must have run) and every foreign archive (a missing archive counts as fully untranslated). */
	bool Load(FText& OutError);

	const FKzLocTargetInfo& GetTarget() const { return Target; }

	/** State of a text anchored to (Namespace, Key) whose current source string is SourceString. */
	EKzTranslationState GetTextState(const FString& Namespace, const FString& Key, const FString& SourceString, const FString& Culture) const;

	/** The archive entry for (Namespace, Key) in Culture: the source it was translated against and the translation itself. False when the archive has no entry. */
	bool GetArchiveEntry(const FString& Namespace, const FString& Key, const FString& Culture, FString& OutSource, FString& OutTranslation) const;

	/** True when the source audio package has a localized variant for Culture. */
	bool IsAudioLocalized(const FString& SourcePackageName, const FString& Culture) const;

private:
	FKzLocTargetInfo Target;
	TSharedPtr<FLocTextHelper> LocHelper;
	TSet<FString> LoadedCultures;
};

/** Per-culture translation coverage counters. */
struct FKzCultureCoverage
{
	FString Culture;

	/** Localizable texts in the queried assets (line texts + speaker overrides). */
	int32 Total = 0;

	/** Texts with a translation matching the current source. */
	int32 Translated = 0;

	/** Texts with a translation that predates the current source; needs review. */
	int32 Stale = 0;

	/** Texts with no translation at all. */
	int32 Missing = 0;

	/** Lines with source audio. */
	int32 VoicedLines = 0;

	/** Voiced lines whose audio has a localized variant for this culture. */
	int32 LocalizedAudio = 0;
};

/** One stale translation: it predates the current source text and needs review. */
struct FKzStaleTranslation
{
	/** Owning UKzDialogueAsset or UKzSpeakerAsset. */
	UObject* Asset = nullptr;

	/** Line index for dialogue rows; INDEX_NONE for speaker-asset name rows. */
	int32 LineIndex = INDEX_NONE;

	FString Key;
	FString Culture;
};

/** Result counters for a translation CSV import. */
struct FKzTranslationImportStats
{
	/** Rows written into the target culture's archive. */
	int32 Imported = 0;

	/** Rows whose source hash no longer matches the asset: the authored text changed after export, the stale translation was skipped. */
	int32 Drifted = 0;

	/** Rows whose asset / line / key could not be resolved anymore. */
	int32 Unresolved = 0;

	/** Rows with an empty Translation cell. */
	int32 Untranslated = 0;
};

/**
 * CSV translation round-trip for dialogue assets.
 *
 * Export writes one row per localizable FText (line text, speaker override) carrying the
 * stable namespace/key plus translator context: speaker, notes, max characters, tags,
 * audio path and the source hash used later for drift detection.
 *
 * Import parses the filled CSV back, skips rows whose source hash drifted from the asset,
 * and writes the surviving translations straight into the project's localization target
 * archive (FLocTextHelper), ready for the dashboard's Compile Text step.
 */
class FKzDialogueTranslationCsv
{
public:
	/** Adds Export / Import entries to the content browser context menu of UKzDialogueAsset. Owner: "KzDialogueEditor". */
	static void RegisterMenus();

	/** Writes the assets' localizable texts to CsvPath. Fails with OutError when there is nothing to export or the file cannot be written. */
	static bool ExportAssets(const TArray<UKzDialogueAsset*>& Assets, const FString& CsvPath, FText& OutError, const FKzExportLineFilter& LineFilter = nullptr);

	/** Imports a filled CSV into the localization target archive of the given culture. Drift and resolution failures are counted in OutStats and detailed in the output log. */
	static bool ImportCsv(const FString& CsvPath, const FString& Culture, FKzTranslationImportStats& OutStats, FText& OutError);

	/** Resolves the project's localization target layout from the KzDialogue settings target name. Fails when the target was never created. */
	static bool ReadLocTargetInfo(FKzLocTargetInfo& Out, FText& OutError);

	/** Computes per-culture coverage counters plus the stale-translation list for the given assets. Fails when the localization target or its manifest cannot be read. */
	static bool BuildCoverage(const TArray<UKzDialogueAsset*>& Assets, TArray<FKzCultureCoverage>& OutCultures, TArray<FKzStaleTranslation>& OutStale, FText& OutError);

	/** Interactive export flow (load assets, dirty warning, save-file dialog, ExportAssets, notification). Shared by the content browser menu and the dashboard. */
	static void ExportInteractive(TArray<FAssetData> SelectedAssets, const FKzExportLineFilter& LineFilter = nullptr);

	/** Interactive per-culture import flow (open-file dialog, culture-mismatch guard, ImportCsv, stats notification). Shared by the content browser menu and the dashboard. */
	static void ImportInteractive(const FString& Culture);

	/** Writes one .po per foreign culture into Directory (<Culture>/<Target>.po), scoped to the assets' lines passing LineFilter. Context (speaker, notes, max characters) travels as "#." comments; existing translations fill msgstr, stale ones flagged "#, fuzzy". */
	static bool ExportPoFiles(const TArray<UKzDialogueAsset*>& Assets, const FString& Directory, FText& OutError, const FKzExportLineFilter& LineFilter = nullptr);

	/** Interactive PO export: directory dialog + ExportPoFiles + notification. */
	static void ExportPoInteractive(TArray<FAssetData> SelectedAssets, const FKzExportLineFilter& LineFilter = nullptr);

	/** Imports a translated .po into Culture's archive. Entries whose msgid no longer matches the gathered source are skipped as drifted. */
	static bool ImportPoFile(const FString& PoPath, const FString& Culture, FKzTranslationImportStats& OutStats, FText& OutError);

	/** Interactive per-culture PO import flow (open-file dialog, ImportPoFile, stats notification). */
	static void ImportPoInteractive(const FString& Culture);

	/** Writes one clickable warning per stale translation into the KzDialogueL10N message log. Does not open the log. */
	static void LogStaleTranslations(const TArray<FKzStaleTranslation>& Stale);

	/** "Spanish (es)" style label for a culture code, falling back to the bare code when the engine has no display name for it. */
	static FText GetCultureDisplayLabel(const FString& Culture);
};