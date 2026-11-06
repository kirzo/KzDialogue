// Copyright 2026 kirzo

#include "Script/KzScriptImport.h"
#include "Script/KzFountainParser.h"
#include "Factories/KzDialogueAssetFactory.h"
#include "Localization/KzL10N.h"
#include "KzDialogueAsset.h"
#include "KzSpeakerAsset.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "ContentBrowserMenuContexts.h"
#include "DesktopPlatformModule.h"
#include "Editor.h"
#include "EditorDirectories.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Notifications/NotificationManager.h"
#include "IDetailsView.h"
#include "Misc/FileHelper.h"
#include "Misc/MessageDialog.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "PropertyCustomizationHelpers.h"
#include "PropertyEditorModule.h"
#include "ScopedTransaction.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "ToolMenus.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "KzScriptImport"

namespace
{
	enum class ELineChange : uint8 { Keep, Change, Add, Remove };

	/** A script block bound to its speaker. Null speaker = narration. */
	struct FScriptLine
	{
		const FKzFountainBlock* Block = nullptr;
		UKzSpeakerAsset* Speaker = nullptr;
	};

	struct FPlannedLine
	{
		ELineChange Change = ELineChange::Add;
		int32 OldIndex = INDEX_NONE;
		int32 NewIndex = INDEX_NONE;
	};

	struct FPlannedAsset
	{
		const FKzFountainSection* Section = nullptr;
		UKzDialogueAsset* Existing = nullptr;
		FString AssetName;
		TArray<FScriptLine> NewLines;
		TArray<FPlannedLine> Lines;

		int32 Count(ELineChange Change) const
		{
			return Lines.FilterByPredicate([Change](const FPlannedLine& Line) { return Line.Change == Change; }).Num();
		}
	};

	struct FCueResolution
	{
		bool bSkip = false;
		UKzSpeakerAsset* Speaker = nullptr;
	};

	/** A speaker made in the import dialog, created as an asset only when the import is confirmed. */
	struct FNewSpeaker
	{
		TStrongObjectPtr<UKzSpeakerAsset> Speaker;
		FString PackageName;
	};

	struct FImportPlan
	{
		FString ScriptFile;
		FString Folder;
		TArray<FKzFountainSection> Sections;
		TMap<FString, FAssetData> SpeakersByToken;
		FString SpeakerFolder = TEXT("/Game/Dialogue/Speakers");
		TMap<FString, FCueResolution> Cues;
		TArray<FNewSpeaker> NewSpeakers;
		TArray<FPlannedAsset> Assets;
		TArray<UKzDialogueAsset*> Orphans;
		TArray<FText> Warnings;
	};

	FString SourceOf(const FText& Text)
	{
		const FString* Source = FTextInspector::GetSourceString(Text);
		return Source ? *Source : FString();
	}

	/** Same script by file name: the path differs between machines. */
	bool IsSameScript(const FString& A, const FString& B)
	{
		return !A.IsEmpty() && FPaths::GetCleanFilename(A).Equals(FPaths::GetCleanFilename(B), ESearchCase::IgnoreCase);
	}

	FString Describe(const UKzSpeakerAsset* Speaker, const FString& Text, const FString& Notes)
	{
		FString Out = (Speaker ? Speaker->Token.ToString() : TEXT("Narration")) + TEXT(": ") + Text;
		if (!Notes.IsEmpty())
		{
			Out += TEXT("  [") + Notes + TEXT("]");
		}
		return Out;
	}

