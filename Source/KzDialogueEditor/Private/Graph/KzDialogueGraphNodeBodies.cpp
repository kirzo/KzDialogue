// Copyright 2026 kirzo

#include "Graph/KzDialogueGraphNodeBodies.h"
#include "Graph/KzDialogueGraphNodes.h"
#include "KzDialogueAsset.h"
#include "KzDialogueEditor.h"
#include "KzEdGraphNode.h"
#include "KzGraphEditorUtils.h"
#include "KzSpeakerAsset.h"
#include "Sound/SoundWave.h"
#include "Styling/SlateIconFinder.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "KzDialogueGraphNodeBodies"

namespace
{
	constexpr float BodyWidth = 260.0f;
	const FLinearColor SpeakerColor(0.75f, 0.75f, 0.75f);
	const FLinearColor MissingColor(0.95f, 0.45f, 0.2f);
	const FLinearColor ConditionColor(FColor(230, 120, 120));

	/** What a line reference points to: a line, an alias, or nothing. */
	struct FRefView
	{
		const FKzDialogueLine* Line = nullptr;
		const FKzDialogueAlias* Alias = nullptr;

		const FKzDialogueSpeaker* GetSpeaker() const { return Line ? &Line->Speaker : (Alias ? &Alias->Speaker : nullptr); }
	};

	/** Resolves a reference on every paint, remembering where it was found so that costs nothing once found. */
	struct FRefCache
	{
		int32 LineIndex = INDEX_NONE;
		int32 AliasIndex = INDEX_NONE;
		bool bTriedLoad = false;

		FRefView Resolve(const FKzDialogueLineRef& Ref)
		{
			if (Ref.Asset.IsNull())
			{
				return FRefView();
			}

			const UKzDialogueAsset* Asset = Ref.Asset.Get();
			if (!Asset && !bTriedLoad)
			{
				bTriedLoad = true;
				Asset = Ref.Asset.LoadSynchronous();
			}
			if (!Asset)
			{
				return FRefView();
			}

			if (!Asset->Lines.IsValidIndex(LineIndex) || Asset->Lines[LineIndex].LineId != Ref.LineId)
			{
				LineIndex = Asset->IndexOfLine(Ref.LineId);
			}
			if (LineIndex != INDEX_NONE)
			{
				return { &Asset->Lines[LineIndex], nullptr };
			}

			if (!Asset->Aliases.IsValidIndex(AliasIndex) || Asset->Aliases[AliasIndex].AliasId != Ref.LineId)
			{
				AliasIndex = Asset->Aliases.IndexOfByPredicate([&Ref](const FKzDialogueAlias& Alias) { return Alias.AliasId == Ref.LineId; });
			}
			return AliasIndex != INDEX_NONE ? FRefView{ nullptr, &Asset->Aliases[AliasIndex] } : FRefView();
		}
	};

	FText GetSpeakerText(const FRefView& View)
	{
		const FKzDialogueSpeaker* Speaker = View.GetSpeaker();
		return Speaker ? Speaker->GetEditorLabel() : LOCTEXT("NoLine", "No line");
	}

	FSlateColor GetSpeakerColor(const FRefView& View)
	{
		const FKzDialogueSpeaker* Speaker = View.GetSpeaker();
		if (!Speaker)
		{
			return MissingColor;
		}
		return Speaker->Asset && Speaker->Asset->bUseSubtitleColor ? Speaker->Asset->SubtitleColor : SpeakerColor;
	}

	FText GetBodyText(const FRefView& View)
	{
		if (View.Line)
		{
			return View.Line->Text;
		}
		return View.Alias ? View.Alias->GetDisplayLabel() : FText::GetEmpty();
	}

	TSharedRef<SWidget> MakeConditionText(const FText& Text)
	{
		return SNew(STextBlock)
			.Text(Text)
			.ColorAndOpacity(ConditionColor)
			.WrapTextAt(BodyWidth - 16.0f);
	}

