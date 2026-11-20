// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Graph/KzDialogueGraphConditions.h"
#include "KzDialogueTypes.h"
#include "KzGraphNode.h"
#include "KzDialogueGraphNodes.generated.h"

class UKzDialogueGraph;
class ULevelSequence;

/** Base of dialogue graph nodes. */
USTRUCT(meta = (Hidden))
struct KZDIALOGUE_API FKzDialogueGraphNode : public FKzGraphNode
{
	GENERATED_BODY()
};

/** The name of an entry, picked from the graph's entries in the editor. EntriesOf names a sibling graph property to list that graph's instead. */
USTRUCT()
struct KZDIALOGUE_API FKzDialogueGraphEntryName
{
	GENERATED_BODY()

	FKzDialogueGraphEntryName() = default;
	explicit FKzDialogueGraphEntryName(FName InName) : Name(InName) {}

	UPROPERTY(EditAnywhere, Category = "Entry")
	FName Name;
};

/** Where a play starts, by name. A graph can have several, such as Start and SecondVisit. */
USTRUCT(meta = (DisplayName = "Entry", Category = "Flow", Keywords = "start begin"))
struct KZDIALOGUE_API FKzDialogueGraphNode_Entry : public FKzDialogueGraphNode
{
	GENERATED_BODY()

	static const FName DefaultName;

	UPROPERTY(EditAnywhere, Category = "Entry", meta = (EditCondition = "bRenamable", EditConditionHides, HideEditConditionToggle))
	FName Name;

	/** False for the graph's Start. */
	UPROPERTY()
	bool bRenamable = true;

	virtual FName GetEntryName() const override { return Name; }

#if WITH_EDITOR
	virtual void GetInputPins(const UKzGraph& Graph, TArray<FKzGraphPinDesc>& OutPins) const override {}
	virtual FText GetSubtitle() const override;
	virtual FLinearColor GetColor() const override;
	virtual FSlateIcon GetIcon() const override;
	virtual void Validate(TArray<FKzValidationIssue>& OutIssues) const override;
#endif
};

/** The entry every graph begins with, and plays start from by default. It is neither deleted nor renamed. */
USTRUCT(meta = (DisplayName = "Start", Hidden))
struct KZDIALOGUE_API FKzDialogueGraphNode_Start : public FKzDialogueGraphNode_Entry
{
	GENERATED_BODY()

	FKzDialogueGraphNode_Start()
	{
		Name = DefaultName;
		bRenamable = false;
	}

#if WITH_EDITOR
	virtual FText GetSubtitle() const override { return FText::GetEmpty(); }
	virtual bool CanUserDelete() const override { return false; }
	virtual bool CanDuplicate() const override { return false; }
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
USTRUCT(meta = (DisplayName = "Line", Category = "Dialogue", Shortcut = "L", Keywords = "say text speech"))
struct KZDIALOGUE_API FKzDialogueGraphNode_Line : public FKzDialogueGraphStep
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Line", meta = (InlineLine))
	FKzDialogueLineRef Line;

#if WITH_EDITOR
	virtual FLinearColor GetColor() const override;
	virtual FSlateIcon GetIcon() const override;
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

	/** Marked first when the options show, while available. Only one option should be. */
	UPROPERTY(EditAnywhere, Category = "Option")
	bool bDefault = false;

	/** What kind of option it is, for the choice widget to style its row: one that moves the story on, one that leaves the conversation... */
	UPROPERTY(EditAnywhere, Category = "Option", meta = (Categories = "Dialogue.Option"))
	FGameplayTagContainer Tags;

	/** Names the option's output, so its link survives reordering. */
	UPROPERTY()
	FGuid Id;
};

/** Offers options to the player and goes on through the one chosen. */
USTRUCT(meta = (DisplayName = "Choice", Category = "Dialogue", Shortcut = "O", Keywords = "option question answer"))
struct KZDIALOGUE_API FKzDialogueGraphNode_Choice : public FKzDialogueGraphStep
{
	GENERATED_BODY()

	static const FName TimeoutOutput;

	UPROPERTY(EditAnywhere, Category = "Choice")
	TArray<FKzDialogueChoiceOption> Options;

	/** Seconds to choose before leaving through Timeout. Zero waits as long as it takes. */
	UPROPERTY(EditAnywhere, Category = "Choice", meta = (ClampMin = "0", Units = "Seconds"))
	float TimeLimit = 0.0f;

