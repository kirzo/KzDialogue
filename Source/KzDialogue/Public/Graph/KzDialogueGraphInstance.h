// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "KzDialogueProvider.h"
#include "KzDialogueTypes.h"
#include "KzDialogueGraphInstance.generated.h"

class AActor;
class UKzDialogueGraph;
class UKzDialogueGraphInstance;
class UKzDialogueMemorySubsystem;
class UKzDialoguePlayer;
class UKzDialogueSubsystem;
struct FKzDialogueGraphNode_Choice;
struct FKzDialogueGraphNode_SubGraph;
struct FKzDialogueNetGraphEntry;

/** Who plays a dialogue graph and how. Empty fields take the graph's settings. */
USTRUCT(BlueprintType)
struct KZDIALOGUE_API FKzDialogueGraphContext
{
	GENERATED_BODY()

	/** Who started the dialogue, such as the player talking to an NPC. Conditions check its tags. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue Graph")
	TObjectPtr<AActor> Instigator;

	/** Who the dialogue belongs to, such as the NPC talked to. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue Graph")
	TObjectPtr<AActor> Owner;

	/** Empty takes the graph's channel. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue Graph", meta = (Categories = "Dialogue.Channel"))
	FGameplayTag Channel;

	/** -1 takes the graph's priority. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue Graph")
	int32 Priority = -1;

	/** Inherit takes the graph's mode. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue Graph")
	EKzDialogueAdvanceMode AdvanceMode = EKzDialogueAdvanceMode::Inherit;
};

/** An option a Choice offers. */
USTRUCT(BlueprintType)
struct KZDIALOGUE_API FKzDialogueGraphOption
{
	GENERATED_BODY()

	/** The line the option reads. */
	UPROPERTY(BlueprintReadOnly, Category = "Dialogue Graph")
	FKzDialogueLine Line;

	/** False for an option shown disabled because its condition fails. */
	UPROPERTY(BlueprintReadOnly, Category = "Dialogue Graph")
	bool bAvailable = true;

	/** The Choice marks it Default: the one to mark first. */
	UPROPERTY(BlueprintReadOnly, Category = "Dialogue Graph")
	bool bDefault = false;

	/** What SelectOption takes for it. */
	UPROPERTY(BlueprintReadOnly, Category = "Dialogue Graph")
	int32 Index = INDEX_NONE;
};

/** A Sub Graph node a play went in through, to come back to. */
USTRUCT()
struct FKzDialogueGraphCaller
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<UKzDialogueGraph> Graph;

	int32 Node = INDEX_NONE;
};

/** What a saved play was doing. */
UENUM(BlueprintType)
enum class EKzDialogueGraphStateStep : uint8
{
	/** Nothing to resume. */
	None,
	/** Done with its node: a resumed play goes on from the node's output. */
	Passed,
	/** On a line: a resumed play shows it again. */
	Line,
	/** A Choice waited for an answer. */
	Choosing,
	/** A Wait waited for its time or for Continue. */
	Waiting
};

/** A node of a dialogue graph, by the id that survives recompiling. */
USTRUCT(BlueprintType)
struct KZDIALOGUE_API FKzDialogueGraphNodeRef
{
	GENERATED_BODY()

	UPROPERTY(SaveGame)
	TSoftObjectPtr<UKzDialogueGraph> Graph;

	UPROPERTY(SaveGame)
	FGuid Node;
};

