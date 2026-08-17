// Copyright 2026 kirzo

#include "Localization/KzDialogueTranslationCsv.h"

#include "KzDialogueAsset.h"
#include "KzSpeakerAsset.h"
#include "Settings/KzDialogueSettings.h"

#include "ContentBrowserMenuContexts.h"
#include "DesktopPlatformModule.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Engine/Blueprint.h"
#include "Engine/DataTable.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Internationalization/Culture.h"
#include "Internationalization/PackageLocalizationUtil.h"
#include "Internationalization/TextNamespaceUtil.h"
#include "Misc/PackageName.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Logging/MessageLog.h"
#include "Logging/TokenizedMessage.h"
#include "Internationalization/InternationalizationManifest.h"
#include "LocTextHelper.h"
#include "Misc/UObjectToken.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"
#include "Serialization/Csv/CsvParser.h"
#include "ToolMenus.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectHash.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "KzDialogueTranslationCsv"

DEFINE_LOG_CATEGORY_STATIC(LogKzDialogueL10N, Log, All);

bool FKzDialogueTranslationCsv::ReadLocTargetInfo(FKzLocTargetInfo& Out, FText& OutError)
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

bool FKzLocQuery::Load(FText& OutError)
{
	if (!FKzDialogueTranslationCsv::ReadLocTargetInfo(Target, OutError)) { return false; }

	LocHelper = MakeShared<FLocTextHelper>(Target.TargetPath, Target.ManifestName, Target.ArchiveName, Target.NativeCulture, Target.ForeignCultures, nullptr);
	if (!LocHelper->LoadManifest(ELocTextHelperLoadFlags::Load, &OutError))
	{
		OutError = FText::Format(LOCTEXT("CoverageNoManifest", "Could not load the localization manifest. Run Gather Text in the Localization Dashboard first. {0}"), OutError);
		LocHelper.Reset();
		return false;
	}

	// A culture whose archive is missing simply counts as fully untranslated.
	for (const FString& Culture : Target.ForeignCultures)
	{
		if (LocHelper->LoadForeignArchive(Culture, ELocTextHelperLoadFlags::Load, nullptr))
		{
			LoadedCultures.Add(Culture);
		}
	}
	return true;
}

EKzTranslationState FKzLocQuery::GetTextState(const FString& Namespace, const FString& Key, const FString& SourceString, const FString& Culture) const
{
	if (!LocHelper.IsValid() || !LoadedCultures.Contains(Culture))
	{
		return EKzTranslationState::Missing;
	}

	const TSharedPtr<FArchiveEntry> Entry = LocHelper->FindTranslation(Culture, FLocKey(Namespace), FLocKey(Key), nullptr);
	if (!Entry.IsValid() || Entry->Translation.Text.IsEmpty())
	{
		return EKzTranslationState::Missing;
	}
	return Entry->Source.Text == SourceString ? EKzTranslationState::Translated : EKzTranslationState::Stale;
}

bool FKzLocQuery::GetArchiveEntry(const FString& Namespace, const FString& Key, const FString& Culture, FString& OutSource, FString& OutTranslation) const
{
	if (!LocHelper.IsValid() || !LoadedCultures.Contains(Culture))
	{
		return false;
	}

	const TSharedPtr<FArchiveEntry> Entry = LocHelper->FindTranslation(Culture, FLocKey(Namespace), FLocKey(Key), nullptr);
	if (!Entry.IsValid())
	{
		return false;
	}

	OutSource = Entry->Source.Text;
	OutTranslation = Entry->Translation.Text;
	return true;
}

bool FKzLocQuery::IsAudioLocalized(const FString& SourcePackageName, const FString& Culture) const
{
	FString LocalizedPackage;
	return FPackageLocalizationUtil::ConvertSourceToLocalized(SourcePackageName, Culture, LocalizedPackage) && FPackageName::DoesPackageExist(LocalizedPackage);
}

void FKzLocQuery::EnumerateOtherTexts(TFunctionRef<void(const FString& Namespace, const FString& Key, const FString& Source)> Callback) const
{
	if (!LocHelper.IsValid()) { return; }

	LocHelper->EnumerateSourceTexts([&Callback](TSharedRef<FManifestEntry> Entry) -> bool
	{
		const FString Namespace = Entry->Namespace.GetString();
		if (!Namespace.StartsWith(TEXT("KzDialogue.")) && !Namespace.StartsWith(TEXT("KzSpeaker.")))
		{
			for (const FManifestContext& Context : Entry->Contexts)
			{
				if (!Context.bIsOptional)
				{
					Callback(Namespace, Context.Key.GetString(), Entry->Source.Text);
				}
			}
		}
		return true;   // continue enumeration
	}, true);
}

TArray<FString> FKzLocQuery::GetSourceLocations(const FString& Namespace, const FString& Key) const
{
	TArray<FString> Locations;
	if (!LocHelper.IsValid()) { return Locations; }

	if (const TSharedPtr<FManifestEntry> Entry = LocHelper->FindSourceText(FLocKey(Namespace), FLocKey(Key)))
	{
		for (const FManifestContext& Context : Entry->Contexts)
		{
			Locations.Add(Context.SourceLocation);
		}
	}
	return Locations;
}

