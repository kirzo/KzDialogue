// Copyright 2026 kirzo

#include "Localization/KzDialogueTranslationCsv.h"

#include "KzDialogueAsset.h"
#include "Settings/KzDialogueSettings.h"

#include "ContentBrowserMenuContexts.h"
#include "DesktopPlatformModule.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Notifications/NotificationManager.h"
#include "LocTextHelper.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"
#include "Serialization/Csv/CsvParser.h"
#include "ToolMenus.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "KzDialogueTranslationCsv"

DEFINE_LOG_CATEGORY_STATIC(LogKzDialogueL10N, Log, All);

namespace
{
	/** Localization target layout resolved from the target's generated step ini. */
	struct FKzLocTargetInfo
	{
		FString TargetName;
		FString TargetPath;
		FString ManifestName;
		FString ArchiveName;
		FString NativeCulture;
		TArray<FString> ForeignCultures;
	};

	bool ReadLocTargetInfo(FKzLocTargetInfo& Out, FText& OutError)
	{
		const UKzDialogueSettings* Settings = UKzDialogueSettings::Get();
		Out.TargetName = Settings ? Settings->LocalizationTargetName : TEXT("Game");

		// The dashboard writes the target's cultures into every step ini; _Gather is always present.
		const FString GatherIniPath = FPaths::Combine(FPaths::ProjectConfigDir(), TEXT("Localization"), Out.TargetName + TEXT("_Gather.ini"));
		if (!FPaths::FileExists(GatherIniPath))
		{
			OutError = FText::Format(LOCTEXT("NoLocTarget", "Localization target '{0}' not found ({1} is missing). Create the target in the Localization Dashboard and run Gather Text once."),
				FText::FromString(Out.TargetName), FText::FromString(GatherIniPath));
			return false;
		}

		FConfigFile Config;
		Config.Read(GatherIniPath);

		Config.GetString(TEXT("CommonSettings"), TEXT("NativeCulture"), Out.NativeCulture);

		TArray<FString> Cultures;
		Config.GetArray(TEXT("CommonSettings"), TEXT("CulturesToGenerate"), Cultures);
		for (const FString& Culture : Cultures)
		{
			if (Culture != Out.NativeCulture)
			{
				Out.ForeignCultures.Add(Culture);
			}
		}

		Config.GetString(TEXT("CommonSettings"), TEXT("ManifestName"), Out.ManifestName);
		Config.GetString(TEXT("CommonSettings"), TEXT("ArchiveName"), Out.ArchiveName);
		if (Out.ManifestName.IsEmpty()) { Out.ManifestName = Out.TargetName + TEXT(".manifest"); }
		if (Out.ArchiveName.IsEmpty()) { Out.ArchiveName = Out.TargetName + TEXT(".archive"); }

		Out.TargetPath = FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Localization"), Out.TargetName);
		return true;
	}

	FString CsvEscape(const FString& Value)
	{
		FString Escaped = Value;
		Escaped.ReplaceInline(TEXT("\""), TEXT("\"\""));
		return FString::Printf(TEXT("\"%s\""), *Escaped);
	}

	void ShowNotification(const FText& Message, bool bSuccess)
	{
		FNotificationInfo Info(Message);
		Info.ExpireDuration = 6.0f;
		if (TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
		{
			Item->SetCompletionState(bSuccess ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
		}
	}

	void OnExportClicked(TArray<FAssetData> SelectedAssets)
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

		const FString DefaultName = (Assets.Num() == 1 ? Assets[0]->GetName() : FString(TEXT("KzDialogue"))) + TEXT("_Translation.csv");
		TArray<FString> OutFiles;
		const void* ParentWindow = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);
		if (!FDesktopPlatformModule::Get()->SaveFileDialog(ParentWindow, LOCTEXT("ExportDialogTitle", "Export translation CSV").ToString(),
			FPaths::ProjectSavedDir(), DefaultName, TEXT("CSV files (*.csv)|*.csv"), EFileDialogFlags::None, OutFiles))
		{
			return;
		}

		FText Error;
		if (FKzDialogueTranslationCsv::ExportAssets(Assets, OutFiles[0], Error))
		{
			ShowNotification(FText::Format(LOCTEXT("ExportDone", "Translation CSV exported to {0}."), FText::FromString(OutFiles[0])), true);
		}
		else
		{
			ShowNotification(Error, false);
		}
	}

