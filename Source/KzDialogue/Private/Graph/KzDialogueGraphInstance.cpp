// Copyright 2026 kirzo

#include "Graph/KzDialogueGraphInstance.h"
#include "Graph/KzDialogueGraph.h"
#include "Graph/KzDialogueGraphNodes.h"
#include "KzDialogue.h"
#include "KzDialogueAsset.h"
#include "KzDialogueMemorySubsystem.h"
#include "KzDialoguePlayer.h"
#include "KzDialogueSubsystem.h"
#include "Engine/World.h"
#include "TimerManager.h"

namespace
{
	const FName DefaultOutput(TEXT("Out"));

	/** Nodes in a row that show nothing before the walk calls it a loop. */
	constexpr int32 MaxInstantSteps = 256;
}

UKzDialogueGraphInstance* UKzDialogueGraphInstance::CreateDialogueGraph(const UObject* WorldContextObject, UKzDialogueGraph* Graph, const FKzDialogueGraphContext& Context)
{
	const UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	UKzDialogueSubsystem* Subsystem = World ? World->GetSubsystem<UKzDialogueSubsystem>() : nullptr;
	if (!Subsystem || !IsValid(Graph))
	{
		return nullptr;
	}

	UKzDialogueGraphInstance* Instance = NewObject<UKzDialogueGraphInstance>(Subsystem);
	Instance->Graph = Graph;
	Instance->Context = Context;
	return Instance;
}

UKzDialogueGraphInstance* UKzDialogueGraphInstance::StartDialogueGraph(const UObject* WorldContextObject, UKzDialogueGraph* Graph, const FKzDialogueGraphContext& Context, FName Entry)
{
	UKzDialogueGraphInstance* Instance = CreateDialogueGraph(WorldContextObject, Graph, Context);
	return Instance && Instance->Start(Entry) ? Instance : nullptr;
}

bool UKzDialogueGraphInstance::Start(FName Entry)
{
	UKzDialogueSubsystem* Subsystem = GetSubsystem();
	if (bStarted || !Subsystem || !IsValid(Graph))
	{
		return false;
	}

	Node = Graph->GetData().FindEntry(Entry);
	if (Node == INDEX_NONE)
	{
		UE_LOG(LogKzDialogue, Warning, TEXT("Dialogue graph '%s' has no entry named '%s'."), *Graph->GetName(), *Entry.ToString());
		return false;
	}
	const FGameplayTag Channel = Context.Channel.IsValid() ? Context.Channel : (Graph->Channel.IsValid() ? Graph->Channel : Subsystem->DefaultChannel);
	Player = Subsystem->GetOrCreatePlayer(Channel);
	if (!Player)
	{
		return false;
	}
	bStarted = true;
	Visit(Graph->GetData().GetNodeGuid(Node));

	const int32 Priority = Context.Priority != UKzDialogueSubsystem::InheritPriority ? Context.Priority : Graph->Priority;
	const EKzDialogueAdvanceMode AdvanceMode = Context.AdvanceMode != EKzDialogueAdvanceMode::Inherit ? Context.AdvanceMode : Graph->AdvanceMode;

	UKzDialogueGraphProvider* GraphProvider = NewObject<UKzDialogueGraphProvider>(this);
	GraphProvider->Instance = this;
	Provider = GraphProvider;

	// Bound before playing: the play may start, and even end, inside the call; the guard tells this play's dialogue from the one it replaces.
	Player->OnDialogueStarted.AddDynamic(this, &UKzDialogueGraphInstance::HandleDialogueStarted);
	Player->OnDialogueFinished.AddDynamic(this, &UKzDialogueGraphInstance::HandleDialogueFinished);
	Subsystem->AddGraphInstance(this);
	Subsystem->OnDialogueGraphStarted.Broadcast(this);

	if (!Subsystem->Play(Provider, Channel, Priority, AdvanceMode, Graph->bInterruptible))
	{
		Finish(true);
		return false;
	}
	if (Subsystem->GetLastPlayResult() == EKzDialoguePlayResult::Queued)
	{
		QueuedRequestId = Subsystem->GetLastQueuedRequestId();
		Subsystem->OnQueuedRequestDropped.AddDynamic(this, &UKzDialogueGraphInstance::HandleQueuedRequestDropped);
	}
	return true;
}