	void Notify(const FText& Message, bool bSuccess)
	{
		FNotificationInfo Info(Message);
		Info.ExpireDuration = 6.0f;
		if (TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
		{
			Item->SetCompletionState(bSuccess ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
		}
	}

	TSharedRef<SWindow> MakeDialog(const FText& Title, const FVector2D& Size)
	{
		return SNew(SWindow).Title(Title).ClientSize(Size).SupportsMinimize(false).SupportsMaximize(false);
	}

	void ShowModal(const TSharedRef<SWindow>& Window)
	{
		FSlateApplication::Get().AddModalWindow(Window, FSlateApplication::Get().FindBestParentWindowForDialogs(nullptr));
	}

	bool PickScript(const FText& Title, FString& OutFile)
	{
		TArray<FString> Files;
		const void* ParentWindow = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);
		if (!FDesktopPlatformModule::Get()->OpenFileDialog(ParentWindow, Title.ToString(), FEditorDirectories::Get().GetLastDirectory(ELastDirectory::GENERIC_IMPORT),
			FString(), TEXT("Fountain scripts (*.fountain)|*.fountain"), EFileDialogFlags::None, Files) || Files.IsEmpty())
		{
			return false;
		}
		FEditorDirectories::Get().SetLastDirectory(ELastDirectory::GENERIC_IMPORT, FPaths::GetPath(Files[0]));
		OutFile = Files[0];
		return true;
	}

	void IndexSpeakers(FImportPlan& Plan)
	{
		TArray<FAssetData> Speakers;
		IAssetRegistry::GetChecked().GetAssetsByClass(UKzSpeakerAsset::StaticClass()->GetClassPathName(), Speakers, /*bSearchSubClasses=*/true);
		for (const FAssetData& Data : Speakers)
		{
			FName Token;
			if (Data.GetTagValue(GET_MEMBER_NAME_CHECKED(UKzNamedAsset, Token), Token) && !Token.IsNone())
			{
				Plan.SpeakersByToken.Add(KzFountain::NormalizeName(Token.ToString()), Data);
			}
		}

		// New speakers go where the project keeps the others.
		if (Speakers.Num() > 0)
		{
			Plan.SpeakerFolder = Speakers[0].PackagePath.ToString();
		}
	}

	/** Asks what a character no speaker's token matches is. False when the user cancels the import. */
	bool AskForSpeaker(const FString& Cue, FImportPlan& Plan, FCueResolution& Out)
	{
		enum class EAnswer : uint8 { Cancel, Created, Existing, Narration, Skip };
		struct FState
		{
			EAnswer Answer = EAnswer::Cancel;
			FSoftObjectPath Picked;
		};
		const TSharedRef<FState> State = MakeShared<FState>();

		const TStrongObjectPtr<UKzSpeakerAsset> Draft(NewObject<UKzSpeakerAsset>(GetTransientPackage(), NAME_None, RF_Transactional));
		Draft->Token = FName(*KzFountain::ToPascalCase(Cue));
		Draft->GivenName.Text = FText::FromString(FName::NameToDisplayString(Draft->Token.ToString(), false));

		FDetailsViewArgs DetailsArgs;
		DetailsArgs.bAllowSearch = false;
		DetailsArgs.bHideSelectionTip = true;
		DetailsArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
		const TSharedRef<IDetailsView> Details = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor").CreateDetailView(DetailsArgs);
		Details->SetObject(Draft.Get());

		const TSharedRef<SEditableTextBox> FolderBox = SNew(SEditableTextBox).Text(FText::FromString(Plan.SpeakerFolder));
		const TSharedRef<SWindow> Window = MakeDialog(FText::Format(LOCTEXT("SpeakerTitle", "Unknown Character: {0}"), FText::FromString(Cue)), FVector2D(560.0f, 680.0f));

		auto Close = [State, WeakWindow = TWeakPtr<SWindow>(Window)](EAnswer Answer)
		{
			State->Answer = Answer;
			if (const TSharedPtr<SWindow> Pinned = WeakWindow.Pin())
			{
				Pinned->RequestDestroyWindow();
			}
			return FReply::Handled();
		};

		auto OnCreate = [&Plan, DraftSpeaker = Draft.Get(), FolderBox, Close]()
		{
			const FString Token = DraftSpeaker->Token.ToString();
			const FString Normalized = KzFountain::NormalizeName(Token);
			const FString PackageName = FolderBox->GetText().ToString().TrimStartAndEnd() / (TEXT("SPK_") + Token);

			FText Problem;
			if (DraftSpeaker->Token.IsNone())
			{
				Problem = LOCTEXT("NoToken", "Give the speaker a token: the script's character name is that token in uppercase.");
			}
			else if (Plan.SpeakersByToken.Contains(Normalized) || Plan.NewSpeakers.ContainsByPredicate([&Normalized](const FNewSpeaker& New) { return KzFountain::NormalizeName(New.Speaker->Token.ToString()) == Normalized; }))
			{
				Problem = FText::Format(LOCTEXT("TokenTaken", "Another speaker already uses the token {0}."), FText::FromString(Token));
			}
			else if (FPackageName::IsValidLongPackageName(PackageName, false, &Problem))
			{
				if (FPackageName::DoesPackageExist(PackageName) || FindPackage(nullptr, *PackageName))
				{
					Problem = FText::Format(LOCTEXT("SpeakerExists", "{0} already exists."), FText::FromString(PackageName));
				}
			}

			if (!Problem.IsEmpty())
			{
				FMessageDialog::Open(EAppMsgType::Ok, Problem);
				return FReply::Handled();
			}

			Plan.NewSpeakers.Add({ TStrongObjectPtr<UKzSpeakerAsset>(DraftSpeaker), PackageName });
			return Close(EAnswer::Created);
		};

		Window->SetContent(
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(8.0f)
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.Text(FText::Format(LOCTEXT("SpeakerPrompt", "No speaker's token matches the character {0}. Create its speaker, use an existing one, or choose how to import its lines."), FText::FromString(Cue)))
			]
			+ SVerticalBox::Slot().FillHeight(1.0f).Padding(8.0f, 0.0f)
			[
				Details
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(8.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 8.0f, 0.0f)
				[
					SNew(STextBlock).Text(LOCTEXT("Folder", "Folder"))
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f)
				[
					FolderBox
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(8.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("CreateSpeaker", "Create Speaker"))
					.ToolTipText(LOCTEXT("CreateSpeakerTip", "Create SPK_<Token> in this folder when the import is confirmed."))
					.OnClicked_Lambda(OnCreate)
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(8.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 8.0f, 0.0f)
				[
					SNew(STextBlock).Text(LOCTEXT("Existing", "Existing"))
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f)
				[
					SNew(SObjectPropertyEntryBox)
					.AllowedClass(UKzSpeakerAsset::StaticClass())
					.ObjectPath_Lambda([State] { return State->Picked.ToString(); })
					.OnObjectChanged_Lambda([State](const FAssetData& Data) { State->Picked = Data.GetSoftObjectPath(); })
					.DisplayThumbnail(false)
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(8.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("UseSpeaker", "Use Speaker"))
					.IsEnabled_Lambda([State] { return State->Picked.IsValid(); })
					.OnClicked_Lambda([Close] { return Close(EAnswer::Existing); })
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(8.0f).HAlign(HAlign_Right)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SButton)
					.Text(LOCTEXT("Narration", "Import as Narration"))
					.ToolTipText(LOCTEXT("NarrationTip", "Import its lines without a speaker."))
					.OnClicked_Lambda([Close] { return Close(EAnswer::Narration); })
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(4.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("Skip", "Skip Its Lines"))
					.ToolTipText(LOCTEXT("SkipTip", "Leave its lines out of this import."))
					.OnClicked_Lambda([Close] { return Close(EAnswer::Skip); })
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(4.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("CancelImport", "Cancel Import"))
					.OnClicked_Lambda([Close] { return Close(EAnswer::Cancel); })
				]
			]);
		ShowModal(Window);

		switch (State->Answer)
		{
		case EAnswer::Cancel: return false;
		case EAnswer::Created: Out.Speaker = Draft.Get(); break;
		case EAnswer::Existing: Out.Speaker = Cast<UKzSpeakerAsset>(State->Picked.TryLoad()); break;
		case EAnswer::Narration: break;
		case EAnswer::Skip: Out.bSkip = true; break;
		}

		if (Out.Speaker && KzFountain::NormalizeName(Out.Speaker->Token.ToString()) != KzFountain::NormalizeName(Cue))
		{
			Plan.Warnings.Add(FText::Format(LOCTEXT("CueMismatch", "{0} is imported as the speaker {1}: write its character name as {2} so the next import finds it."),
				FText::FromString(Cue), FText::FromString(Out.Speaker->Token.ToString()), FText::FromString(Out.Speaker->Token.ToString().ToUpper())));
		}
		return true;
	}