	void OnImportClicked(FString Culture)
	{
		TArray<FString> OutFiles;
		const void* ParentWindow = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);
		if (!FDesktopPlatformModule::Get()->OpenFileDialog(ParentWindow,
			FText::Format(LOCTEXT("ImportDialogTitle", "Import translation CSV ({0})"), FText::FromString(Culture)).ToString(),
			FPaths::ProjectSavedDir(), FString(), TEXT("CSV files (*.csv)|*.csv"), EFileDialogFlags::None, OutFiles))
		{
			return;
		}

		// Wrong-file guard: warn when the file name carries a culture token that is not the chosen culture.
		FKzLocTargetInfo Target;
		FText TargetError;
		if (ReadLocTargetInfo(Target, TargetError))
		{
			TArray<FString> Cultures = Target.ForeignCultures;
			if (!Target.NativeCulture.IsEmpty()) { Cultures.Add(Target.NativeCulture); }

			// Split on '_', ' ' and '.' only, so multi-part codes like "es-MX" survive as one token.
			const TCHAR* Delims[] = { TEXT("_"), TEXT(" "), TEXT(".") };
			TArray<FString> Tokens;
			FPaths::GetBaseFilename(OutFiles[0]).ParseIntoArray(Tokens, Delims, UE_ARRAY_COUNT(Delims));

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

			if (!MismatchCulture.IsEmpty())
			{
				const FText Message = FText::Format(
					LOCTEXT("ImportCultureMismatch", "The file name '{0}' mentions culture '{1}', but you are importing into '{2}'. Continue anyway?"),
					FText::FromString(FPaths::GetCleanFilename(OutFiles[0])), FText::FromString(MismatchCulture), FText::FromString(Culture));
				if (FMessageDialog::Open(EAppMsgType::YesNo, Message) != EAppReturnType::Yes)
				{
					return;
				}
			}
		}

		FKzTranslationImportStats Stats;
		FText Error;
		if (!FKzDialogueTranslationCsv::ImportCsv(OutFiles[0], Culture, Stats, Error))
		{
			ShowNotification(Error, false);
			return;
		}

		const FText Summary = FText::Format(
			LOCTEXT("ImportSummary", "{0}: {1} imported, {2} drifted (skipped, see Output Log), {3} untranslated, {4} unresolved. Run Compile Text in the Localization Dashboard to apply."),
			FText::FromString(Culture), Stats.Imported, Stats.Drifted, Stats.Untranslated, Stats.Unresolved);
		ShowNotification(Summary, Stats.Drifted == 0 && Stats.Unresolved == 0);
	}
}

void FKzDialogueTranslationCsv::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(TEXT("KzDialogueEditor"));

	UToolMenu* Menu = UToolMenus::Get()->ExtendMenu(TEXT("ContentBrowser.AssetContextMenu.KzDialogueAsset"));
	FToolMenuSection& Section = Menu->AddSection(TEXT("KzDialogueLocalization"), LOCTEXT("LocalizationSection", "Localization"));

	Section.AddDynamicEntry(TEXT("KzDialogueTranslationCsv"), FNewToolMenuSectionDelegate::CreateLambda([](FToolMenuSection& InSection)
	{
		const UContentBrowserAssetContextMenuContext* Context = InSection.FindContext<UContentBrowserAssetContextMenuContext>();
		if (!Context) { return; }

		InSection.AddMenuEntry(
			TEXT("ExportTranslationCsv"),
			LOCTEXT("ExportCsv", "Export Translation CSV..."),
			LOCTEXT("ExportCsvTip", "Export the selected dialogue assets to a CSV with translator context (speaker, notes, max characters, tags, audio)."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([SelectedAssets = Context->SelectedAssets]() { OnExportClicked(SelectedAssets); })));

		InSection.AddSubMenu(
			TEXT("ImportTranslationCsv"),
			LOCTEXT("ImportCsv", "Import Translation CSV"),
			LOCTEXT("ImportCsvTip", "Import a translated CSV into the localization target archive of a culture. Rows whose source text drifted since export are skipped and reported."),
			FNewToolMenuDelegate::CreateLambda([](UToolMenu* SubMenu)
			{
				FToolMenuSection& CultureSection = SubMenu->AddSection(NAME_None);

				FKzLocTargetInfo Target;
				FText Error;
				if (!ReadLocTargetInfo(Target, Error) || Target.ForeignCultures.IsEmpty())
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
						FText::FromString(Culture),
						FText::Format(LOCTEXT("ImportCultureTip", "Import into the '{0}' archive of target '{1}'."), FText::FromString(Culture), FText::FromString(Target.TargetName)),
						FSlateIcon(),
						FUIAction(FExecuteAction::CreateLambda([Culture]() { OnImportClicked(Culture); })));
				}
			}));
	}));
}