bool UKzDialogueGraphInstance::SelectOption(int32 Index)
{
	const FKzDialogueGraphOption* Offered = Options.FindByPredicate([Index](const FKzDialogueGraphOption& Option) { return Option.Index == Index; });
	const FKzDialogueGraphNode_Choice* Choice = Graph ? Graph->GetData().GetNode<FKzDialogueGraphNode_Choice>(Node) : nullptr;
	if (Step != EStep::Choosing || !Offered || !Offered->bAvailable || !Choice || !Choice->Options.IsValidIndex(Index))
	{
		return false;
	}

	const FKzDialogueChoiceOption& Option = Choice->Options[Index];
	Visit(Option.Id);
	WithdrawOptions();
	Leave(FKzDialogueGraphNode_Choice::GetOptionOutput(Option));
	Proceed();
	return true;
}

void UKzDialogueGraphInstance::Next()
{
	if (Player && bPlaying)
	{
		Player->Next();
	}
}

void UKzDialogueGraphInstance::Continue(FGameplayTag Tag)
{
	const FKzDialogueGraphNode_Wait* Wait = Step == EStep::Waiting ? Graph->GetData().GetNode<FKzDialogueGraphNode_Wait>(Node) : nullptr;
	if (Wait && (!Wait->ContinueTag.IsValid() || Wait->ContinueTag == Tag))
	{
		HandleWaitElapsed();
	}
}

void UKzDialogueGraphInstance::Stop()
{
	if (!IsRunning())
	{
		return;
	}

	if (bPlaying && Player)
	{
		Player->Stop();
	}
	else if (UKzDialogueSubsystem* Subsystem = GetSubsystem(); Subsystem && QueuedRequestId.IsValid())
	{
		Subsystem->CancelQueuedRequest(QueuedRequestId);
	}
	else
	{
		Finish(true);
	}
}

bool UKzDialogueGraphInstance::WasVisited(const FGuid& Id, bool bEver) const
{
	return bEver ? GetVisitCount(Id) > 0 : Visited.Contains(Id);
}

int32 UKzDialogueGraphInstance::GetVisitCount(const FGuid& Id) const
{
	const UKzDialogueMemorySubsystem* Memory = GetMemory();
	return Memory && Graph ? Memory->GetGraphVisitCount(Graph->GraphId, Id) : (Visited.Contains(Id) ? 1 : 0);
}

UKzDialogueMemorySubsystem* UKzDialogueGraphInstance::GetMemory() const
{
	return UKzDialogueMemorySubsystem::Get(this);
}

UKzDialogueSubsystem* UKzDialogueGraphInstance::GetSubsystem() const
{
	return Cast<UKzDialogueSubsystem>(GetOuter());
}

UWorld* UKzDialogueGraphInstance::GetWorld() const
{
	return GetOuter() ? GetOuter()->GetWorld() : nullptr;
}

bool UKzDialogueGraphInstance::HasLine()
{
	if (Step == EStep::Passed)
	{
		Leave(DefaultOutput);
	}
	return Step == EStep::Ready;
}

FKzDialogueLine UKzDialogueGraphInstance::TakeLine()
{
	if (!HasLine())
	{
		return FKzDialogueLine();
	}
	Step = EStep::Passed;
	return MoveTemp(PendingLine);
}

FKzDialogueLine UKzDialogueGraphInstance::PeekLine() const
{
	if (Step == EStep::Ready)
	{
		return PendingLine;
	}
	if (Step != EStep::Passed || !Graph)
	{
		return FKzDialogueLine();
	}

	// Only what is known without running anything: the next node is a plain line, with nothing to check on the way.
	const FKzGraphData& Data = Graph->GetData();
	const int32 Output = Data.FindOutput(Node, DefaultOutput);
	const TConstArrayView<FKzGraphLink> Links = Output != INDEX_NONE ? Data.GetLinks(Node, Output) : TConstArrayView<FKzGraphLink>();
	const FKzDialogueGraphNode_Line* Next = Links.Num() > 0 ? Data.GetNode<FKzDialogueGraphNode_Line>(Links[0].Node) : nullptr;
	const UKzDialogueAsset* Asset = Next && Next->OnlyIf.IsEmpty() ? Next->Line.Asset.Get() : nullptr;
	const FKzDialogueLine* Line = Asset ? Asset->FindLine(Next->Line.LineId) : nullptr;
	return Line ? *Line : FKzDialogueLine();
}