	bool ResolveCues(FImportPlan& Plan)
	{
		for (const FKzFountainSection& Section : Plan.Sections)
		{
			for (const FKzFountainBlock& Block : Section.Blocks)
			{
				const FString Key = KzFountain::NormalizeName(Block.Cue);
				if (Plan.Cues.Contains(Key)) { continue; }

				FCueResolution Resolution;
				if (const FAssetData* Found = Plan.SpeakersByToken.Find(Key))
				{
					Resolution.Speaker = Cast<UKzSpeakerAsset>(Found->GetAsset());
				}
				else if (!AskForSpeaker(Block.Cue, Plan, Resolution))
				{
					return false;
				}
				Plan.Cues.Add(Key, Resolution);
			}
		}
		return true;
	}

	/** Aligns an asset's lines with the script's: equal lines keep, the unmatched pair up by speaker as changes, the rest are added or removed. */
	TArray<FPlannedLine> DiffLines(const TArray<FKzDialogueLine>& Old, const TArray<FScriptLine>& New)
	{
		auto Same = [&](int32 O, int32 N) { return Old[O].Speaker.Asset == New[N].Speaker && SourceOf(Old[O].Text) == New[N].Block->Text; };

		// Longest common subsequence, filled from the end.
		const int32 Width = New.Num() + 1;
		TArray<int32> Common;
		Common.SetNumZeroed((Old.Num() + 1) * Width);
		for (int32 O = Old.Num() - 1; O >= 0; --O)
		{
			for (int32 N = New.Num() - 1; N >= 0; --N)
			{
				Common[O * Width + N] = Same(O, N) ? Common[(O + 1) * Width + N + 1] + 1 : FMath::Max(Common[(O + 1) * Width + N], Common[O * Width + N + 1]);
			}
		}

		TArray<FPlannedLine> Out;
		TArray<int32> GapOld;
		TArray<int32> GapNew;
		auto FlushGap = [&]()
		{
			int32 NextOld = 0;
			for (const int32 N : GapNew)
			{
				int32 Pair = NextOld;
				while (Pair < GapOld.Num() && Old[GapOld[Pair]].Speaker.Asset != New[N].Speaker) { ++Pair; }
				if (Pair == GapOld.Num())
				{
					Out.Add({ ELineChange::Add, INDEX_NONE, N });
					continue;
				}
				for (; NextOld < Pair; ++NextOld) { Out.Add({ ELineChange::Remove, GapOld[NextOld], INDEX_NONE }); }
				Out.Add({ ELineChange::Change, GapOld[Pair], N });
				NextOld = Pair + 1;
			}
			for (; NextOld < GapOld.Num(); ++NextOld) { Out.Add({ ELineChange::Remove, GapOld[NextOld], INDEX_NONE }); }
			GapOld.Reset();
			GapNew.Reset();
		};

		int32 O = 0;
		int32 N = 0;
		while (O < Old.Num() || N < New.Num())
		{
			if (O < Old.Num() && N < New.Num() && Same(O, N))
			{
				FlushGap();
				const bool bNotesChanged = Old[O].TranslatorNotes != New[N].Block->Notes;
				Out.Add({ bNotesChanged ? ELineChange::Change : ELineChange::Keep, O, N });
				++O;
				++N;
			}
			else if (N < New.Num() && (O == Old.Num() || Common[O * Width + N + 1] >= Common[(O + 1) * Width + N]))
			{
				GapNew.Add(N++);
			}
			else
			{
				GapOld.Add(O++);
			}
		}
		FlushGap();
		return Out;
	}

