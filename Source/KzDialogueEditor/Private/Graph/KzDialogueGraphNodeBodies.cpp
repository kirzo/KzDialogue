// Copyright 2026 kirzo

#include "Graph/KzDialogueGraphNodeBodies.h"
#include "Graph/KzDialogueGraphConditions.h"
#include "Graph/KzDialogueGraphNodes.h"
#include "Framework/Text/RichTextLayoutMarshaller.h"
#include "Framework/Text/SlateTextLayout.h"
#include "Framework/Text/SlateTextRun.h"
#include "Input/DragAndDrop.h"
#include "KzDialogueAsset.h"
#include "KzDialogueEditor.h"
#include "KzDialogueEditorStyle.h"
#include "KzEdGraphNode.h"
#include "KzGraphEditorUtils.h"
#include "KzSpeakerAsset.h"
#include "ScopedTransaction.h"
#include "Sound/SoundWave.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateIconFinder.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/SRichTextBlock.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "KzDialogueGraphNodeBodies"

namespace
{
	constexpr float BodyWidth = 260.0f;
	const FLinearColor SpeakerColor(0.75f, 0.75f, 0.75f);
	const FLinearColor MissingColor(0.95f, 0.45f, 0.2f);
	const FLinearColor HeaderColor(FColor(0x9A, 0xA1, 0xA9));
	const FLinearColor PillTextColor(FColor(0xDD, 0xE1, 0xE6));
	const FLinearColor ChipColor(FColor(0xC9, 0xCE, 0xD4));
	const FLinearColor MutedColor(FColor(0x7D, 0x84, 0x8C));
	const FLinearColor DropColor(FColor(0x34, 0xD1, 0xE0));

	struct FBrushes
	{
		FSlateRoundedBoxBrush Box{ FLinearColor(FColor(0x23, 0x26, 0x29)), 6.0f, FLinearColor(FColor(0x33, 0x38, 0x3E)), 1.0f };
		FSlateRoundedBoxBrush Pill{ FLinearColor(FColor(0x30, 0x35, 0x3C)), FLinearColor::Transparent, 0.0f };
		FSlateRoundedBoxBrush NestedPill{ FLinearColor(FColor(0x3A, 0x40, 0x48)), FLinearColor::Transparent, 0.0f };
		FSlateRoundedBoxBrush Group{ FLinearColor(FColor(0x30, 0x35, 0x3C)), 8.0f, FLinearColor::Transparent, 0.0f };
		FSlateRoundedBoxBrush Chip{ FLinearColor::Transparent, FLinearColor(FColor(0x59, 0x60, 0x6A)), 1.0f };
		FSlateRoundedBoxBrush Badge{ FLinearColor(FColor(0x3A, 0x40, 0x48)), FLinearColor::Transparent, 0.0f };
		FSlateRoundedBoxBrush Subtitle{ FLinearColor(FColor(0x0F, 0x10, 0x11)), 4.0f };
	};

	const FBrushes& Brushes()
	{
		static const FBrushes Instance;
		return Instance;
	}

	FSlateFontInfo SmallFont()
	{
		return FCoreStyle::GetDefaultFontStyle("Regular", 8);
	}

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

	FText GetBodyText(const FRefView& View)
	{
		if (View.Line)
		{
			return View.Line->Text;
		}
		return View.Alias ? View.Alias->GetDisplayLabel() : FText::GetEmpty();
	}

	FLinearColor GetSpeakerColor(const FKzDialogueSpeaker& Speaker)
	{
		return Speaker.Asset && Speaker.Asset->bUseSubtitleColor ? Speaker.Asset->SubtitleColor : SpeakerColor;
	}

	/** "Speaker: text" in one paragraph, the speaker part in its own color, as the game's subtitles read. */
	class FSubtitleMarshaller : public FRichTextLayoutMarshaller
	{
	public:
		FSubtitleMarshaller() : FRichTextLayoutMarshaller(TArray<TSharedRef<ITextDecorator>>(), &FAppStyle::Get()) {}

		int32 SpeakerLength = 0;
		FLinearColor SpeakerTextColor = FLinearColor::White;

