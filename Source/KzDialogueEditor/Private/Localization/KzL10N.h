// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "AssetRegistry/AssetData.h"
#include "EditorDirectories.h"
#include "Serialization/Csv/CsvParser.h"

class FLocKey;
class FLocMetadataObject;
class FLocTextHelper;
class FMessageLog;
class UKzDialogueAsset;
class UKzNamedAsset;
struct FKzDialogueLine;
struct FLocItem;

DECLARE_LOG_CATEGORY_EXTERN(LogKzDialogueL10N, Log, All);

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

/** Localization target, manifest and archives opened once, answering per-text and per-audio questions cheaply afterwards. */
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

	/** A current Culture translation of any gathered text whose source is exactly Source, to fill a missing one. False when none. */
	bool FindIdenticalSourceTranslation(const FString& Source, const FString& Culture, FString& OutTranslation) const;

private:
	FKzLocTargetInfo Target;
	TSharedPtr<FLocTextHelper> LocHelper;
	TSet<FString> LoadedCultures;

	/** Gathered identities (namespace, key) by source string, built by the first FindIdenticalSourceTranslation. */
	mutable TMap<FString, TArray<TPair<FString, FString>>> IdentitiesBySource;
};

/**
 * The one way translations reach the archives on disk: the manifest and the requested archives load once, writes
 * are collected, and Save writes each touched archive (clearing a read-only flag, logged) and announces it.
 */
class FKzArchiveWriter
{
public:
	/** Loads the target, its manifest and the archives of Cultures (every foreign culture when empty); a missing archive is created. */
	bool Open(FText& OutError, const TArray<FString>& Cultures = TArray<FString>());

	const FKzLocTargetInfo& GetTarget() const { return Target; }
	FLocTextHelper& GetHelper() const { return *Helper; }

	/** FLocTextHelper::ImportTranslation, remembering the culture for Save. */
	bool Write(const FString& Culture, const FLocKey& Namespace, const FLocKey& Key, TSharedPtr<FLocMetadataObject> KeyMetadata, const FLocItem& Source, const FLocItem& Translation, bool bOptional = false);

	/** Saves every archive written since Open. */
	bool Save(FText& OutError);

	bool SaveManifest(FText& OutError);

	/** Fired after the manifest or translations reach the disk, so every open view of them refreshes. */
	static FSimpleMulticastDelegate OnArchivesChanged;

private:
	FKzLocTargetInfo Target;
	TSharedPtr<FLocTextHelper> Helper;
	TSet<FString> WrittenCultures;
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

	/** Rows whose Translation cell still holds what the export pre-filled: skipped, so archive edits made meanwhile survive. */
	int32 Unchanged = 0;

	/** Rows whose placeholders do not match the source's: skipped, they would break the line at runtime. */
	int32 Rejected = 0;

	/** Imported rows over their line's MaxCharacters: written, and flagged in the log. */
	int32 TooLong = 0;
};

/** One localizable text of the dialogue set: a line, a speaker name field or a shared word form. */
struct FKzLocText
{
	FString Namespace;
	FString Key;
	FString Source;

	/** The dialogue asset, speaker or word authoring the text. */
	const UObject* Owner = nullptr;

	/** Set for line texts. */
	const FKzDialogueLine* Line = nullptr;

	/** Speaker field key or word form name ("Text" for a word's default form); empty for lines. */
	FString Field;

	/** Drift hash the CSV round-trips; 0 for words, which round-trip their manifest source hash instead (ExportHash). */
	uint32 SourceHash = 0;
};

/**
 * Localization tooling for the dialogue set: the target, its manifest and archives (KzL10NTarget.cpp), the CSV
 * round-trip with translator context and drift detection (KzL10NCsv.cpp), the PO round-trip (KzL10NPo.cpp), and
 * the operations that rewrite authored texts inside transactions (KzL10NRewrite.cpp).
 */