	/** Matches each section to a dialogue in the folder (by script section, then by name) or a new name, and diffs its lines. */
	void PlanAssets(FImportPlan& Plan)
	{
		TArray<FAssetData> FolderAssets;
		IAssetRegistry::GetChecked().GetAssetsByPath(FName(*Plan.Folder), FolderAssets);

		TArray<UKzDialogueAsset*> Unclaimed;
		TSet<FString> TakenNames;
		for (const FAssetData& Data : FolderAssets)
		{
			TakenNames.Add(Data.AssetName.ToString());
			if (Data.IsInstanceOf(UKzDialogueAsset::StaticClass()))
			{
				if (UKzDialogueAsset* Dialogue = Cast<UKzDialogueAsset>(Data.GetAsset())) { Unclaimed.Add(Dialogue); }
			}
		}

		for (const FKzFountainSection& Section : Plan.Sections)
		{
			FPlannedAsset& Planned = Plan.Assets.AddDefaulted_GetRef();
			Planned.Section = &Section;
			const FString Wanted = Section.AssetName.IsEmpty() ? TEXT("DLG_") + KzFountain::ToPascalCase(Section.Title) : Section.AssetName;

			int32 Index = Unclaimed.IndexOfByPredicate([&](const UKzDialogueAsset* Dialogue)
			{
				return IsSameScript(Dialogue->ScriptSource, Plan.ScriptFile) && Dialogue->ScriptSection == Section.Title;
			});
			if (Index == INDEX_NONE)
			{
				Index = Unclaimed.IndexOfByPredicate([&](const UKzDialogueAsset* Dialogue)
				{
					return Dialogue->GetName() == Wanted && (Dialogue->ScriptSource.IsEmpty() || IsSameScript(Dialogue->ScriptSource, Plan.ScriptFile));
				});
			}

			if (Index != INDEX_NONE)
			{
				Planned.Existing = Unclaimed[Index];
				Planned.AssetName = Planned.Existing->GetName();
				Unclaimed.RemoveAt(Index);
			}
			else
			{
				Planned.AssetName = Wanted;
				for (int32 Suffix = 2; TakenNames.Contains(Planned.AssetName); ++Suffix)
				{
					Planned.AssetName = FString::Printf(TEXT("%s_%d"), *Wanted, Suffix);
				}
				TakenNames.Add(Planned.AssetName);
			}

			for (const FKzFountainBlock& Block : Section.Blocks)
			{
				const FCueResolution& Cue = Plan.Cues.FindChecked(KzFountain::NormalizeName(Block.Cue));
				if (!Cue.bSkip) { Planned.NewLines.Add({ &Block, Cue.Speaker }); }
			}
			Planned.Lines = DiffLines(Planned.Existing ? Planned.Existing->Lines : TArray<FKzDialogueLine>(), Planned.NewLines);
		}

		Plan.Orphans = Unclaimed.FilterByPredicate([&Plan](const UKzDialogueAsset* Dialogue) { return IsSameScript(Dialogue->ScriptSource, Plan.ScriptFile); });
	}

