// Copyright 2026 kirzo

#include "Localization/KzL10N.h"

#include "KzDialogueAsset.h"
#include "KzSpeakerAsset.h"
#include "KzWordAsset.h"
#include "Settings/KzDialogueSettings.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Internationalization/Culture.h"
#include "Internationalization/PackageLocalizationUtil.h"
#include "Misc/PackageName.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "HAL/PlatformFileManager.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Internationalization/InternationalizationManifest.h"
#include "LocalizationModule.h"
#include "LocalizationSettings.h"
#include "LocalizationTargetTypes.h"
#include "Logging/MessageLog.h"
#include "Logging/TokenizedMessage.h"
#include "LocTextHelper.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "KzDialogueL10N"

using namespace KzDialogueL10N::Detail;

DEFINE_LOG_CATEGORY(LogKzDialogueL10N);

namespace
{
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

	/** Inline writes share one log page until an import opens its own. */
	bool GInlineEditsPageOpen = false;
}

namespace KzDialogueL10N::Detail
{
	FString NormalizeNewlines(FString Value)
	{
		Value.ReplaceInline(TEXT("\r\n"), TEXT("\n"));
		Value.ReplaceInline(TEXT("\r"), TEXT("\n"));
		return Value;
	}

	bool SourceMatches(const FString& A, const FString& B)
	{
		return NormalizeNewlines(A).Equals(NormalizeNewlines(B), ESearchCase::CaseSensitive);
	}

	uint32 ExportHash(const FString& Value)
	{
		return FCrc::StrCrc32(*NormalizeNewlines(Value));
	}

	bool IsSpreadsheetError(const FString& Value)
	{
		static const TCHAR* Errors[] = { TEXT("#NAME?"), TEXT("#VALUE!"), TEXT("#REF!"), TEXT("#DIV/0!"), TEXT("#N/A"), TEXT("#NUM!"), TEXT("#NULL!") };
		const FString Trimmed = Value.TrimStartAndEnd();
		for (const TCHAR* Error : Errors)
		{
			if (Trimmed.Equals(Error, ESearchCase::IgnoreCase)) { return true; }
		}
		return false;
	}

	void BeginLogPage(FMessageLog& Log, const FText& Title)
	{
		Log.NewPage(Title);
		GInlineEditsPageOpen = false;
	}

	void LogRow(FMessageLog& Log, const FString& AssetPath, const FString& Message)
	{
		TSharedRef<FTokenizedMessage> Entry = Log.Warning();
		if (!AssetPath.IsEmpty()) { Entry->AddToken(FAssetNameToken::Create(AssetPath)); }
		Entry->AddToken(FTextToken::Create(FText::FromString(Message)));
	}

	void ShowNotification(const FText& Message, bool bSuccess, bool bWithLogLink)
	{
		FNotificationInfo Info(Message);
		Info.ExpireDuration = 6.0f;
		if (bWithLogLink)
		{
			Info.HyperlinkText = LOCTEXT("ShowLog", "Show log");
			Info.Hyperlink = FSimpleDelegate::CreateLambda([] { FMessageLog(KzDialogueL10N::MessageLogName).Open(EMessageSeverity::Info, /*bOpenEvenIfEmpty=*/true); });
		}
		if (TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
		{
			Item->SetCompletionState(bSuccess ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
		}
	}

	FString LastDirectory(ELastDirectory::Type Kind)
	{
		const FString Last = FEditorDirectories::Get().GetLastDirectory(Kind);
		return Last.IsEmpty() ? FPaths::ProjectSavedDir() : Last;
	}
}

bool KzDialogueL10N::ReadLocTargetInfo(FKzLocTargetInfo& Out, FText& OutError)
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
	if (!KzDialogueL10N::ReadLocTargetInfo(Target, OutError)) { return false; }

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
	// The registry's package list, not the disk: the coverage asks per line and culture on every refresh.
	return FPackageLocalizationUtil::ConvertSourceToLocalized(SourcePackageName, Culture, LocalizedPackage) && IAssetRegistry::GetChecked().DoesPackageExistOnDisk(FName(*LocalizedPackage));
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

FSimpleMulticastDelegate FKzArchiveWriter::OnArchivesChanged;

bool FKzArchiveWriter::Open(FText& OutError, const TArray<FString>& Cultures)
{
	if (!KzDialogueL10N::ReadLocTargetInfo(Target, OutError)) { return false; }

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

void KzDialogueL10N::CollectLocTexts(const TArray<UKzDialogueAsset*>& Assets, const FKzExportLineFilter& LineFilter, bool bAllSpeakers, bool bAllWords, TArray<FKzLocText>& Out)
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

bool KzDialogueL10N::WriteTranslation(const FString& Culture, const TArray<TPair<FString, FString>>& Identities, const FString& Source, const FString& Translation, FText& OutError)
{
	FKzArchiveWriter Writer;
	if (!Writer.Open(OutError, { Culture })) { return false; }

	// Archive writes have no undo: each one is logged with what it replaced, to restore by hand.
	TArray<FText> Replaced;
	for (const TPair<FString, FString>& Identity : Identities)
	{
		const TSharedPtr<FArchiveEntry> Previous = Writer.GetHelper().FindTranslation(Culture, FLocKey(Identity.Key), FLocKey(Identity.Value), nullptr);
		const FString OldTranslation = Previous.IsValid() ? Previous->Translation.Text : FString();
		if (Writer.Write(Culture, FLocKey(Identity.Key), FLocKey(Identity.Value), nullptr, FLocItem(Source), FLocItem(Translation)))
		{
			Replaced.Add(FText::Format(LOCTEXT("InlineWriteLog", "{0} {1},{2}: '{3}' -> '{4}'"), FText::FromString(Culture), FText::FromString(Identity.Key), FText::FromString(Identity.Value), FText::FromString(OldTranslation), FText::FromString(Translation)));
		}
	}
	if (Replaced.IsEmpty())
	{
		OutError = LOCTEXT("WriteTranslationFailed", "The translation could not be written into the archive (identity no longer gathered?).");
		return false;
	}
	if (!Writer.Save(OutError)) { return false; }

	FMessageLog Log(MessageLogName);
	if (!GInlineEditsPageOpen)
	{
		Log.NewPage(LOCTEXT("InlineEditsPage", "Inline translation edits"));
		GInlineEditsPageOpen = true;
	}
	for (const FText& Entry : Replaced)
	{
		Log.Info(Entry);
	}
	return true;
}

FText KzDialogueL10N::GetCultureDisplayLabel(const FString& Culture)
{
	const FCulturePtr CulturePtr = FInternationalization::Get().GetCulture(Culture);
	const FString DisplayName = CulturePtr.IsValid() ? CulturePtr->GetDisplayName() : FString();
	if (DisplayName.IsEmpty() || DisplayName == Culture)
	{
		return FText::FromString(Culture);
	}
	return FText::FromString(FString::Printf(TEXT("%s (%s)"), *DisplayName, *Culture));
}

bool KzDialogueL10N::GeneratePseudoTranslations(int32& OutCount, FText& OutError)
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

#undef LOCTEXT_NAMESPACE