namespace KzDialogueL10N
{
	/** The "Dialogue Localization" message log: one page per import, plus the inline edits. Registered at module startup. */
	inline const FName MessageLogName(TEXT("KzDialogueL10N"));

	/** Adds the Export / Import entries to the content browser context menu of UKzDialogueAsset. */
	void RegisterMenus();

	/** Resolves the project's localization target layout from the KzDialogue settings target name. Fails when the target was never created. */
	bool ReadLocTargetInfo(FKzLocTargetInfo& Out, FText& OutError);

	/** "Spanish (es)" style label for a culture code, falling back to the bare code when the engine has no display name for it. */
	FText GetCultureDisplayLabel(const FString& Culture);

	/** The tokens the project's named assets claim, read from the asset registry. */
	TSet<FName> CollectNamedTokens();

	/** The placeholders that break Translation against Source: a source one it lost, or one it added that no named asset claims. A named token may change its part ("{Kirzo}" as "{Kirzo:given}") and may be added (the "{player:gender}|gender(...)" mechanism lives only in translations). */
	TSet<FString> FindPlaceholderProblems(const FString& Source, const FString& Translation, const TSet<FName>& NamedTokens);

	/** The texts of Assets' lines passing LineFilter, of their speakers (and the aliases' when unfiltered) and of those speakers' words, each identity once. bAllSpeakers / bAllWords add every speaker / word asset of the project. */
	void CollectLocTexts(const TArray<UKzDialogueAsset*>& Assets, const FKzExportLineFilter& LineFilter, bool bAllSpeakers, bool bAllWords, TArray<FKzLocText>& Out);

	/** Writes one translation for every given (Namespace, Key) identity straight into Culture's archive: the panel's inline editing. Source is the string the translation was made against (the current one). There is no undo, so each write lands on the message log with the translation it replaced. */
	bool WriteTranslation(const FString& Culture, const TArray<TPair<FString, FString>>& Identities, const FString& Source, const FString& Translation, FText& OutError);

	/**
	 * Fills the pseudo culture's archive (KzDialogue settings) with machine-generated
	 * translations of every gathered source: accented letters, ~40% length padding and
	 * bracket delimiters, format arguments preserved. Adds the culture to the localization
	 * target when missing (persisted like the Localization Dashboard does) and warns when it
	 * is staged for packaging. Idempotent; re-run after each Gather, then compile the culture.
	 */
	bool GeneratePseudoTranslations(int32& OutCount, FText& OutError);

	/** Writes the assets' localizable texts to CsvPath. Fails with OutError when there is nothing to export or the file cannot be written. OtherIdentities appends those gathered project texts ("Namespace,Key") as asset-less rows validated by the manifest at import. TranslationCulture pre-fills the Translation column with that culture's current translations (stale ones land in StaleTranslation as reference only). */
	bool ExportAssets(const TArray<UKzDialogueAsset*>& Assets, const FString& CsvPath, FText& OutError, const FKzExportLineFilter& LineFilter = nullptr, const TSet<FString>* OtherIdentities = nullptr, const FString& TranslationCulture = FString());

	/** Interactive export flow (load assets, dirty warning, save-file dialog, ExportAssets, notification). Shared by the content browser menu and the dashboard. OtherIdentities appends those project texts to the same CSV; TranslationCulture pre-fills its translations and suffixes the default file name ("_es"). */
	void ExportCsvInteractive(TArray<FAssetData> SelectedAssets, const FKzExportLineFilter& LineFilter = nullptr, TSharedPtr<TSet<FString>> OtherIdentities = nullptr, const FString& TranslationCulture = FString());

	/** Imports a filled CSV into the localization target archive of the given culture. Drift and resolution failures are counted in OutStats and detailed on a message log page. */
	bool ImportCsv(const FString& CsvPath, const FString& Culture, FKzTranslationImportStats& OutStats, FText& OutError);

	/** Interactive per-culture import flow (open-file dialog, culture-mismatch guard, ImportCsv, stats notification). Shared by the content browser menu and the dashboard. */
	void ImportCsvInteractive(const FString& Culture);