	void GatherWarnings(FImportPlan& Plan)
	{
		TSet<FName> KnownTokens = KzDialogueL10N::CollectNamedTokens();
		for (const FNewSpeaker& New : Plan.NewSpeakers) { KnownTokens.Add(New.Speaker->Token); }

		for (const FPlannedAsset& Planned : Plan.Assets)
		{
			TSet<FName> Tokens;
			for (const FScriptLine& Line : Planned.NewLines) { UKzNamedAsset::GatherTextTokens(FText::FromString(Line.Block->Text), Tokens); }
			for (const FName Token : Tokens)
			{
				if (!KnownTokens.Contains(Token))
				{
					Plan.Warnings.Add(FText::Format(LOCTEXT("UnknownToken", "{0}: no named asset has the token {1}."),
						FText::FromString(Planned.AssetName), FText::FromString(FString::Printf(TEXT("{%s}"), *Token.ToString()))));
				}
			}

			const int32 Removed = Planned.Count(ELineChange::Remove);
			if (Planned.Existing && Removed > 0)
			{
				const FName PackageName = Planned.Existing->GetPackage()->GetFName();
				TArray<FName> Referencers;
				IAssetRegistry::GetChecked().GetReferencers(PackageName, Referencers);
				Referencers.Remove(PackageName);
				if (Referencers.Num() > 0)
				{
					Plan.Warnings.Add(FText::Format(LOCTEXT("RemovedReferenced", "{0} loses {1} line(s) and is used by {2}: check that none of them points at a removed line."),
						FText::FromString(Planned.AssetName), Removed, FText::FromString(FString::JoinBy(Referencers, TEXT(", "), [](FName Name) { return FPackageName::GetShortName(Name); }))));
				}
			}
		}
	}

	bool IsUpToDate(const FPlannedAsset& Planned, const FImportPlan& Plan)
	{
		return Planned.Existing && Planned.Existing->ScriptSource == Plan.ScriptFile && Planned.Existing->ScriptSection == Planned.Section->Title
			&& !Planned.Lines.ContainsByPredicate([](const FPlannedLine& Line) { return Line.Change != ELineChange::Keep; });
	}