/** A play saved halfway, to keep in the game's save: Get State into it, and resume from it after Apply Dialogue Memory. */
USTRUCT(BlueprintType)
struct KZDIALOGUE_API FKzDialogueGraphState
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Dialogue Graph")
	EKzDialogueGraphStateStep Step = EKzDialogueGraphStateStep::None;

	/** Where the play was, in the graph it was in. */
	UPROPERTY(SaveGame)
	FKzDialogueGraphNodeRef Node;

	/** The Sub Graph nodes it went in through, outermost first. */
	UPROPERTY(SaveGame)
	TArray<FKzDialogueGraphNodeRef> Callers;

	/** The line on screen, also a question kept while choosing; for an alias, the line it picked. */
	UPROPERTY(SaveGame)
	FKzDialogueLineRef ShownLine;

	/** Seconds left of a Choice's time limit or of a Wait. */
	UPROPERTY(SaveGame)
	float TimeLeft = 0.0f;

	/** What this play went through. */
	UPROPERTY(SaveGame)
	TArray<FGuid> Visited;

	UPROPERTY(SaveGame)
	FGameplayTag Channel;

	UPROPERTY(SaveGame)
	int32 Priority = -1;

	UPROPERTY(SaveGame)
	EKzDialogueAdvanceMode AdvanceMode = EKzDialogueAdvanceMode::Inherit;

	bool IsValid() const { return Step != EKzDialogueGraphStateStep::None; }

	/** The graph the play started with. */
	const TSoftObjectPtr<UKzDialogueGraph>& GetGraph() const { return Callers.IsEmpty() ? Node.Graph : Callers[0].Graph; }
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FKzOnDialogueGraphOptions, UKzDialogueGraphInstance*, Instance, const TArray<FKzDialogueGraphOption>&, Options);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FKzOnDialogueGraphOptionsWithdrawn, UKzDialogueGraphInstance*, Instance);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FKzOnDialogueGraphEvent, UKzDialogueGraphInstance*, Instance, FGameplayTag, Event);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FKzOnDialogueGraphEnded, UKzDialogueGraphInstance*, Instance, FName, Exit, bool, bInterrupted);

/**
 * One play of a dialogue graph. It walks the graph from an entry, hands each line to the player of its channel as one dialogue,
 * offers the options of each Choice and goes on through the one chosen, and tells gameplay of its events and of how it ended.
 * Two plays of the same graph are two instances reading the same asset.
 */
UCLASS(BlueprintType)
class KZDIALOGUE_API UKzDialogueGraphInstance : public UObject
{
	GENERATED_BODY()

public:
	/** A play of Graph, not started yet, so its events can be bound before anything happens. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Graph", meta = (WorldContext = "WorldContextObject", AutoCreateRefTerm = "Context", AdvancedDisplay = "Context"))
	static UKzDialogueGraphInstance* CreateDialogueGraph(const UObject* WorldContextObject, UKzDialogueGraph* Graph, const FKzDialogueGraphContext& Context);

	/** Creates a play of Graph and starts it. Events of the nodes before its first line fire before this returns: bind them with Create Dialogue Graph and Start. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Graph", meta = (WorldContext = "WorldContextObject", AutoCreateRefTerm = "Context", AdvancedDisplay = "Context,Entry"))
	static UKzDialogueGraphInstance* StartDialogueGraph(const UObject* WorldContextObject, UKzDialogueGraph* Graph, const FKzDialogueGraphContext& Context, FName Entry = TEXT("Start"));

	/** Starts from the entry with that name. False when there is no such entry or the channel refused the dialogue; a queued play starts later. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Graph")
	bool Start(FName Entry = TEXT("Start"));

	/**
	 * Resumes a saved play instead of starting, on an instance created for the graph the play started with. Channel, priority and advance mode come from the state; the context adds the actors.
	 * False when the state started in another graph, a graph or node of it is gone, or the channel refused the dialogue.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Graph")
	bool Resume(const FKzDialogueGraphState& State);

	/** What the play is doing, for the game's save; empty when it is not running or is closing. */
	UFUNCTION(BlueprintPure, Category = "Dialogue|Graph")
	FKzDialogueGraphState GetState() const;

