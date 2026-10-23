// Copyright 2026 kirzo

#include "Localization/KzDialogueTranslationCsv.h"

#include "KzDialogueAsset.h"
#include "KzSpeakerAsset.h"
#include "KzWordAsset.h"
#include "Settings/KzDialogueSettings.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "ContentBrowserMenuContexts.h"
#include "DesktopPlatformModule.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Engine/Blueprint.h"
#include "Engine/DataTable.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Internationalization/Culture.h"
#include "Internationalization/PackageLocalizationUtil.h"
#include "Internationalization/TextNamespaceUtil.h"
#include "Misc/PackageName.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "HAL/PlatformFileManager.h"
#include "ScopedTransaction.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Internationalization/InternationalizationManifest.h"
#include "LocalizationModule.h"
#include "LocalizationSettings.h"
#include "LocalizationTargetTypes.h"
#include "LocTextHelper.h"
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
		if (!Namespace.StartsWith(TEXT("KzDialogue.")) && !Namespace.StartsWith(TEXT("KzSpeaker.")) && !Namespace.StartsWith(TEXT("KzWord.")))
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

bool FKzLocQuery::GetManifestSource(const FString& Namespace, const FString& Key, FString& OutSource) const
{
	if (!LocHelper.IsValid()) { return false; }

	if (const TSharedPtr<FManifestEntry> Entry = LocHelper->FindSourceText(FLocKey(Namespace), FLocKey(Key)))
	{
		for (const FManifestContext& Context : Entry->Contexts)
		{
			if (Context.Key == FLocKey(Key))
			{
				OutSource = Entry->Source.Text;
				return true;
			}
		}
	}
	return false;
}

TArray<FString> FKzLocQuery::GetSourceLocations(const FString& Namespace, const FString& Key) const
{
	TArray<FString> Locations;
	if (!LocHelper.IsValid()) { return Locations; }

	if (const TSharedPtr<FManifestEntry> Entry = LocHelper->FindSourceText(FLocKey(Namespace), FLocKey(Key)))
	{
		// The entry spans every identical-source sibling in the namespace (one context per
		// key), so only this key's contexts belong to the asked identity.
		for (const FManifestContext& Context : Entry->Contexts)
		{
			if (Context.Key == FLocKey(Key))
			{
				Locations.Add(Context.SourceLocation);
			}
		}
	}
	return Locations;
}

namespace
{
	FString CsvEscape(const FString& Value)
	{
		FString Escaped = Value;
		Escaped.ReplaceInline(TEXT("\""), TEXT("\"\""));
		return FString::Printf(TEXT("\"%s\""), *Escaped);
	}

	FString NormalizeNewlines(FString Value)
	{
		Value.ReplaceInline(TEXT("\r\n"), TEXT("\n"));
		Value.ReplaceInline(TEXT("\r"), TEXT("\n"));
		return Value;
	}

	/** Newline-flavor-insensitive source comparison: file round-trips (the PO \n escape, spreadsheet editors) lose the original CRLF/LF flavor, and that must not count as drift. */
	bool SourceMatches(const FString& A, const FString& B)
	{
		return NormalizeNewlines(A).Equals(NormalizeNewlines(B), ESearchCase::CaseSensitive);
	}

	/** One accented look-alike per plain letter, so pseudo text stays readable while exercising fonts and encoding. */
	TCHAR PseudoAccent(TCHAR Char)
	{
		// a e i o u y c n -> a-acute e-acute i-acute o-acute u-acute y-acute c-cedilla n-tilde (and uppercase).
		static const FString Plain = TEXT("aeiouycnAEIOUYCN");
		static const FString Accented = TEXT("\u00E1\u00E9\u00ED\u00F3\u00FA\u00FD\u00E7\u00F1\u00C1\u00C9\u00CD\u00D3\u00DA\u00DD\u00C7\u00D1");
		int32 Index = INDEX_NONE;
		return Plain.FindChar(Char, Index) ? Accented[Index] : Char;
	}

	/** Machine pseudo-translation: accented letters, ~40% length padding and bracket delimiters. Format syntax survives verbatim ("{...}" arguments, backtick escapes, "|keyword" of modifiers, plural and ordinal form keys) so runtime formatting keeps working. */
	FString MakePseudoTranslation(const FString& Source)
	{
		FString Out;
		Out.Reserve(Source.Len() * 2 + 8);
		Out += TEXT("[");

		enum class EFormArgs : uint8 { None, Key, Value };
		EFormArgs FormArgs = EFormArgs::None;
		bool bInQuote = false;

		int32 BraceDepth = 0;
		for (int32 i = 0; i < Source.Len(); ++i)
		{
			const TCHAR Char = Source[i];

			// Backtick escapes the next character in FText::Format patterns: copy both.
			if (Char == TEXT('`') && i + 1 < Source.Len())
			{
				Out.AppendChar(Char);
				Out.AppendChar(Source[++i]);
				continue;
			}

			if (Char == TEXT('{')) { ++BraceDepth; }
			else if (Char == TEXT('}')) { BraceDepth = FMath::Max(0, BraceDepth - 1); }

			if (BraceDepth > 0 || Char == TEXT('}'))
			{
				Out.AppendChar(Char);
				continue;
			}

			// Inside "|plural(one=..., other=...)" the engine matches the form keys literally.
			if (FormArgs != EFormArgs::None)
			{
				if (Char == TEXT('"'))
				{
					bInQuote = !bInQuote;
				}
				else if (!bInQuote && Char == TEXT(')'))
				{
					FormArgs = EFormArgs::None;
				}
				else if (!bInQuote && Char == TEXT(','))
				{
					FormArgs = EFormArgs::Key;
				}
				else if (FormArgs == EFormArgs::Key)
				{
					if (Char == TEXT('=')) { FormArgs = EFormArgs::Value; }
				}
				else
				{
					Out.AppendChar(PseudoAccent(Char));
					continue;
				}
				Out.AppendChar(Char);
				continue;
			}

			if (Char == TEXT('|'))
			{
				// The keyword is parser syntax; gender arguments are plain text.
				FString Keyword;
				while (i + 1 < Source.Len() && FChar::IsAlpha(Source[i + 1]))
				{
					Keyword.AppendChar(Source[++i]);
				}
				Out.AppendChar(Char);
				Out += Keyword;
				if ((Keyword == TEXT("plural") || Keyword == TEXT("ordinal")) && i + 1 < Source.Len() && Source[i + 1] == TEXT('('))
				{
					Out.AppendChar(Source[++i]);
					FormArgs = EFormArgs::Key;
				}
				continue;
			}

			Out.AppendChar(PseudoAccent(Char));
		}

		// ~40% padding makes English-sized UI reveal German-sized overflow.
		const int32 Padding = FMath::Max(2, Source.Len() * 2 / 5);
		for (int32 i = 0; i < Padding; ++i) { Out.AppendChar(TEXT('~')); }
		Out += TEXT("]");
		return Out;
	}

	template <typename AssetType>
	void AddAllAssetsOfClass(TSet<AssetType*>& Out)
	{
		const IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
		TArray<FAssetData> Found;
		Registry.GetAssetsByClass(AssetType::StaticClass()->GetClassPathName(), Found, /*bSearchSubClasses=*/true);
		for (const FAssetData& Data : Found)
		{
			if (AssetType* Asset = Cast<AssetType>(Data.GetAsset())) { Out.Add(Asset); }
		}
	}

	/** A CSV with its columns indexed by header name, so reordered or extended files keep importing. */
	struct FKzCsvTable
	{
		TUniquePtr<FCsvParser> Parser;
		TMap<FString, int32> Columns;
	};

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