	bool ConfirmPreview(const FImportPlan& Plan)
	{
		const FLinearColor Added(0.35f, 0.8f, 0.35f);
		const FLinearColor Changed(0.95f, 0.75f, 0.25f);
		const FLinearColor Removed(0.9f, 0.35f, 0.35f);
		const FLinearColor Warning(0.95f, 0.55f, 0.2f);
		const FLinearColor Muted(0.55f, 0.55f, 0.55f);

		const TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);
		auto AddRow = [&Rows](const FString& Text, const FLinearColor& Color, float Indent = 0.0f, bool bBold = false)
		{
			Rows->AddSlot().AutoHeight().Padding(Indent, 2.0f, 0.0f, 2.0f)
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.ColorAndOpacity(Color)
				.Font(FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", 9))
				.Text(FText::FromString(Text))
			];
		};

		for (const FText& Message : Plan.Warnings) { AddRow(TEXT("! ") + Message.ToString(), Warning); }
		for (const FNewSpeaker& New : Plan.NewSpeakers)
		{
			AddRow(FString::Printf(TEXT("+ %s (token %s)"), *New.PackageName, *New.Speaker->Token.ToString()), Added, 0.0f, true);
		}

		for (const FPlannedAsset& Planned : Plan.Assets)
		{
			if (IsUpToDate(Planned, Plan))
			{
				AddRow(Planned.AssetName + TEXT(": up to date"), Muted, 0.0f, true);
				continue;
			}

			AddRow(Planned.Existing
				? FString::Printf(TEXT("%s: %d changed, %d added, %d removed"), *Planned.AssetName, Planned.Count(ELineChange::Change), Planned.Count(ELineChange::Add), Planned.Count(ELineChange::Remove))
				: FString::Printf(TEXT("%s: new, %d line(s)"), *Planned.AssetName, Planned.NewLines.Num()),
				FLinearColor::White, 0.0f, true);

			for (const FPlannedLine& Line : Planned.Lines)
			{
				const FKzDialogueLine* Old = Line.OldIndex != INDEX_NONE ? &Planned.Existing->Lines[Line.OldIndex] : nullptr;
				const FScriptLine* New = Line.NewIndex != INDEX_NONE ? &Planned.NewLines[Line.NewIndex] : nullptr;
				const FString OldText = Old ? Describe(Old->Speaker.Asset, SourceOf(Old->Text), Old->TranslatorNotes) : FString();
				const FString NewText = New ? Describe(New->Speaker, New->Block->Text, New->Block->Notes) : FString();
				switch (Line.Change)
				{
				case ELineChange::Keep: break;
				case ELineChange::Add: AddRow(TEXT("+ ") + NewText, Added, 16.0f); break;
				case ELineChange::Remove: AddRow(TEXT("- ") + OldText, Removed, 16.0f); break;
				case ELineChange::Change:
					AddRow(TEXT("~ ") + NewText, Changed, 16.0f);
					AddRow(TEXT("was ") + OldText, Muted, 32.0f);
					break;
				}
			}
		}

		for (const UKzDialogueAsset* Orphan : Plan.Orphans)
		{
			AddRow(Orphan->GetName() + TEXT(": its section is no longer in the script. Kept; delete it by hand if nothing uses it."), Warning, 0.0f, true);
		}