	/** Chooses one of the options offered. False when nothing is offered or that option is not available. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Graph")
	bool SelectOption(int32 Index);

	/** Advances the line shown, for Manual dialogues. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Graph")
	void Next();

	/** Releases a Wait whose Continue Tag matches Tag; a Wait without one takes any. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Graph")
	void Continue(FGameplayTag Tag);

	/** Ends the play, interrupted, through the dialogue's exit animations. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Graph")
	void Stop();

	UFUNCTION(BlueprintPure, Category = "Dialogue|Graph")
	bool IsRunning() const { return bStarted && !bFinished; }

	/** The options offered now; empty when no Choice waits for an answer. */
	UFUNCTION(BlueprintPure, Category = "Dialogue|Graph")
	const TArray<FKzDialogueGraphOption>& GetOptions() const { return Options; }

	/** Seconds left of the time limit of the options offered, for a countdown; 0 without one or when nothing is offered. */
	UFUNCTION(BlueprintPure, Category = "Dialogue|Graph")
	float GetTimeLeft() const;

	/** The graph the play is in now: the one it started with, or a Sub Graph it went into. */
	UFUNCTION(BlueprintPure, Category = "Dialogue|Graph")
	UKzDialogueGraph* GetGraph() const { return Graph; }

	UFUNCTION(BlueprintPure, Category = "Dialogue|Graph")
	const FKzDialogueGraphContext& GetContext() const { return Context; }

	/** The player the play's lines go to; null before it starts. */
	UFUNCTION(BlueprintPure, Category = "Dialogue|Graph")
	UKzDialoguePlayer* GetPlayer() const { return Player; }

	/** A Choice offers its options. */
	UPROPERTY(BlueprintAssignable, Category = "Dialogue|Graph")
	FKzOnDialogueGraphOptions OnOptionsOffered;

	/** The options offered are gone: one was chosen, the time ran out, or the play ended. */
	UPROPERTY(BlueprintAssignable, Category = "Dialogue|Graph")
	FKzOnDialogueGraphOptionsWithdrawn OnOptionsWithdrawn;

	/** An Event node fired. */
	UPROPERTY(BlueprintAssignable, Category = "Dialogue|Graph")
	FKzOnDialogueGraphEvent OnEvent;

	/** The play ended, through the Exit with that name (none without one), or interrupted. */
	UPROPERTY(BlueprintAssignable, Category = "Dialogue|Graph")
	FKzOnDialogueGraphEnded OnEnded;

	/** True when this play, or with bEver any play of the graph, went through the node or chose the option with that id. */
	bool WasVisited(const FGuid& Id, bool bEver) const;

	/** Times any play of the graph went through the node or chose the option with that id. */
	int32 GetVisitCount(const FGuid& Id) const;

	UKzDialogueMemorySubsystem* GetMemory() const;
	UKzDialogueSubsystem* GetSubsystem() const;

	/** A copy of a server play on a client: its options and countdown follow the server's, Select Option and Next go there, and Continue and Stop are refused. */
	UFUNCTION(BlueprintPure, Category = "Dialogue|Graph")
	bool IsProxy() const { return bProxy; }

	/** Names the play across the network; 0 off it. */
	int32 GetNetPlayId() const { return NetPlayId; }

	/** The state its copies on the clients follow. */
	void FillNetEntry(FKzDialogueNetGraphEntry& Entry) const;

	/** A copy follows its server play's new state; the first one starts it. */
	void ApplyNetEntry(const FKzDialogueNetGraphEntry& Entry);

	/** A copy ends, as its server play did or went away. */
	void EndProxy();

	virtual UWorld* GetWorld() const override;

	// The provider's questions, as the player asks them.
	bool HasLine();
	FKzDialogueLine TakeLine();
	FKzDialogueLine PeekLine() const;
	bool HoldsLine();
	bool IsWaiting() const { return Step == EStep::Choosing || Step == EStep::Waiting; }

private:
	enum class EStep : uint8
	{
		/** The current node is done: the walk goes on from its output when asked. */
		Passed,
		/** A line is ready for the player. */
		Ready,
		/** A Choice waits for SelectOption. */
		Choosing,
		/** A Wait waits for its time or for Continue. */
		Waiting,
		/** The walk is over; the dialogue only has to close. */
		Ended
	};