	/** The node's own body under its Only If; texts are built once, as the node's widget is rebuilt on every edit. */
	TSharedPtr<SWidget> MakeStepBody(const UKzEdGraphNode& EdNode, TSharedPtr<SWidget> Content)
	{
		const FKzDialogueGraphStep* Step = EdNode.Node.GetPtr<FKzDialogueGraphStep>();
		if (!Step || Step->OnlyIf.IsEmpty())
		{
			return Content;
		}

		const TSharedRef<SVerticalBox> Box = SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(8.0f, 4.0f, 8.0f, 0.0f)
			[
				MakeConditionText(FText::Format(LOCTEXT("OnlyIf", "Only if {0}"), Step->OnlyIf.GetDescription()))
			];
		if (Content.IsValid())
		{
			Box->AddSlot().AutoHeight()[Content.ToSharedRef()];
		}
		return SNew(SBox).MaxDesiredWidth(BodyWidth)[Box];
	}

	TSharedPtr<SWidget> MakeOnlyIfBody(const UKzEdGraphNode& EdNode)
	{
		return MakeStepBody(EdNode, nullptr);
	}

	TSharedPtr<SWidget> MakeBranchBody(const UKzEdGraphNode& EdNode)
	{
		const FKzDialogueGraphNode_Branch* Branch = EdNode.Node.GetPtr<FKzDialogueGraphNode_Branch>();
		if (!Branch || Branch->Branches.IsEmpty())
		{
			return MakeStepBody(EdNode, nullptr);
		}

		const TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);
		for (int32 Index = 0; Index < Branch->Branches.Num(); ++Index)
		{
			const FKzDialogueBranch& Each = Branch->Branches[Index];
			const FText Condition = Each.Condition.IsEmpty() ? LOCTEXT("Always", "always") : Each.Condition.GetDescription();
			const FText Text = Each.Label.IsNone()
				? FText::Format(INVTEXT("{0}. {1}"), Index + 1, Condition)
				: FText::Format(INVTEXT("{0}. {1}: {2}"), Index + 1, FText::FromName(Each.Label), Condition);
			Rows->AddSlot()
			.AutoHeight()
			.Padding(0.0f, 1.0f)
			[
				MakeConditionText(Text)
			];
		}