		bool bConfirmed = false;
		const TSharedRef<SWindow> Window = MakeDialog(LOCTEXT("PreviewTitle", "Import Fountain Script"), FVector2D(900.0f, 680.0f));
		const TWeakPtr<SWindow> WeakWindow = Window;
		Window->SetContent(
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(8.0f)
			[
				SNew(STextBlock).Text(FText::Format(LOCTEXT("PreviewHeader", "{0} into {1}"), FText::FromString(FPaths::GetCleanFilename(Plan.ScriptFile)), FText::FromString(Plan.Folder)))
			]
			+ SVerticalBox::Slot().FillHeight(1.0f).Padding(8.0f, 0.0f)
			[
				SNew(SScrollBox) + SScrollBox::Slot()[Rows]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(8.0f).HAlign(HAlign_Right)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SButton)
					.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("PrimaryButton"))
					.Text(LOCTEXT("Import", "Import"))
					.OnClicked_Lambda([&bConfirmed, WeakWindow]
					{
						bConfirmed = true;
						if (const TSharedPtr<SWindow> Pinned = WeakWindow.Pin()) { Pinned->RequestDestroyWindow(); }
						return FReply::Handled();
					})
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(4.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("Cancel", "Cancel"))
					.OnClicked_Lambda([WeakWindow]
					{
						if (const TSharedPtr<SWindow> Pinned = WeakWindow.Pin()) { Pinned->RequestDestroyWindow(); }
						return FReply::Handled();
					})
				]
			]);
		ShowModal(Window);
		return bConfirmed;
	}

	void Apply(const FImportPlan& Plan)
	{
		const FScopedTransaction Transaction(LOCTEXT("ImportTransaction", "Import Fountain Script"));

		for (const FNewSpeaker& New : Plan.NewSpeakers)
		{
			UKzSpeakerAsset* Speaker = New.Speaker.Get();
			Speaker->Rename(*FPackageName::GetShortName(New.PackageName), CreatePackage(*New.PackageName), REN_DontCreateRedirectors | REN_NonTransactional);
			Speaker->SetFlags(RF_Public | RF_Standalone);
			Speaker->PostEditChange();
			FAssetRegistryModule::AssetCreated(Speaker);
			Speaker->MarkPackageDirty();
		}

		IAssetTools& AssetTools = FAssetToolsModule::GetModule().Get();
		TArray<UObject*> Imported;
		int32 Created = 0;
		int32 Updated = 0;
		for (const FPlannedAsset& Planned : Plan.Assets)
		{
			if (IsUpToDate(Planned, Plan)) { continue; }

			UKzDialogueAsset* Asset = Planned.Existing;
			if (Asset)
			{
				Asset->Modify();
				++Updated;
			}
			else
			{
				Asset = Cast<UKzDialogueAsset>(AssetTools.CreateAsset(Planned.AssetName, Plan.Folder, UKzDialogueAsset::StaticClass(), NewObject<UKzDialogueAssetFactory>()));
				if (!Asset) { continue; }
				Asset->DisplayName = Planned.Section->Title;
				++Created;
			}

			// Kept and changed lines carry their id, audio and timing over; only speaker, text and notes come from the script.
			TArray<FKzDialogueLine> Lines;
			for (const FPlannedLine& PlannedLine : Planned.Lines)
			{
				if (PlannedLine.Change == ELineChange::Remove) { continue; }

				const FScriptLine& New = Planned.NewLines[PlannedLine.NewIndex];
				FKzDialogueLine& Line = PlannedLine.OldIndex != INDEX_NONE ? Lines.Add_GetRef(Asset->Lines[PlannedLine.OldIndex]) : Lines.AddDefaulted_GetRef();
				Line.Speaker.Asset = New.Speaker;
				if (SourceOf(Line.Text) != New.Block->Text)
				{
					Line.Text = FText::FromString(New.Block->Text);
				}
				Line.TranslatorNotes = New.Block->Notes;
			}

			Asset->Lines = MoveTemp(Lines);
			Asset->ScriptSource = Plan.ScriptFile;
			Asset->ScriptSection = Planned.Section->Title;
			Asset->PostEditChange();
			Asset->MarkPackageDirty();
			Imported.Add(Asset);
		}

		if (Imported.Num() > 0)
		{
			GEditor->SyncBrowserToObjects(Imported);
		}
		Notify(FText::Format(LOCTEXT("Imported", "{0}: {1} dialogue(s) created, {2} updated, {3} speaker(s) created. Save them to keep the changes."),
			FText::FromString(FPaths::GetCleanFilename(Plan.ScriptFile)), Created, Updated, Plan.NewSpeakers.Num()), true);
	}

	void Reimport(const TArray<FAssetData>& Selected)
	{
		TArray<TPair<FString, FString>> Jobs;
		for (const FAssetData& Data : Selected)
		{
			const UKzDialogueAsset* Dialogue = Cast<UKzDialogueAsset>(Data.GetAsset());
			if (Dialogue && !Dialogue->ScriptSource.IsEmpty())
			{
				Jobs.AddUnique({ Dialogue->ScriptSource, Data.PackagePath.ToString() });
			}
		}

		if (Jobs.IsEmpty())
		{
			Notify(LOCTEXT("NotImported", "The selected dialogues were not imported from a script."), false);
			return;
		}

		for (TPair<FString, FString>& Job : Jobs)
		{
			if (FPaths::FileExists(Job.Key) || PickScript(FText::Format(LOCTEXT("LocateScript", "Locate {0}"), FText::FromString(FPaths::GetCleanFilename(Job.Key))), Job.Key))
			{
				KzScriptImport::Import(Job.Key, Job.Value);
			}
		}
	}
}