	/** The dialogue line text or speaker field a CSV row targets, with its drift hash; null when the asset, line or field is gone. */
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
			Speaker->ForEachLocalizedField([&](const TCHAR* FieldKey, FText& Text, uint32 Hash, FKzWordText*)
			{
				if (Key == FieldKey) { Found = &Text; OutHash = Hash; }
			});
		}
		return Found;
	}

	/** Localization files synced from source control are often read-only on disk and the JSON save just fails: drop the flag (logged) instead. */
	void EnsureFileWritable(const FString& Path)
	{
		IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
		if (PlatformFile.FileExists(*Path) && PlatformFile.IsReadOnly(*Path))
		{
			UE_LOG(LogKzDialogueL10N, Warning, TEXT("'%s' was read-only; clearing the flag to write it (check it out in source control if applicable)."), *Path);
			PlatformFile.SetReadOnly(*Path, false);
		}
	}

	/**
	 * Copies each culture's best existing translation among FromIdentities into ToIdentity, creating ToIdentity's
	 * manifest entry when it is not gathered yet (the next Gather regenerates the manifest anyway). Stale translations
	 * are never migrated; with bKeepCurrent a culture where ToIdentity already has a current translation is left alone.
	 */
	bool CopyTranslationsToIdentity(const TArray<TPair<FString, FString>>& FromIdentities, const TPair<FString, FString>& ToIdentity, const FString& SourceText, bool bKeepCurrent, FText& OutError)
	{
		FKzArchiveWriter Writer;
		if (!Writer.Open(OutError)) { return false; }
		FLocTextHelper& LocHelper = Writer.GetHelper();

		if (!LocHelper.FindSourceText(FLocKey(ToIdentity.Key), FLocKey(ToIdentity.Value)).IsValid())
		{
			FManifestContext Context;
			Context.Key = FLocKey(ToIdentity.Value);
			Context.SourceLocation = TEXT("KzDialogue line text");
			if (!LocHelper.AddSourceText(FLocKey(ToIdentity.Key), FLocItem(SourceText), Context) || !Writer.SaveManifest(OutError))
			{
				return false;
			}
		}

		auto CurrentTranslation = [&LocHelper, &SourceText](const FString& Culture, const TPair<FString, FString>& Identity)
		{
			const TSharedPtr<FArchiveEntry> Entry = LocHelper.FindTranslation(Culture, FLocKey(Identity.Key), FLocKey(Identity.Value), nullptr);
			return Entry.IsValid() && SourceMatches(Entry->Source.Text, SourceText) ? Entry->Translation.Text : FString();
		};

		int32 CulturesWritten = 0;
		for (const FString& Culture : Writer.GetTarget().ForeignCultures)
		{
			if (bKeepCurrent && !CurrentTranslation(Culture, ToIdentity).IsEmpty()) { continue; }

			FString Translation;
			for (const TPair<FString, FString>& From : FromIdentities)
			{
				Translation = CurrentTranslation(Culture, From);
				if (!Translation.IsEmpty()) { break; }
			}
			if (!Translation.IsEmpty() && Writer.Write(Culture, FLocKey(ToIdentity.Key), FLocKey(ToIdentity.Value), nullptr, FLocItem(SourceText), FLocItem(Translation)))
			{
				++CulturesWritten;
			}
		}

		if (!Writer.Save(OutError)) { return false; }
		if (CulturesWritten > 0)
		{
			UE_LOG(LogKzDialogueL10N, Log, TEXT("Copied %d culture translation(s) into '%s,%s'."), CulturesWritten, *ToIdentity.Key, *ToIdentity.Value);
		}
		return true;
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

	void OnExportClicked(TArray<FAssetData> SelectedAssets, const FKzExportLineFilter& LineFilter = nullptr, TSharedPtr<TSet<FString>> OtherIdentities = nullptr, const FString& TranslationCulture = FString())
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
			FPaths::ProjectSavedDir(), DefaultName, TEXT("CSV files (*.csv)|*.csv"), EFileDialogFlags::None, OutFiles))
		{
			return;
		}

		FText Error;
		if (FKzDialogueTranslationCsv::ExportAssets(Assets, OutFiles[0], Error, LineFilter, OtherIdentities.Get(), TranslationCulture))
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
}

FSimpleMulticastDelegate FKzArchiveWriter::OnArchivesChanged;

bool FKzArchiveWriter::Open(FText& OutError, const TArray<FString>& Cultures)
{
	if (!FKzDialogueTranslationCsv::ReadLocTargetInfo(Target, OutError)) { return false; }

	// A culture the step inis do not list yet (a fresh pseudo culture) still gets its archive.
	TArray<FString> HelperCultures = Target.ForeignCultures;
	for (const FString& Culture : Cultures) { HelperCultures.AddUnique(Culture); }

	Helper = MakeShared<FLocTextHelper>(Target.TargetPath, Target.ManifestName, Target.ArchiveName, Target.NativeCulture, HelperCultures, nullptr);
	if (!Helper->LoadManifest(ELocTextHelperLoadFlags::Load, &OutError))
	{
		OutError = FText::Format(LOCTEXT("ImportNoManifest", "Could not load the localization manifest. Run Gather Text in the Localization Dashboard first. {0}"), OutError);
		return false;
	}
	if (!Helper->LoadNativeArchive(ELocTextHelperLoadFlags::LoadOrCreate, &OutError)) { return false; }
	for (const FString& Culture : Cultures.IsEmpty() ? Target.ForeignCultures : Cultures)
	{
		if (!Helper->LoadForeignArchive(Culture, ELocTextHelperLoadFlags::LoadOrCreate, &OutError)) { return false; }
	}
	return true;
}

bool FKzArchiveWriter::Write(const FString& Culture, const FLocKey& Namespace, const FLocKey& Key, TSharedPtr<FLocMetadataObject> KeyMetadata, const FLocItem& Source, const FLocItem& Translation, bool bOptional)
{
	if (!Helper->ImportTranslation(Culture, Namespace, Key, KeyMetadata, Source, Translation, bOptional)) { return false; }
	WrittenCultures.Add(Culture);
	return true;
}

bool FKzArchiveWriter::Save(FText& OutError)
{
	int32 Saved = 0;
	for (const FString& Culture : WrittenCultures)
	{
		EnsureFileWritable(Target.TargetPath / Culture / Target.ArchiveName);
		if (!Helper->SaveArchive(Culture, &OutError)) { break; }
		++Saved;
	}

	const bool bAllSaved = Saved == WrittenCultures.Num();
	WrittenCultures.Reset();
	if (Saved > 0) { OnArchivesChanged.Broadcast(); }
	return bAllSaved;
}

bool FKzArchiveWriter::SaveManifest(FText& OutError)
{
	EnsureFileWritable(Target.TargetPath / Target.ManifestName);
	if (!Helper->SaveManifest(&OutError)) { return false; }
	OnArchivesChanged.Broadcast();
	return true;
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
	}));
}