		virtual void SetText(const FString& SourceString, FTextLayout& TargetTextLayout) override
		{
			const FTextBlockStyle& Normal = static_cast<FSlateTextLayout&>(TargetTextLayout).GetDefaultTextStyle();
			const FTextBlockStyle Speaker = FTextBlockStyle(Normal).SetColorAndOpacity(SpeakerTextColor);
			const TSharedRef<FString> Line = MakeShared<FString>(SourceString);
			const int32 Split = FMath::Clamp(SpeakerLength, 0, Line->Len());

			TArray<TSharedRef<IRun>> Runs;
			if (Split > 0)
			{
				Runs.Add(FSlateTextRun::Create(FRunInfo(), Line, Speaker, FTextRange(0, Split)));
			}
			if (Split < Line->Len() || Runs.IsEmpty())
			{
				Runs.Add(FSlateTextRun::Create(FRunInfo(), Line, Normal, FTextRange(Split, Line->Len())));
			}
			TargetTextLayout.AddLine(FTextLayout::FNewLineData(Line, MoveTemp(Runs)));
		}
	};

	/** Which conditions of a node: its Only If or one branch's, then down through nested groups. */
	struct FConditionsPath
	{
		int32 Branch = INDEX_NONE;
		TArray<int32> Groups;

		bool operator==(const FConditionsPath& Other) const { return Branch == Other.Branch && Groups == Other.Groups; }

		FConditionsPath Child(int32 Group) const
		{
			FConditionsPath Path = *this;
			Path.Groups.Add(Group);
			return Path;
		}

		FKzDialogueConditions* Resolve(TInstancedStruct<FKzGraphNode>& Node) const
		{
			FKzDialogueConditions* List = nullptr;
			if (Branch == INDEX_NONE)
			{
				FKzDialogueGraphStep* Step = Node.GetMutablePtr<FKzDialogueGraphStep>();
				List = Step ? &Step->OnlyIf : nullptr;
			}
			else if (FKzDialogueGraphNode_Branch* BranchNode = Node.GetMutablePtr<FKzDialogueGraphNode_Branch>(); BranchNode && BranchNode->Branches.IsValidIndex(Branch))
			{
				List = &BranchNode->Branches[Branch].Condition;
			}

			for (const int32 Group : Groups)
			{
				FKzDialogueCondition_Group* Found = List && List->Conditions.IsValidIndex(Group) ? List->Conditions[Group].GetMutablePtr<FKzDialogueCondition_Group>() : nullptr;
				List = Found ? &Found->Conditions : nullptr;
			}
			return List;
		}
	};

	/** Changes a list of conditions as one undoable edit of the node, which rebuilds its widget. */
	void EditConditions(const TWeakObjectPtr<UKzEdGraphNode>& WeakNode, const FConditionsPath& Path, const FText& What, TFunctionRef<void(FKzDialogueConditions&)> Edit)
	{
		UKzEdGraphNode* EdNode = WeakNode.Get();
		if (!EdNode || !Path.Resolve(EdNode->Node))
		{
			return;
		}

		const FScopedTransaction Transaction(What);
		EdNode->Modify();
		Edit(*Path.Resolve(EdNode->Node));
		EdNode->PostEditChange();
	}

	/** The icon and its tint for each built-in condition; others take a filter. */
	const FSlateBrush* GetConditionIcon(const UScriptStruct* Struct, FLinearColor& OutColor)
	{
		struct FLook
		{
			const UScriptStruct* Struct;
			const FSlateBrush* Icon;
			FColor Color;
		};
		static const FLook Looks[] = {
			{ FKzDialogueCondition_Fact::StaticStruct(), FAppStyle::GetBrush("Icons.Tag"), FColor(0x34, 0xD1, 0xE0) },
			{ FKzDialogueCondition_LineSeen::StaticStruct(), FAppStyle::GetBrush("Icons.Visible"), FColor(0xFF, 0xD1, 0x66) },
			{ FKzDialogueCondition_Visited::StaticStruct(), FAppStyle::GetBrush("Icons.Recent"), FColor(0x9B, 0xE1, 0x5D) },
			{ FKzDialogueCondition_InstigatorTags::StaticStruct(), FAppStyle::GetBrush("Icons.Role"), FColor(0xB9, 0xA7, 0xFF) },
			{ FKzDialogueCondition_Chance::StaticStruct(), FKzDialogueEditorStyle::Get().GetBrush("Kz.Dialogue.Condition.Chance"), FColor(0xF4, 0xA2, 0x61) },
			{ FKzDialogueCondition_Group::StaticStruct(), FAppStyle::GetBrush("Icons.GroupActors"), FColor(0xC9, 0xCE, 0xD4) },
			{ FKzDialogueCondition_Blueprint::StaticStruct(), FAppStyle::GetBrush("Icons.Blueprint"), FColor(0x6F, 0x93, 0xD8) },
		};

		for (const FLook& Look : Looks)
		{
			if (Look.Struct == Struct)
			{
				OutColor = FLinearColor(Look.Color);
				return Look.Icon;
			}
		}
		OutColor = ChipColor;
		return FAppStyle::GetBrush("Icons.Filter");
	}