void KzScriptImport::Import(const FString& ScriptFile, const FString& Folder)
{
	FString Script;
	if (!FFileHelper::LoadFileToString(Script, *ScriptFile))
	{
		Notify(FText::Format(LOCTEXT("ReadFailed", "Could not read {0}."), FText::FromString(ScriptFile)), false);
		return;
	}

	FImportPlan Plan;
	Plan.ScriptFile = FPaths::ConvertRelativePathToFull(ScriptFile);
	Plan.Folder = Folder;
	Plan.Sections = KzFountain::Parse(Script, FPaths::GetBaseFilename(ScriptFile));
	if (Plan.Sections.IsEmpty())
	{
		Notify(FText::Format(LOCTEXT("NoDialogue", "{0} has no dialogue."), FText::FromString(FPaths::GetCleanFilename(ScriptFile))), false);
		return;
	}

	IndexSpeakers(Plan);
	if (!ResolveCues(Plan)) { return; }
	PlanAssets(Plan);
	GatherWarnings(Plan);

	const bool bNothingToShow = Plan.NewSpeakers.IsEmpty() && Plan.Orphans.IsEmpty() && Plan.Warnings.IsEmpty()
		&& !Plan.Assets.ContainsByPredicate([&Plan](const FPlannedAsset& Planned) { return !IsUpToDate(Planned, Plan); });
	if (bNothingToShow)
	{
		Notify(FText::Format(LOCTEXT("UpToDate", "{0}: the dialogues are up to date."), FText::FromString(FPaths::GetCleanFilename(ScriptFile))), true);
		return;
	}

	if (ConfirmPreview(Plan))
	{
		Apply(Plan);
	}
}

void KzScriptImport::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(TEXT("KzDialogueEditor"));

	FToolMenuSection& FolderSection = UToolMenus::Get()->ExtendMenu(TEXT("ContentBrowser.FolderContextMenu"))->AddSection(TEXT("KzDialogueScript"), LOCTEXT("DialogueSection", "Dialogue"));
	FolderSection.AddDynamicEntry(TEXT("ImportFountainScript"), FNewToolMenuSectionDelegate::CreateLambda([](FToolMenuSection& InSection)
	{
		const UContentBrowserFolderContext* Context = InSection.FindContext<UContentBrowserFolderContext>();
		if (!Context || Context->GetSelectedPackagePaths().Num() != 1) { return; }

		InSection.AddMenuEntry(
			TEXT("ImportFountainScript"),
			LOCTEXT("ImportScript", "Import Fountain Script..."),
			LOCTEXT("ImportScriptTip", "Create or update one dialogue per section of a Fountain script in this folder. Unknown characters ask for their speaker; a preview lists every change before applying it."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([Folder = Context->GetSelectedPackagePaths()[0]]
			{
				FString File;
				if (PickScript(LOCTEXT("PickScript", "Import Fountain Script"), File))
				{
					KzScriptImport::Import(File, Folder);
				}
			})));
	}));

	FToolMenuSection& AssetSection = UToolMenus::Get()->ExtendMenu(TEXT("ContentBrowser.AssetContextMenu.KzDialogueAsset"))->AddSection(TEXT("KzDialogueScript"), LOCTEXT("ScriptSection", "Script"));
	AssetSection.AddDynamicEntry(TEXT("ReimportFountainScript"), FNewToolMenuSectionDelegate::CreateLambda([](FToolMenuSection& InSection)
	{
		const UContentBrowserAssetContextMenuContext* Context = InSection.FindContext<UContentBrowserAssetContextMenuContext>();
		if (!Context) { return; }

		InSection.AddMenuEntry(
			TEXT("ReimportFountainScript"),
			LOCTEXT("ReimportScript", "Reimport Script"),
			LOCTEXT("ReimportScriptTip", "Update the selected dialogues, and the rest of their script's sections in the same folder, from the Fountain script they were imported from."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([SelectedAssets = Context->SelectedAssets] { Reimport(SelectedAssets); })));
	}));
}

#undef LOCTEXT_NAMESPACE
