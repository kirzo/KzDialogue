// Copyright 2026 kirzo

#include "Localization/KzL10N.h"

#include "KzDialogueAsset.h"
#include "KzSpeakerAsset.h"
#include "KzWordAsset.h"
#include "ContentBrowserMenuContexts.h"
#include "DesktopPlatformModule.h"
#include "Framework/Application/SlateApplication.h"
#include "Internationalization/InternationalizationManifest.h"
#include "Logging/MessageLog.h"
#include "LocTextHelper.h"
#include "Misc/FileHelper.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"
#include "ToolMenus.h"

#define LOCTEXT_NAMESPACE "KzDialogueL10N"

using namespace KzDialogueL10N::Detail;

namespace
{
	FString CsvEscape(const FString& Value)
	{
		FString Escaped = Value;
		Escaped.ReplaceInline(TEXT("\""), TEXT("\"\""));
		return FString::Printf(TEXT("\"%s\""), *Escaped);
	}
}

namespace KzDialogueL10N::Detail
{
	bool ParseTranslationCsv(const FString& CsvPath, std::initializer_list<const TCHAR*> RequiredColumns, FKzCsvTable& Out, FText& OutError)
	{
		FString Content;
		if (!FFileHelper::LoadFileToString(Content, *CsvPath))
		{
			OutError = FText::Format(LOCTEXT("ImportReadFailed", "Could not read {0}."), FText::FromString(CsvPath));
			return false;
		}

		Out.Parser = MakeUnique<FCsvParser>(MoveTemp(Content));
		const FCsvParser::FRows& Rows = Out.Parser->GetRows();
		if (Rows.Num() < 2)
		{
			OutError = LOCTEXT("ImportEmpty", "The CSV has no data rows.");
			return false;
		}

		for (int32 i = 0; i < Rows[0].Num(); ++i)
		{
			Out.Columns.Add(FString(Rows[0][i]), i);
		}
		for (const TCHAR* Column : RequiredColumns)
		{
			if (!Out.Columns.Contains(Column))
			{
				OutError = FText::Format(LOCTEXT("ImportMissingColumn", "The CSV is missing the required column '{0}'."), FText::FromString(Column));
				return false;
			}
		}
		return true;
	}

	FText* ResolveAuthoredText(UObject* Asset, const FString& Namespace, const FString& Key, uint32& OutHash, bool& bOutSharedLine)
	{
		bOutSharedLine = false;
		if (UKzDialogueAsset* Dialogue = Cast<UKzDialogueAsset>(Asset))
		{
			const int32 LineIdx = Dialogue->IndexOfLineByTextId(Namespace, Key);
			if (LineIdx == INDEX_NONE) { return nullptr; }

			FKzDialogueLine& Line = Dialogue->Lines[LineIdx];
			OutHash = Line.SourceTextHash;
			bOutSharedLine = Line.SharedTextId.IsValid();
			return &Line.Text;
		}

		FText* Found = nullptr;
		if (UKzSpeakerAsset* Speaker = Cast<UKzSpeakerAsset>(Asset))
		{
			Speaker->ForEachLocalizedField([&](const TCHAR* FieldKey, FText& Text, FKzWordText*)
			{
				if (Key == FieldKey) { Found = &Text; OutHash = KzComputeSourceTextHash(Text); }
			});
		}
		return Found;
	}