bool UKzDialogueGraphInstance::HoldsLine()
{
	HasLine();
	return Step == EStep::Choosing;
}

void UKzDialogueGraphInstance::Leave(FName Output)
{
	UKzDialogueSubsystem* Subsystem = GetSubsystem();
	const FKzGraphData& Data = Graph->GetData();
	int32 Target = FindTarget(Node, Output);

	for (int32 Steps = 0; ; ++Steps)
	{
		if (Target == INDEX_NONE)
		{
			EndWalk(NAME_None);
			return;
		}
		if (Steps >= MaxInstantSteps)
		{
			UE_LOG(LogKzDialogue, Warning, TEXT("Dialogue graph '%s' went through %d nodes without a line, a choice or a wait: a loop that shows nothing. Ending it."), *Graph->GetName(), MaxInstantSteps);
			EndWalk(NAME_None);
			return;
		}

		Node = Target;
		Visit(Data.GetNodeGuid(Node));
		Target = INDEX_NONE;

		if (const FKzDialogueGraphNode_Line* Line = Data.GetNode<FKzDialogueGraphNode_Line>(Node))
		{
			if (Subsystem && Subsystem->ResolveLineRef(Line->Line, PendingLine))
			{
				Step = EStep::Ready;
				return;
			}
			UE_LOG(LogKzDialogue, Warning, TEXT("Dialogue graph '%s': a Line node resolves to nothing; skipping it."), *Graph->GetName());
			Target = FindTarget(Node, DefaultOutput);
		}
		else if (const FKzDialogueGraphNode_Choice* Choice = Data.GetNode<FKzDialogueGraphNode_Choice>(Node))
		{
			if (OfferOptions(*Choice))
			{
				return;
			}
			// Nothing to offer: the way out is the one for no answer.
			Target = FindTarget(Node, FKzDialogueGraphNode_Choice::TimeoutOutput);
		}
		else if (const FKzDialogueGraphNode_Branch* Branch = Data.GetNode<FKzDialogueGraphNode_Branch>(Node))
		{
			const FKzDialogueBranch* Taken = Branch->Branches.FindByPredicate([this](const FKzDialogueBranch& Each) { return Each.Condition.Evaluate({ *this, Node }); });
			Target = FindTarget(Node, Taken ? FKzDialogueGraphNode_Branch::GetBranchOutput(*Taken) : FKzDialogueGraphNode_Branch::ElseOutput);
		}
		else if (const FKzDialogueGraphNode_Random* Random = Data.GetNode<FKzDialogueGraphNode_Random>(Node))
		{
			float Total = 0.0f;
			for (const FKzDialogueRandomOutput& Each : Random->Outputs)
			{
				Total += FMath::Max(Each.Weight, 0.0f);
			}
			float Roll = FMath::FRand() * Total;
			for (const FKzDialogueRandomOutput& Each : Random->Outputs)
			{
				Roll -= FMath::Max(Each.Weight, 0.0f);
				if (Each.Weight > 0.0f && Roll <= 0.0f)
				{
					Target = FindTarget(Node, FKzDialogueGraphNode_Random::GetOutput(Each));
					break;
				}
			}
		}
		else if (const FKzDialogueGraphNode_Event* Event = Data.GetNode<FKzDialogueGraphNode_Event>(Node))
		{
			const int32 EventNode = Node;
			OnEvent.Broadcast(this, Event->Event);
			if (Subsystem)
			{
				Subsystem->OnDialogueGraphEvent.Broadcast(this, Event->Event);
			}
			// A listener may have stopped the play.
			if (bFinished || Step == EStep::Ended)
			{
				return;
			}
			Target = FindTarget(EventNode, DefaultOutput);
		}
		else if (const FKzDialogueGraphNode_Action* Action = Data.GetNode<FKzDialogueGraphNode_Action>(Node))
		{
			const int32 ActionNode = Node;
			for (const TInstancedStruct<FKzDialogueAction>& Each : Action->Actions)
			{
				if (Each.IsValid())
				{
					Each.Get().Execute(*this);
				}
			}
			if (bFinished || Step == EStep::Ended)
			{
				return;
			}
			Target = FindTarget(ActionNode, DefaultOutput);
		}
		else if (const FKzDialogueGraphNode_Wait* Wait = Data.GetNode<FKzDialogueGraphNode_Wait>(Node))
		{
			Step = EStep::Waiting;
			if (Wait->Seconds > 0.0f && GetWorld())
			{
				GetWorld()->GetTimerManager().SetTimer(StepTimer, FTimerDelegate::CreateUObject(this, &UKzDialogueGraphInstance::HandleWaitElapsed), Wait->Seconds, false);
			}
			return;
		}
		else if (const FKzDialogueGraphNode_GoTo* GoTo = Data.GetNode<FKzDialogueGraphNode_GoTo>(Node))
		{
			Target = Data.FindEntry(GoTo->Entry.Name);
		}
		else if (const FKzDialogueGraphNode_Exit* Exit = Data.GetNode<FKzDialogueGraphNode_Exit>(Node))
		{
			EndWalk(Exit->Name);
			return;
		}
		else
		{
			// An Entry reached through Go To, or any node that only passes on.
			Target = FindTarget(Node, DefaultOutput);
		}
	}
}