	TSharedRef<SWidget> MakeSmallText(const FText& Text, const FLinearColor& Color)
	{
		return SNew(STextBlock)
			.Text(Text)
			.Font(SmallFont())
			.ColorAndOpacity(Color);
	}

	TSharedRef<SWidget> MakeIcon(const FSlateBrush* Brush, const FLinearColor& Color)
	{
		return SNew(SImage)
			.Image(Brush)
			.ColorAndOpacity(Color)
			.DesiredSizeOverride(FVector2D(12.0f, 12.0f));
	}

	/** "all of" or "any of", switched with a click. */
	TSharedRef<SWidget> MakeMatchChip(const TWeakObjectPtr<UKzEdGraphNode>& WeakNode, const FConditionsPath& Path, EKzDialogueConditionMatch Match)
	{
		return SNew(SBorder)
			.BorderImage(&Brushes().Chip)
			.Padding(FMargin(7.0f, 0.0f))
			.Cursor(EMouseCursor::Hand)
			.ToolTipText(LOCTEXT("MatchTip", "Whether every condition must hold, or any one. Click to switch."))
			.OnMouseButtonDown_Lambda([WeakNode, Path, Match](const FGeometry&, const FPointerEvent& Event)
			{
				if (Event.GetEffectingButton() != EKeys::LeftMouseButton)
				{
					return FReply::Unhandled();
				}
				const EKzDialogueConditionMatch Switched = Match == EKzDialogueConditionMatch::All ? EKzDialogueConditionMatch::Any : EKzDialogueConditionMatch::All;
				EditConditions(WeakNode, Path, LOCTEXT("SwitchMatch", "Switch Condition Match"), [Switched](FKzDialogueConditions& List) { List.Match = Switched; });
				return FReply::Handled();
			})
			[
				MakeSmallText(Match == EKzDialogueConditionMatch::All ? LOCTEXT("AllOf", "all of") : LOCTEXT("AnyOf", "any of"), ChipColor)
			];
	}

	/** A condition on its way to another place in the same list. */
	class FConditionDragDrop : public FDragDropOperation
	{
	public:
		DRAG_DROP_OPERATOR_TYPE(FConditionDragDrop, FDragDropOperation)

		static TSharedRef<FConditionDragDrop> New(const TWeakObjectPtr<UKzEdGraphNode>& InNode, const FConditionsPath& InPath, int32 InIndex, const FText& InLabel)
		{
			const TSharedRef<FConditionDragDrop> Operation = MakeShared<FConditionDragDrop>();
			Operation->Node = InNode;
			Operation->Path = InPath;
			Operation->Index = InIndex;
			Operation->Label = InLabel;
			Operation->Construct();
			return Operation;
		}

		TWeakObjectPtr<UKzEdGraphNode> Node;
		FConditionsPath Path;
		int32 Index = INDEX_NONE;
		FText Label;

		virtual TSharedPtr<SWidget> GetDefaultDecorator() const override
		{
			return SNew(SBorder)
				.BorderImage(&Brushes().Pill)
				.Padding(FMargin(10.0f, 3.0f))
				[
					MakeSmallText(Label, PillTextColor)
				];
		}
	};