	bool ConfirmImportCulture(const FString& FilePath, const FString& DeclaredCulture, const FString& Culture)
	{
		FText Message;
		if (!DeclaredCulture.IsEmpty())
		{
			if (DeclaredCulture.Equals(Culture, ESearchCase::IgnoreCase)) { return true; }
			Message = FText::Format(LOCTEXT("ImportDeclaredCultureMismatch", "'{0}' was exported for culture '{1}', but you are importing into '{2}'. Continue anyway?"),
				FText::FromString(FPaths::GetCleanFilename(FilePath)), FText::FromString(DeclaredCulture), FText::FromString(Culture));
		}
		else
		{
			// No declared culture: fall back to a culture token in the file name.
			FKzLocTargetInfo Target;
			FText TargetError;
			if (!KzDialogueL10N::ReadLocTargetInfo(Target, TargetError)) { return true; }

			TArray<FString> Cultures = Target.ForeignCultures;
			if (!Target.NativeCulture.IsEmpty()) { Cultures.Add(Target.NativeCulture); }

			// Split on '_', ' ' and '.' only, so multi-part codes like "es-MX" survive as one token.
			const TCHAR* Delims[] = { TEXT("_"), TEXT(" "), TEXT(".") };
			TArray<FString> Tokens;
			FPaths::GetBaseFilename(FilePath).ParseIntoArray(Tokens, Delims, UE_ARRAY_COUNT(Delims));

			FString MismatchCulture;
			for (const FString& Token : Tokens)
			{
				for (const FString& Other : Cultures)
				{
					if (Token.Equals(Other, ESearchCase::IgnoreCase) && !Other.Equals(Culture, ESearchCase::IgnoreCase))
					{
						MismatchCulture = Other;
					}
				}
			}
			if (MismatchCulture.IsEmpty()) { return true; }

			Message = FText::Format(LOCTEXT("ImportCultureMismatch", "The file name '{0}' mentions culture '{1}', but you are importing into '{2}'. Continue anyway?"),
				FText::FromString(FPaths::GetCleanFilename(FilePath)), FText::FromString(MismatchCulture), FText::FromString(Culture));
		}
		return FMessageDialog::Open(EAppMsgType::YesNo, Message) == EAppReturnType::Yes;
	}
}

namespace KzDialogueL10N
{
	void ExportCsvInteractive(TArray<FAssetData> SelectedAssets, const FKzExportLineFilter& LineFilter, TSharedPtr<TSet<FString>> OtherIdentities, const FString& TranslationCulture)
	{
		TArray<UKzDialogueAsset*> Assets;
		for (const FAssetData& Data : SelectedAssets)
		{
			if (UKzDialogueAsset* Asset = Cast<UKzDialogueAsset>(Data.GetAsset()))
			{
				Assets.Add(Asset);
				if (Asset->GetOutermost()->IsDirty())
				{
					UE_LOG(LogKzDialogueL10N, Warning, TEXT("Exporting '%s' with unsaved changes; save it before gathering or the CSV keys may not match the manifest."), *Asset->GetPathName());
				}
			}
		}
		if (Assets.IsEmpty()) { return; }

		// The culture suffix names the round-trip file and feeds the import's wrong-file guard.
		const FString CultureSuffix = TranslationCulture.IsEmpty() ? FString() : TEXT("_") + TranslationCulture;
		const FString DefaultName = (Assets.Num() == 1 ? Assets[0]->GetName() : FString(TEXT("KzDialogue"))) + TEXT("_Translation") + CultureSuffix + TEXT(".csv");
		TArray<FString> OutFiles;
		const void* ParentWindow = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);
		if (!FDesktopPlatformModule::Get()->SaveFileDialog(ParentWindow, LOCTEXT("ExportDialogTitle", "Export translation CSV").ToString(),
			LastDirectory(ELastDirectory::GENERIC_EXPORT), DefaultName, TEXT("CSV files (*.csv)|*.csv"), EFileDialogFlags::None, OutFiles))
		{
			return;
		}
		FEditorDirectories::Get().SetLastDirectory(ELastDirectory::GENERIC_EXPORT, FPaths::GetPath(OutFiles[0]));