	/** Writes one .po per foreign culture into Directory (<Culture>/<Target>.po), scoped to the assets' lines passing LineFilter. Context (speaker, notes, max characters) travels as "#." comments; existing translations fill msgstr, stale ones flagged "#, fuzzy". OnlyCulture narrows the file set to that culture. */
	bool ExportPoFiles(const TArray<UKzDialogueAsset*>& Assets, const FString& Directory, FText& OutError, const FKzExportLineFilter& LineFilter = nullptr, const FString& OnlyCulture = FString());

	/** Writes one <Culture>/<Target>_Other.po per foreign culture with the gathered texts outside the dialogue namespaces (all of them, or just the "Namespace,Key" identities in OnlyIdentities). Complements the dialogue exports without overlap. */
	bool ExportOtherTextsPoFiles(const FString& Directory, FText& OutError, const TSet<FString>* OnlyIdentities = nullptr, const FString& OnlyCulture = FString());

	/** Interactive PO export: directory dialog + ExportPoFiles + notification. With bWithOtherTexts the same directory also receives the _Other files (OtherIdentities narrows them to those "Namespace,Key" texts); assets may then be empty for an Other-texts-only export. */
	void ExportPoInteractive(TArray<FAssetData> SelectedAssets, const FKzExportLineFilter& LineFilter = nullptr, bool bWithOtherTexts = false, TSharedPtr<TSet<FString>> OtherIdentities = nullptr, const FString& OnlyCulture = FString());

	/** Imports a translated .po into Culture's archive. Entries whose msgid no longer matches the gathered source are skipped as drifted. */
	bool ImportPoFile(const FString& PoPath, const FString& Culture, FKzTranslationImportStats& OutStats, FText& OutError);

	/** Interactive per-culture PO import flow (open-file dialog, ImportPoFile, stats notification). */
	void ImportPoInteractive(const FString& Culture);

	/**
	 * Token-rename refactor, bound to UKzNamedAsset::OnTokenRenamed at module startup:
	 * rewrites every dialogue line using "{OldToken}" / "{OldToken:part}" to the asset's new
	 * token (case-insensitive, inside the ongoing edit transaction so Ctrl+Z reverts them),
	 * and the dialogue archives' sources and translations so nothing turns stale. Undo and redo
	 * broadcast the rename again, so the archives follow. Up-to-date recorded-take hashes are
	 * preserved: renaming a token does not change what was spoken.
	 */
	void RenameNamedTokenReferences(UKzNamedAsset* Renamed, FName OldToken);

	/** Culture-less import of SourceText edits made in an exported CSV: rewrites the AUTHORED texts (dialogue lines, speaker fields, project-text occurrences) keeping their namespace/key. A row applies only when its text did not change after the export (hash still matches); both-sides edits count as conflicted and the asset wins. Transacted; dirty assets must be saved and Gather re-run. */
	bool ImportSourceFixes(const FString& CsvPath, FKzSourceFixStats& OutStats, FText& OutError);

	/** Interactive flow for ImportSourceFixes: open-file dialog + stats notification. */
	void ImportSourceFixesInteractive();

	/**
	 * Rewrites every ASSET-authored occurrence of the given identical-source texts so they all
	 * share one canonical namespace/key (the identity with the most existing translations wins):
	 * gather then collapses them into a single localizable entry. C++-authored texts cannot be
	 * rewritten and count as skipped. Dirty assets must be saved and Gather re-run afterwards.
	 * OutHandled collects the non-canonical identities fully resolved (rewritten now or by an
	 * earlier merge): their manifest entries are leftovers safe to hide until the next Gather.
	 */
	bool MergeIdenticalTexts(const FString& SourceText, const TArray<TPair<FString, FString>>& Identities, FText& OutError, int32& OutRewritten, int32& OutSkipped, TArray<TPair<FString, FString>>* OutHandled = nullptr);