bool FKzDialogueTranslationCsv::BuildCoverage(const TArray<UKzDialogueAsset*>& Assets, TArray<FKzCultureCoverage>& OutCultures, TArray<FKzStaleTranslation>& OutStale, FText& OutError)
{
	FKzLocQuery Query;
	if (!Query.Load(OutError)) { return false; }

	// Same per-text unit as the CSV export: line texts plus referenced speaker-asset name fields.
	struct FKzCoverageText
	{
		UObject* Asset = nullptr;
		int32 LineIndex = INDEX_NONE;
		FString Namespace;
		FString Key;
		FString Source;
	};
	TArray<FKzCoverageText> Texts;
	TArray<FString> VoicedPackages;
	TSet<UKzSpeakerAsset*> Speakers;
	for (UKzDialogueAsset* Asset : Assets)
	{
		if (!Asset) { continue; }
		for (int32 i = 0; i < Asset->Lines.Num(); ++i)
		{
			const FKzDialogueLine& Line = Asset->Lines[i];
			const TOptional<FString> Namespace = FTextInspector::GetNamespace(Line.Text);
			const TOptional<FString> Key = FTextInspector::GetKey(Line.Text);
			const FString* SourceString = FTextInspector::GetSourceString(Line.Text);
			if (!Line.Text.IsEmpty() && Namespace.IsSet() && Key.IsSet() && SourceString)
			{
				Texts.Add({ Asset, i, Namespace.GetValue(), Key.GetValue(), *SourceString });
			}

			if (Line.Speaker.Asset) { Speakers.Add(Line.Speaker.Asset); }

			// Audio localization is plain data, no project policy: subtitles-only projects read
			// 0/N and move on. Lines opted out via bLocalizeAudio do not count at all.
			if (!Line.Audio.IsNull() && Line.bLocalizeAudio)
			{
				VoicedPackages.Add(Line.Audio.ToSoftObjectPath().GetLongPackageName());
			}
		}
		for (const FKzDialogueAlias& Alias : Asset->Aliases)
		{
			if (Alias.Speaker.Asset) { Speakers.Add(Alias.Speaker.Asset); }
		}
	}

	for (UKzSpeakerAsset* Speaker : Speakers)
	{
		auto AddSpeakerText = [&](const FText& Source)
		{
			const TOptional<FString> Namespace = FTextInspector::GetNamespace(Source);
			const TOptional<FString> Key = FTextInspector::GetKey(Source);
			const FString* SourceString = FTextInspector::GetSourceString(Source);
			if (Source.IsEmpty() || !Namespace.IsSet() || !Key.IsSet() || !SourceString) { return; }
			Texts.Add({ Speaker, INDEX_NONE, Namespace.GetValue(), Key.GetValue(), *SourceString });
		};
		AddSpeakerText(Speaker->DisplayName);
		AddSpeakerText(Speaker->GivenName);
		AddSpeakerText(Speaker->FamilyName);
		AddSpeakerText(Speaker->Honorific);
		AddSpeakerText(Speaker->Qualifier);
	}

	for (const FString& Culture : Query.GetTarget().ForeignCultures)
	{
		FKzCultureCoverage& Coverage = OutCultures.AddDefaulted_GetRef();
		Coverage.Culture = Culture;
		Coverage.Total = Texts.Num();
		Coverage.VoicedLines = VoicedPackages.Num();

		for (const FKzCoverageText& Text : Texts)
		{
			switch (Query.GetTextState(Text.Namespace, Text.Key, Text.Source, Culture))
			{
			case EKzTranslationState::Translated:
				++Coverage.Translated;
				break;
			case EKzTranslationState::Stale:
				++Coverage.Stale;
				OutStale.Add({ Text.Asset, Text.LineIndex, Text.Key, Culture });
				break;
			default:
				break;
			}
		}
		Coverage.Missing = Coverage.Total - Coverage.Translated - Coverage.Stale;

		for (const FString& SourcePackage : VoicedPackages)
		{
			if (Query.IsAudioLocalized(SourcePackage, Culture))
			{
				++Coverage.LocalizedAudio;
			}
		}
	}

	return true;
}

namespace
{
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

	void OnExportClicked(TArray<FAssetData> SelectedAssets, const FKzExportLineFilter& LineFilter = nullptr)
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
		if (FKzDialogueTranslationCsv::ExportAssets(Assets, OutFiles[0], Error, LineFilter))
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
		if (FKzDialogueTranslationCsv::ReadLocTargetInfo(Target, TargetError))
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

	void OnCoverageClicked(TArray<FAssetData> SelectedAssets)
	{
		TArray<UKzDialogueAsset*> Assets;
		for (const FAssetData& Data : SelectedAssets)
		{
			if (UKzDialogueAsset* Asset = Cast<UKzDialogueAsset>(Data.GetAsset()))
			{
				Assets.Add(Asset);
			}
		}
		if (Assets.IsEmpty()) { return; }

		TArray<FKzCultureCoverage> Cultures;
		TArray<FKzStaleTranslation> Stale;
		FText Error;
		if (!FKzDialogueTranslationCsv::BuildCoverage(Assets, Cultures, Stale, Error))
		{
			ShowNotification(Error, false);
			return;
		}

		FMessageLog Log(TEXT("KzDialogueL10N"));
		Log.NewPage(LOCTEXT("CoveragePageTitle", "Translation coverage"));
		Log.Info(FText::Format(LOCTEXT("CoverageHeader", "{0} asset(s), {1} localizable text(s)."), Assets.Num(), Cultures.Num() > 0 ? Cultures[0].Total : 0));

		for (const FKzCultureCoverage& Coverage : Cultures)
		{
			FText CultureLine = FText::Format(LOCTEXT("CoverageCulture", "{0}: {1}/{2} translated, {3} missing, {4} stale."),
				FText::FromString(Coverage.Culture), Coverage.Translated, Coverage.Total, Coverage.Missing, Coverage.Stale);

			if (Coverage.VoicedLines > 0)
			{
				CultureLine = FText::Format(LOCTEXT("CoverageCultureAudio", "{0} Audio: {1}/{2} localized."), CultureLine, Coverage.LocalizedAudio, Coverage.VoicedLines);
			}

			Log.Info(CultureLine);
		}

		// Stale entries are the actionable ones: translated against an older source, need review.
		FKzDialogueTranslationCsv::LogStaleTranslations(Stale);

		Log.Open(EMessageSeverity::Info, true);
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
				if (!FKzDialogueTranslationCsv::ReadLocTargetInfo(Target, Error) || Target.ForeignCultures.IsEmpty())
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
						FKzDialogueTranslationCsv::GetCultureDisplayLabel(Culture),
						FText::Format(LOCTEXT("ImportCultureTip", "Import into the '{0}' archive of target '{1}'."), FText::FromString(Culture), FText::FromString(Target.TargetName)),
						FSlateIcon(),
						FUIAction(FExecuteAction::CreateLambda([Culture]() { OnImportClicked(Culture); })));
				}
			}));

		InSection.AddMenuEntry(
			TEXT("TranslationCoverage"),
			LOCTEXT("Coverage", "Translation Coverage Report"),
			LOCTEXT("CoverageTip", "Show how many texts of the selected dialogue assets are translated, missing or stale per culture."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([SelectedAssets = Context->SelectedAssets]() { OnCoverageClicked(SelectedAssets); })));
	}));
}