int32 UKzDialogueGraphInstance::FindTarget(int32 FromNode, FName Output) const
{
	const FKzGraphData& Data = Graph->GetData();
	const int32 OutputIndex = Data.FindOutput(FromNode, Output);
	if (OutputIndex == INDEX_NONE)
	{
		return INDEX_NONE;
	}

	for (const FKzGraphLink& Link : Data.GetLinks(FromNode, OutputIndex))
	{
		const FKzDialogueGraphStep* Target = Data.GetNode<FKzDialogueGraphStep>(Link.Node);
		if (!Target || Target->OnlyIf.Evaluate({ *this, Link.Node }))
		{
			return Link.Node;
		}
	}
	return INDEX_NONE;
}

bool UKzDialogueGraphInstance::OfferOptions(const FKzDialogueGraphNode_Choice& Choice)
{
	UKzDialogueSubsystem* Subsystem = GetSubsystem();
	Options.Reset();
	bool bAnyAvailable = false;

	for (int32 Index = 0; Index < Choice.Options.Num(); ++Index)
	{
		const FKzDialogueChoiceOption& Option = Choice.Options[Index];
		const bool bAvailable = Option.Condition.Evaluate({ *this, Node });
		if (!bAvailable && !Option.bShowWhenUnavailable)
		{
			continue;
		}

		FKzDialogueGraphOption& Offered = Options.AddDefaulted_GetRef();
		Offered.Index = Index;
		Offered.bAvailable = bAvailable;
		if (Subsystem)
		{
			Subsystem->ResolveLineRef(Option.Line, Offered.Line);
		}
		bAnyAvailable |= bAvailable;
	}

	if (!bAnyAvailable)
	{
		Options.Reset();
		return false;
	}

	Step = EStep::Choosing;
	if (Choice.TimeLimit > 0.0f && GetWorld())
	{
		GetWorld()->GetTimerManager().SetTimer(StepTimer, FTimerDelegate::CreateUObject(this, &UKzDialogueGraphInstance::HandleChoiceTimeout), Choice.TimeLimit, false);
	}
	OnOptionsOffered.Broadcast(this, Options);
	return true;
}

void UKzDialogueGraphInstance::WithdrawOptions()
{
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(StepTimer);
	}
	if (!Options.IsEmpty())
	{
		Options.Reset();
		OnOptionsWithdrawn.Broadcast(this);
	}
}

