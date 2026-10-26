// Copyright 2026 kirzo

#include "Graph/KzDialogueGraphNodes.h"
#include "KzDialogueAsset.h"

#define LOCTEXT_NAMESPACE "KzDialogueGraphNodes"

const FName FKzDialogueGraphNode_Entry::DefaultName(TEXT("Start"));
const FName FKzDialogueGraphNode_Choice::TimeoutOutput(TEXT("Timeout"));
const FName FKzDialogueGraphNode_Branch::ElseOutput(TEXT("Else"));

#if WITH_EDITOR
namespace
{
	// Dark enough for a white title. Branch takes the engine's bool color: it is a condition.
	FLinearColor FlowColor() { return FLinearColor(FColor(0x1E, 0x66, 0x70)); }
	FLinearColor LineColor() { return FLinearColor(FColor(0x24, 0x52, 0x7A)); }
	FLinearColor ChoiceColor() { return FLinearColor(FColor(0x5B, 0x2F, 0x7A)); }
	FLinearColor BranchColor() { return FLinearColor(0.3f, 0.0f, 0.0f); }
	FLinearColor RandomColor() { return FLinearColor(FColor(0x8A, 0x5A, 0x0F)); }
	FLinearColor GameplayColor() { return FLinearColor(FColor(0x2D, 0x6A, 0x3E)); }

	/** Gives every entry a valid id of its own, so a new or duplicated entry gets its own output. */
	template<typename T>
	void EnsureUniqueIds(TArray<T>& Entries)
	{
		TSet<FGuid> Seen;
		for (T& Entry : Entries)
		{
			bool bSeen = false;
			Seen.Add(Entry.Id, &bSeen);
			if (!Entry.Id.IsValid() || bSeen)
			{
				Entry.Id = FGuid::NewGuid();
				Seen.Add(Entry.Id);
			}
		}
	}

	/** The text a line or alias reference shows in the editor; empty when it resolves to nothing. */
	FText GetRefText(const FKzDialogueLineRef& Ref)
	{
		const UKzDialogueAsset* Asset = Ref.Asset.LoadSynchronous();
		if (!Asset)
		{
			return FText::GetEmpty();
		}
		if (const FKzDialogueLine* Line = Asset->FindLine(Ref.LineId))
		{
			return Line->Text;
		}
		const FKzDialogueAlias* Alias = Asset->FindAlias(Ref.LineId);
		return Alias ? Alias->GetDisplayLabel() : FText::GetEmpty();
	}

	void ValidateRef(const FKzDialogueLineRef& Ref, const FText& What, TArray<FKzValidationIssue>& OutIssues)
	{
		if (!Ref.IsValid())
		{
			OutIssues.Add(FKzValidationIssue(EKzValidationSeverity::Error, FText::Format(LOCTEXT("NoLine", "{0} has no line."), What), NAME_None));
		}
		else if (GetRefText(Ref).IsEmpty())
		{
			OutIssues.Add(FKzValidationIssue(EKzValidationSeverity::Error, FText::Format(LOCTEXT("MissingLine", "{0} points to a line or alias that no longer exists."), What), NAME_None));
		}
	}
}

FText FKzDialogueGraphNode_Entry::GetSubtitle() const
{
	return FText::FromName(Name);
}

FLinearColor FKzDialogueGraphNode_Entry::GetColor() const
{
	return FlowColor();
}

void FKzDialogueGraphNode_Entry::Validate(TArray<FKzValidationIssue>& OutIssues) const
{
	if (Name.IsNone())
	{
		OutIssues.Add(FKzValidationIssue(EKzValidationSeverity::Error, LOCTEXT("EntryNoName", "The entry needs a name to be started by."), NAME_None));
	}
}

FLinearColor FKzDialogueGraphNode_Line::GetColor() const
{
	return LineColor();
}

void FKzDialogueGraphNode_Line::GetSearchTexts(TArray<FText>& OutTexts) const
{
	OutTexts.Add(GetRefText(Line));
}

void FKzDialogueGraphNode_Line::Validate(TArray<FKzValidationIssue>& OutIssues) const
{
	ValidateRef(Line, LOCTEXT("TheLine", "The node"), OutIssues);
}

void FKzDialogueGraphNode_Choice::GetOutputPins(const UKzGraph& Graph, TArray<FKzGraphPinDesc>& OutPins) const
{
	for (int32 Index = 0; Index < Options.Num(); ++Index)
	{
		OutPins.Add({ GetOptionOutput(Options[Index]), FText::AsNumber(Index + 1), NAME_None, GetRefText(Options[Index].Line) });
	}
	if (TimeLimit > 0.0f)
	{
		OutPins.Add({ TimeoutOutput, LOCTEXT("Timeout", "Timeout"), NAME_None, LOCTEXT("TimeoutTip", "Where the dialogue goes when no option was chosen in time.") });
	}
}

FLinearColor FKzDialogueGraphNode_Choice::GetColor() const
{
	return ChoiceColor();
}

void FKzDialogueGraphNode_Choice::GetSearchTexts(TArray<FText>& OutTexts) const
{
	for (const FKzDialogueChoiceOption& Option : Options)
	{
		OutTexts.Add(GetRefText(Option.Line));
	}
}