	/** Said while the options wait for an answer, after Idle Delay and then every Idle Interval: a line, or an alias for a different one each time. The options stay; choosing cuts it short. Empty says nothing. */
	UPROPERTY(EditAnywhere, Category = "Choice|Idle", meta = (InlineLine))
	FKzDialogueLineRef IdleLine;

	/** Seconds the options wait before the first idle line. */
	UPROPERTY(EditAnywhere, Category = "Choice|Idle", meta = (ClampMin = "0", Units = "Seconds"))
	float IdleDelay = 10.0f;

	/** Seconds after an idle line ends before the next one. Zero says it once. */
	UPROPERTY(EditAnywhere, Category = "Choice|Idle", meta = (ClampMin = "0", Units = "Seconds"))
	float IdleInterval = 15.0f;

	static FName GetOptionOutput(const FKzDialogueChoiceOption& Option) { return FName(Option.Id.ToString(EGuidFormats::Digits)); }

#if WITH_EDITOR
	virtual void GetOutputPins(const UKzGraph& Graph, TArray<FKzGraphPinDesc>& OutPins) const override;
	virtual FLinearColor GetColor() const override;
	virtual FSlateIcon GetIcon() const override;
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
USTRUCT(meta = (DisplayName = "Branch", Category = "Logic", Shortcut = "B", Keywords = "if condition switch"))
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
	virtual FSlateIcon GetIcon() const override;
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
USTRUCT(meta = (DisplayName = "Random", Category = "Logic", Shortcut = "R", Keywords = "chance weight"))
struct KZDIALOGUE_API FKzDialogueGraphNode_Random : public FKzDialogueGraphStep
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Random")
	TArray<FKzDialogueRandomOutput> Outputs;

	/** Never takes the output this play took here last time, while there is another: variations that do not repeat back to back. */
	UPROPERTY(EditAnywhere, Category = "Random")
	bool bNoRepeat = false;

	static FName GetOutput(const FKzDialogueRandomOutput& Output) { return FName(Output.Id.ToString(EGuidFormats::Digits)); }

#if WITH_EDITOR
	virtual void GetOutputPins(const UKzGraph& Graph, TArray<FKzGraphPinDesc>& OutPins) const override;
	virtual FLinearColor GetColor() const override;
	virtual FSlateIcon GetIcon() const override;
	virtual void Validate(TArray<FKzValidationIssue>& OutIssues) const override;
	virtual void PostEditChange() override;
#endif
};

/** Tells gameplay something happened, by tag, and goes on at once. */
USTRUCT(meta = (DisplayName = "Event", Category = "Gameplay", Shortcut = "E", Keywords = "notify signal gameplay"))
struct KZDIALOGUE_API FKzDialogueGraphNode_Event : public FKzDialogueGraphStep
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Event")
	FGameplayTag Event;

#if WITH_EDITOR
	virtual FText GetSubtitle() const override;
	virtual FLinearColor GetColor() const override;
	virtual FSlateIcon GetIcon() const override;
	virtual void Validate(TArray<FKzValidationIssue>& OutIssues) const override;
#endif
};

/** Runs its actions, such as noting a fact, and goes on at once. */
USTRUCT(meta = (DisplayName = "Action", Category = "Gameplay", Shortcut = "A", Keywords = "fact set do"))
struct KZDIALOGUE_API FKzDialogueGraphNode_Action : public FKzDialogueGraphStep
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Action", meta = (ExcludeBaseStruct))
	TArray<TInstancedStruct<FKzDialogueAction>> Actions;

#if WITH_EDITOR
	virtual FLinearColor GetColor() const override;
	virtual FSlateIcon GetIcon() const override;
#endif
};

/** Holds the dialogue open, with no line, for a while or until gameplay calls Continue. */
USTRUCT(meta = (DisplayName = "Wait", Category = "Flow", Shortcut = "W", Keywords = "delay pause continue"))
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
	virtual FSlateIcon GetIcon() const override;
#endif
};

/** Whether the conversation waits for a Sequence node's sequence. */
UENUM()
enum class EKzDialogueSequenceMode : uint8
{
	/** The conversation goes on when the sequence ends, such as after an intro or a topic's cinematic. */
	Wait,
	/** The conversation goes on at once, the sequence playing behind it, such as a loop under a Choice. */
	Background
};

/** Where a Sequence node starts its sequence. */
UENUM()
enum class EKzDialogueSequenceStart : uint8
{
	Beginning,
	/** At the marked frame named Start Marker. */
	Marker,
	/** Where this play left the same sequence last time; at the beginning the first time. */
	Resume
};