	/** One condition: dragged to reorder its list, with a line showing where it would land. */
	class SConditionPill : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SConditionPill) {}
			SLATE_ARGUMENT(const FSlateBrush*, Brush)
			SLATE_ARGUMENT(FText, Label)
			SLATE_DEFAULT_SLOT(FArguments, Content)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs, const TWeakObjectPtr<UKzEdGraphNode>& InNode, const FConditionsPath& InPath, int32 InIndex)
		{
			Node = InNode;
			Path = InPath;
			Index = InIndex;
			Label = InArgs._Label;
			SetCursor(EMouseCursor::GrabHand);

			auto MakeDropLine = [this](bool bAbove)
			{
				return SNew(SBox)
					.HeightOverride(2.0f)
					[
						SNew(SImage)
						.Image(FAppStyle::GetBrush("WhiteBrush"))
						.ColorAndOpacity(DropColor)
						.Visibility_Lambda([this, bAbove]() { return DropAbove.IsSet() && DropAbove.GetValue() == bAbove ? EVisibility::Visible : EVisibility::Hidden; })
					];
			};

			ChildSlot
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[MakeDropLine(true)]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SBorder)
					.BorderImage(InArgs._Brush)
					.Padding(FMargin(6.0f, 3.0f, 10.0f, 3.0f))
					[
						InArgs._Content.Widget
					]
				]
				+ SVerticalBox::Slot().AutoHeight()[MakeDropLine(false)]
			];
		}

		virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
		{
			// Taken here, or the panel would drag the whole node.
			return MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton ? FReply::Handled().DetectDrag(SharedThis(this), EKeys::LeftMouseButton) : FReply::Unhandled();
		}

		virtual FReply OnDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
		{
			return FReply::Handled().BeginDragDrop(FConditionDragDrop::New(Node, Path, Index, Label));
		}

		virtual FReply OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override
		{
			const TSharedPtr<FConditionDragDrop> Operation = DragDropEvent.GetOperationAs<FConditionDragDrop>();
			if (!Accepts(Operation))
			{
				return FReply::Unhandled();
			}
			DropAbove = MyGeometry.AbsoluteToLocal(DragDropEvent.GetScreenSpacePosition()).Y < MyGeometry.GetLocalSize().Y * 0.5f;
			return FReply::Handled();
		}

		virtual void OnDragLeave(const FDragDropEvent& DragDropEvent) override
		{
			DropAbove.Reset();
		}

		virtual FReply OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override
		{
			const TSharedPtr<FConditionDragDrop> Operation = DragDropEvent.GetOperationAs<FConditionDragDrop>();
			const bool bAbove = DropAbove.Get(true);
			DropAbove.Reset();
			if (!Accepts(Operation))
			{
				return FReply::Unhandled();
			}

			const int32 From = Operation->Index;
			const int32 To = (bAbove ? Index : Index + 1) - (From < Index ? 1 : 0);
			if (To != From)
			{
				EditConditions(Node, Path, LOCTEXT("ReorderCondition", "Reorder Condition"), [From, To](FKzDialogueConditions& List)
				{
					if (List.Conditions.IsValidIndex(From) && To >= 0 && To < List.Conditions.Num())
					{
						TInstancedStruct<FKzDialogueCondition> Moved = MoveTemp(List.Conditions[From]);
						List.Conditions.RemoveAt(From);
						List.Conditions.Insert(MoveTemp(Moved), To);
					}
				});
			}
			return FReply::Handled();
		}

	private:
		bool Accepts(const TSharedPtr<FConditionDragDrop>& Operation) const
		{
			return Operation.IsValid() && Operation->Node == Node && Operation->Path == Path && Operation->Index != Index;
		}

		TWeakObjectPtr<UKzEdGraphNode> Node;
		FConditionsPath Path;
		int32 Index = INDEX_NONE;
		FText Label;
		TOptional<bool> DropAbove;
	};

	/** The pill's row: grip, "not" when inverted, type icon and the condition's text. */
	TSharedRef<SHorizontalBox> MakePillRow(const FKzDialogueCondition& Condition, const UScriptStruct* Struct)
	{
		FLinearColor IconColor;
		const FSlateBrush* Icon = GetConditionIcon(Struct, IconColor);

		const TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 4.0f, 0.0f)
			[
				MakeIcon(FAppStyle::GetBrush("Icons.DragHandle"), MutedColor)
			];

		if (Condition.bInvert)
		{
			Row->AddSlot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 4.0f, 0.0f)
			[
				SNew(SBorder)
				.BorderImage(&Brushes().Chip)
				.Padding(FMargin(5.0f, 0.0f))
				[
					MakeSmallText(LOCTEXT("Not", "not"), ChipColor)
				]
			];
		}

		Row->AddSlot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0.0f, 0.0f, 5.0f, 0.0f)
		[
			MakeIcon(Icon, IconColor)
		];
		Row->AddSlot()
		.FillWidth(1.0f)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(Condition.GetDescription())
			.Font(SmallFont())
			.ColorAndOpacity(PillTextColor)
			.WrapTextAt(BodyWidth - 80.0f)
		];
		return Row;
	}

	TSharedRef<SWidget> MakeConditionList(const TWeakObjectPtr<UKzEdGraphNode>& WeakNode, const FConditionsPath& Path, const FKzDialogueConditions& Conditions, bool bNested)
	{
		const TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
		for (int32 Index = 0; Index < Conditions.Conditions.Num(); ++Index)
		{
			const TInstancedStruct<FKzDialogueCondition>& Each = Conditions.Conditions[Index];
			if (!Each.IsValid())
			{
				continue;
			}

			const TSharedRef<SHorizontalBox> Row = MakePillRow(Each.Get(), Each.GetScriptStruct());
			TSharedRef<SWidget> Content = Row;
			const FSlateBrush* Brush = bNested ? &Brushes().NestedPill : &Brushes().Pill;

			// A group holds its own list, with its own match, inside its pill.
			if (const FKzDialogueCondition_Group* Group = Each.GetPtr<FKzDialogueCondition_Group>())
			{
				Row->AddSlot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(6.0f, 0.0f, 0.0f, 0.0f)
				[
					MakeMatchChip(WeakNode, Path.Child(Index), Group->Conditions.Match)
				];
				Content = SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()[Row]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(12.0f, 3.0f, 0.0f, 1.0f)
					[
						Group->Conditions.IsEmpty() ? MakeSmallText(LOCTEXT("EmptyGroup", "always"), MutedColor) : MakeConditionList(WeakNode, Path.Child(Index), Group->Conditions, true)
					];
				Brush = &Brushes().Group;
			}

			List->AddSlot()
			.AutoHeight()
			[
				SNew(SConditionPill, WeakNode, Path, Index)
				.Brush(Brush)
				.Label(Each.Get().GetDescription())
				[
					Content
				]
			];
		}
		return List;
	}

	/** A box of conditions: a header saying what they are for, with their match, then a pill each. */
	TSharedRef<SWidget> MakeConditionBox(const TWeakObjectPtr<UKzEdGraphNode>& WeakNode, const FConditionsPath& Path, const FKzDialogueConditions& Conditions, const TSharedRef<SWidget>& Title)
	{
		const TSharedRef<SHorizontalBox> Header = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				Title
			];
		Header->AddSlot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			Conditions.IsEmpty() ? MakeSmallText(LOCTEXT("Always", "always"), MutedColor) : MakeMatchChip(WeakNode, Path, Conditions.Match)
		];

		const TSharedRef<SVerticalBox> Content = SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(2.0f, 0.0f, 0.0f, 0.0f)
			[
				Header
			];
		if (!Conditions.IsEmpty())
		{
			Content->AddSlot()
			.AutoHeight()
			.Padding(0.0f, 4.0f, 0.0f, 0.0f)
			[
				MakeConditionList(WeakNode, Path, Conditions, false)
			];
		}

		return SNew(SBorder)
			.BorderImage(&Brushes().Box)
			.Padding(6.0f)
			[
				Content
			];
	}

	TWeakObjectPtr<UKzEdGraphNode> GetWeakNode(const UKzEdGraphNode& EdNode)
	{
		// The body edits the node it shows; the registry hands it over const.
		return TWeakObjectPtr<UKzEdGraphNode>(const_cast<UKzEdGraphNode*>(&EdNode));
	}

	/** The node's own body under its Only If box, when it has one. Built once: the node's widget is rebuilt on every edit. */
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
			.Padding(8.0f, 8.0f, 8.0f, 0.0f)
			[
				MakeConditionBox(GetWeakNode(EdNode), FConditionsPath(), Step->OnlyIf, MakeSmallText(LOCTEXT("OnlyIf", "Only if"), HeaderColor))
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

		const TSharedRef<SVerticalBox> Boxes = SNew(SVerticalBox);
		for (int32 Index = 0; Index < Branch->Branches.Num(); ++Index)
		{
			const FKzDialogueBranch& Each = Branch->Branches[Index];

			// The number matches the branch's output pin.
			const TSharedRef<SHorizontalBox> Title = SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SBox)
					.WidthOverride(16.0f)
					.HeightOverride(16.0f)
					[
						SNew(SBorder)
						.BorderImage(&Brushes().Badge)
						.HAlign(HAlign_Center)
						.VAlign(VAlign_Center)
						.Padding(0.0f)
						[
							MakeSmallText(FText::AsNumber(Index + 1), PillTextColor)
						]
					]
				];
			if (!Each.Label.IsNone())
			{
				Title->AddSlot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(6.0f, 0.0f, 0.0f, 0.0f)
				[
					MakeSmallText(FText::FromName(Each.Label), PillTextColor)
				];
			}

			FConditionsPath Path;
			Path.Branch = Index;
			Boxes->AddSlot()
			.AutoHeight()
			.Padding(0.0f, Index > 0 ? 6.0f : 0.0f, 0.0f, 0.0f)
			[
				MakeConditionBox(GetWeakNode(EdNode), Path, Each.Condition, Title)
			];
		}

		return MakeStepBody(EdNode, SNew(SBox)
			.MaxDesiredWidth(BodyWidth)
			.Padding(FMargin(8.0f, 8.0f, 8.0f, 2.0f))
			[
				Boxes
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

		// The marshaller learns where the speaker ends from the text it is about to lay out.
		const TSharedRef<FSubtitleMarshaller> Marshaller = MakeShared<FSubtitleMarshaller>();
		auto GetSubtitle = [Resolve, Marshaller]()
		{
			const FRefView View = Resolve();
			if (!View.Line && !View.Alias)
			{
				const FText Missing = LOCTEXT("NoLine", "No line");
				Marshaller->SpeakerLength = Missing.ToString().Len();
				Marshaller->SpeakerTextColor = MissingColor;
				return Missing;
			}

			const FString Text = GetBodyText(View).ToString().Replace(TEXT("\n"), TEXT(" "));
			const FKzDialogueSpeaker* Speaker = View.GetSpeaker();
			const FString Prefix = Speaker && Speaker->IsValid() ? Speaker->GetEditorLabel().ToString() + TEXT(": ") : FString();
			Marshaller->SpeakerLength = Prefix.Len();
			Marshaller->SpeakerTextColor = Speaker ? GetSpeakerColor(*Speaker) : SpeakerColor;
			return FText::FromString(Prefix + Text);
		};

		return MakeStepBody(EdNode, SNew(SBox)
			.MaxDesiredWidth(BodyWidth)
			.Padding(FMargin(8.0f, 8.0f, 8.0f, 2.0f))
			[
				SNew(SBorder)
				.BorderImage(&Brushes().Subtitle)
				.Padding(FMargin(10.0f, 6.0f))
				[
					SNew(SOverlay)
					+ SOverlay::Slot()
					[
						SNew(SRichTextBlock)
						.Marshaller(Marshaller)
						.Text_Lambda(GetSubtitle)
						.Justification(ETextJustify::Center)
						.WrapTextAt(BodyWidth - 36.0f)
					]
					+ SOverlay::Slot()
					.HAlign(HAlign_Right)
					.VAlign(VAlign_Bottom)
					.Padding(0.0f, 0.0f, -6.0f, -3.0f)
					[
						SNew(SImage)
						.Image(FSlateIconFinder::FindIconBrushForClass(USoundWave::StaticClass()))
						.ColorAndOpacity(MutedColor)
						.DesiredSizeOverride(FVector2D(10.0f, 10.0f))
						.ToolTipText(LOCTEXT("HasAudio", "The line has audio."))
						.Visibility_Lambda([Resolve]()
						{
							const FRefView View = Resolve();
							return View.Line && !View.Line->Audio.IsNull() ? EVisibility::Visible : EVisibility::Collapsed;
						})
					]
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