		FText Error;
		if (KzDialogueL10N::ExportAssets(Assets, OutFiles[0], Error, LineFilter, OtherIdentities.Get(), TranslationCulture))
		{
			ShowNotification(FText::Format(LOCTEXT("ExportDone", "Translation CSV exported to {0}."), FText::FromString(OutFiles[0])), true);
		}
		else
		{
			ShowNotification(Error, false);
		}
	}

	void ImportCsvInteractive(const FString& Culture)
	{
		TArray<FString> OutFiles;
		const void* ParentWindow = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);
		if (!FDesktopPlatformModule::Get()->OpenFileDialog(ParentWindow,
			FText::Format(LOCTEXT("ImportDialogTitle", "Import translation CSV ({0})"), FText::FromString(Culture)).ToString(),
			LastDirectory(ELastDirectory::GENERIC_IMPORT), FString(), TEXT("CSV files (*.csv)|*.csv"), EFileDialogFlags::None, OutFiles))
		{
			return;
		}
		FEditorDirectories::Get().SetLastDirectory(ELastDirectory::GENERIC_IMPORT, FPaths::GetPath(OutFiles[0]));

		// Per-culture exports stamp every row's Culture cell; the first one says what the file is for.
		FString DeclaredCulture;
		{
			FKzCsvTable Table;
			FText ParseError;
			const int32* CultureCol = ParseTranslationCsv(OutFiles[0], {}, Table, ParseError) ? Table.Columns.Find(TEXT("Culture")) : nullptr;
			const FCsvParser::FRows* Rows = CultureCol ? &Table.Parser->GetRows() : nullptr;
			for (int32 RowIdx = 1; Rows && RowIdx < Rows->Num() && DeclaredCulture.IsEmpty(); ++RowIdx)
			{
				if ((*Rows)[RowIdx].IsValidIndex(*CultureCol)) { DeclaredCulture = FString((*Rows)[RowIdx][*CultureCol]).TrimStartAndEnd(); }
			}
		}
		if (!ConfirmImportCulture(OutFiles[0], DeclaredCulture, Culture)) { return; }

		FKzTranslationImportStats Stats;
		FText Error;
		if (!KzDialogueL10N::ImportCsv(OutFiles[0], Culture, Stats, Error))
		{
			ShowNotification(Error, false);
			return;
		}

		FFormatNamedArguments Args;
		Args.Add(TEXT("Culture"), FText::FromString(Culture));
		Args.Add(TEXT("Imported"), Stats.Imported);
		Args.Add(TEXT("TooLong"), Stats.TooLong);
		Args.Add(TEXT("Unchanged"), Stats.Unchanged);
		Args.Add(TEXT("Drifted"), Stats.Drifted);
		Args.Add(TEXT("Rejected"), Stats.Rejected);
		Args.Add(TEXT("Untranslated"), Stats.Untranslated);
		Args.Add(TEXT("Unresolved"), Stats.Unresolved);
		const FText Summary = FText::Format(
			LOCTEXT("ImportSummary", "{Culture}: {Imported} imported ({TooLong} too long), {Unchanged} unchanged, {Drifted} drifted and {Rejected} with broken placeholders (skipped), {Untranslated} untranslated, {Unresolved} unresolved. Run Compile Text in the Localization Dashboard to apply."),
			Args);
		ShowNotification(Summary, Stats.Drifted == 0 && Stats.Rejected == 0 && Stats.Unresolved == 0, /*bWithLogLink=*/true);
	}
}

void KzDialogueL10N::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(TEXT("KzDialogueEditor"));

	UToolMenu* Menu = UToolMenus::Get()->ExtendMenu(TEXT("ContentBrowser.AssetContextMenu.KzDialogueAsset"));
	FToolMenuSection& Section = Menu->AddSection(TEXT("KzDialogueLocalization"), LOCTEXT("LocalizationSection", "Localization"));

	Section.AddDynamicEntry(TEXT("KzDialogueL10N"), FNewToolMenuSectionDelegate::CreateLambda([](FToolMenuSection& InSection)
	{
		const UContentBrowserAssetContextMenuContext* Context = InSection.FindContext<UContentBrowserAssetContextMenuContext>();
		if (!Context) { return; }

		InSection.AddMenuEntry(
			TEXT("ExportTranslationCsv"),
			LOCTEXT("ExportCsv", "Export Translation CSV..."),
			LOCTEXT("ExportCsvTip", "Export the selected dialogue assets to a CSV with translator context (speaker, notes, max characters, tags, audio)."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([SelectedAssets = Context->SelectedAssets]() { KzDialogueL10N::ExportCsvInteractive(SelectedAssets); })));

		InSection.AddSubMenu(
			TEXT("ImportTranslationCsv"),
			LOCTEXT("ImportCsv", "Import Translation CSV"),
			LOCTEXT("ImportCsvTip", "Import a translated CSV into the localization target archive of a culture. Rows whose source text drifted since export are skipped and reported."),
			FNewToolMenuDelegate::CreateLambda([](UToolMenu* SubMenu)
			{
				FToolMenuSection& CultureSection = SubMenu->AddSection(NAME_None);

				FKzLocTargetInfo Target;
				FText Error;
				if (!KzDialogueL10N::ReadLocTargetInfo(Target, Error) || Target.ForeignCultures.IsEmpty())
				{
					CultureSection.AddMenuEntry(TEXT("NoCultures"),
						LOCTEXT("NoCultures", "No localization target cultures found"),
						Error,
						FSlateIcon(),
						FUIAction(FExecuteAction(), FCanExecuteAction::CreateLambda([] { return false; })));
					return;
				}

				for (const FString& Culture : Target.ForeignCultures)
				{
					CultureSection.AddMenuEntry(FName(*Culture),
						KzDialogueL10N::GetCultureDisplayLabel(Culture),
						FText::Format(LOCTEXT("ImportCultureTip", "Import into the '{0}' archive of target '{1}'."), FText::FromString(Culture), FText::FromString(Target.TargetName)),
						FSlateIcon(),
						FUIAction(FExecuteAction::CreateLambda([Culture]() { KzDialogueL10N::ImportCsvInteractive(Culture); })));
				}
			}));
	}));
}

