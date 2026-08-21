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

	/** The manifest's source string for (Namespace, Key). False when the identity is not in the manifest. */
	bool GetManifestSource(const FString& Namespace, const FString& Key, FString& OutSource) const;

	/** Every gathered text whose namespace is OUTSIDE the dialogue pipeline (KzDialogue.* / KzSpeaker.*): the project's other texts (UI, menus, whatever the target gathers). */
	void EnumerateOtherTexts(TFunctionRef<void(const FString& Namespace, const FString& Key, const FString& Source)> Callback) const;

	/** The manifest contexts' source locations for (Namespace, Key): asset object paths, or code locations for C++-authored texts. */
	TArray<FString> GetSourceLocations(const FString& Namespace, const FString& Key) const;

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

/** Result counters for a source-fix import (ImportSourceFixes). */
struct FKzSourceFixStats
{
	/** Rows whose authored source text was rewritten. */
	int32 Fixed = 0;

	/** Rows edited on both sides (the asset changed after the export) or authored where the rewrite cannot reach; the asset wins. */
	int32 Conflicted = 0;

	/** Rows already matching the authored text. */
	int32 Unchanged = 0;

	/** Rows whose asset / line / field / manifest identity could not be resolved. */
	int32 Unresolved = 0;
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

	/** Writes the assets' localizable texts to CsvPath. Fails with OutError when there is nothing to export or the file cannot be written. OtherIdentities appends those gathered project texts ("Namespace,Key") as asset-less rows validated by the manifest at import. TranslationCulture pre-fills the Translation column with that culture's current translations (stale ones land in StaleTranslation as reference only). */
	static bool ExportAssets(const TArray<UKzDialogueAsset*>& Assets, const FString& CsvPath, FText& OutError, const FKzExportLineFilter& LineFilter = nullptr, const TSet<FString>* OtherIdentities = nullptr, const FString& TranslationCulture = FString());

	/** Imports a filled CSV into the localization target archive of the given culture. Drift and resolution failures are counted in OutStats and detailed in the output log. */
	static bool ImportCsv(const FString& CsvPath, const FString& Culture, FKzTranslationImportStats& OutStats, FText& OutError);

	/** Writes one translation for every given (Namespace, Key) identity straight into Culture's archive: the panel's inline editing. Source is the string the translation was made against (the current one). */
	static bool WriteTranslation(const FString& Culture, const TArray<TPair<FString, FString>>& Identities, const FString& Source, const FString& Translation, FText& OutError);

	/**
	 * Token-rename refactor, bound to UKzNamedAsset::OnTokenRenamed at module startup:
	 * rewrites every dialogue line using "{OldToken}" / "{OldToken:part}" to the asset's new
	 * token (case-insensitive, inside the ongoing edit transaction so Ctrl+Z reverts both),
	 * and updates the archives' sources and translations so nothing turns stale. Up-to-date
	 * recorded-take hashes are preserved: renaming a token does not change what was spoken.
	 */
	static void RenameNamedTokenReferences(class UKzNamedAsset* Renamed, FName OldToken);

	/** Culture-less import of SourceText edits made in an exported CSV: rewrites the AUTHORED texts (dialogue lines, speaker fields, project-text occurrences) keeping their namespace/key. A row applies only when the asset did not change after the export (hash still matches); both-sides edits count as conflicted and the asset wins. Transacted; dirty assets must be saved and Gather re-run. */
	static bool ImportSourceFixes(const FString& CsvPath, FKzSourceFixStats& OutStats, FText& OutError);

	/** Interactive flow for ImportSourceFixes: open-file dialog + stats notification. */
	static void ImportSourceFixesInteractive();

	/** Resolves the project's localization target layout from the KzDialogue settings target name. Fails when the target was never created. */
	static bool ReadLocTargetInfo(FKzLocTargetInfo& Out, FText& OutError);

	/** Computes per-culture coverage counters plus the stale-translation list for the given assets. Fails when the localization target or its manifest cannot be read. */
	static bool BuildCoverage(const TArray<UKzDialogueAsset*>& Assets, TArray<FKzCultureCoverage>& OutCultures, TArray<FKzStaleTranslation>& OutStale, FText& OutError);

	/** Interactive export flow (load assets, dirty warning, save-file dialog, ExportAssets, notification). Shared by the content browser menu and the dashboard. OtherIdentities appends those project texts to the same CSV; TranslationCulture pre-fills its translations and suffixes the default file name ("_es"). */
	static void ExportInteractive(TArray<FAssetData> SelectedAssets, const FKzExportLineFilter& LineFilter = nullptr, TSharedPtr<TSet<FString>> OtherIdentities = nullptr, const FString& TranslationCulture = FString());

	/** Interactive per-culture import flow (open-file dialog, culture-mismatch guard, ImportCsv, stats notification). Shared by the content browser menu and the dashboard. */
	static void ImportInteractive(const FString& Culture);

