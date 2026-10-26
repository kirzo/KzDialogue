// Copyright 2026 kirzo

#include "Graph/KzAsyncPlayDialogueGraph.h"

UKzAsyncPlayDialogueGraph* UKzAsyncPlayDialogueGraph::PlayDialogueGraph(const UObject* WorldContextObject, UKzDialogueGraph* Graph, const FKzDialogueGraphContext& Context, FName Entry)
{
	UKzAsyncPlayDialogueGraph* Action = NewObject<UKzAsyncPlayDialogueGraph>();
	Action->Instance = UKzDialogueGraphInstance::CreateDialogueGraph(WorldContextObject, Graph, Context);
	Action->Entry = Entry;
	Action->RegisterWithGameInstance(WorldContextObject);
	return Action;
}

void UKzAsyncPlayDialogueGraph::Activate()
{
	if (Instance)
	{
		Instance->OnOptionsOffered.AddDynamic(this, &UKzAsyncPlayDialogueGraph::HandleOptions);
		Instance->OnEvent.AddDynamic(this, &UKzAsyncPlayDialogueGraph::HandleEvent);
		Instance->OnEnded.AddDynamic(this, &UKzAsyncPlayDialogueGraph::HandleEnded);
		Started.Broadcast(Instance, FGameplayTag(), NAME_None);
	}

	// A start refused before playing does not end the instance, so it is reported here.
	if ((!Instance || !Instance->Start(Entry)) && !bEnded)
	{
		bEnded = true;
		Cancelled.Broadcast(Instance, FGameplayTag(), NAME_None);
		SetReadyToDestroy();
	}
}

void UKzAsyncPlayDialogueGraph::HandleOptions(UKzDialogueGraphInstance* InInstance, const TArray<FKzDialogueGraphOption>& Options)
{
	OptionsOffered.Broadcast(InInstance, FGameplayTag(), NAME_None);
}

void UKzAsyncPlayDialogueGraph::HandleEvent(UKzDialogueGraphInstance* InInstance, FGameplayTag Event)
{
	EventFired.Broadcast(InInstance, Event, NAME_None);
}

void UKzAsyncPlayDialogueGraph::HandleEnded(UKzDialogueGraphInstance* InInstance, FName Exit, bool bInterrupted)
{
	bEnded = true;
	(bInterrupted ? Cancelled : Finished).Broadcast(InInstance, FGameplayTag(), Exit);
	SetReadyToDestroy();
}