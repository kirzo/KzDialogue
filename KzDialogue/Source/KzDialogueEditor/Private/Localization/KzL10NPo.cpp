// Copyright 2026 kirzo

#include "Localization/KzL10N.h"

#include "KzDialogueAsset.h"
#include "KzSpeakerAsset.h"
#include "DesktopPlatformModule.h"
#include "Framework/Application/SlateApplication.h"
#include "Internationalization/InternationalizationManifest.h"
#include "Logging/MessageLog.h"
#include "LocTextHelper.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#define LOCTEXT_NAMESPACE "KzDialogueL10N"

using namespace KzDialogueL10N::Detail;

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

	/** The culture a .po header declares ("Language: es"); empty when it declares none. */
	FString ReadPoLanguage(const TArray<FString>& FileLines)
	{
		for (const FString& RawLine : FileLines)
		{
			const FString Line = RawLine.TrimStartAndEnd();
			if (Line.StartsWith(TEXT("msgctxt "))) { break; }   // past the header
			if (Line.StartsWith(TEXT("\"Language:")))
			{
				return Line.Mid(10).Replace(TEXT("\\n"), TEXT("")).Replace(TEXT("\""), TEXT("")).TrimStartAndEnd();
			}
		}
		return FString();
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
					// The source it was translated against, so Poedit and Weblate show the diff.
					Po += TEXT("#, fuzzy") LINE_TERMINATOR;
					Po += FString::Printf(TEXT("#| msgid \"%s\"") LINE_TERMINATOR, *PoEscape(ArchiveSource));
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

bool KzDialogueL10N::ExportPoFiles(const TArray<UKzDialogueAsset*>& Assets, const FString& Directory, FText& OutError, const FKzExportLineFilter& LineFilter, const FString& OnlyCulture)
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

bool KzDialogueL10N::ExportOtherTextsPoFiles(const FString& Directory, FText& OutError, const TSet<FString>* OnlyIdentities, const FString& OnlyCulture)
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

void KzDialogueL10N::ExportPoInteractive(TArray<FAssetData> SelectedAssets, const FKzExportLineFilter& LineFilter, bool bWithOtherTexts, TSharedPtr<TSet<FString>> OtherIdentities, const FString& OnlyCulture)
{
	TArray<UKzDialogueAsset*> Assets;
	for (const FAssetData& Data : SelectedAssets)
	{
		if (UKzDialogueAsset* Asset = Cast<UKzDialogueAsset>(Data.GetAsset())) { Assets.Add(Asset); }
	}
	if (Assets.IsEmpty() && !bWithOtherTexts) { return; }

	const void* ParentWindow = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);
	FString OutDirectory;
	if (!FDesktopPlatformModule::Get()->OpenDirectoryDialog(ParentWindow, LOCTEXT("ExportPoDialogTitle", "Export PO files to...").ToString(), LastDirectory(ELastDirectory::GENERIC_EXPORT), OutDirectory))
	{
		return;
	}
	FEditorDirectories::Get().SetLastDirectory(ELastDirectory::GENERIC_EXPORT, OutDirectory);

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

bool KzDialogueL10N::ImportPoFile(const FString& PoPath, const FString& Culture, FKzTranslationImportStats& OutStats, FText& OutError)
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

	FMessageLog Log(MessageLogName);
	BeginLogPage(Log, FText::Format(LOCTEXT("PoImportPage", "Import {0} ({1})"), FText::FromString(FPaths::GetCleanFilename(PoPath)), FText::FromString(Culture)));

	const TSet<FName> NamedTokens = CollectNamedTokens();

	// A msgid that no longer matches the manifest source is drifted. Fuzzy entries are unconfirmed work: translators clear the flag when they revise them.
	auto ProcessEntry = [&Writer, &LocHelper, &OutStats, &Log, &NamedTokens, Culture](const FString& RawCtxt, const FString& RawId, const FString& RawStr, bool bFuzzy, int32 MaxCharacters)
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
			LogRow(Log, FString(), FString::Printf(TEXT("PO entry with unrecognized msgctxt '%s' (expected 'Namespace,Key')."), *Ctxt));
			return;
		}

		const TSharedPtr<FManifestEntry> ManifestEntry = LocHelper.FindSourceText(FLocKey(Namespace), FLocKey(Key));
		if (!ManifestEntry.IsValid())
		{
			++OutStats.Unresolved;
			LogRow(Log, FString(), FString::Printf(TEXT("Unresolved PO entry '%s,%s': not gathered (added since the last Gather, or gone). Run Gather Text, or drop the entry."), *Namespace, *Key));
			return;
		}
		if (!SourceMatches(ManifestEntry->Source.Text, Id))
		{
			++OutStats.Drifted;
			LogRow(Log, FString(), FString::Printf(TEXT("Drifted PO entry '%s,%s': the source text changed after this file was exported; needs retranslation."), *Namespace, *Key));
			return;
		}
		if (!CheckImportedTranslation(ManifestEntry->Source.Text, Str, MaxCharacters, NamedTokens, OutStats, Log, FString(), FString::Printf(TEXT("PO entry '%s,%s'"), *Namespace, *Key)))
		{
			return;
		}

		if (Writer.Write(Culture, FLocKey(Namespace), FLocKey(Key), nullptr, FLocItem(ManifestEntry->Source.Text), FLocItem(Str)))
		{
			++OutStats.Imported;
		}
		else
		{
			++OutStats.Unresolved;
			LogRow(Log, FString(), FString::Printf(TEXT("Failed to write PO entry '%s,%s' into the '%s' archive."), *Namespace, *Key, *Culture));
		}
	};

	// Minimal .po reader: msgctxt/msgid/msgstr plus bare-string continuation lines.
	FString Ctxt;
	FString Id;
	FString Str;
	FString* Continuation = nullptr;
	bool bSeenStr = false;
	bool bFuzzy = false;
	int32 MaxCharacters = 0;

	auto Quoted = [](const FString& In) -> FString
	{
		const FString Value = In.TrimStartAndEnd();
		return Value.Len() >= 2 && Value.StartsWith(TEXT("\"")) && Value.EndsWith(TEXT("\"")) ? Value.Mid(1, Value.Len() - 2) : FString();
	};

	auto FlushEntry = [&]()
	{
		if (!bSeenStr) { return; }
		ProcessEntry(Ctxt, Id, Str, bFuzzy, MaxCharacters);
		Ctxt.Reset(); Id.Reset(); Str.Reset();
		bSeenStr = false;
		bFuzzy = false;
		MaxCharacters = 0;
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
			// A blank or comment line closes the previous entry, so a flag or context comment belongs to the next one.
			FlushEntry();
			if (Line.StartsWith(TEXT("#,")) && Line.Contains(TEXT("fuzzy")))
			{
				bFuzzy = true;
			}
			else if (Line.StartsWith(TEXT("#. MaxCharacters:")))
			{
				MaxCharacters = FCString::Atoi(*Line.Mid(17));
			}
			Continuation = nullptr;
		}
	}
	FlushEntry();

	if (!Writer.Save(OutError)) { return false; }

	Log.Info(FText::Format(LOCTEXT("ImportLogSummary", "{0} imported ({1} too long), {2} drifted, {3} rejected, {4} untranslated, {5} unresolved."), OutStats.Imported, OutStats.TooLong, OutStats.Drifted, OutStats.Rejected, OutStats.Untranslated, OutStats.Unresolved));
	return true;
}