	/**
	 * Rewrites every ASSET-authored occurrence of the given texts as culture invariant
	 * (non-localizable): gather then drops them from the manifest. C++-authored texts cannot
	 * be rewritten and count as skipped. Dirty assets must be saved and Gather re-run.
	 * OutHandled collects the identities fully resolved, as in MergeIdenticalTexts.
	 */
	bool MakeTextsNonLocalizable(const FString& SourceText, const TArray<TPair<FString, FString>>& Identities, FText& OutError, int32& OutRewritten, int32& OutSkipped, TArray<TPair<FString, FString>>* OutHandled = nullptr);

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
	bool MergeLineTexts(const TArray<FKzLineTextRef>& Lines, FText& OutError);

	/** Reverts one line to its own localization identity, seeding it with the shared translation so no work is lost. Transacted; save + Gather afterwards. */
	bool UnmergeLineText(UKzDialogueAsset* Asset, const FGuid LineId, FText& OutError);
}

/** Shared by the KzL10N*.cpp files; not for other callers. */
namespace KzDialogueL10N::Detail
{
	/** LF line endings, as the archives store them: spreadsheet round-trips rewrite in-cell newlines as CRLF. */
	FString NormalizeNewlines(FString Value);

	/** Newline-flavor-insensitive source comparison: file round-trips (the PO \n escape, spreadsheet editors) lose the original CRLF/LF flavor, and that must not count as drift. */
	bool SourceMatches(const FString& A, const FString& B);

	/** CRC32 of Value's LF form: what an export wrote, so an import tells edited cells and changed sources from untouched ones. */
	uint32 ExportHash(const FString& Value);

	/** True for the error values a spreadsheet leaves when it reads a cell starting with '=', '+', '-' or '@' as a formula. */
	bool IsSpreadsheetError(const FString& Value);

	/** A CSV with its columns indexed by header name, so reordered or extended files keep importing. */
	struct FKzCsvTable
	{
		TUniquePtr<FCsvParser> Parser;
		TMap<FString, int32> Columns;
	};

	/** Reads CsvPath and indexes its header row; fails when it has no data rows or lacks one of RequiredColumns. */
	bool ParseTranslationCsv(const FString& CsvPath, std::initializer_list<const TCHAR*> RequiredColumns, FKzCsvTable& Out, FText& OutError);

	/** The dialogue line text or speaker field a CSV row targets, with its drift hash; null when the asset, line or field is gone. */
	FText* ResolveAuthoredText(UObject* Asset, const FString& Namespace, const FString& Key, uint32& OutHash, bool& bOutSharedLine);

	/** Opens the message log page of one operation (an import). */
	void BeginLogPage(FMessageLog& Log, const FText& Title);

	/** A row an import skipped, with its asset as a clickable token when it has one. */
	void LogRow(FMessageLog& Log, const FString& AssetPath, const FString& Message);

	/** The checks on a translation an import is about to write. False (Rejected, logged) when its placeholders break the source's; over MaxCharacters it is still written, logged and counted TooLong. What names the row in the log. */
	bool CheckImportedTranslation(const FString& Source, const FString& Translation, int32 MaxCharacters, const TSet<FName>& NamedTokens, FKzTranslationImportStats& Stats, FMessageLog& Log, const FString& AssetPath, const FString& What);

	/** Wrong-file guard of the interactive imports: asks before importing a file that declares another culture (DeclaredCulture, from its content) or, when it declares none, whose name mentions another one. */
	bool ConfirmImportCulture(const FString& FilePath, const FString& DeclaredCulture, const FString& Culture);

	/** The toast closing an interactive flow; bWithLogLink adds a "Show log" link to the message log. */
	void ShowNotification(const FText& Message, bool bSuccess, bool bWithLogLink = false);

	/** The folder the last dialog of this kind used, so a round trip does not start from Saved each time. */
	FString LastDirectory(ELastDirectory::Type Kind);
}