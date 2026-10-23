// Copyright 2026 kirzo

#include "Localization/KzL10N.h"

#include "KzDialogueAsset.h"
#include "KzNamedAsset.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "DesktopPlatformModule.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Engine/Blueprint.h"
#include "Engine/DataTable.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Internationalization/TextNamespaceUtil.h"
#include "ScopedTransaction.h"
#include "Framework/Application/SlateApplication.h"
#include "Internationalization/InternationalizationManifest.h"
#include "Logging/MessageLog.h"
#include "LocTextHelper.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectHash.h"

#define LOCTEXT_NAMESPACE "KzDialogueL10N"

using namespace KzDialogueL10N::Detail;

namespace
{
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

void KzDialogueL10N::RenameNamedTokenReferences(UKzNamedAsset* Renamed, FName OldToken)
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

bool KzDialogueL10N::MergeIdenticalTexts(const FString& SourceText, const TArray<TPair<FString, FString>>& Identities, FText& OutError, int32& OutRewritten, int32& OutSkipped, TArray<TPair<FString, FString>>* OutHandled)
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

bool KzDialogueL10N::MakeTextsNonLocalizable(const FString& SourceText, const TArray<TPair<FString, FString>>& Identities, FText& OutError, int32& OutRewritten, int32& OutSkipped, TArray<TPair<FString, FString>>* OutHandled)
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

bool KzDialogueL10N::MergeLineTexts(const TArray<FKzLineTextRef>& Lines, FText& OutError)
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

bool KzDialogueL10N::UnmergeLineText(UKzDialogueAsset* Asset, const FGuid LineId, FText& OutError)
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

bool KzDialogueL10N::ImportSourceFixes(const FString& CsvPath, FKzSourceFixStats& OutStats, FText& OutError)
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

	FMessageLog Log(MessageLogName);
	BeginLogPage(Log, FText::Format(LOCTEXT("SourceFixPage", "Source fixes {0}"), FText::FromString(FPaths::GetCleanFilename(CsvPath))));

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
			LogRow(Log, AssetPath, FString::Printf(TEXT("Source-fix row %d: empty SourceText for '%s,%s'; emptying a text is an editor decision, not a CSV one."), RowIdx + 1, *Namespace, *Key));
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
				LogRow(Log, FString(), FString::Printf(TEXT("Source-fix row %d: project text '%s,%s' is not gathered (added since the last Gather, or gone)."), RowIdx + 1, *Namespace, *Key));
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
				// sides) or lives where the rewrite cannot reach.
				++OutStats.Conflicted;
				LogRow(Log, FString(), FString::Printf(TEXT("Conflicted source-fix row %d: project text '%s,%s' could not be rewritten; the Output Log says where it lives."), RowIdx + 1, *Namespace, *Key));
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
			LogRow(Log, AssetPath, FString::Printf(TEXT("Conflicted source-fix row %d, key '%s': a shared text; fix it in the editor or unmerge it first."), RowIdx + 1, *Key));
			continue;
		}
		if (!TargetText)
		{
			++OutStats.Unresolved;
			LogRow(Log, AssetPath, FString::Printf(TEXT("Source-fix row %d, key '%s': the asset, line or field no longer exists."), RowIdx + 1, *Key));
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
			LogRow(Log, AssetPath, FString::Printf(TEXT("Conflicted source-fix row %d, key '%s': changed after this CSV was exported; fix it in the editor instead."), RowIdx + 1, *Key));
			continue;
		}

		const TOptional<FString> CurrentNamespace = FTextInspector::GetNamespace(*TargetText);
		const TOptional<FString> CurrentKey = FTextInspector::GetKey(*TargetText);
		if (!CurrentNamespace.IsSet() || !CurrentKey.IsSet())
		{
			++OutStats.Conflicted;
			LogRow(Log, AssetPath, FString::Printf(TEXT("Conflicted source-fix row %d, key '%s': no longer localizable."), RowIdx + 1, *Key));
			continue;
		}

		Asset->Modify();
		*TargetText = FText::ChangeKey(CurrentNamespace.GetValue(), CurrentKey.GetValue(), FText::FromString(CsvSource));
		Asset->PostEditChange();
		Asset->MarkPackageDirty();
		++OutStats.Fixed;
	}

	Log.Info(FText::Format(LOCTEXT("SourceFixLogSummary", "{0} fixed, {1} conflicted, {2} unchanged, {3} unresolved. Save the dirty assets and re-run Gather."), OutStats.Fixed, OutStats.Conflicted, OutStats.Unchanged, OutStats.Unresolved));
	return true;
}

void KzDialogueL10N::ImportSourceFixesInteractive()
{
	TArray<FString> OutFiles;
	const void* ParentWindow = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);
	if (!FDesktopPlatformModule::Get()->OpenFileDialog(ParentWindow, LOCTEXT("SourceFixDialogTitle", "Import source fixes from CSV").ToString(),
		LastDirectory(ELastDirectory::GENERIC_IMPORT), FString(), TEXT("CSV files (*.csv)|*.csv"), EFileDialogFlags::None, OutFiles))
	{
		return;
	}
	FEditorDirectories::Get().SetLastDirectory(ELastDirectory::GENERIC_IMPORT, FPaths::GetPath(OutFiles[0]));

	FKzSourceFixStats Stats;
	FText Error;
	if (!ImportSourceFixes(OutFiles[0], Stats, Error))
	{
		ShowNotification(Error, false);
		return;
	}

	const FText Summary = FText::Format(
		LOCTEXT("SourceFixSummary", "Source fixes: {0} fixed, {1} conflicted (skipped), {2} unchanged, {3} unresolved. Save the dirty assets and run Gather; translations against the old wording turn stale."),
		Stats.Fixed, Stats.Conflicted, Stats.Unchanged, Stats.Unresolved);
	ShowNotification(Summary, Stats.Conflicted == 0 && Stats.Unresolved == 0, /*bWithLogLink=*/true);
}

#undef LOCTEXT_NAMESPACE