void FKzDialogueTranslationCsv::CollectLocTexts(const TArray<UKzDialogueAsset*>& Assets, const FKzExportLineFilter& LineFilter, bool bAllSpeakers, bool bAllWords, TArray<FKzLocText>& Out)
{
	// Texts sharing one localization identity (merged line texts) appear once.
	TSet<FString> Identities;
	auto Add = [&Out, &Identities](const FText& Text, const UObject* Owner, const FKzDialogueLine* Line, const FString& Field, uint32 SourceHash)
	{
		const TOptional<FString> Namespace = FTextInspector::GetNamespace(Text);
		const TOptional<FString> Key = FTextInspector::GetKey(Text);
		const FString* Source = FTextInspector::GetSourceString(Text);
		if (!Namespace.IsSet() || !Key.IsSet() || !Source || Source->IsEmpty()) { return; }

		bool bAlreadyAdded = false;
		Identities.Add(Namespace.GetValue() + TEXT(",") + Key.GetValue(), &bAlreadyAdded);
		if (!bAlreadyAdded)
		{
			Out.Add({ Namespace.GetValue(), Key.GetValue(), *Source, Owner, Line, Field, SourceHash });
		}
	};

	TSet<UKzSpeakerAsset*> SpeakerSet;
	for (const UKzDialogueAsset* Asset : Assets)
	{
		if (!Asset) { continue; }
		for (const FKzDialogueLine& Line : Asset->Lines)
		{
			if (LineFilter && !LineFilter(*Asset, Line)) { continue; }
			Add(Line.Text, Asset, &Line, FString(), Line.SourceTextHash);
			if (Line.Speaker.Asset) { SpeakerSet.Add(Line.Speaker.Asset); }
		}
		if (!LineFilter)
		{
			for (const FKzDialogueAlias& Alias : Asset->Aliases)
			{
				if (Alias.Speaker.Asset) { SpeakerSet.Add(Alias.Speaker.Asset); }
			}
		}
	}
	if (bAllSpeakers) { AddAllAssetsOfClass(SpeakerSet); }

	TArray<UKzSpeakerAsset*> Speakers = SpeakerSet.Array();
	Speakers.Sort([](const UKzSpeakerAsset& A, const UKzSpeakerAsset& B) { return A.GetName() < B.GetName(); });

	TSet<UKzWordAsset*> WordSet;
	for (const UKzSpeakerAsset* Speaker : Speakers)
	{
		Speaker->ForEachLocalizedField([&](const TCHAR* Field, const FText& Text, uint32 SourceHash, const FKzWordText* WordText)
		{
			Add(Text, Speaker, nullptr, Field, SourceHash);
			if (WordText && WordText->Word) { WordSet.Add(WordText->Word); }
		});
	}
	if (bAllWords) { AddAllAssetsOfClass(WordSet); }

	TArray<UKzWordAsset*> Words = WordSet.Array();
	Words.Sort([](const UKzWordAsset& A, const UKzWordAsset& B) { return A.GetName() < B.GetName(); });
	for (const UKzWordAsset* Word : Words)
	{
		Add(Word->Text, Word, nullptr, TEXT("Text"), 0);
		for (const TPair<EKzGender, FText>& Form : Word->GenderForms)
		{
			Add(Form.Value, Word, nullptr, StaticEnum<EKzGender>()->GetNameStringByValue(static_cast<int64>(Form.Key)), 0);
		}
	}
}