bool KzDialogueL10N::ExportAssets(const TArray<UKzDialogueAsset*>& Assets, const FString& CsvPath, FText& OutError, const FKzExportLineFilter& LineFilter, const TSet<FString>* OtherIdentities, const FString& TranslationCulture)
{
	FString Csv = TEXT("Asset,Namespace,Key,Speaker,SourceText,Translation,TranslatorNotes,MaxCharacters,Audio,LocalizeAudio,SourceHash,StaleTranslation,TranslationHash,Culture");
	Csv += LINE_TERMINATOR;

	// Archives feed the per-culture translation columns and the project-text rows.
	FKzLocQuery Query;
	bool bQueryLoaded = false;
	if (!TranslationCulture.IsEmpty() || (OtherIdentities && !OtherIdentities->IsEmpty()))
	{
		FText QueryError;
		bQueryLoaded = Query.Load(QueryError);
		if (!TranslationCulture.IsEmpty() && !bQueryLoaded)
		{
			OutError = QueryError;
			return false;
		}
	}

	// Current translations pre-fill the Translation column so the file round-trips per culture.
	// Stale ones land in StaleTranslation instead: visible as reference, but a re-import cannot
	// mistake them for confirmed work (only the Translation column imports).
	auto FillCultureCells = [&Query, &bQueryLoaded, &TranslationCulture](const FString& Namespace, const FString& Key, const FString& Source, FString& OutTranslation, FString& OutStale)
	{
		if (TranslationCulture.IsEmpty() || !bQueryLoaded) { return; }

		FString ArchiveSource;
		FString ArchiveTranslation;
		if (!Query.GetArchiveEntry(Namespace, Key, TranslationCulture, ArchiveSource, ArchiveTranslation)) { return; }

		if (Query.GetTextState(Namespace, Key, Source, TranslationCulture) == EKzTranslationState::Stale)
		{
			OutStale = ArchiveTranslation;
		}
		else
		{
			OutTranslation = ArchiveTranslation;
		}
	};

	int32 NumRows = 0;
	auto AddRow = [&](const FString& AssetPath, const FString& Namespace, const FString& Key, const FString& Speaker, const FString& Source, const FString& Notes, int32 MaxChars, const FString& Audio, const TCHAR* LocalizeAudio, uint32 SourceHash)
	{
		FString Translation;
		FString StaleTranslation;
		FillCultureCells(Namespace, Key, Source, Translation, StaleTranslation);

		Csv += CsvEscape(AssetPath) + TEXT(",");
		Csv += CsvEscape(Namespace) + TEXT(",");
		Csv += CsvEscape(Key) + TEXT(",");
		Csv += CsvEscape(Speaker) + TEXT(",");
		Csv += CsvEscape(Source) + TEXT(",");
		Csv += CsvEscape(Translation) + TEXT(",");
		Csv += CsvEscape(Notes) + TEXT(",");
		Csv += FString::FromInt(MaxChars) + TEXT(",");
		Csv += CsvEscape(Audio) + TEXT(",");
		Csv += FString(LocalizeAudio) + TEXT(",");
		Csv += FString::Printf(TEXT("%u"), SourceHash) + TEXT(",");
		Csv += CsvEscape(StaleTranslation) + TEXT(",");
		// Fingerprints the pre-filled translation, so a re-import leaves untouched cells alone.
		Csv += (Translation.IsEmpty() ? FString() : FString::Printf(TEXT("%u"), ExportHash(Translation))) + TEXT(",");
		// The culture a per-culture file is for, so its import can tell a wrong file by content.
		Csv += TranslationCulture;
		Csv += LINE_TERMINATOR;
		++NumRows;
	};

	// Word and project-text rows leave Asset empty: the manifest validates them at import, against their source's hash.
	TArray<FKzLocText> Texts;
	CollectLocTexts(Assets, LineFilter, /*bAllSpeakers=*/false, /*bAllWords=*/true, Texts);
	for (const FKzLocText& Text : Texts)
	{
		if (const FKzDialogueLine* Line = Text.Line)
		{
			// Empty for narration lines; GetEditorLabel's "<Narration>" placeholder is editor UI, not translator context.
			const FString Speaker = Line->Speaker.IsValid() ? Line->Speaker.GetEditorLabel().ToString() : FString();
			// "no" tells the studio this take deliberately plays in every culture; empty otherwise.
			const TCHAR* LocalizeAudio = !Line->Audio.IsNull() && !Line->bLocalizeAudio ? TEXT("no") : TEXT("");
			AddRow(Text.Owner->GetPathName(), Text.Namespace, Text.Key, Speaker, Text.Source, Line->TranslatorNotes, Line->MaxCharacters, Line->Audio.ToSoftObjectPath().ToString(), LocalizeAudio, Text.SourceHash);
		}
		else if (const UKzSpeakerAsset* Speaker = Cast<UKzSpeakerAsset>(Text.Owner))
		{
			AddRow(Speaker->GetPathName(), Text.Namespace, Text.Key, Speaker->GetResolvedDisplayName().ToString(), Text.Source, FString(), 0, FString(), TEXT(""), Text.SourceHash);
		}
		else
		{
			AddRow(FString(), Text.Namespace, Text.Key, FString(), Text.Source, FString(), 0, FString(), TEXT(""), ExportHash(Text.Source));
		}
	}

	// Project texts ride along the same way when requested.
	if (OtherIdentities && !OtherIdentities->IsEmpty() && bQueryLoaded)
	{
		Query.EnumerateOtherTexts([&AddRow, OtherIdentities](const FString& Namespace, const FString& Key, const FString& Source)
		{
			if (OtherIdentities->Contains(Namespace + TEXT(",") + Key))
			{
				AddRow(FString(), Namespace, Key, FString(), Source, FString(), 0, FString(), TEXT(""), ExportHash(Source));
			}
		});
	}

	if (NumRows == 0)
	{
		OutError = LOCTEXT("ExportNoRows", "The selected assets contain no localizable text.");
		return false;
	}

	if (!FFileHelper::SaveStringToFile(Csv, *CsvPath, FFileHelper::EEncodingOptions::ForceUTF8))
	{
		OutError = FText::Format(LOCTEXT("ExportWriteFailed", "Could not write {0}."), FText::FromString(CsvPath));
		return false;
	}

	UE_LOG(LogKzDialogueL10N, Log, TEXT("Exported %d translation rows from %d asset(s) to %s."), NumRows, Assets.Num(), *CsvPath);
	return true;
}