bool FKzDialogueTranslationCsv::ExportAssets(const TArray<UKzDialogueAsset*>& Assets, const FString& CsvPath, FText& OutError, const FKzExportLineFilter& LineFilter)
{
	FString Csv = TEXT("Asset,Namespace,Key,Speaker,SourceText,Translation,TranslatorNotes,MaxCharacters,Audio,LocalizeAudio,SourceHash");
	Csv += LINE_TERMINATOR;

	int32 NumRows = 0;
	for (const UKzDialogueAsset* Asset : Assets)
	{
		if (!Asset) { continue; }
		const FString AssetPath = Asset->GetPathName();

		for (const FKzDialogueLine& Line : Asset->Lines)
		{
			if (LineFilter && !LineFilter(*Asset, Line)) { continue; }
			// Empty for narration lines; GetDisplayLabel's "<Narration>" placeholder is editor UI, not translator context.
			const FString Speaker = Line.Speaker.IsValid() ? Line.Speaker.GetDisplayLabel().ToString() : FString();
			const FString Audio = Line.Audio.ToSoftObjectPath().ToString();
			// "no" tells the studio this take deliberately plays in every culture; empty otherwise.
			const FString LocalizeAudio = !Line.Audio.IsNull() && !Line.bLocalizeAudio ? TEXT("no") : TEXT("");

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
				Csv += LocalizeAudio + TEXT(",");
				Csv += FString::Printf(TEXT("%u"), SourceHash);
				Csv += LINE_TERMINATOR;
				++NumRows;
			};

			AddRow(Line.Text, Line.TranslatorNotes, Line.MaxCharacters, Line.SourceTextHash);
		}
	}

	// Speaker assets referenced by the exported lines/aliases: a character's name fields are
	// localized once on its asset, so they export as their own rows (deduped across assets).
	// A filtered export only carries the speakers of the included lines.
	TSet<const UKzSpeakerAsset*> Speakers;
	for (const UKzDialogueAsset* Asset : Assets)
	{
		if (!Asset) { continue; }
		for (const FKzDialogueLine& Line : Asset->Lines)
		{
			if (LineFilter && !LineFilter(*Asset, Line)) { continue; }
			if (Line.Speaker.Asset) { Speakers.Add(Line.Speaker.Asset); }
		}
		if (!LineFilter)
		{
			for (const FKzDialogueAlias& Alias : Asset->Aliases)
			{
				if (Alias.Speaker.Asset) { Speakers.Add(Alias.Speaker.Asset); }
			}
		}
	}

	for (const UKzSpeakerAsset* Speaker : Speakers)
	{
		const FString SpeakerPath = Speaker->GetPathName();
		const FString SpeakerLabel = Speaker->GetResolvedDisplayName().ToString();

		auto AddSpeakerRow = [&](const FText& Source, uint32 SourceHash)
		{
			const TOptional<FString> Namespace = FTextInspector::GetNamespace(Source);
			const TOptional<FString> Key = FTextInspector::GetKey(Source);
			const FString* SourceString = FTextInspector::GetSourceString(Source);
			if (Source.IsEmpty() || !Namespace.IsSet() || !Key.IsSet() || !SourceString) { return; }

			Csv += CsvEscape(SpeakerPath) + TEXT(",");
			Csv += CsvEscape(Namespace.GetValue()) + TEXT(",");
			Csv += CsvEscape(Key.GetValue()) + TEXT(",");
			Csv += CsvEscape(SpeakerLabel) + TEXT(",");
			Csv += CsvEscape(*SourceString) + TEXT(",");
			Csv += TEXT(",");
			Csv += TEXT(",");
			Csv += TEXT("0,");
			Csv += TEXT(",");
			Csv += TEXT(",");
			Csv += FString::Printf(TEXT("%u"), SourceHash);
			Csv += LINE_TERMINATOR;
			++NumRows;
		};

		AddSpeakerRow(Speaker->DisplayName, Speaker->SourceDisplayNameHash);
		AddSpeakerRow(Speaker->GivenName, Speaker->SourceGivenNameHash);
		AddSpeakerRow(Speaker->FamilyName, Speaker->SourceFamilyNameHash);
		AddSpeakerRow(Speaker->Honorific, Speaker->SourceHonorificHash);
		AddSpeakerRow(Speaker->Qualifier, Speaker->SourceQualifierHash);
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

	TMap<FString, UObject*> LoadedAssets;

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

		UObject*& Asset = LoadedAssets.FindOrAdd(AssetPath);
		if (!Asset)
		{
			Asset = LoadObject<UObject>(nullptr, *AssetPath);
		}

		// Resolve the row to its source text + drift hash. Dialogue rows key by
		// "<LineIdDigits>-Text"; speaker-asset rows key by field name.
		const FText* SourceText = nullptr;
		uint32 CurrentHash = 0;

		if (UKzDialogueAsset* Dialogue = Cast<UKzDialogueAsset>(Asset))
		{
			FString GuidPart, FieldPart;
			FGuid LineId;
			const bool bKeyParsed = Key.Split(TEXT("-"), &GuidPart, &FieldPart, ESearchCase::CaseSensitive, ESearchDir::FromEnd)
				&& FGuid::ParseExact(GuidPart, EGuidFormats::Digits, LineId)
				&& FieldPart == TEXT("Text");
			const int32 LineIdx = bKeyParsed ? Dialogue->IndexOfLine(LineId) : INDEX_NONE;
			if (LineIdx != INDEX_NONE)
			{
				SourceText = &Dialogue->Lines[LineIdx].Text;
				CurrentHash = Dialogue->Lines[LineIdx].SourceTextHash;
			}
		}
		else if (const UKzSpeakerAsset* SpeakerAsset = Cast<UKzSpeakerAsset>(Asset))
		{
			if (Key == TEXT("DisplayName")) { SourceText = &SpeakerAsset->DisplayName; CurrentHash = SpeakerAsset->SourceDisplayNameHash; }
			else if (Key == TEXT("GivenName")) { SourceText = &SpeakerAsset->GivenName; CurrentHash = SpeakerAsset->SourceGivenNameHash; }
			else if (Key == TEXT("FamilyName")) { SourceText = &SpeakerAsset->FamilyName; CurrentHash = SpeakerAsset->SourceFamilyNameHash; }
			else if (Key == TEXT("Honorific")) { SourceText = &SpeakerAsset->Honorific; CurrentHash = SpeakerAsset->SourceHonorificHash; }
			else if (Key == TEXT("Qualifier")) { SourceText = &SpeakerAsset->Qualifier; CurrentHash = SpeakerAsset->SourceQualifierHash; }
		}

		if (!SourceText)
		{
			++OutStats.Unresolved;
			UE_LOG(LogKzDialogueL10N, Warning, TEXT("Unresolved row %d: asset '%s', key '%s'. The asset, line or field no longer exists."), RowIdx + 1, *AssetPath, *Key);
			continue;
		}

		const uint32 CsvHash = static_cast<uint32>(FCString::Strtoui64(*Cell(HashCol), nullptr, 10));
		if (CsvHash != CurrentHash)
		{
			++OutStats.Drifted;
			UE_LOG(LogKzDialogueL10N, Warning, TEXT("Drifted row %d: asset '%s', key '%s'. The source text changed after this CSV was exported; needs retranslation."), RowIdx + 1, *AssetPath, *Key);
			continue;
		}

		// Hash matched, so the asset's current source string equals the one this row was translated against.
		const FString* SourceString = FTextInspector::GetSourceString(*SourceText);

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

void FKzDialogueTranslationCsv::ExportInteractive(TArray<FAssetData> SelectedAssets, const FKzExportLineFilter& LineFilter)
{
	OnExportClicked(MoveTemp(SelectedAssets), LineFilter);
}

FText FKzDialogueTranslationCsv::GetCultureDisplayLabel(const FString& Culture)
{
	const FCulturePtr CulturePtr = FInternationalization::Get().GetCulture(Culture);
	const FString DisplayName = CulturePtr.IsValid() ? CulturePtr->GetDisplayName() : FString();
	if (DisplayName.IsEmpty() || DisplayName == Culture)
	{
		return FText::FromString(Culture);
	}
	return FText::FromString(FString::Printf(TEXT("%s (%s)"), *DisplayName, *Culture));
}

void FKzDialogueTranslationCsv::ImportInteractive(const FString& Culture)
{
	OnImportClicked(Culture);
}

namespace
{
	/** One .po entry: extracted comments plus the escaped-on-write text triplet. */
	struct FKzPoEntry
	{
		TArray<FString> Comments;
		FString Namespace;
		FString Key;
		FString Source;
	};

	FString PoEscape(const FString& In)
	{
		FString Out = In;
		Out.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
		Out.ReplaceInline(TEXT("\""), TEXT("\\\""));
		Out.ReplaceInline(TEXT("\r\n"), TEXT("\\n"));
		Out.ReplaceInline(TEXT("\n"), TEXT("\\n"));
		Out.ReplaceInline(TEXT("\r"), TEXT("\\n"));
		Out.ReplaceInline(TEXT("\t"), TEXT("\\t"));
		return Out;
	}

	FString PoUnescape(const FString& In)
	{
		FString Out;
		Out.Reserve(In.Len());
		for (int32 i = 0; i < In.Len(); ++i)
		{
			if (In[i] == TEXT('\\') && i + 1 < In.Len())
			{
				++i;
				switch (In[i])
				{
				case TEXT('n'): Out.AppendChar(TEXT('\n')); break;
				case TEXT('t'): Out.AppendChar(TEXT('\t')); break;
				default: Out.AppendChar(In[i]); break;
				}
			}
			else
			{
				Out.AppendChar(In[i]);
			}
		}
		return Out;
	}

	void AppendPoText(TArray<FKzPoEntry>& Entries, const FText& Source, TArray<FString>&& Comments)
	{
		const TOptional<FString> Namespace = FTextInspector::GetNamespace(Source);
		const TOptional<FString> Key = FTextInspector::GetKey(Source);
		const FString* SourceString = FTextInspector::GetSourceString(Source);
		if (Source.IsEmpty() || !Namespace.IsSet() || !Key.IsSet() || !SourceString) { return; }

		FKzPoEntry& Entry = Entries.AddDefaulted_GetRef();
		Entry.Comments = MoveTemp(Comments);
		Entry.Namespace = Namespace.GetValue();
		Entry.Key = Key.GetValue();
		Entry.Source = *SourceString;
	}

	/** One .po per foreign culture (<Directory>/<Culture>/<Target><FileNameSuffix>.po): existing translations fill msgstr, stale ones get the standard fuzzy flag. */
	bool WriteKzPoFiles(const TArray<FKzPoEntry>& Entries, const FKzLocQuery& Query, const FString& Directory, const FString& FileNameSuffix, FText& OutError)
	{
		for (const FString& Culture : Query.GetTarget().ForeignCultures)
		{
			FString Po;
			Po += TEXT("msgid \"\"") LINE_TERMINATOR;
			Po += TEXT("msgstr \"\"") LINE_TERMINATOR;
			Po += FString::Printf(TEXT("\"Project-Id-Version: %s\\n\"") LINE_TERMINATOR, *Query.GetTarget().TargetName);
			Po += FString::Printf(TEXT("\"Language: %s\\n\"") LINE_TERMINATOR, *Culture);
			Po += TEXT("\"MIME-Version: 1.0\\n\"") LINE_TERMINATOR;
			Po += TEXT("\"Content-Type: text/plain; charset=UTF-8\\n\"") LINE_TERMINATOR;
			Po += TEXT("\"Content-Transfer-Encoding: 8bit\\n\"") LINE_TERMINATOR;

			for (const FKzPoEntry& Entry : Entries)
			{
				Po += LINE_TERMINATOR;
				for (const FString& Comment : Entry.Comments)
				{
					Po += FString::Printf(TEXT("#. %s") LINE_TERMINATOR, *Comment);
				}

				FString ArchiveSource;
				FString Translation;
				Query.GetArchiveEntry(Entry.Namespace, Entry.Key, Culture, ArchiveSource, Translation);
				if (Query.GetTextState(Entry.Namespace, Entry.Key, Entry.Source, Culture) == EKzTranslationState::Stale)
				{
					Po += TEXT("#, fuzzy") LINE_TERMINATOR;
				}

				Po += FString::Printf(TEXT("msgctxt \"%s,%s\"") LINE_TERMINATOR, *PoEscape(Entry.Namespace), *PoEscape(Entry.Key));
				Po += FString::Printf(TEXT("msgid \"%s\"") LINE_TERMINATOR, *PoEscape(Entry.Source));
				Po += FString::Printf(TEXT("msgstr \"%s\"") LINE_TERMINATOR, *PoEscape(Translation));
			}

			const FString FilePath = Directory / Culture / (Query.GetTarget().TargetName + FileNameSuffix + TEXT(".po"));
			if (!FFileHelper::SaveStringToFile(Po, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
			{
				OutError = FText::Format(LOCTEXT("PoWriteFailed", "Could not write {0}."), FText::FromString(FilePath));
				return false;
			}
		}

		return true;
	}
}

bool FKzDialogueTranslationCsv::ExportPoFiles(const TArray<UKzDialogueAsset*>& Assets, const FString& Directory, FText& OutError, const FKzExportLineFilter& LineFilter)
{
	FKzLocQuery Query;
	if (!Query.Load(OutError)) { return false; }

	// Same text set as the CSV export: line texts plus the name fields of the involved
	// speaker assets (only the speakers of included lines when filtering).
	TArray<FKzPoEntry> Entries;
	TSet<const UKzSpeakerAsset*> Speakers;
	for (const UKzDialogueAsset* Asset : Assets)
	{
		if (!Asset) { continue; }
		for (const FKzDialogueLine& Line : Asset->Lines)
		{
			if (LineFilter && !LineFilter(*Asset, Line)) { continue; }

			TArray<FString> Comments;
			if (Line.Speaker.IsValid()) { Comments.Add(FString::Printf(TEXT("Speaker: %s"), *Line.Speaker.GetDisplayLabel().ToString())); }
			if (!Line.TranslatorNotes.IsEmpty()) { Comments.Add(FString::Printf(TEXT("Notes: %s"), *Line.TranslatorNotes)); }
			if (Line.MaxCharacters > 0) { Comments.Add(FString::Printf(TEXT("MaxCharacters: %d"), Line.MaxCharacters)); }
			if (!Line.Audio.IsNull() && !Line.bLocalizeAudio) { Comments.Add(TEXT("Audio: do not localize (the source take plays in every culture)")); }
			AppendPoText(Entries, Line.Text, MoveTemp(Comments));

			if (Line.Speaker.Asset) { Speakers.Add(Line.Speaker.Asset); }
		}
		if (!LineFilter)
		{
			for (const FKzDialogueAlias& Alias : Asset->Aliases)
			{
				if (Alias.Speaker.Asset) { Speakers.Add(Alias.Speaker.Asset); }
			}
		}
	}
	for (const UKzSpeakerAsset* Speaker : Speakers)
	{
		const TArray<FString> Comments = { FString::Printf(TEXT("Speaker asset: %s"), *Speaker->GetName()) };
		AppendPoText(Entries, Speaker->DisplayName, CopyTemp(Comments));
		AppendPoText(Entries, Speaker->GivenName, CopyTemp(Comments));
		AppendPoText(Entries, Speaker->FamilyName, CopyTemp(Comments));
		AppendPoText(Entries, Speaker->Honorific, CopyTemp(Comments));
		AppendPoText(Entries, Speaker->Qualifier, CopyTemp(Comments));
	}

	if (Entries.IsEmpty())
	{
		OutError = LOCTEXT("PoNothingToExport", "Nothing to export: no localizable texts matched.");
		return false;
	}

	return WriteKzPoFiles(Entries, Query, Directory, TEXT(""), OutError);
}

bool FKzDialogueTranslationCsv::ExportOtherTextsPoFiles(const FString& Directory, FText& OutError)
{
	FKzLocQuery Query;
	if (!Query.Load(OutError)) { return false; }

	// Everything the target gathered outside the dialogue namespaces: UI, menus, etc.
	// The msgctxt already carries namespace/key, so no extra comments are needed.
	TArray<FKzPoEntry> Entries;
	Query.EnumerateOtherTexts([&Entries](const FString& Namespace, const FString& Key, const FString& Source)
	{
		FKzPoEntry& Entry = Entries.AddDefaulted_GetRef();
		Entry.Namespace = Namespace;
		Entry.Key = Key;
		Entry.Source = Source;
	});

	if (Entries.IsEmpty())
	{
		OutError = LOCTEXT("PoNoOtherTexts", "Nothing to export: the target has no gathered texts outside the dialogue namespaces.");
		return false;
	}

	return WriteKzPoFiles(Entries, Query, Directory, TEXT("_Other"), OutError);
}

void FKzDialogueTranslationCsv::ExportOtherTextsPoInteractive()
{
	const void* ParentWindow = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);
	FString OutDirectory;
	if (!FDesktopPlatformModule::Get()->OpenDirectoryDialog(ParentWindow, LOCTEXT("ExportOtherPoDialogTitle", "Export project texts PO files to...").ToString(), FPaths::ProjectSavedDir(), OutDirectory))
	{
		return;
	}

	FText Error;
	if (ExportOtherTextsPoFiles(OutDirectory, Error))
	{
		ShowNotification(FText::Format(LOCTEXT("OtherPoExportDone", "Project texts PO files exported to {0}."), FText::FromString(OutDirectory)), true);
	}
	else
	{
		ShowNotification(Error, false);
	}
}

void FKzDialogueTranslationCsv::ExportPoInteractive(TArray<FAssetData> SelectedAssets, const FKzExportLineFilter& LineFilter)
{
	TArray<UKzDialogueAsset*> Assets;
	for (const FAssetData& Data : SelectedAssets)
	{
		if (UKzDialogueAsset* Asset = Cast<UKzDialogueAsset>(Data.GetAsset())) { Assets.Add(Asset); }
	}
	if (Assets.IsEmpty()) { return; }

	const void* ParentWindow = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);
	FString OutDirectory;
	if (!FDesktopPlatformModule::Get()->OpenDirectoryDialog(ParentWindow, LOCTEXT("ExportPoDialogTitle", "Export PO files to...").ToString(), FPaths::ProjectSavedDir(), OutDirectory))
	{
		return;
	}

	FText Error;
	if (ExportPoFiles(Assets, OutDirectory, Error, LineFilter))
	{
		ShowNotification(FText::Format(LOCTEXT("PoExportDone", "PO files exported to {0}."), FText::FromString(OutDirectory)), true);
	}
	else
	{
		ShowNotification(Error, false);
	}
}

bool FKzDialogueTranslationCsv::ImportPoFile(const FString& PoPath, const FString& Culture, FKzTranslationImportStats& OutStats, FText& OutError)
{
	TArray<FString> FileLines;
	if (!FFileHelper::LoadFileToStringArray(FileLines, *PoPath))
	{
		OutError = FText::Format(LOCTEXT("PoReadFailed", "Could not read {0}."), FText::FromString(PoPath));
		return false;
	}

	FKzLocTargetInfo Target;
	if (!ReadLocTargetInfo(Target, OutError)) { return false; }
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
	if (!LocHelper.LoadNativeArchive(ELocTextHelperLoadFlags::LoadOrCreate, &OutError) ||
		!LocHelper.LoadForeignArchive(Culture, ELocTextHelperLoadFlags::LoadOrCreate, &OutError))
	{
		return false;
	}

	// Entries validate against the MANIFEST: an entry whose msgid no longer matches the
	// gathered source was translated against old text and is skipped as drifted.
	auto ProcessEntry = [&LocHelper, &OutStats, Culture](const FString& RawCtxt, const FString& RawId, const FString& RawStr)
	{
		const FString Ctxt = PoUnescape(RawCtxt);
		const FString Id = PoUnescape(RawId);
		const FString Str = PoUnescape(RawStr);

		if (Ctxt.IsEmpty() && Id.IsEmpty()) { return; }   // .po header block

		if (Str.IsEmpty())
		{
			++OutStats.Untranslated;
			return;
		}

		FString Namespace;
		FString Key;
		if (!Ctxt.Split(TEXT(","), &Namespace, &Key))
		{
			++OutStats.Unresolved;
			UE_LOG(LogKzDialogueL10N, Warning, TEXT("PO entry with unrecognized msgctxt '%s' (expected 'Namespace,Key')."), *Ctxt);
			return;
		}

		const TSharedPtr<FManifestEntry> ManifestEntry = LocHelper.FindSourceText(FLocKey(Namespace), FLocKey(Key));
		if (!ManifestEntry.IsValid())
		{
			++OutStats.Unresolved;
			UE_LOG(LogKzDialogueL10N, Warning, TEXT("Unresolved PO entry '%s,%s': not in the manifest anymore. Re-run Gather Text or drop the entry."), *Namespace, *Key);
			return;
		}
		if (!ManifestEntry->Source.Text.Equals(Id, ESearchCase::CaseSensitive))
		{
			++OutStats.Drifted;
			UE_LOG(LogKzDialogueL10N, Warning, TEXT("Drifted PO entry '%s,%s': the source text changed after this file was exported; needs retranslation."), *Namespace, *Key);
			return;
		}

		if (LocHelper.ImportTranslation(Culture, FLocKey(Namespace), FLocKey(Key), nullptr, FLocItem(ManifestEntry->Source.Text), FLocItem(Str), false))
		{
			++OutStats.Imported;
		}
		else
		{
			++OutStats.Unresolved;
			UE_LOG(LogKzDialogueL10N, Warning, TEXT("Failed to write PO entry '%s,%s' into the '%s' archive."), *Namespace, *Key, *Culture);
		}
	};

	// Minimal .po reader: msgctxt/msgid/msgstr plus bare-string continuation lines.
	FString Ctxt;
	FString Id;
	FString Str;
	FString* Continuation = nullptr;
	bool bSeenStr = false;

	auto Quoted = [](const FString& In) -> FString
	{
		const FString Value = In.TrimStartAndEnd();
		return Value.Len() >= 2 && Value.StartsWith(TEXT("\"")) && Value.EndsWith(TEXT("\"")) ? Value.Mid(1, Value.Len() - 2) : FString();
	};

	for (const FString& RawLine : FileLines)
	{
		const FString Line = RawLine.TrimStartAndEnd();

		if (Line.StartsWith(TEXT("msgctxt ")) || Line.StartsWith(TEXT("msgid ")))
		{
			if (bSeenStr)
			{
				ProcessEntry(Ctxt, Id, Str);
				Ctxt.Reset(); Id.Reset(); Str.Reset();
				bSeenStr = false;
			}
			if (Line.StartsWith(TEXT("msgctxt ")))
			{
				Ctxt = Quoted(Line.Mid(8));
				Continuation = &Ctxt;
			}
			else
			{
				Id = Quoted(Line.Mid(6));
				Continuation = &Id;
			}
		}
		else if (Line.StartsWith(TEXT("msgstr ")))
		{
			Str = Quoted(Line.Mid(7));
			Continuation = &Str;
			bSeenStr = true;
		}
		else if (Line.StartsWith(TEXT("\"")) && Continuation)
		{
			*Continuation += Quoted(Line);
		}
		else
		{
			// Comments and blank lines end any string continuation.
			Continuation = nullptr;
		}
	}
	if (bSeenStr)
	{
		ProcessEntry(Ctxt, Id, Str);
	}

	if (OutStats.Imported > 0 && !LocHelper.SaveArchive(Culture, &OutError))
	{
		return false;
	}

	UE_LOG(LogKzDialogueL10N, Log, TEXT("Imported %s translations from %s: %d imported, %d drifted, %d untranslated, %d unresolved."),
		*Culture, *PoPath, OutStats.Imported, OutStats.Drifted, OutStats.Untranslated, OutStats.Unresolved);
	return true;
}

namespace
{
	/**
	 * Rewrites every asset-authored occurrence of the given (namespace, key) identities whose
	 * source is SourceText, applying Transform to the matched FText. Walks reflected object
	 * properties, data table rows (raw struct memory) and Blueprint graph pin defaults; C++
	 * code sites cannot be rewritten and are logged with ManualFixHint. Rewritten blueprints
	 * are marked modified so the save recompiles the gathered bytecode.
	 */
	void RewriteAuthoredTextOccurrences(const FKzLocQuery& Query, const TArray<TPair<FString, FString>>& Identities, const FString& SourceText, TFunctionRef<FText(const FText&)> Transform, const FString& ManualFixHint, int32& OutRewritten, int32& OutSkipped)
	{
		for (const TPair<FString, FString>& Identity : Identities)
		{
			const FString& Namespace = Identity.Key;
			const FString& Key = Identity.Value;

			const TArray<FString> Locations = Query.GetSourceLocations(Namespace, Key);
			if (Locations.IsEmpty())
			{
				++OutSkipped;
				UE_LOG(LogKzDialogueL10N, Warning, TEXT("Rewrite: '%s,%s' has no manifest source location; re-run Gather and retry."), *Namespace, *Key);
				continue;
			}

			for (const FString& Location : Locations)
			{
				// Asset-authored texts carry object paths; anything else (C++ code sites)
				// cannot be rewritten from here.
				if (!Location.StartsWith(TEXT("/")))
				{
					++OutSkipped;
					UE_LOG(LogKzDialogueL10N, Warning, TEXT("Rewrite: '%s,%s' is authored in code (%s); %s."), *Namespace, *Key, *Location, *ManualFixHint);
					continue;
				}

				FString PackageName = Location;
				int32 DotIndex = INDEX_NONE;
				if (PackageName.FindChar(TEXT('.'), DotIndex))
				{
					PackageName.LeftInline(DotIndex);
				}

				UPackage* Package = LoadPackage(nullptr, *PackageName, LOAD_None);
				if (!Package)
				{
					++OutSkipped;
					UE_LOG(LogKzDialogueL10N, Warning, TEXT("Rewrite: could not load package '%s' for '%s,%s'."), *PackageName, *Namespace, *Key);
					continue;
				}

				bool bRewrote = false;
				int32 NearMisses = 0;

				auto TryRewriteText = [&](UObject* Owner, const FText* TextPtr)
				{
					if (!TextPtr) { return; }

					const TOptional<FString> TextNamespace = FTextInspector::GetNamespace(*TextPtr);
					const TOptional<FString> TextKey = FTextInspector::GetKey(*TextPtr);
					const FString* TextSource = FTextInspector::GetSourceString(*TextPtr);
					if (!TextNamespace.IsSet() || !TextKey.IsSet() || !TextSource) { return; }
					if (TextKey.GetValue() != Key) { return; }

					// The live text's namespace carries the editor-only package localization
					// id suffix ("" becomes " [ABCD...]"); the manifest stores it stripped.
					const FString CleanNamespace = TextNamespaceUtil::StripPackageNamespace(TextNamespace.GetValue());
					if (CleanNamespace != Namespace || *TextSource != SourceText)
					{
						++NearMisses;
						UE_LOG(LogKzDialogueL10N, Log, TEXT("Rewrite near-miss in '%s': key matched but namespace '%s' (expected '%s') / source '%s' (expected '%s')."), *Owner->GetPathName(), *CleanNamespace, *Namespace, **TextSource, *SourceText);
						return;
					}

					Owner->Modify();
					FText* MutableText = const_cast<FText*>(TextPtr);
					*MutableText = Transform(*MutableText);
					bRewrote = true;
				};

				// Rows of data tables are raw struct memory and graph pins are not reflected
				// properties, so both get their own passes next to the object property walk.
				TArray<UObject*> Objects;
				GetObjectsWithPackage(Package, Objects, /*bIncludeNestedObjects=*/true);
				for (UObject* Object : Objects)
				{
					for (TPropertyValueIterator<FTextProperty> It(Object->GetClass(), Object); It; ++It)
					{
						TryRewriteText(Object, static_cast<const FText*>(It.Value()));
					}

					if (const UDataTable* Table = Cast<UDataTable>(Object))
					{
						if (const UScriptStruct* RowStruct = Table->GetRowStruct())
						{
							for (const TPair<FName, uint8*>& Row : Table->GetRowMap())
							{
								for (TPropertyValueIterator<FTextProperty> It(RowStruct, Row.Value); It; ++It)
								{
									TryRewriteText(Object, static_cast<const FText*>(It.Value()));
								}
							}
						}
					}

					if (UEdGraphNode* Node = Cast<UEdGraphNode>(Object))
					{
						for (UEdGraphPin* Pin : Node->Pins)
						{
							if (Pin)
							{
								TryRewriteText(Node, &Pin->DefaultTextValue);
							}
						}
					}
				}

				if (bRewrote)
				{
					// Rewritten graph literals only reach the gathered bytecode after a
					// recompile; marking the blueprint modified makes the save do it.
					for (UObject* Object : Objects)
					{
						if (UBlueprint* Blueprint = Cast<UBlueprint>(Object))
						{
							FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
						}
					}
					Package->MarkPackageDirty();
					++OutRewritten;
				}
				else
				{
					++OutSkipped;
					UE_LOG(LogKzDialogueL10N, Warning, TEXT("Rewrite: '%s,%s' not found inside '%s' (%d near-miss(es) logged above; stale manifest needing a re-gather, or storage this walk cannot reach)."), *Namespace, *Key, *PackageName, NearMisses);
				}
			}
		}
	}
}

bool FKzDialogueTranslationCsv::MergeIdenticalTexts(const FString& SourceText, const TArray<TPair<FString, FString>>& Identities, FText& OutError, int32& OutRewritten, int32& OutSkipped, TPair<FString, FString>* OutCanonical)
{
	OutRewritten = 0;
	OutSkipped = 0;

	if (Identities.Num() < 2)
	{
		OutError = LOCTEXT("MergeNothing", "Nothing to merge: fewer than two identities.");
		return false;
	}

	FKzLocQuery Query;
	if (!Query.Load(OutError)) { return false; }

	// Canonical = the identity with the most existing translations, so the merge keeps the
	// best already-done work; ties resolve to the first.
	int32 CanonicalIndex = 0;
	int32 BestScore = -1;
	for (int32 i = 0; i < Identities.Num(); ++i)
	{
		int32 Score = 0;
		for (const FString& Culture : Query.GetTarget().ForeignCultures)
		{
			if (Query.GetTextState(Identities[i].Key, Identities[i].Value, SourceText, Culture) == EKzTranslationState::Translated)
			{
				++Score;
			}
		}
		if (Score > BestScore)
		{
			BestScore = Score;
			CanonicalIndex = i;
		}
	}
	const FString CanonicalNamespace = Identities[CanonicalIndex].Key;
	const FString CanonicalKey = Identities[CanonicalIndex].Value;
	if (OutCanonical)
	{
		*OutCanonical = Identities[CanonicalIndex];
	}

	TArray<TPair<FString, FString>> ToRewrite = Identities;
	ToRewrite.RemoveAt(CanonicalIndex);

	RewriteAuthoredTextOccurrences(Query, ToRewrite, SourceText,
		[&CanonicalNamespace, &CanonicalKey](const FText& Text) { return FText::ChangeKey(CanonicalNamespace, CanonicalKey, Text); },
		FString::Printf(TEXT("change its NSLOCTEXT/LOCTEXT to namespace '%s', key '%s' by hand"), *CanonicalNamespace, *CanonicalKey),
		OutRewritten, OutSkipped);

	UE_LOG(LogKzDialogueL10N, Log, TEXT("Merge to '%s,%s': %d occurrence(s) rewritten, %d skipped. Save the dirty assets and re-run Gather."), *CanonicalNamespace, *CanonicalKey, OutRewritten, OutSkipped);
	return true;
}

bool FKzDialogueTranslationCsv::MakeTextsNonLocalizable(const FString& SourceText, const TArray<TPair<FString, FString>>& Identities, FText& OutError, int32& OutRewritten, int32& OutSkipped)
{
	OutRewritten = 0;
	OutSkipped = 0;

	if (Identities.IsEmpty())
	{
		OutError = LOCTEXT("NonLocNothing", "Nothing to rewrite: no identities.");
		return false;
	}

	FKzLocQuery Query;
	if (!Query.Load(OutError)) { return false; }

	RewriteAuthoredTextOccurrences(Query, Identities, SourceText,
		[](const FText& Text)
		{
			const FString* Source = FTextInspector::GetSourceString(Text);
			return FText::AsCultureInvariant(Source ? *Source : Text.ToString());
		},
		FString(TEXT("wrap it in FText::AsCultureInvariant / INVTEXT by hand")),
		OutRewritten, OutSkipped);

	UE_LOG(LogKzDialogueL10N, Log, TEXT("Non-localizable: %d occurrence(s) rewritten, %d skipped. Save the dirty assets and re-run Gather."), OutRewritten, OutSkipped);
	return true;
}

void FKzDialogueTranslationCsv::ImportPoInteractive(const FString& Culture)
{
	TArray<FString> OutFiles;
	const void* ParentWindow = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);
	if (!FDesktopPlatformModule::Get()->OpenFileDialog(ParentWindow,
		FText::Format(LOCTEXT("ImportPoDialogTitle", "Import PO ({0})"), FText::FromString(Culture)).ToString(),
		FPaths::ProjectSavedDir(), FString(), TEXT("PO files (*.po)|*.po"), EFileDialogFlags::None, OutFiles))
	{
		return;
	}

	FKzTranslationImportStats Stats;
	FText Error;
	if (ImportPoFile(OutFiles[0], Culture, Stats, Error))
	{
		ShowNotification(FText::Format(LOCTEXT("PoImportDone", "PO import ({0}): {1} imported, {2} drifted, {3} untranslated, {4} unresolved."),
			FText::FromString(Culture), Stats.Imported, Stats.Drifted, Stats.Untranslated, Stats.Unresolved), true);
	}
	else
	{
		ShowNotification(Error, false);
	}
}

void FKzDialogueTranslationCsv::LogStaleTranslations(const TArray<FKzStaleTranslation>& Stale)
{
	if (Stale.Num() == 0) { return; }

	FMessageLog Log(TEXT("KzDialogueL10N"));
	for (const FKzStaleTranslation& Entry : Stale)
	{
		const FText Detail = Entry.LineIndex != INDEX_NONE
			? FText::Format(LOCTEXT("CoverageStaleLine", "line {0} ({1}): '{2}' translation predates the current source text; needs review."), Entry.LineIndex + 1, FText::FromString(Entry.Key), FText::FromString(Entry.Culture))
			: FText::Format(LOCTEXT("CoverageStaleSpeaker", "{0}: '{1}' translation predates the current name; needs review."), FText::FromString(Entry.Key), FText::FromString(Entry.Culture));

		Log.Warning()
			->AddToken(FUObjectToken::Create(Entry.Asset))
			->AddToken(FTextToken::Create(Detail));
	}
}

#undef LOCTEXT_NAMESPACE