	/** Writes one .po per foreign culture into Directory (<Culture>/<Target>.po), scoped to the assets' lines passing LineFilter. Context (speaker, notes, max characters) travels as "#." comments; existing translations fill msgstr, stale ones flagged "#, fuzzy". OnlyCulture narrows the file set to that culture. */
	static bool ExportPoFiles(const TArray<UKzDialogueAsset*>& Assets, const FString& Directory, FText& OutError, const FKzExportLineFilter& LineFilter = nullptr, const FString& OnlyCulture = FString());

	/** Interactive PO export: directory dialog + ExportPoFiles + notification. With bWithOtherTexts the same directory also receives the _Other files (OtherIdentities narrows them to those "Namespace,Key" texts); assets may then be empty for an Other-texts-only export. */
	static void ExportPoInteractive(TArray<FAssetData> SelectedAssets, const FKzExportLineFilter& LineFilter = nullptr, bool bWithOtherTexts = false, TSharedPtr<TSet<FString>> OtherIdentities = nullptr, const FString& OnlyCulture = FString());

	/** Writes one <Culture>/<Target>_Other.po per foreign culture with the gathered texts outside the dialogue namespaces (all of them, or just the "Namespace,Key" identities in OnlyIdentities). Complements the dialogue exports without overlap. */
	static bool ExportOtherTextsPoFiles(const FString& Directory, FText& OutError, const TSet<FString>* OnlyIdentities = nullptr, const FString& OnlyCulture = FString());

	/**
	 * Rewrites every ASSET-authored occurrence of the given identical-source texts so they all
	 * share one canonical namespace/key (the identity with the most existing translations wins):
	 * gather then collapses them into a single localizable entry. C++-authored texts cannot be
	 * rewritten and count as skipped. Dirty assets must be saved and Gather re-run afterwards.
	 * OutHandled collects the non-canonical identities fully resolved (rewritten now or by an
	 * earlier merge): their manifest entries are leftovers safe to hide until the next Gather.
	 */
	static bool MergeIdenticalTexts(const FString& SourceText, const TArray<TPair<FString, FString>>& Identities, FText& OutError, int32& OutRewritten, int32& OutSkipped, TArray<TPair<FString, FString>>* OutHandled = nullptr);

	/**
	 * Rewrites every ASSET-authored occurrence of the given texts as culture invariant
	 * (non-localizable): gather then drops them from the manifest. C++-authored texts cannot
	 * be rewritten and count as skipped. Dirty assets must be saved and Gather re-run.
	 * OutHandled collects the identities fully resolved, as in MergeIdenticalTexts.
	 */
	static bool MakeTextsNonLocalizable(const FString& SourceText, const TArray<TPair<FString, FString>>& Identities, FText& OutError, int32& OutRewritten, int32& OutSkipped, TArray<TPair<FString, FString>>* OutHandled = nullptr);

	/** One dialogue line participating in a text merge. */
	struct FKzLineTextRef
	{
		UKzDialogueAsset* Asset = nullptr;
		FGuid LineId;
	};

	/**
	 * Collapses the LOCALIZATION IDENTITY of two or more identical-source line texts into one
	 * shared entry (KzDialogue.Shared namespace, fresh guid key): one translation serves them
	 * all. Lines stay separate entities; audio, timing and VO are unrelated. Assigns the
	 * SharedTextId marker (transacted), re-keys through the asset refresh, and migrates each
	 * culture's best existing translation into the shared identity. Editing a merged text
	 * detaches that line automatically. Dirty assets must be saved and Gather re-run.
	 */
	static bool MergeLineTexts(const TArray<FKzLineTextRef>& Lines, FText& OutError);

	/** Reverts one line to its own localization identity, seeding it with the shared translation so no work is lost. Transacted; save + Gather afterwards. */
	static bool UnmergeLineText(UKzDialogueAsset* Asset, const FGuid LineId, FText& OutError);

	/**
	 * Fills the pseudo culture's archive (KzDialogue settings) with machine-generated
	 * translations of every gathered source: accented letters, ~40% length padding and
	 * bracket delimiters, format arguments preserved. Adds the culture to the localization
	 * target when missing (persisted like the Localization Dashboard does) and warns when it
	 * is staged for packaging. Idempotent; re-run after each Gather, then compile the culture.
	 */
	static bool GeneratePseudoTranslations(int32& OutCount, FText& OutError);

	/** Imports a translated .po into Culture's archive. Entries whose msgid no longer matches the gathered source are skipped as drifted. */
	static bool ImportPoFile(const FString& PoPath, const FString& Culture, FKzTranslationImportStats& OutStats, FText& OutError);

	/** Interactive per-culture PO import flow (open-file dialog, ImportPoFile, stats notification). */
	static void ImportPoInteractive(const FString& Culture);

	/** Writes one clickable warning per stale translation into the KzDialogueL10N message log. Does not open the log. */
	static void LogStaleTranslations(const TArray<FKzStaleTranslation>& Stale);

	/** "Spanish (es)" style label for a culture code, falling back to the bare code when the engine has no display name for it. */
	static FText GetCultureDisplayLabel(const FString& Culture);
};