bool FKzDialogueTranslationCsv::ExportAssets(const TArray<UKzDialogueAsset*>& Assets, const FString& CsvPath, FText& OutError, const FKzExportLineFilter& LineFilter, const TSet<FString>* OtherIdentities, const FString& TranslationCulture)
{
	FString Csv = TEXT("Asset,Namespace,Key,Speaker,SourceText,Translation,TranslatorNotes,MaxCharacters,Audio,LocalizeAudio,SourceHash,StaleTranslation");
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
		Csv += CsvEscape(StaleTranslation);
		Csv += LINE_TERMINATOR;
		++NumRows;
	};

	// Word and project-text rows leave Asset empty: the manifest validates them at import instead of an asset hash.
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
			AddRow(FString(), Text.Namespace, Text.Key, FString(), Text.Source, FString(), 0, FString(), TEXT(""), 0);
		}
	}

	// Project texts ride along the same way when requested.
	if (OtherIdentities && !OtherIdentities->IsEmpty() && bQueryLoaded)
	{
		Query.EnumerateOtherTexts([&AddRow, OtherIdentities](const FString& Namespace, const FString& Key, const FString& Source)
		{
			if (OtherIdentities->Contains(Namespace + TEXT(",") + Key))
			{
				AddRow(FString(), Namespace, Key, FString(), Source, FString(), 0, FString(), TEXT(""), 0);
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

bool FKzDialogueTranslationCsv::ImportCsv(const FString& CsvPath, const FString& Culture, FKzTranslationImportStats& OutStats, FText& OutError)
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

		// Asset-less rows are project texts: the manifest (not an asset hash) validates them,
		// same as the PO import.
		if (AssetPath.IsEmpty())
		{
			const FString Namespace = Cell(NamespaceCol);
			const TSharedPtr<FManifestEntry> ManifestEntry = LocHelper.FindSourceText(FLocKey(Namespace), FLocKey(Key));
			if (!ManifestEntry.IsValid())
			{
				++OutStats.Unresolved;
				UE_LOG(LogKzDialogueL10N, Warning, TEXT("Unresolved row %d: project text '%s,%s' is not in the manifest anymore. Re-run Gather Text or drop the row."), RowIdx + 1, *Namespace, *Key);
				continue;
			}
			if (SourceTextCol && !SourceMatches(ManifestEntry->Source.Text, Cell(SourceTextCol)))
			{
				++OutStats.Drifted;
				UE_LOG(LogKzDialogueL10N, Warning, TEXT("Drifted row %d: project text '%s,%s' changed after this CSV was exported; needs retranslation."), RowIdx + 1, *Namespace, *Key);
				continue;
			}

			if (Writer.Write(Culture, FLocKey(Namespace), FLocKey(Key), nullptr, FLocItem(ManifestEntry->Source.Text), FLocItem(Translation)))
			{
				++OutStats.Imported;
			}
			else
			{
				++OutStats.Unresolved;
				UE_LOG(LogKzDialogueL10N, Warning, TEXT("Failed to write row %d into the '%s' archive: project text '%s,%s'."), RowIdx + 1, *Culture, *Namespace, *Key);
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
			UE_LOG(LogKzDialogueL10N, Warning, TEXT("Unresolved row %d: asset '%s', key '%s'. The asset, line or field no longer exists."), RowIdx + 1, *AssetPath, *Key);
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
				UE_LOG(LogKzDialogueL10N, Warning, TEXT("Drifted row %d: asset '%s', key '%s'. The source text changed after this CSV was exported; needs retranslation."), RowIdx + 1, *AssetPath, *Key);
				continue;
			}
		}

		// Into the text's current identity, so a row exported before a merge or unmerge lands where the line lives now.
		const TOptional<FString> CurrentNamespace = FTextInspector::GetNamespace(*SourceText);
		const TOptional<FString> CurrentKey = FTextInspector::GetKey(*SourceText);
		const FString* SourceString = FTextInspector::GetSourceString(*SourceText);
		if (CurrentNamespace.IsSet() && CurrentKey.IsSet() && SourceString
			&& Writer.Write(Culture, FLocKey(CurrentNamespace.GetValue()), FLocKey(CurrentKey.GetValue()), nullptr, FLocItem(*SourceString), FLocItem(Translation)))
		{
			++OutStats.Imported;
		}
		else
		{
			++OutStats.Unresolved;
			UE_LOG(LogKzDialogueL10N, Warning, TEXT("Failed to write row %d into the '%s' archive: asset '%s', key '%s'."), RowIdx + 1, *Culture, *AssetPath, *Key);
		}
	}

	if (!Writer.Save(OutError)) { return false; }

	UE_LOG(LogKzDialogueL10N, Log, TEXT("Imported %s translations from %s: %d imported, %d drifted, %d untranslated, %d unresolved."),
		*Culture, *CsvPath, OutStats.Imported, OutStats.Drifted, OutStats.Untranslated, OutStats.Unresolved);
	return true;
}

void FKzDialogueTranslationCsv::ExportInteractive(TArray<FAssetData> SelectedAssets, const FKzExportLineFilter& LineFilter, TSharedPtr<TSet<FString>> OtherIdentities, const FString& TranslationCulture)
{
	OnExportClicked(MoveTemp(SelectedAssets), LineFilter, OtherIdentities, TranslationCulture);
}

bool FKzDialogueTranslationCsv::WriteTranslation(const FString& Culture, const TArray<TPair<FString, FString>>& Identities, const FString& Source, const FString& Translation, FText& OutError)
{
	FKzArchiveWriter Writer;
	if (!Writer.Open(OutError, { Culture })) { return false; }

	int32 Written = 0;
	for (const TPair<FString, FString>& Identity : Identities)
	{
		if (Writer.Write(Culture, FLocKey(Identity.Key), FLocKey(Identity.Value), nullptr, FLocItem(Source), FLocItem(Translation)))
		{
			++Written;
		}
	}
	if (Written == 0)
	{
		OutError = LOCTEXT("WriteTranslationFailed", "The translation could not be written into the archive (identity no longer gathered?).");
		return false;
	}
	return Writer.Save(OutError);
}

void FKzDialogueTranslationCsv::RenameNamedTokenReferences(UKzNamedAsset* Renamed, FName OldToken)
{
	const FName NewToken = Renamed ? Renamed->Token : NAME_None;
	if (OldToken.IsNone() || NewToken.IsNone() || OldToken == NewToken) { return; }

	// Case-insensitive, mirroring how tokens resolve (FName lookups ignore case).
	const FString OldBase = FString::Printf(TEXT("{%s}"), *OldToken.ToString());
	const FString NewBase = FString::Printf(TEXT("{%s}"), *NewToken.ToString());
	const FString OldWithPart = FString::Printf(TEXT("{%s:"), *OldToken.ToString());
	const FString NewWithPart = FString::Printf(TEXT("{%s:"), *NewToken.ToString());
	auto ReplaceTokens = [&](const FString& In)
	{
		FString Out = In.Replace(*OldBase, *NewBase, ESearchCase::IgnoreCase);
		Out = Out.Replace(*OldWithPart, *NewWithPart, ESearchCase::IgnoreCase);
		return Out;
	};

	// One affected text: its identity plus the source strings before and after the rename,
	// so the archives can follow without turning anything stale.
	struct FKzRenamedText
	{
		FString Namespace;
		FString Key;
		FString OldSource;
		FString NewSource;
	};
	TArray<FKzRenamedText> RenamedTexts;

	const IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	TArray<FAssetData> DialogueAssets;
	Registry.GetAssetsByClass(UKzDialogueAsset::StaticClass()->GetClassPathName(), DialogueAssets, /*bSearchSubClasses=*/true);

	int32 ChangedAssets = 0;
	for (const FAssetData& Data : DialogueAssets)
	{
		UKzDialogueAsset* Dialogue = Cast<UKzDialogueAsset>(Data.GetAsset());
		if (!Dialogue) { continue; }

		bool bChanged = false;
		for (FKzDialogueLine& Line : Dialogue->Lines)
		{
			const TOptional<FString> Namespace = FTextInspector::GetNamespace(Line.Text);
			const TOptional<FString> Key = FTextInspector::GetKey(Line.Text);
			const FString* Source = FTextInspector::GetSourceString(Line.Text);
			if (!Namespace.IsSet() || !Key.IsSet() || !Source || Source->IsEmpty()) { continue; }

			const FString NewSource = ReplaceTokens(*Source);
			if (NewSource == *Source) { continue; }

			if (!bChanged)
			{
				Dialogue->Modify();
				bChanged = true;
			}

			// Renaming a token does not change what was recorded: an up-to-date take stays
			// up to date under the rewritten text's hash.
			const bool bTakeWasCurrent = Line.RecordedTextHash != 0 && Line.RecordedTextHash == Line.SourceTextHash;
			Line.Text = FText::ChangeKey(Namespace.GetValue(), Key.GetValue(), FText::FromString(NewSource));
			if (bTakeWasCurrent)
			{
				Line.RecordedTextHash = KzComputeSourceTextHash(Line.Text);
			}

			RenamedTexts.Add({ TextNamespaceUtil::StripPackageNamespace(Namespace.GetValue()), Key.GetValue(), *Source, NewSource });
		}

		if (bChanged)
		{
			Dialogue->PostEditChange();
			Dialogue->MarkPackageDirty();
			++ChangedAssets;
		}
	}

	// Follow up in the archives: the token is rewritten inside each translation and its recorded source.
	// A translation that matched the old source stays current; a stale one stays stale.
	FKzArchiveWriter Writer;
	FText ArchiveError;
	if (RenamedTexts.Num() > 0 && Writer.Open(ArchiveError))
	{
		for (const FString& Culture : Writer.GetTarget().ForeignCultures)
		{
			for (const FKzRenamedText& Text : RenamedTexts)
			{
				const TSharedPtr<FArchiveEntry> Entry = Writer.GetHelper().FindTranslation(Culture, FLocKey(Text.Namespace), FLocKey(Text.Key), nullptr);
				if (!Entry.IsValid() || Entry->Translation.Text.IsEmpty()) { continue; }

				const FString NewSource = SourceMatches(Entry->Source.Text, Text.OldSource) ? Text.NewSource : ReplaceTokens(Entry->Source.Text);
				Writer.Write(Culture, FLocKey(Text.Namespace), FLocKey(Text.Key), nullptr, FLocItem(NewSource), FLocItem(ReplaceTokens(Entry->Translation.Text)));
			}
		}
		if (!Writer.Save(ArchiveError))
		{
			UE_LOG(LogKzDialogueL10N, Warning, TEXT("Token rename: the archives could not be saved: %s"), *ArchiveError.ToString());
		}
	}

	ShowNotification(FText::Format(
		LOCTEXT("TokenRenameDone", "Token '{0}' renamed to '{1}': {2} line(s) in {3} asset(s) rewritten, archives updated. Save the dirty assets and re-run Gather."),
		FText::FromName(OldToken), FText::FromName(NewToken), RenamedTexts.Num(), ChangedAssets), true);
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

	/** One .po per foreign culture (<Directory>/<Culture>/<Target><FileNameSuffix>.po): existing translations fill msgstr, stale ones get the standard fuzzy flag. OnlyCulture narrows the set to that culture. */
	bool WriteKzPoFiles(const TArray<FKzPoEntry>& Entries, const FKzLocQuery& Query, const FString& Directory, const FString& FileNameSuffix, const FString& OnlyCulture, FText& OutError)
	{
		for (const FString& Culture : Query.GetTarget().ForeignCultures)
		{
			if (!OnlyCulture.IsEmpty() && Culture != OnlyCulture) { continue; }
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
					// A bare line inside an entry breaks PO parsers: every comment line needs its own marker.
					TArray<FString> CommentLines;
					NormalizeNewlines(Comment).ParseIntoArray(CommentLines, TEXT("\n"), false);
					for (const FString& CommentLine : CommentLines)
					{
						Po += FString::Printf(TEXT("#. %s") LINE_TERMINATOR, *CommentLine);
					}
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

bool FKzDialogueTranslationCsv::ExportPoFiles(const TArray<UKzDialogueAsset*>& Assets, const FString& Directory, FText& OutError, const FKzExportLineFilter& LineFilter, const FString& OnlyCulture)
{
	FKzLocQuery Query;
	if (!Query.Load(OutError)) { return false; }

	TArray<FKzLocText> Texts;
	CollectLocTexts(Assets, LineFilter, /*bAllSpeakers=*/false, /*bAllWords=*/true, Texts);

	TArray<FKzPoEntry> Entries;
	for (const FKzLocText& Text : Texts)
	{
		FKzPoEntry& Entry = Entries.AddDefaulted_GetRef();
		Entry.Namespace = Text.Namespace;
		Entry.Key = Text.Key;
		Entry.Source = Text.Source;

		if (const FKzDialogueLine* Line = Text.Line)
		{
			if (Line->Speaker.IsValid()) { Entry.Comments.Add(FString::Printf(TEXT("Speaker: %s"), *Line->Speaker.GetEditorLabel().ToString())); }
			if (!Line->TranslatorNotes.IsEmpty()) { Entry.Comments.Add(FString::Printf(TEXT("Notes: %s"), *Line->TranslatorNotes)); }
			if (Line->MaxCharacters > 0) { Entry.Comments.Add(FString::Printf(TEXT("MaxCharacters: %d"), Line->MaxCharacters)); }
			if (!Line->Audio.IsNull() && !Line->bLocalizeAudio) { Entry.Comments.Add(TEXT("Audio: do not localize (the source take plays in every culture)")); }
		}
		else if (Text.Owner->IsA<UKzSpeakerAsset>())
		{
			Entry.Comments.Add(FString::Printf(TEXT("Speaker asset: %s"), *Text.Owner->GetName()));
		}
		else
		{
			Entry.Comments.Add(Text.Field == TEXT("Text") ? FString::Printf(TEXT("Word asset: %s"), *Text.Owner->GetName()) : FString::Printf(TEXT("Word asset: %s (%s form)"), *Text.Owner->GetName(), *Text.Field));
		}
	}

	if (Entries.IsEmpty())
	{
		OutError = LOCTEXT("PoNothingToExport", "Nothing to export: no localizable texts matched.");
		return false;
	}

	return WriteKzPoFiles(Entries, Query, Directory, TEXT(""), OnlyCulture, OutError);
}

bool FKzDialogueTranslationCsv::ExportOtherTextsPoFiles(const FString& Directory, FText& OutError, const TSet<FString>* OnlyIdentities, const FString& OnlyCulture)
{
	FKzLocQuery Query;
	if (!Query.Load(OutError)) { return false; }

	// Everything the target gathered outside the dialogue namespaces: UI, menus, etc.
	// The msgctxt already carries namespace/key, so no extra comments are needed.
	TArray<FKzPoEntry> Entries;
	Query.EnumerateOtherTexts([&Entries, OnlyIdentities](const FString& Namespace, const FString& Key, const FString& Source)
	{
		if (OnlyIdentities && !OnlyIdentities->Contains(Namespace + TEXT(",") + Key)) { return; }

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

	return WriteKzPoFiles(Entries, Query, Directory, TEXT("_Other"), OnlyCulture, OutError);
}

void FKzDialogueTranslationCsv::ExportPoInteractive(TArray<FAssetData> SelectedAssets, const FKzExportLineFilter& LineFilter, bool bWithOtherTexts, TSharedPtr<TSet<FString>> OtherIdentities, const FString& OnlyCulture)
{
	TArray<UKzDialogueAsset*> Assets;
	for (const FAssetData& Data : SelectedAssets)
	{
		if (UKzDialogueAsset* Asset = Cast<UKzDialogueAsset>(Data.GetAsset())) { Assets.Add(Asset); }
	}
	if (Assets.IsEmpty() && !bWithOtherTexts) { return; }

	const void* ParentWindow = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);
	FString OutDirectory;
	if (!FDesktopPlatformModule::Get()->OpenDirectoryDialog(ParentWindow, LOCTEXT("ExportPoDialogTitle", "Export PO files to...").ToString(), FPaths::ProjectSavedDir(), OutDirectory))
	{
		return;
	}

	// Dialogue and Other files land in the same per-culture folders; either half alone is a
	// valid export, so success means at least one of them wrote.
	FText Error;
	bool bWroteLines = false;
	bool bWroteOthers = false;
	if (!Assets.IsEmpty())
	{
		bWroteLines = ExportPoFiles(Assets, OutDirectory, Error, LineFilter, OnlyCulture);
	}
	if (bWithOtherTexts)
	{
		FText OtherError;
		bWroteOthers = ExportOtherTextsPoFiles(OutDirectory, OtherError, OtherIdentities.Get(), OnlyCulture);
		if (!bWroteOthers && Error.IsEmpty()) { Error = OtherError; }
	}

	if (bWroteLines || bWroteOthers)
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

	FKzArchiveWriter Writer;
	if (!Writer.Open(OutError, { Culture })) { return false; }
	FLocTextHelper& LocHelper = Writer.GetHelper();

	// A msgid that no longer matches the manifest source is drifted. Fuzzy entries are unconfirmed work: translators clear the flag when they revise them.
	auto ProcessEntry = [&Writer, &LocHelper, &OutStats, Culture](const FString& RawCtxt, const FString& RawId, const FString& RawStr, bool bFuzzy)
	{
		const FString Ctxt = PoUnescape(RawCtxt);
		const FString Id = PoUnescape(RawId);
		const FString Str = PoUnescape(RawStr);

		if (Ctxt.IsEmpty() && Id.IsEmpty()) { return; }   // .po header block

		if (Str.IsEmpty() || bFuzzy)
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
		if (!SourceMatches(ManifestEntry->Source.Text, Id))
		{
			++OutStats.Drifted;
			UE_LOG(LogKzDialogueL10N, Warning, TEXT("Drifted PO entry '%s,%s': the source text changed after this file was exported; needs retranslation."), *Namespace, *Key);
			return;
		}

		if (Writer.Write(Culture, FLocKey(Namespace), FLocKey(Key), nullptr, FLocItem(ManifestEntry->Source.Text), FLocItem(Str)))
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
	bool bFuzzy = false;

	auto Quoted = [](const FString& In) -> FString
	{
		const FString Value = In.TrimStartAndEnd();
		return Value.Len() >= 2 && Value.StartsWith(TEXT("\"")) && Value.EndsWith(TEXT("\"")) ? Value.Mid(1, Value.Len() - 2) : FString();
	};

	auto FlushEntry = [&]()
	{
		if (!bSeenStr) { return; }
		ProcessEntry(Ctxt, Id, Str, bFuzzy);
		Ctxt.Reset(); Id.Reset(); Str.Reset();
		bSeenStr = false;
		bFuzzy = false;
	};

	for (const FString& RawLine : FileLines)
	{
		const FString Line = RawLine.TrimStartAndEnd();

		if (Line.StartsWith(TEXT("msgctxt ")) || Line.StartsWith(TEXT("msgid ")))
		{
			FlushEntry();
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
			// A blank or comment line closes the previous entry, so a flag comment belongs to the next one.
			FlushEntry();
			if (Line.StartsWith(TEXT("#,")) && Line.Contains(TEXT("fuzzy")))
			{
				bFuzzy = true;
			}
			Continuation = nullptr;
		}
	}
	FlushEntry();

	if (!Writer.Save(OutError)) { return false; }

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
	 *
	 * Occurrences already in the target state (recognised by IsAlreadyTransformed: a previous
	 * rewrite whose Gather has not run yet) count as done, not skipped. OutHandled collects the
	 * identities with no skipped location at all: their manifest entries are pure leftovers the
	 * caller may hide until the next Gather.
	 */
	void RewriteAuthoredTextOccurrences(const FKzLocQuery& Query, const TArray<TPair<FString, FString>>& Identities, const FString& SourceText, TFunctionRef<FText(const FText&)> Transform, TFunctionRef<bool(const FText&)> IsAlreadyTransformed, const FString& ManualFixHint, int32& OutRewritten, int32& OutSkipped, TArray<TPair<FString, FString>>* OutHandled)
	{
		for (const TPair<FString, FString>& Identity : Identities)
		{
			const FString& Namespace = Identity.Key;
			const FString& Key = Identity.Value;
			bool bIdentityFullyHandled = true;

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
					bIdentityFullyHandled = false;
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
					bIdentityFullyHandled = false;
					UE_LOG(LogKzDialogueL10N, Warning, TEXT("Rewrite: could not load package '%s' for '%s,%s'."), *PackageName, *Namespace, *Key);
					continue;
				}

				bool bRewrote = false;
				bool bAlreadyTransformed = false;
				int32 NearMisses = 0;

				auto TryRewriteText = [&](UObject* Owner, const FText* TextPtr)
				{
					if (!TextPtr) { return; }

					// A previous rewrite may already have transformed this occurrence while
					// the manifest still lists the old identity (Gather pending).
					if (IsAlreadyTransformed(*TextPtr))
					{
						bAlreadyTransformed = true;
						return;
					}

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

				// Levels saved as one file per actor keep each actor in its own package, so
				// the package walk cannot see them. Loading a non-partitioned level pulls its
				// external actors into memory, so they join the walk here; world-partition
				// actors left unloaded by streaming still count as skipped below.
				if (const UWorld* World = UWorld::FindWorldInPackage(Package))
				{
					if (const ULevel* Level = World->PersistentLevel)
					{
						for (AActor* Actor : Level->Actors)
						{
							if (Actor && Actor->IsPackageExternal())
							{
								Objects.Add(Actor);
								GetObjectsWithOuter(Actor, Objects, /*bIncludeNestedObjects=*/true);
							}
						}
					}
				}
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
					// Dirtying rides on each owner's Modify(): for external actors that is
					// the actor's own package, not the level's.
					for (UObject* Object : Objects)
					{
						if (UBlueprint* Blueprint = Cast<UBlueprint>(Object))
						{
							FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
						}
					}
					++OutRewritten;
				}
				else if (bAlreadyTransformed)
				{
					UE_LOG(LogKzDialogueL10N, Log, TEXT("Rewrite: '%s,%s' was already rewritten inside '%s'; the manifest catches up on the next Gather."), *Namespace, *Key, *PackageName);
				}
				else
				{
					++OutSkipped;
					bIdentityFullyHandled = false;
					UE_LOG(LogKzDialogueL10N, Warning, TEXT("Rewrite: '%s,%s' not found inside '%s' (%d near-miss(es) logged above; stale manifest needing a re-gather, or storage this walk cannot reach)."), *Namespace, *Key, *PackageName, NearMisses);
				}
			}

			if (bIdentityFullyHandled && OutHandled)
			{
				OutHandled->Add(Identity);
			}
		}
	}
}

bool FKzDialogueTranslationCsv::MergeIdenticalTexts(const FString& SourceText, const TArray<TPair<FString, FString>>& Identities, FText& OutError, int32& OutRewritten, int32& OutSkipped, TArray<TPair<FString, FString>>* OutHandled)
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

	TArray<TPair<FString, FString>> ToRewrite = Identities;
	ToRewrite.RemoveAt(CanonicalIndex);

	// One undoable step: the rewrite Modify()s every touched object.
	const FScopedTransaction Transaction(LOCTEXT("MergeTextsTrans", "Merge Identical Texts"));

	RewriteAuthoredTextOccurrences(Query, ToRewrite, SourceText,
		[&CanonicalNamespace, &CanonicalKey](const FText& Text) { return FText::ChangeKey(CanonicalNamespace, CanonicalKey, Text); },
		[&CanonicalNamespace, &CanonicalKey, &SourceText](const FText& Text)
		{
			const TOptional<FString> TextNamespace = FTextInspector::GetNamespace(Text);
			const TOptional<FString> TextKey = FTextInspector::GetKey(Text);
			const FString* TextSource = FTextInspector::GetSourceString(Text);
			return TextNamespace.IsSet() && TextKey.IsSet() && TextSource && TextKey.GetValue() == CanonicalKey && TextNamespaceUtil::StripPackageNamespace(TextNamespace.GetValue()) == CanonicalNamespace && *TextSource == SourceText;
		},
		FString::Printf(TEXT("change its NSLOCTEXT/LOCTEXT to namespace '%s', key '%s' by hand"), *CanonicalNamespace, *CanonicalKey),
		OutRewritten, OutSkipped, OutHandled);

	// Cultures the canonical lacks take their translation from the merged identities.
	FText ArchiveError;
	if (!CopyTranslationsToIdentity(ToRewrite, { CanonicalNamespace, CanonicalKey }, SourceText, /*bKeepCurrent=*/true, ArchiveError))
	{
		UE_LOG(LogKzDialogueL10N, Warning, TEXT("Texts merged, but migrating translations failed: %s"), *ArchiveError.ToString());
	}

	UE_LOG(LogKzDialogueL10N, Log, TEXT("Merge to '%s,%s': %d occurrence(s) rewritten, %d skipped. Save the dirty assets and re-run Gather."), *CanonicalNamespace, *CanonicalKey, OutRewritten, OutSkipped);
	return true;
}

bool FKzDialogueTranslationCsv::MakeTextsNonLocalizable(const FString& SourceText, const TArray<TPair<FString, FString>>& Identities, FText& OutError, int32& OutRewritten, int32& OutSkipped, TArray<TPair<FString, FString>>* OutHandled)
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

	// One undoable step: the rewrite Modify()s every touched object.
	const FScopedTransaction Transaction(LOCTEXT("NonLocTexts", "Make Texts Non-Localizable"));

	RewriteAuthoredTextOccurrences(Query, Identities, SourceText,
		[](const FText& Text)
		{
			const FString* Source = FTextInspector::GetSourceString(Text);
			return FText::AsCultureInvariant(Source ? *Source : Text.ToString());
		},
		[&SourceText](const FText& Text) { return Text.IsCultureInvariant() && Text.ToString() == SourceText; },
		FString(TEXT("wrap it in FText::AsCultureInvariant / INVTEXT by hand")),
		OutRewritten, OutSkipped, OutHandled);

	UE_LOG(LogKzDialogueL10N, Log, TEXT("Non-localizable: %d occurrence(s) rewritten, %d skipped. Save the dirty assets and re-run Gather."), OutRewritten, OutSkipped);
	return true;
}

bool FKzDialogueTranslationCsv::MergeLineTexts(const TArray<FKzLineTextRef>& Lines, FText& OutError)
{
	// Resolve the participants: at least two localizable lines with identical SOURCE strings.
	struct FResolved
	{
		UKzDialogueAsset* Asset = nullptr;
		FKzDialogueLine* Line = nullptr;
	};
	TArray<FResolved> Resolved;
	for (const FKzLineTextRef& Ref : Lines)
	{
		FKzDialogueLine* Line = Ref.Asset ? Ref.Asset->Lines.FindByPredicate([&Ref](const FKzDialogueLine& L) { return L.LineId == Ref.LineId; }) : nullptr;
		if (Line) { Resolved.Add({ Ref.Asset, Line }); }
	}
	if (Resolved.Num() < 2)
	{
		OutError = LOCTEXT("MergeLinesNothing", "Nothing to merge: fewer than two lines.");
		return false;
	}

	const FString* ReferenceSource = FTextInspector::GetSourceString(Resolved[0].Line->Text);
	const FString SourceText = ReferenceSource ? *ReferenceSource : FString();
	if (SourceText.IsEmpty())
	{
		OutError = LOCTEXT("MergeLinesEmpty", "Cannot merge empty texts.");
		return false;
	}
	for (const FResolved& Entry : Resolved)
	{
		const FString* Source = FTextInspector::GetSourceString(Entry.Line->Text);
		if (Entry.Line->Text.IsCultureInvariant() || !Source || !Source->Equals(SourceText, ESearchCase::CaseSensitive))
		{
			OutError = LOCTEXT("MergeLinesMismatch", "Cannot merge: the lines are not all localizable with identical source text.");
			return false;
		}
	}

	// The pre-merge identities feed the translation migration below.
	TArray<TPair<FString, FString>> OldIdentities;
	for (const FResolved& Entry : Resolved)
	{
		const TOptional<FString> Namespace = FTextInspector::GetNamespace(Entry.Line->Text);
		const TOptional<FString> Key = FTextInspector::GetKey(Entry.Line->Text);
		if (Namespace.IsSet() && Key.IsSet()) { OldIdentities.Emplace(Namespace.GetValue(), Key.GetValue()); }
	}

	const FGuid SharedId = FGuid::NewGuid();
	const uint32 SharedHash = KzComputeSourceTextHash(Resolved[0].Line->Text);

	{
		// One undoable step covering every touched asset; PostEditChange re-keys the texts
		// through the metadata refresh.
		const FScopedTransaction Transaction(LOCTEXT("MergeLineTextsTrans", "Merge Line Texts"));
		TSet<UKzDialogueAsset*> Assets;
		for (const FResolved& Entry : Resolved) { Assets.Add(Entry.Asset); }
		for (UKzDialogueAsset* Asset : Assets) { Asset->Modify(); }
		for (const FResolved& Entry : Resolved)
		{
			Entry.Line->SharedTextId = SharedId;
			Entry.Line->SharedTextSourceHash = SharedHash;
		}
		for (UKzDialogueAsset* Asset : Assets) { Asset->PostEditChange(); }
	}

	// Keep finished work: each culture's best existing translation moves to the shared entry.
	const TPair<FString, FString> SharedIdentity = Resolved[0].Asset->GetLineTextId(*Resolved[0].Line);
	FText ArchiveError;
	if (!CopyTranslationsToIdentity(OldIdentities, SharedIdentity, SourceText, /*bKeepCurrent=*/false, ArchiveError))
	{
		UE_LOG(LogKzDialogueL10N, Warning, TEXT("Line texts merged, but migrating translations failed: %s"), *ArchiveError.ToString());
	}

	UE_LOG(LogKzDialogueL10N, Log, TEXT("Merged %d line texts into '%s,%s'. Save the dirty assets and re-run Gather."), Resolved.Num(), *SharedIdentity.Key, *SharedIdentity.Value);
	return true;
}

bool FKzDialogueTranslationCsv::UnmergeLineText(UKzDialogueAsset* Asset, const FGuid LineId, FText& OutError)
{
	FKzDialogueLine* Line = Asset ? Asset->Lines.FindByPredicate([&LineId](const FKzDialogueLine& L) { return L.LineId == LineId; }) : nullptr;
	if (!Line || !Line->SharedTextId.IsValid())
	{
		OutError = LOCTEXT("UnmergeNothing", "The line does not share its text.");
		return false;
	}

	const TPair<FString, FString> SharedIdentity = Asset->GetLineTextId(*Line);
	const FString* Source = FTextInspector::GetSourceString(Line->Text);
	const FString SourceText = Source ? *Source : FString();

	{
		const FScopedTransaction Transaction(LOCTEXT("UnmergeLineTextTrans", "Unmerge Line Text"));
		Asset->Modify();
		Line->SharedTextId.Invalidate();
		Line->SharedTextSourceHash = 0;
		Asset->PostEditChange();
	}

	// Seed the recovered own identity with the shared translation, so unmerging loses no work.
	if (!SourceText.IsEmpty())
	{
		FText ArchiveError;
		if (!CopyTranslationsToIdentity({ SharedIdentity }, Asset->GetLineTextId(*Line), SourceText, /*bKeepCurrent=*/false, ArchiveError))
		{
			UE_LOG(LogKzDialogueL10N, Warning, TEXT("Line text unmerged, but seeding its translations failed: %s"), *ArchiveError.ToString());
		}
	}

	UE_LOG(LogKzDialogueL10N, Log, TEXT("Unmerged line text from '%s,%s'. Save the dirty asset and re-run Gather."), *SharedIdentity.Key, *SharedIdentity.Value);
	return true;
}

bool FKzDialogueTranslationCsv::GeneratePseudoTranslations(int32& OutCount, FText& OutError)
{
	OutCount = 0;

	const UKzDialogueSettings* Settings = UKzDialogueSettings::Get();
	const FString PseudoCulture = Settings ? Settings->PseudoCulture : FString();
	if (PseudoCulture.IsEmpty())
	{
		OutError = LOCTEXT("PseudoDisabled", "No pseudo culture set in the KzDialogue settings.");
		return false;
	}

	// The culture must live in the localization target so gather/compile know it; added here
	// and persisted the way the Localization Dashboard does (settings mirror + default ini).
	if (ULocalizationTarget* Target = ILocalizationModule::Get().GetLocalizationTargetByName(Settings->LocalizationTargetName, /*bIsEngineTarget=*/false))
	{
		const bool bKnown = Target->Settings.SupportedCulturesStatistics.ContainsByPredicate([&PseudoCulture](const FCultureStatistics& Stats) { return Stats.CultureName == PseudoCulture; });
		if (!bKnown)
		{
			Target->Settings.SupportedCulturesStatistics.Add(FCultureStatistics(PseudoCulture));
			FPropertyChangedEvent Event(nullptr);
			GetMutableDefault<ULocalizationSettings>()->PostEditChangeProperty(Event);
			UE_LOG(LogKzDialogueL10N, Log, TEXT("Added pseudo culture '%s' to localization target '%s'."), *PseudoCulture, *Target->Settings.Name);
		}
	}

	// Belt and braces: pseudo data must never ride into packaged builds unnoticed.
	TArray<FString> CulturesToStage;
	GConfig->GetArray(TEXT("/Script/UnrealEd.ProjectPackagingSettings"), TEXT("CulturesToStage"), CulturesToStage, GGameIni);
	if (CulturesToStage.Contains(PseudoCulture))
	{
		UE_LOG(LogKzDialogueL10N, Warning, TEXT("Pseudo culture '%s' is in the packaging CulturesToStage and would ship with packaged builds. Remove it there; QA builds opt in with -CookCultures."), *PseudoCulture);
	}

	// The step inis may not list a freshly added culture until the next Gather: the writer gets it explicitly.
	FKzArchiveWriter Writer;
	if (!Writer.Open(OutError, { PseudoCulture })) { return false; }

	int32 Count = 0;
	Writer.GetHelper().EnumerateSourceTexts([&Writer, &PseudoCulture, &Count](TSharedRef<FManifestEntry> Entry)
	{
		const FString Pseudo = MakePseudoTranslation(Entry->Source.Text);
		for (const FManifestContext& Context : Entry->Contexts)
		{
			if (Writer.Write(PseudoCulture, Entry->Namespace, Context.Key, Context.KeyMetadataObj, Entry->Source, FLocItem(Pseudo), Context.bIsOptional))
			{
				++Count;
			}
		}
		return true;
	}, /*bCheckDependencies=*/true);

	if (!Writer.Save(OutError)) { return false; }

	OutCount = Count;
	UE_LOG(LogKzDialogueL10N, Log, TEXT("Generated %d pseudo-translations into '%s'. Compile the culture and switch to it in PIE."), Count, *PseudoCulture);
	return true;
}

bool FKzDialogueTranslationCsv::ImportSourceFixes(const FString& CsvPath, FKzSourceFixStats& OutStats, FText& OutError)
{
	FKzCsvTable Table;
	if (!ParseTranslationCsv(CsvPath, { TEXT("Asset"), TEXT("Namespace"), TEXT("Key"), TEXT("SourceText"), TEXT("SourceHash") }, Table, OutError)) { return false; }
	const FCsvParser::FRows& Rows = Table.Parser->GetRows();
	const int32* AssetCol = Table.Columns.Find(TEXT("Asset"));
	const int32* NamespaceCol = Table.Columns.Find(TEXT("Namespace"));
	const int32* KeyCol = Table.Columns.Find(TEXT("Key"));
	const int32* SourceTextCol = Table.Columns.Find(TEXT("SourceText"));
	const int32* HashCol = Table.Columns.Find(TEXT("SourceHash"));

	// Project-text rows rewrite their authored occurrences through the manifest.
	FKzLocQuery Query;
	FText QueryError;
	const bool bQueryLoaded = Query.Load(QueryError);

	const FScopedTransaction Transaction(LOCTEXT("SourceFixTrans", "Import Source Fixes"));

	TMap<FString, UObject*> LoadedAssets;
	for (int32 RowIdx = 1; RowIdx < Rows.Num(); ++RowIdx)
	{
		const TArray<const TCHAR*>& Cells = Rows[RowIdx];
		auto Cell = [&Cells](const int32* Col) { return Cells.IsValidIndex(*Col) ? FString(Cells[*Col]) : FString(); };

		// Normalized to LF: spreadsheet round-trips rewrite in-cell newlines as CRLF, and
		// authored texts should not inherit that.
		const FString CsvSource = NormalizeNewlines(Cell(SourceTextCol));
		const FString AssetPath = Cell(AssetCol);
		const FString Namespace = Cell(NamespaceCol);
		const FString Key = Cell(KeyCol);

		if (CsvSource.IsEmpty())
		{
			++OutStats.Unresolved;
			UE_LOG(LogKzDialogueL10N, Warning, TEXT("Source-fix row %d: empty SourceText for '%s,%s'; emptying a text is an editor decision, not a CSV one."), RowIdx + 1, *Namespace, *Key);
			continue;
		}

		// Asset-less rows are project texts: the manifest holds the exported source and the
		// rewrite machinery updates the authored occurrences in place.
		if (AssetPath.IsEmpty())
		{
			FString ManifestSource;
			if (!bQueryLoaded || !Query.GetManifestSource(Namespace, Key, ManifestSource))
			{
				++OutStats.Unresolved;
				UE_LOG(LogKzDialogueL10N, Warning, TEXT("Source-fix row %d: project text '%s,%s' is not in the manifest anymore."), RowIdx + 1, *Namespace, *Key);
				continue;
			}
			if (SourceMatches(ManifestSource, CsvSource))
			{
				++OutStats.Unchanged;
				continue;
			}

			int32 Rewritten = 0;
			int32 Skipped = 0;
			TArray<TPair<FString, FString>> Handled;
			const TArray<TPair<FString, FString>> Identity = { TPair<FString, FString>(Namespace, Key) };
			RewriteAuthoredTextOccurrences(Query, Identity, ManifestSource,
				[&Namespace, &Key, &CsvSource](const FText&) { return FText::ChangeKey(Namespace, Key, FText::FromString(CsvSource)); },
				[&Namespace, &Key, &CsvSource](const FText& Text)
				{
					const TOptional<FString> TextNamespace = FTextInspector::GetNamespace(Text);
					const TOptional<FString> TextKey = FTextInspector::GetKey(Text);
					const FString* TextSource = FTextInspector::GetSourceString(Text);
					return TextNamespace.IsSet() && TextKey.IsSet() && TextSource && TextKey.GetValue() == Key && TextNamespaceUtil::StripPackageNamespace(TextNamespace.GetValue()) == Namespace && *TextSource == CsvSource;
				},
				FString(TEXT("update the source text in code by hand")),
				Rewritten, Skipped, &Handled);

			if (Handled.IsEmpty())
			{
				// The authored text no longer matches the exported source (edited on both
				// sides) or lives where the rewrite cannot reach; details logged above.
				++OutStats.Conflicted;
			}
			else if (Rewritten > 0)
			{
				++OutStats.Fixed;
			}
			else
			{
				++OutStats.Unchanged;
			}
			continue;
		}

		UObject*& Asset = LoadedAssets.FindOrAdd(AssetPath);
		if (!Asset)
		{
			Asset = LoadObject<UObject>(nullptr, *AssetPath);
		}

		// Same row resolution as the translation import; here the resolved text is the WRITE target.
		uint32 CurrentHash = 0;
		bool bSharedLine = false;
		FText* TargetText = ResolveAuthoredText(Asset, Namespace, Key, CurrentHash, bSharedLine);
		if (bSharedLine)
		{
			// Editing one line would detach it from the others; the fix is a decision for the editor.
			++OutStats.Conflicted;
			UE_LOG(LogKzDialogueL10N, Warning, TEXT("Conflicted source-fix row %d: asset '%s', key '%s' is a shared text; fix it in the editor or unmerge it first."), RowIdx + 1, *AssetPath, *Key);
			continue;
		}
		if (!TargetText)
		{
			++OutStats.Unresolved;
			UE_LOG(LogKzDialogueL10N, Warning, TEXT("Source-fix row %d: asset '%s', key '%s'. The asset, line or field no longer exists."), RowIdx + 1, *AssetPath, *Key);
			continue;
		}

		const FString* CurrentSource = FTextInspector::GetSourceString(*TargetText);
		if (CurrentSource && SourceMatches(*CurrentSource, CsvSource))
		{
			++OutStats.Unchanged;
			continue;
		}

		// The hash proves the asset kept the exported wording: the CSV edit is the only side.
		const uint32 CsvHash = static_cast<uint32>(FCString::Strtoui64(*Cell(HashCol), nullptr, 10));
		if (CsvHash != CurrentHash)
		{
			++OutStats.Conflicted;
			UE_LOG(LogKzDialogueL10N, Warning, TEXT("Conflicted source-fix row %d: asset '%s', key '%s' changed after this CSV was exported; fix it in the editor instead."), RowIdx + 1, *AssetPath, *Key);
			continue;
		}

		const TOptional<FString> CurrentNamespace = FTextInspector::GetNamespace(*TargetText);
		const TOptional<FString> CurrentKey = FTextInspector::GetKey(*TargetText);
		if (!CurrentNamespace.IsSet() || !CurrentKey.IsSet())
		{
			++OutStats.Conflicted;
			UE_LOG(LogKzDialogueL10N, Warning, TEXT("Conflicted source-fix row %d: asset '%s', key '%s' is no longer localizable."), RowIdx + 1, *AssetPath, *Key);
			continue;
		}

		Asset->Modify();
		*TargetText = FText::ChangeKey(CurrentNamespace.GetValue(), CurrentKey.GetValue(), FText::FromString(CsvSource));
		Asset->PostEditChange();
		Asset->MarkPackageDirty();
		++OutStats.Fixed;
	}

	UE_LOG(LogKzDialogueL10N, Log, TEXT("Source fixes from %s: %d fixed, %d conflicted, %d unchanged, %d unresolved. Save the dirty assets and re-run Gather."),
		*CsvPath, OutStats.Fixed, OutStats.Conflicted, OutStats.Unchanged, OutStats.Unresolved);
	return true;
}

void FKzDialogueTranslationCsv::ImportSourceFixesInteractive()
{
	TArray<FString> OutFiles;
	const void* ParentWindow = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);
	if (!FDesktopPlatformModule::Get()->OpenFileDialog(ParentWindow, LOCTEXT("SourceFixDialogTitle", "Import source fixes from CSV").ToString(),
		FPaths::ProjectSavedDir(), FString(), TEXT("CSV files (*.csv)|*.csv"), EFileDialogFlags::None, OutFiles))
	{
		return;
	}

	FKzSourceFixStats Stats;
	FText Error;
	if (!ImportSourceFixes(OutFiles[0], Stats, Error))
	{
		ShowNotification(Error, false);
		return;
	}

	const FText Summary = FText::Format(
		LOCTEXT("SourceFixSummary", "Source fixes: {0} fixed, {1} conflicted (skipped, see Output Log), {2} unchanged, {3} unresolved. Save the dirty assets and run Gather; translations against the old wording turn stale."),
		Stats.Fixed, Stats.Conflicted, Stats.Unchanged, Stats.Unresolved);
	ShowNotification(Summary, Stats.Conflicted == 0 && Stats.Unresolved == 0);
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

#undef LOCTEXT_NAMESPACE