/**
 * Plays a level sequence, such as a cinematic, with the bindings it has in its level. A play shows one sequence at a time:
 * the next Sequence node replaces it, the end of the play stops it, and one that ends holds its last frame until then.
 * A Skip stops it and lands on the next sequence the play waits for.
 */
USTRUCT(meta = (DisplayName = "Sequence", Category = "Flow", Shortcut = "S", Keywords = "cinematic cutscene sequencer camera"))
struct KZDIALOGUE_API FKzDialogueGraphNode_Sequence : public FKzDialogueGraphStep
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Sequence")
	TObjectPtr<ULevelSequence> Sequence;

	UPROPERTY(EditAnywhere, Category = "Sequence")
	EKzDialogueSequenceMode Mode = EKzDialogueSequenceMode::Wait;

	/** Plays again from its beginning each time it ends. */
	UPROPERTY(EditAnywhere, Category = "Sequence", meta = (EditCondition = "Mode == EKzDialogueSequenceMode::Background", EditConditionHides))
	bool bLoop = false;

	UPROPERTY(EditAnywhere, Category = "Sequence")
	EKzDialogueSequenceStart Start = EKzDialogueSequenceStart::Beginning;

	UPROPERTY(EditAnywhere, Category = "Sequence", meta = (EditCondition = "Start == EKzDialogueSequenceStart::Marker", EditConditionHides))
	FString StartMarker;

	/** A marked frame to end at instead of the end of the sequence. Empty plays it to the end. */
	UPROPERTY(EditAnywhere, Category = "Sequence")
	FString EndMarker;

	/** Seconds the picture takes to come in from black when the sequence starts. Zero cuts in, unless the picture is black from a fade out: then it comes back over that fade's time. */
	UPROPERTY(EditAnywhere, Category = "Sequence|Fade", meta = (ClampMin = "0", Units = "Seconds"))
	float FadeIn = 0.0f;

	/** Seconds the picture takes to go to black at the end of the sequence, or before the play replaces or stops it. Zero cuts out. */
	UPROPERTY(EditAnywhere, Category = "Sequence|Fade", meta = (ClampMin = "0", Units = "Seconds"))
	float FadeOut = 0.0f;

#if WITH_EDITOR
	virtual FText GetSubtitle() const override;
	virtual FLinearColor GetColor() const override;
	virtual FSlateIcon GetIcon() const override;
	virtual void Validate(TArray<FKzValidationIssue>& OutIssues) const override;
#endif
};

/** Plays another dialogue graph from one of its entries, then goes on through the output named after the Exit it ended by, or Out. */
USTRUCT(meta = (DisplayName = "Sub Graph", Category = "Flow", Keywords = "call nested include graph"))
struct KZDIALOGUE_API FKzDialogueGraphNode_SubGraph : public FKzDialogueGraphStep
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Sub Graph")
	TObjectPtr<UKzDialogueGraph> Graph;

	UPROPERTY(EditAnywhere, Category = "Sub Graph", meta = (EntriesOf = "Graph"))
	FKzDialogueGraphEntryName Entry = FKzDialogueGraphEntryName(FKzDialogueGraphNode_Entry::DefaultName);

#if WITH_EDITOR
	virtual void GetOutputPins(const UKzGraph& Owner, TArray<FKzGraphPinDesc>& OutPins) const override;
	virtual FText GetSubtitle() const override;
	virtual FLinearColor GetColor() const override;
	virtual FSlateIcon GetIcon() const override;
	virtual void Validate(TArray<FKzValidationIssue>& OutIssues) const override;
#endif
};

/** Ends the play with a name gameplay receives, such as Accepted or Refused. In a Sub Graph, goes back to the caller through the output with that name. */
USTRUCT(meta = (DisplayName = "Exit", Category = "Flow", Shortcut = "X", Keywords = "end finish stop"))
struct KZDIALOGUE_API FKzDialogueGraphNode_Exit : public FKzDialogueGraphStep
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Exit")
	FName Name;

#if WITH_EDITOR
	virtual void GetOutputPins(const UKzGraph& Graph, TArray<FKzGraphPinDesc>& OutPins) const override {}
	virtual FText GetSubtitle() const override;
	virtual FLinearColor GetColor() const override;
	virtual FSlateIcon GetIcon() const override;
#endif
};