		return MakeStepBody(EdNode, SNew(SBox)
			.MaxDesiredWidth(BodyWidth)
			.Padding(FMargin(8.0f, 4.0f))
			[
				Rows
			]);
	}

	TSharedPtr<SWidget> MakeLineBody(const UKzEdGraphNode& EdNode)
	{
		const TWeakObjectPtr<const UKzEdGraphNode> WeakNode(&EdNode);
		const TSharedRef<FRefCache> Cache = MakeShared<FRefCache>();
		auto Resolve = [WeakNode, Cache]()
		{
			const UKzEdGraphNode* Node = WeakNode.Get();
			const FKzDialogueGraphNode_Line* Line = Node ? Node->Node.GetPtr<FKzDialogueGraphNode_Line>() : nullptr;
			return Line ? Cache->Resolve(Line->Line) : FRefView();
		};

		return MakeStepBody(EdNode, SNew(SBox)
			.MaxDesiredWidth(BodyWidth)
			.Padding(FMargin(8.0f, 4.0f))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text_Lambda([Resolve]() { return GetSpeakerText(Resolve()); })
						.ColorAndOpacity_Lambda([Resolve]() { return GetSpeakerColor(Resolve()); })
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(4.0f, 0.0f, 0.0f, 0.0f)
					[
						SNew(SImage)
						.Image(FSlateIconFinder::FindIconBrushForClass(USoundWave::StaticClass()))
						.DesiredSizeOverride(FVector2D(12.0f, 12.0f))
						.ToolTipText(LOCTEXT("HasAudio", "The line has audio."))
						.Visibility_Lambda([Resolve]()
						{
							const FRefView View = Resolve();
							return View.Line && !View.Line->Audio.IsNull() ? EVisibility::Visible : EVisibility::Collapsed;
						})
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 2.0f, 0.0f, 0.0f)
				[
					SNew(STextBlock)
					.Text_Lambda([Resolve]() { return GetBodyText(Resolve()); })
					.WrapTextAt(BodyWidth - 16.0f)
				]
			]);
	}

	TSharedPtr<SWidget> MakeChoiceBody(const UKzEdGraphNode& EdNode)
	{
		const FKzDialogueGraphNode_Choice* Choice = EdNode.Node.GetPtr<FKzDialogueGraphNode_Choice>();
		if (!Choice || Choice->Options.IsEmpty())
		{
			return MakeStepBody(EdNode, nullptr);
		}

		// Rebuilt with the node whenever its options change, so one row per option is enough.
		const TWeakObjectPtr<const UKzEdGraphNode> WeakNode(&EdNode);
		const TSharedRef<SVerticalBox> Options = SNew(SVerticalBox);
		for (int32 Index = 0; Index < Choice->Options.Num(); ++Index)
		{
			auto Resolve = [WeakNode, Index, Cache = MakeShared<FRefCache>()]()
			{
				const UKzEdGraphNode* Node = WeakNode.Get();
				const FKzDialogueGraphNode_Choice* Each = Node ? Node->Node.GetPtr<FKzDialogueGraphNode_Choice>() : nullptr;
				return Each && Each->Options.IsValidIndex(Index) ? Cache->Resolve(Each->Options[Index].Line) : FRefView();
			};

			Options->AddSlot()
			.AutoHeight()
			.Padding(0.0f, 1.0f)
			[
				SNew(STextBlock)
				.Text_Lambda([Resolve, Index]()
				{
					const FText Text = GetBodyText(Resolve());
					return FText::Format(INVTEXT("{0}. {1}"), Index + 1, Text.IsEmpty() ? LOCTEXT("NoOptionLine", "(no line)") : Text);
				})
				.WrapTextAt(BodyWidth - 16.0f)
			];
		}

		return MakeStepBody(EdNode, SNew(SBox)
			.MaxDesiredWidth(BodyWidth)
			.Padding(FMargin(8.0f, 4.0f))
			[
				Options
			]);
	}

	void OpenLine(const UKzEdGraphNode& Node)
	{
		const FKzDialogueGraphNode_Line* Line = Node.Node.GetPtr<FKzDialogueGraphNode_Line>();
		if (UKzDialogueAsset* Asset = Line ? Line->Line.Asset.LoadSynchronous() : nullptr)
		{
			FKzDialogueEditorModule::OpenAssetAtEntry(Asset, Line->Line.LineId);
		}
	}
}

void KzDialogueGraphNodeBodies::Register()
{
	KzGraphEditor::RegisterNodeBody(FKzDialogueGraphNode_Line::StaticStruct(), FKzOnMakeGraphNodeBody::CreateStatic(&MakeLineBody));
	KzGraphEditor::RegisterNodeBody(FKzDialogueGraphNode_Choice::StaticStruct(), FKzOnMakeGraphNodeBody::CreateStatic(&MakeChoiceBody));
	KzGraphEditor::RegisterNodeBody(FKzDialogueGraphNode_Branch::StaticStruct(), FKzOnMakeGraphNodeBody::CreateStatic(&MakeBranchBody));
	KzGraphEditor::RegisterNodeBody(FKzDialogueGraphStep::StaticStruct(), FKzOnMakeGraphNodeBody::CreateStatic(&MakeOnlyIfBody));
	KzGraphEditor::RegisterNodeDoubleClick(FKzDialogueGraphNode_Line::StaticStruct(), FKzOnGraphNodeDoubleClicked::CreateStatic(&OpenLine));
}

void KzDialogueGraphNodeBodies::Unregister()
{
	KzGraphEditor::UnregisterNodeBody(FKzDialogueGraphNode_Line::StaticStruct());
	KzGraphEditor::UnregisterNodeBody(FKzDialogueGraphNode_Choice::StaticStruct());
	KzGraphEditor::UnregisterNodeBody(FKzDialogueGraphNode_Branch::StaticStruct());
	KzGraphEditor::UnregisterNodeBody(FKzDialogueGraphStep::StaticStruct());
	KzGraphEditor::UnregisterNodeDoubleClick(FKzDialogueGraphNode_Line::StaticStruct());
}

#undef LOCTEXT_NAMESPACE