bool FKzDialogueTranslationCsv::ExportAssets(const TArray<UKzDialogueAsset*>& Assets, const FString& CsvPath, FText& OutError)
{
	FString Csv = TEXT("Asset,Namespace,Key,Speaker,SourceText,Translation,TranslatorNotes,MaxCharacters,Audio,SourceHash");
	Csv += LINE_TERMINATOR;

	int32 NumRows = 0;
	for (const UKzDialogueAsset* Asset : Assets)
	{
		if (!Asset) { continue; }
		const FString AssetPath = Asset->GetPathName();

		for (const FKzDialogueLine& Line : Asset->Lines)
		{
			// Empty for narration lines; GetDisplayLabel's "<Narration>" placeholder is editor UI, not translator context.
			const FString Speaker = Line.Speaker.IsValid() ? Line.Speaker.GetDisplayLabel().ToString() : FString();
			const FString Audio = Line.Audio.ToSoftObjectPath().ToString();

			auto AddRow = [&](const FText& Source, const FString& Notes, int32 MaxChars, uint32 SourceHash)
			{
				const TOptional<FString> Namespace = FTextInspector::GetNamespace(Source);
				const TOptional<FString> Key = FTextInspector::GetKey(Source);
				const FString* SourceString = FTextInspector::GetSourceString(Source);
				if (Source.IsEmpty() || !Namespace.IsSet() || !Key.IsSet() || !SourceString) { return; }

				Csv += CsvEscape(AssetPath) + TEXT(",");
				Csv += CsvEscape(Namespace.GetValue()) + TEXT(",");
				Csv += CsvEscape(Key.GetValue()) + TEXT(",");
				Csv += CsvEscape(Speaker) + TEXT(",");
				Csv += CsvEscape(*SourceString) + TEXT(",");
				Csv += TEXT(",");
				Csv += CsvEscape(Notes) + TEXT(",");
				Csv += FString::FromInt(MaxChars) + TEXT(",");
				Csv += CsvEscape(Audio) + TEXT(",");
				Csv += FString::Printf(TEXT("%u"), SourceHash);
				Csv += LINE_TERMINATOR;
				++NumRows;
			};

			AddRow(Line.Text, Line.TranslatorNotes, Line.MaxCharacters, Line.SourceTextHash);
			AddRow(Line.Speaker.DisplayNameOverride, FString(), 0, Line.SourceSpeakerHash);
		}
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

bool FKzDialogueTranslationCsv::ImportCsv(const FString& CsvPath, const FString& Culture, FKzTranslationImportStats& OutStats, FText& OutError)
{
	FString CsvContent;
	if (!FFileHelper::LoadFileToString(CsvContent, *CsvPath))
	{
		OutError = FText::Format(LOCTEXT("ImportReadFailed", "Could not read {0}."), FText::FromString(CsvPath));
		return false;
	}

	const FCsvParser Parser(CsvContent);
	const FCsvParser::FRows& Rows = Parser.GetRows();
	if (Rows.Num() < 2)
	{
		OutError = LOCTEXT("ImportEmpty", "The CSV has no data rows.");
		return false;
	}

	// Resolve columns by header name so reordered or extended CSVs keep importing.
	TMap<FString, int32> Columns;
	for (int32 i = 0; i < Rows[0].Num(); ++i)
	{
		Columns.Add(FString(Rows[0][i]), i);
	}
	const int32* AssetCol = Columns.Find(TEXT("Asset"));
	const int32* NamespaceCol = Columns.Find(TEXT("Namespace"));
	const int32* KeyCol = Columns.Find(TEXT("Key"));
	const int32* TranslationCol = Columns.Find(TEXT("Translation"));
	const int32* HashCol = Columns.Find(TEXT("SourceHash"));
	if (!AssetCol || !NamespaceCol || !KeyCol || !TranslationCol || !HashCol)
	{
		OutError = LOCTEXT("ImportBadHeader", "The CSV is missing one of the required columns: Asset, Namespace, Key, Translation, SourceHash.");
		return false;
	}

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

	FLocTextHelper LocHelper(Target.TargetPath, Target.ManifestName, Target.ArchiveName, Target.NativeCulture, Target.ForeignCultures, nullptr);
	if (!LocHelper.LoadManifest(ELocTextHelperLoadFlags::Load, &OutError))
	{
		OutError = FText::Format(LOCTEXT("ImportNoManifest", "Could not load the localization manifest. Run Gather Text in the Localization Dashboard first. {0}"), OutError);
		return false;
	}
	// Archives may not exist yet for a culture that was added but never exported; create on demand.
	if (!LocHelper.LoadNativeArchive(ELocTextHelperLoadFlags::LoadOrCreate, &OutError) ||
		!LocHelper.LoadForeignArchive(Culture, ELocTextHelperLoadFlags::LoadOrCreate, &OutError))
	{
		return false;
	}

	TMap<FString, UKzDialogueAsset*> LoadedAssets;

	for (int32 RowIdx = 1; RowIdx < Rows.Num(); ++RowIdx)
	{
		const TArray<const TCHAR*>& Cells = Rows[RowIdx];
		auto Cell = [&Cells](const int32* Col) { return Cells.IsValidIndex(*Col) ? FString(Cells[*Col]) : FString(); };

		const FString Translation = Cell(TranslationCol);
		if (Translation.IsEmpty())
		{
			++OutStats.Untranslated;
			continue;
		}

		const FString AssetPath = Cell(AssetCol);
		const FString Key = Cell(KeyCol);

		UKzDialogueAsset*& Asset = LoadedAssets.FindOrAdd(AssetPath);
		if (!Asset)
		{
			Asset = LoadObject<UKzDialogueAsset>(nullptr, *AssetPath);
		}

		// Key pattern: "<LineIdDigits>-Text" / "<LineIdDigits>-Speaker".
		FString GuidPart, FieldPart;
		FGuid LineId;
		const bool bKeyParsed = Key.Split(TEXT("-"), &GuidPart, &FieldPart, ESearchCase::CaseSensitive, ESearchDir::FromEnd)
			&& FGuid::ParseExact(GuidPart, EGuidFormats::Digits, LineId);
		const int32 LineIdx = (Asset && bKeyParsed) ? Asset->IndexOfLine(LineId) : INDEX_NONE;
		if (LineIdx == INDEX_NONE)
		{
			++OutStats.Unresolved;
			UE_LOG(LogKzDialogueL10N, Warning, TEXT("Unresolved row %d: asset '%s', key '%s'. The asset or line no longer exists."), RowIdx + 1, *AssetPath, *Key);
			continue;
		}

		const FKzDialogueLine& Line = Asset->Lines[LineIdx];
		const bool bSpeakerRow = FieldPart == TEXT("Speaker");
		const uint32 CurrentHash = bSpeakerRow ? Line.SourceSpeakerHash : Line.SourceTextHash;
		const uint32 CsvHash = static_cast<uint32>(FCString::Strtoui64(*Cell(HashCol), nullptr, 10));
		if (CsvHash != CurrentHash)
		{
			++OutStats.Drifted;
			UE_LOG(LogKzDialogueL10N, Warning, TEXT("Drifted row %d: asset '%s', key '%s'. The source text changed after this CSV was exported; needs retranslation."), RowIdx + 1, *AssetPath, *Key);
			continue;
		}

		// Hash matched, so the asset's current source string equals the one this row was translated against.
		const FText& SourceText = bSpeakerRow ? Line.Speaker.DisplayNameOverride : Line.Text;
		const FString* SourceString = FTextInspector::GetSourceString(SourceText);

		if (LocHelper.ImportTranslation(Culture, FLocKey(Cell(NamespaceCol)), FLocKey(Key), nullptr, FLocItem(SourceString ? *SourceString : FString()), FLocItem(Translation), false))
		{
			++OutStats.Imported;
		}
		else
		{
			++OutStats.Unresolved;
			UE_LOG(LogKzDialogueL10N, Warning, TEXT("Failed to write row %d into the '%s' archive: asset '%s', key '%s'."), RowIdx + 1, *Culture, *AssetPath, *Key);
		}
	}

	if (OutStats.Imported > 0 && !LocHelper.SaveArchive(Culture, &OutError))
	{
		return false;
	}

	UE_LOG(LogKzDialogueL10N, Log, TEXT("Imported %s translations from %s: %d imported, %d drifted, %d untranslated, %d unresolved."),
		*Culture, *CsvPath, OutStats.Imported, OutStats.Drifted, OutStats.Untranslated, OutStats.Unresolved);
	return true;
}

#undef LOCTEXT_NAMESPACE