void FKzDialogueGraphNode_Choice::Validate(TArray<FKzValidationIssue>& OutIssues) const
{
	if (Options.IsEmpty())
	{
		OutIssues.Add(FKzValidationIssue(EKzValidationSeverity::Error, LOCTEXT("NoOptions", "The choice has no options."), NAME_None));
	}
	for (int32 Index = 0; Index < Options.Num(); ++Index)
	{
		ValidateRef(Options[Index].Line, FText::Format(LOCTEXT("OptionN", "Option {0}"), Index + 1), OutIssues);
	}
}

void FKzDialogueGraphNode_Choice::PostEditChange()
{
	EnsureUniqueIds(Options);
}

void FKzDialogueGraphNode_Branch::GetOutputPins(const UKzGraph& Graph, TArray<FKzGraphPinDesc>& OutPins) const
{
	for (int32 Index = 0; Index < Branches.Num(); ++Index)
	{
		const FKzDialogueBranch& Branch = Branches[Index];
		OutPins.Add({ GetBranchOutput(Branch), Branch.Label.IsNone() ? FText::AsNumber(Index + 1) : FText::FromName(Branch.Label), NAME_None, LOCTEXT("BranchTip", "Taken when this branch's condition holds and no branch before it does.") });
	}
	OutPins.Add({ ElseOutput, LOCTEXT("Else", "Else"), NAME_None, LOCTEXT("ElseTip", "Taken when no branch holds.") });
}

FLinearColor FKzDialogueGraphNode_Branch::GetColor() const
{
	return BranchColor();
}

void FKzDialogueGraphNode_Branch::Validate(TArray<FKzValidationIssue>& OutIssues) const
{
	if (Branches.IsEmpty())
	{
		OutIssues.Add(FKzValidationIssue(EKzValidationSeverity::Warning, LOCTEXT("NoBranches", "The branch has no conditions: it always goes through Else."), NAME_None));
	}
}

void FKzDialogueGraphNode_Branch::PostEditChange()
{
	EnsureUniqueIds(Branches);
}

void FKzDialogueGraphNode_Random::GetOutputPins(const UKzGraph& Graph, TArray<FKzGraphPinDesc>& OutPins) const
{
	for (int32 Index = 0; Index < Outputs.Num(); ++Index)
	{
		OutPins.Add({ GetOutput(Outputs[Index]), FText::AsNumber(Index + 1), NAME_None, FText::Format(LOCTEXT("WeightTip", "Weight {0}"), FText::AsNumber(Outputs[Index].Weight)) });
	}
}

FLinearColor FKzDialogueGraphNode_Random::GetColor() const
{
	return RandomColor();
}

void FKzDialogueGraphNode_Random::Validate(TArray<FKzValidationIssue>& OutIssues) const
{
	float Total = 0.0f;
	for (const FKzDialogueRandomOutput& Output : Outputs)
	{
		Total += Output.Weight;
	}
	if (Total <= 0.0f)
	{
		OutIssues.Add(FKzValidationIssue(EKzValidationSeverity::Error, LOCTEXT("NoWeight", "The random node needs at least one output with weight."), NAME_None));
	}
}

void FKzDialogueGraphNode_Random::PostEditChange()
{
	EnsureUniqueIds(Outputs);
}

FText FKzDialogueGraphNode_Event::GetSubtitle() const
{
	return FText::FromName(Event.GetTagName());
}

FLinearColor FKzDialogueGraphNode_Event::GetColor() const
{
	return GameplayColor();
}

void FKzDialogueGraphNode_Event::Validate(TArray<FKzValidationIssue>& OutIssues) const
{
	if (!Event.IsValid())
	{
		OutIssues.Add(FKzValidationIssue(EKzValidationSeverity::Error, LOCTEXT("NoEvent", "The event has no tag."), NAME_None));
	}
}

FLinearColor FKzDialogueGraphNode_Action::GetColor() const
{
	return GameplayColor();
}

FText FKzDialogueGraphNode_Wait::GetSubtitle() const
{
	if (Seconds > 0.0f)
	{
		return FText::Format(LOCTEXT("WaitSeconds", "{0} s"), FText::AsNumber(Seconds));
	}
	return ContinueTag.IsValid() ? FText::Format(LOCTEXT("WaitTag", "Until Continue {0}"), FText::FromName(ContinueTag.GetTagName())) : LOCTEXT("WaitContinue", "Until Continue");
}

FLinearColor FKzDialogueGraphNode_Wait::GetColor() const
{
	return FlowColor();
}

FText FKzDialogueGraphNode_GoTo::GetSubtitle() const
{
	return FText::FromName(Entry);
}

FLinearColor FKzDialogueGraphNode_GoTo::GetColor() const
{
	return FlowColor();
}

FText FKzDialogueGraphNode_Exit::GetSubtitle() const
{
	return Name.IsNone() ? FText::GetEmpty() : FText::FromName(Name);
}

FLinearColor FKzDialogueGraphNode_Exit::GetColor() const
{
	return FlowColor();
}
#endif

#undef LOCTEXT_NAMESPACE