void KzDialogueL10N::ImportPoInteractive(const FString& Culture)
{
	TArray<FString> OutFiles;
	const void* ParentWindow = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);
	if (!FDesktopPlatformModule::Get()->OpenFileDialog(ParentWindow,
		FText::Format(LOCTEXT("ImportPoDialogTitle", "Import PO ({0})"), FText::FromString(Culture)).ToString(),
		LastDirectory(ELastDirectory::GENERIC_IMPORT), FString(), TEXT("PO files (*.po)|*.po"), EFileDialogFlags::None, OutFiles))
	{
		return;
	}
	FEditorDirectories::Get().SetLastDirectory(ELastDirectory::GENERIC_IMPORT, FPaths::GetPath(OutFiles[0]));

	TArray<FString> FileLines;
	FFileHelper::LoadFileToStringArray(FileLines, *OutFiles[0]);
	if (!ConfirmImportCulture(OutFiles[0], ReadPoLanguage(FileLines), Culture)) { return; }

	FKzTranslationImportStats Stats;
	FText Error;
	if (ImportPoFile(OutFiles[0], Culture, Stats, Error))
	{
		FFormatNamedArguments Args;
		Args.Add(TEXT("Culture"), FText::FromString(Culture));
		Args.Add(TEXT("Imported"), Stats.Imported);
		Args.Add(TEXT("TooLong"), Stats.TooLong);
		Args.Add(TEXT("Drifted"), Stats.Drifted);
		Args.Add(TEXT("Rejected"), Stats.Rejected);
		Args.Add(TEXT("Untranslated"), Stats.Untranslated);
		Args.Add(TEXT("Unresolved"), Stats.Unresolved);
		ShowNotification(FText::Format(LOCTEXT("PoImportDone", "PO import ({Culture}): {Imported} imported ({TooLong} too long), {Drifted} drifted and {Rejected} with broken placeholders (skipped), {Untranslated} untranslated, {Unresolved} unresolved. Run Compile Text to apply."), Args),
			Stats.Drifted == 0 && Stats.Rejected == 0 && Stats.Unresolved == 0, /*bWithLogLink=*/true);
	}
	else
	{
		ShowNotification(Error, false);
	}
}

#undef LOCTEXT_NAMESPACE