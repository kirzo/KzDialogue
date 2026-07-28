// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"

class UKzDialogueAsset;

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
	static bool ExportAssets(const TArray<UKzDialogueAsset*>& Assets, const FString& CsvPath, FText& OutError);

	/** Imports a filled CSV into the localization target archive of the given culture. Drift and resolution failures are counted in OutStats and detailed in the output log. */
	static bool ImportCsv(const FString& CsvPath, const FString& Culture, FKzTranslationImportStats& OutStats, FText& OutError);

	/** Resolves the project's localization target layout from the KzDialogue settings target name. Fails when the target was never created. */
	static bool ReadLocTargetInfo(FKzLocTargetInfo& Out, FText& OutError);
};