void UKzDialogueGraphInstance::Visit(const FGuid& Id)
{
	Visited.Add(Id);
	if (UKzDialogueMemorySubsystem* Memory = GetMemory())
	{
		Memory->RecordGraphVisit(Graph->GraphId, Id);
	}
}

void UKzDialogueGraphInstance::EndWalk(FName Exit)
{
	ExitName = Exit;
	Step = EStep::Ended;
	WithdrawOptions();
}

void UKzDialogueGraphInstance::Proceed()
{
	if (!Player || !bPlaying)
	{
		return;
	}
	if (Player->IsHoldingLine())
	{
		Player->ReleaseHeldLine();
	}
	else if (Player->GetState() == EKzDialogueState::Waiting)
	{
		Player->ResumeWaiting();
	}
}

void UKzDialogueGraphInstance::HandleChoiceTimeout()
{
	if (Step != EStep::Choosing)
	{
		return;
	}
	WithdrawOptions();
	Leave(FKzDialogueGraphNode_Choice::TimeoutOutput);
	Proceed();
}

void UKzDialogueGraphInstance::HandleWaitElapsed()
{
	if (Step != EStep::Waiting)
	{
		return;
	}
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(StepTimer);
	}
	Leave(DefaultOutput);
	Proceed();
}

void UKzDialogueGraphInstance::HandleDialogueStarted(UKzDialoguePlayer* InPlayer)
{
	if (InPlayer && InPlayer->GetProvider() == Provider)
	{
		bPlaying = true;
		QueuedRequestId.Invalidate();
	}
}

void UKzDialogueGraphInstance::HandleDialogueFinished(UKzDialoguePlayer* InPlayer, EKzDialogueFinishReason Reason)
{
	if (bPlaying)
	{
		Finish(Reason == EKzDialogueFinishReason::Interrupted || Reason == EKzDialogueFinishReason::Aborted || Step != EStep::Ended);
	}
}

void UKzDialogueGraphInstance::HandleQueuedRequestDropped(FGameplayTag InChannel, FGuid RequestId)
{
	if (RequestId == QueuedRequestId)
	{
		Finish(true);
	}
}

void UKzDialogueGraphInstance::Finish(bool bInterrupted)
{
	if (bFinished)
	{
		return;
	}
	bFinished = true;
	bPlaying = false;
	WithdrawOptions();

	if (Player)
	{
		Player->OnDialogueStarted.RemoveAll(this);
		Player->OnDialogueFinished.RemoveAll(this);
	}
	if (UKzDialogueSubsystem* Subsystem = GetSubsystem())
	{
		Subsystem->OnQueuedRequestDropped.RemoveAll(this);
		Subsystem->RemoveGraphInstance(this);
	}

	OnEnded.Broadcast(this, bInterrupted ? NAME_None : ExitName, bInterrupted);
}

bool UKzDialogueGraphProvider::HasNext_Implementation() const
{
	UKzDialogueGraphInstance* Owner = Instance.Get();
	return Owner && Owner->HasLine();
}

FKzDialogueLine UKzDialogueGraphProvider::Advance_Implementation()
{
	UKzDialogueGraphInstance* Owner = Instance.Get();
	return Owner ? Owner->TakeLine() : FKzDialogueLine();
}

FKzDialogueLine UKzDialogueGraphProvider::PeekNext_Implementation() const
{
	const UKzDialogueGraphInstance* Owner = Instance.Get();
	return Owner ? Owner->PeekLine() : FKzDialogueLine();
}

bool UKzDialogueGraphProvider::HoldsLine_Implementation()
{
	UKzDialogueGraphInstance* Owner = Instance.Get();
	return Owner && Owner->HoldsLine();
}

bool UKzDialogueGraphProvider::IsWaiting_Implementation() const
{
	const UKzDialogueGraphInstance* Owner = Instance.Get();
	return Owner && Owner->IsWaiting();
}

FString UKzDialogueGraphProvider::GetHistoryLabel() const
{
	const UKzDialogueGraphInstance* Owner = Instance.Get();
	return Owner && Owner->GetGraph() ? FString::Printf(TEXT("graph %s"), *Owner->GetGraph()->GetName()) : Super::GetHistoryLabel();
}