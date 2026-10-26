// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Graph/KzDialogueGraphConditions.h"
#include "KzDialogueTypes.h"
#include "KzGraphNode.h"
#include "KzDialogueGraphNodes.generated.h"

/** Base of dialogue graph nodes. */
USTRUCT(meta = (Hidden))
struct KZDIALOGUE_API FKzDialogueGraphNode : public FKzGraphNode
{
	GENERATED_BODY()
};

/** Where a play starts, by name. A graph can have several, such as Start and SecondVisit. */
USTRUCT(meta = (DisplayName = "Entry", Keywords = "start begin"))
struct KZDIALOGUE_API FKzDialogueGraphNode_Entry : public FKzDialogueGraphNode
{
	GENERATED_BODY()

	static const FName DefaultName;

	UPROPERTY(EditAnywhere, Category = "Entry")
	FName Name = DefaultName;

	virtual FName GetEntryName() const override { return Name; }

#if WITH_EDITOR
	virtual void GetInputPins(const UKzGraph& Graph, TArray<FKzGraphPinDesc>& OutPins) const override {}
	virtual FText GetSubtitle() const override;
	virtual FLinearColor GetColor() const override;
	virtual void Validate(TArray<FKzValidationIssue>& OutIssues) const override;
#endif
};

/** A node links can lead to. */
USTRUCT(meta = (Hidden))
struct KZDIALOGUE_API FKzDialogueGraphStep : public FKzDialogueGraphNode
{
	GENERATED_BODY()

	/** When an output has several links, a play takes the first node, in link order, whose Only If holds. Empty always holds. */
	UPROPERTY(EditAnywhere, Category = "Flow")
	FKzDialogueConditions OnlyIf;
};

/** Presents a line, or one variant of an alias. */
USTRUCT(meta = (DisplayName = "Line", Shortcut = "L", Keywords = "say text speech"))
struct KZDIALOGUE_API FKzDialogueGraphNode_Line : public FKzDialogueGraphStep
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Line", meta = (InlineLine))
	FKzDialogueLineRef Line;

#if WITH_EDITOR
	virtual FLinearColor GetColor() const override;
	virtual void GetSearchTexts(TArray<FText>& OutTexts) const override;
	virtual void Validate(TArray<FKzValidationIssue>& OutIssues) const override;
#endif
};

/** One option of a Choice. */
USTRUCT()
struct KZDIALOGUE_API FKzDialogueChoiceOption
{
	GENERATED_BODY()

	/** What the option reads. When it should also be spoken, the Line after it says it. */
	UPROPERTY(EditAnywhere, Category = "Option", meta = (InlineLine))
	FKzDialogueLineRef Line;

	/** Offered only while this holds. */
	UPROPERTY(EditAnywhere, Category = "Option")
	FKzDialogueConditions Condition;

	/** Shown disabled, instead of hidden, while Condition fails. */
	UPROPERTY(EditAnywhere, Category = "Option")
	bool bShowWhenUnavailable = false;

	/** Names the option's output, so its link survives reordering. */
	UPROPERTY()
	FGuid Id;
};

/** Offers options to the player and goes on through the one chosen. */
USTRUCT(meta = (DisplayName = "Choice", Shortcut = "O", Keywords = "option question answer"))
struct KZDIALOGUE_API FKzDialogueGraphNode_Choice : public FKzDialogueGraphStep
{
	GENERATED_BODY()

	static const FName TimeoutOutput;

	UPROPERTY(EditAnywhere, Category = "Choice")
	TArray<FKzDialogueChoiceOption> Options;

	/** Seconds to choose before leaving through Timeout. Zero waits as long as it takes. */
	UPROPERTY(EditAnywhere, Category = "Choice", meta = (ClampMin = "0", Units = "Seconds"))
	float TimeLimit = 0.0f;

	static FName GetOptionOutput(const FKzDialogueChoiceOption& Option) { return FName(Option.Id.ToString(EGuidFormats::Digits)); }

#if WITH_EDITOR
	virtual void GetOutputPins(const UKzGraph& Graph, TArray<FKzGraphPinDesc>& OutPins) const override;
	virtual FLinearColor GetColor() const override;
	virtual void GetSearchTexts(TArray<FText>& OutTexts) const override;
	virtual void Validate(TArray<FKzValidationIssue>& OutIssues) const override;
	virtual void PostEditChange() override;
#endif
};

/** One branch of a Branch node. */
USTRUCT()
struct KZDIALOGUE_API FKzDialogueBranch
{
	GENERATED_BODY()

	/** Shown on the output; empty shows its number. */
	UPROPERTY(EditAnywhere, Category = "Branch")
	FName Label;

	UPROPERTY(EditAnywhere, Category = "Branch")
	FKzDialogueConditions Condition;

	/** Names the branch's output, so its link survives reordering. */
	UPROPERTY()
	FGuid Id;
};

/** Goes on through the first branch whose condition holds, else through Else. */
USTRUCT(meta = (DisplayName = "Branch", Shortcut = "B", Keywords = "if condition switch"))
struct KZDIALOGUE_API FKzDialogueGraphNode_Branch : public FKzDialogueGraphStep
{
	GENERATED_BODY()