bool KzDialogueL10N::ImportCsv(const FString& CsvPath, const FString& Culture, FKzTranslationImportStats& OutStats, FText& OutError)
{
	FKzCsvTable Table;
	if (!ParseTranslationCsv(CsvPath, { TEXT("Asset"), TEXT("Namespace"), TEXT("Key"), TEXT("Translation"), TEXT("SourceHash") }, Table, OutError)) { return false; }
	const FCsvParser::FRows& Rows = Table.Parser->GetRows();
	const int32* AssetCol = Table.Columns.Find(TEXT("Asset"));
	const int32* NamespaceCol = Table.Columns.Find(TEXT("Namespace"));
	const int32* KeyCol = Table.Columns.Find(TEXT("Key"));
	const int32* SourceTextCol = Table.Columns.Find(TEXT("SourceText"));
	const int32* TranslationCol = Table.Columns.Find(TEXT("Translation"));
	const int32* HashCol = Table.Columns.Find(TEXT("SourceHash"));
	const int32* TranslationHashCol = Table.Columns.Find(TEXT("TranslationHash"));
	const int32* MaxCharactersCol = Table.Columns.Find(TEXT("MaxCharacters"));
	const TSet<FName> NamedTokens = CollectNamedTokens();

	FKzLocTargetInfo Target;
	if (!ReadLocTargetInfo(Target, OutError))
	{
		return false;
	}
	if (!Target.ForeignCultures.Contains(Culture))
	{
		OutError = FText::Format(LOCTEXT("ImportUnknownCulture", "Culture '{0}' is not configured in localization target '{1}'."), FText::FromString(Culture), FText::FromString(Target.TargetName));
		return false;
	}

	FKzArchiveWriter Writer;
	if (!Writer.Open(OutError, { Culture })) { return false; }
	FLocTextHelper& LocHelper = Writer.GetHelper();

	FMessageLog Log(MessageLogName);
	BeginLogPage(Log, FText::Format(LOCTEXT("CsvImportPage", "Import {0} ({1})"), FText::FromString(FPaths::GetCleanFilename(CsvPath)), FText::FromString(Culture)));

	TMap<FString, UObject*> LoadedAssets;

	for (int32 RowIdx = 1; RowIdx < Rows.Num(); ++RowIdx)
	{
		const TArray<const TCHAR*>& Cells = Rows[RowIdx];
		auto Cell = [&Cells](const int32* Col) { return Cells.IsValidIndex(*Col) ? FString(Cells[*Col]) : FString(); };

		// Spreadsheet round-trips rewrite in-cell newlines as CRLF; archives keep plain LF.
		const FString Translation = NormalizeNewlines(Cell(TranslationCol));
		if (Translation.IsEmpty())
		{
			++OutStats.Untranslated;
			continue;
		}

		const FString AssetPath = Cell(AssetCol);
		const FString Key = Cell(KeyCol);

		// An untouched pre-filled cell would revert archive edits made while the file was out.
		const FString TranslationHash = TranslationHashCol ? Cell(TranslationHashCol) : FString();
		if (!TranslationHash.IsEmpty() && ExportHash(Translation) == static_cast<uint32>(FCString::Strtoui64(*TranslationHash, nullptr, 10)))
		{
			++OutStats.Unchanged;
			continue;
		}
		if (IsSpreadsheetError(Translation))
		{
			++OutStats.Unresolved;
			LogRow(Log, AssetPath, FString::Printf(TEXT("Row %d, key '%s': the spreadsheet turned the translation into '%s' (a cell starting with =, +, - or @ read as a formula); type it again as text."), RowIdx + 1, *Key, *Translation));
			continue;
		}

		// Asset-less rows are project texts: the manifest (not an asset hash) validates them,
		// same as the PO import.
		if (AssetPath.IsEmpty())
		{
			const FString Namespace = Cell(NamespaceCol);
			const TSharedPtr<FManifestEntry> ManifestEntry = LocHelper.FindSourceText(FLocKey(Namespace), FLocKey(Key));
			if (!ManifestEntry.IsValid())
			{
				++OutStats.Unresolved;
				LogRow(Log, FString(), FString::Printf(TEXT("Unresolved row %d: project text '%s,%s' is not gathered (added since the last Gather, or gone). Run Gather Text, or drop the row."), RowIdx + 1, *Namespace, *Key));
				continue;
			}
			if (SourceTextCol && !SourceMatches(ManifestEntry->Source.Text, Cell(SourceTextCol)))
			{
				++OutStats.Drifted;
				LogRow(Log, FString(), FString::Printf(TEXT("Drifted row %d: project text '%s,%s' changed after this CSV was exported; needs retranslation."), RowIdx + 1, *Namespace, *Key));
				continue;
			}
			if (!CheckImportedTranslation(ManifestEntry->Source.Text, Translation, 0, NamedTokens, OutStats, Log, FString(), FString::Printf(TEXT("Row %d, project text '%s,%s'"), RowIdx + 1, *Namespace, *Key)))
			{
				continue;
			}

			if (Writer.Write(Culture, FLocKey(Namespace), FLocKey(Key), nullptr, FLocItem(ManifestEntry->Source.Text), FLocItem(Translation)))
			{
				++OutStats.Imported;
			}
			else
			{
				++OutStats.Unresolved;
				LogRow(Log, FString(), FString::Printf(TEXT("Failed to write row %d into the '%s' archive: project text '%s,%s'."), RowIdx + 1, *Culture, *Namespace, *Key));
			}
			continue;
		}

		UObject*& Asset = LoadedAssets.FindOrAdd(AssetPath);
		if (!Asset)
		{
			Asset = LoadObject<UObject>(nullptr, *AssetPath);
		}

		uint32 CurrentHash = 0;
		bool bSharedLine = false;
		const FText* SourceText = ResolveAuthoredText(Asset, Cell(NamespaceCol), Key, CurrentHash, bSharedLine);
		if (!SourceText)
		{
			++OutStats.Unresolved;
			LogRow(Log, AssetPath, FString::Printf(TEXT("Unresolved row %d, key '%s': the asset, line or field no longer exists."), RowIdx + 1, *Key));
			continue;
		}

		const uint32 CsvHash = static_cast<uint32>(FCString::Strtoui64(*Cell(HashCol), nullptr, 10));
		if (CsvHash != CurrentHash)
		{
			// A source-fix import may have rewritten the asset after this CSV was exported;
			// the translation is still valid when the row's SourceText matches the current one.
			const FString* CurrentSource = FTextInspector::GetSourceString(*SourceText);
			if (!SourceTextCol || !CurrentSource || !SourceMatches(*CurrentSource, Cell(SourceTextCol)))
			{
				++OutStats.Drifted;
				LogRow(Log, AssetPath, FString::Printf(TEXT("Drifted row %d, key '%s': the source text changed after this CSV was exported; needs retranslation."), RowIdx + 1, *Key));
				continue;
			}
		}

		// Into the text's current identity, so a row exported before a merge or unmerge lands where the line lives now.
		const TOptional<FString> CurrentNamespace = FTextInspector::GetNamespace(*SourceText);
		const TOptional<FString> CurrentKey = FTextInspector::GetKey(*SourceText);
		const FString* SourceString = FTextInspector::GetSourceString(*SourceText);
		const int32 MaxCharacters = MaxCharactersCol ? FCString::Atoi(*Cell(MaxCharactersCol)) : 0;
		if (SourceString && !CheckImportedTranslation(*SourceString, Translation, MaxCharacters, NamedTokens, OutStats, Log, AssetPath, FString::Printf(TEXT("Row %d, key '%s'"), RowIdx + 1, *Key)))
		{
			continue;
		}
		if (CurrentNamespace.IsSet() && CurrentKey.IsSet() && SourceString
			&& Writer.Write(Culture, FLocKey(CurrentNamespace.GetValue()), FLocKey(CurrentKey.GetValue()), nullptr, FLocItem(*SourceString), FLocItem(Translation)))
		{
			++OutStats.Imported;
		}
		else
		{
			++OutStats.Unresolved;
			LogRow(Log, AssetPath, FString::Printf(TEXT("Failed to write row %d, key '%s', into the '%s' archive."), RowIdx + 1, *Key, *Culture));
		}
	}

	if (!Writer.Save(OutError)) { return false; }

	Log.Info(FText::Format(LOCTEXT("CsvImportLogSummary", "{0} imported ({1} too long), {2} unchanged, {3} drifted, {4} rejected, {5} untranslated, {6} unresolved."), OutStats.Imported, OutStats.TooLong, OutStats.Unchanged, OutStats.Drifted, OutStats.Rejected, OutStats.Untranslated, OutStats.Unresolved));
	return true;
}

#undef LOCTEXT_NAMESPACE