	/** From the current node, out through Output and on through every node that runs at once, up to a line, a choice, a wait or the end. */
	void Leave(FName Output);

	/** The first node a link of the output leads to whose Only If holds. */
	int32 FindTarget(int32 FromNode, FName Output) const;

	/** Goes into the Sub Graph at the current node and returns its entry's target; past the node when it cannot. */
	int32 EnterSubGraph(const FKzDialogueGraphNode_SubGraph& SubGraph);

	/** Back to the last Sub Graph node, returning the target of its output named Exit, or of Out. */
	int32 ReturnToCaller(FName Exit);

	/** Gets the player of the play's channel and plays on it. */
	bool PlayOnChannel(bool bVisitNode);

	/** Takes up again the Choice or Wait a resumed play stopped at. */
	void TakeUpResumedNode();

	/** Offers the Choice's options with that time limit; false when none is available. */
	bool OfferOptions(const FKzDialogueGraphNode_Choice& Choice, float TimeLimit);
	void WithdrawOptions();

	void StartWait(float Seconds);

	void Visit(const FGuid& Id);
	void EndWalk(FName Exit);

	/** Lets the player go on after a choice or a wait: the held line leaves, or the waiting dialogue looks for its next line. */
	void Proceed();

	void HandleChoiceTimeout();
	void HandleWaitElapsed();

	UFUNCTION()
	void HandleDialogueStarted(UKzDialoguePlayer* InPlayer);

	UFUNCTION()
	void HandleDialogueFinished(UKzDialoguePlayer* InPlayer, EKzDialogueFinishReason Reason);

	UFUNCTION()
	void HandleQueuedRequestDropped(FGameplayTag InChannel, FGuid RequestId);

	void Finish(bool bInterrupted);

	/** On a server, sends the play's state to its copies. */
	void PublishNet() const;

	/** The graph walked now; its callers wait in Callers. */
	UPROPERTY()
	TObjectPtr<UKzDialogueGraph> Graph;

	UPROPERTY()
	TArray<FKzDialogueGraphCaller> Callers;

	UPROPERTY()
	FKzDialogueGraphContext Context;

	UPROPERTY()
	TObjectPtr<UKzDialoguePlayer> Player;

	UPROPERTY()
	TObjectPtr<UKzDialogueProvider> Provider;

	UPROPERTY()
	FKzDialogueLine PendingLine;

	UPROPERTY()
	TArray<FKzDialogueGraphOption> Options;

	/** What this play went through. */
	TSet<FGuid> Visited;

	FName ExitName;
	FGuid QueuedRequestId;
	FTimerHandle StepTimer;
	int32 Node = INDEX_NONE;
	EStep Step = EStep::Passed;

	/** Set by Resume at a Choice or a Wait: the next walk takes the node up again with this time left, instead of leaving it. */
	float ResumedTimeLeft = -1.0f;

	/** The player is running this play's dialogue, not one before it on the channel. */
	bool bPlaying = false;

	bool bStarted = false;
	bool bFinished = false;

	bool bProxy = false;
	int32 NetPlayId = 0;

	/** Counts the offers of options: on a server the ones made, on a copy the last one shown. */
	int32 NetOfferSerial = 0;

	/** Server time the options' time limit runs out at; 0 without one. */
	double ChoiceEndTime = 0.0;
};

/** Feeds a dialogue graph's lines to its player, one at a time as the walk reaches them. */
UCLASS(NotBlueprintable)
class KZDIALOGUE_API UKzDialogueGraphProvider : public UKzDialogueProvider
{
	GENERATED_BODY()

public:
	TWeakObjectPtr<UKzDialogueGraphInstance> Instance;

	virtual bool HasNext_Implementation() const override;
	virtual FKzDialogueLine Advance_Implementation() override;
	virtual FKzDialogueLine PeekNext_Implementation() const override;
	virtual bool HoldsLine_Implementation() override;
	virtual bool IsWaiting_Implementation() const override;
	virtual FString GetHistoryLabel() const override;
};