	static const FName ElseOutput;

	UPROPERTY(EditAnywhere, Category = "Branch")
	TArray<FKzDialogueBranch> Branches;

	static FName GetBranchOutput(const FKzDialogueBranch& Branch) { return FName(Branch.Id.ToString(EGuidFormats::Digits)); }

#if WITH_EDITOR
	virtual void GetOutputPins(const UKzGraph& Graph, TArray<FKzGraphPinDesc>& OutPins) const override;
	virtual FLinearColor GetColor() const override;
	virtual void Validate(TArray<FKzValidationIssue>& OutIssues) const override;
	virtual void PostEditChange() override;
#endif
};

/** One output of a Random node. */
USTRUCT()
struct KZDIALOGUE_API FKzDialogueRandomOutput
{
	GENERATED_BODY()

	/** Chance against the other outputs' weights. */
	UPROPERTY(EditAnywhere, Category = "Random", meta = (ClampMin = "0"))
	float Weight = 1.0f;

	/** Names the output, so its link survives reordering. */
	UPROPERTY()
	FGuid Id;
};

/** Goes on through one of its outputs, picked by weight. */
USTRUCT(meta = (DisplayName = "Random", Shortcut = "R", Keywords = "chance weight"))
struct KZDIALOGUE_API FKzDialogueGraphNode_Random : public FKzDialogueGraphStep
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Random")
	TArray<FKzDialogueRandomOutput> Outputs;

	static FName GetOutput(const FKzDialogueRandomOutput& Output) { return FName(Output.Id.ToString(EGuidFormats::Digits)); }

#if WITH_EDITOR
	virtual void GetOutputPins(const UKzGraph& Graph, TArray<FKzGraphPinDesc>& OutPins) const override;
	virtual FLinearColor GetColor() const override;
	virtual void Validate(TArray<FKzValidationIssue>& OutIssues) const override;
	virtual void PostEditChange() override;
#endif
};

/** Tells gameplay something happened, by tag, and goes on at once. */
USTRUCT(meta = (DisplayName = "Event", Shortcut = "E", Keywords = "notify signal gameplay"))
struct KZDIALOGUE_API FKzDialogueGraphNode_Event : public FKzDialogueGraphStep
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Event")
	FGameplayTag Event;

#if WITH_EDITOR
	virtual FText GetSubtitle() const override;
	virtual FLinearColor GetColor() const override;
	virtual void Validate(TArray<FKzValidationIssue>& OutIssues) const override;
#endif
};

/** Runs its actions, such as noting a fact, and goes on at once. */
USTRUCT(meta = (DisplayName = "Action", Shortcut = "A", Keywords = "fact set do"))
struct KZDIALOGUE_API FKzDialogueGraphNode_Action : public FKzDialogueGraphStep
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Action", meta = (ExcludeBaseStruct))
	TArray<TInstancedStruct<FKzDialogueAction>> Actions;

#if WITH_EDITOR
	virtual FLinearColor GetColor() const override;
#endif
};

/** Holds the dialogue open, with no line, for a while or until gameplay calls Continue. */
USTRUCT(meta = (DisplayName = "Wait", Shortcut = "W", Keywords = "delay pause continue"))
struct KZDIALOGUE_API FKzDialogueGraphNode_Wait : public FKzDialogueGraphStep
{
	GENERATED_BODY()

	/** Zero waits until gameplay calls Continue. */
	UPROPERTY(EditAnywhere, Category = "Wait", meta = (ClampMin = "0", Units = "Seconds"))
	float Seconds = 0.0f;

	/** The tag a Continue must carry to release the wait; empty takes any. */
	UPROPERTY(EditAnywhere, Category = "Wait")
	FGameplayTag ContinueTag;

#if WITH_EDITOR
	virtual FText GetSubtitle() const override;
	virtual FLinearColor GetColor() const override;
#endif
};

/** Jumps to an Entry of this graph, for loops and menus that come back without wires across the graph. */
USTRUCT(meta = (DisplayName = "Go To", Shortcut = "G", Keywords = "jump loop back"))
struct KZDIALOGUE_API FKzDialogueGraphNode_GoTo : public FKzDialogueGraphStep
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Go To")
	FName Entry = FKzDialogueGraphNode_Entry::DefaultName;

#if WITH_EDITOR
	virtual void GetOutputPins(const UKzGraph& Graph, TArray<FKzGraphPinDesc>& OutPins) const override {}
	virtual FText GetSubtitle() const override;
	virtual FLinearColor GetColor() const override;
#endif
};

/** Ends the play with a name gameplay receives, such as Accepted or Refused. */
USTRUCT(meta = (DisplayName = "Exit", Shortcut = "X", Keywords = "end finish stop"))
struct KZDIALOGUE_API FKzDialogueGraphNode_Exit : public FKzDialogueGraphStep
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Exit")
	FName Name;

#if WITH_EDITOR
	virtual void GetOutputPins(const UKzGraph& Graph, TArray<FKzGraphPinDesc>& OutPins) const override {}
	virtual FText GetSubtitle() const override;
	virtual FLinearColor GetColor() const override;
#endif
};