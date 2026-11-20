// Copyright 2026 kirzo

#include "Tasks/ScriptableTask_PlayDialogueGraph.h"
#include "Graph/KzDialogueGraphInstance.h"

void UScriptableTask_PlayDialogueGraph::BeginTask()
{
	FKzDialogueGraphContext Context;
	Context.Instigator = Instigator;
	Context.Owner = Owner;
	Context.Channel = Channel;

	Instance = UKzDialogueGraphInstance::CreateDialogueGraph(this, Graph, Context);
	if (Instance)
	{
		Instance->OnEnded.AddDynamic(this, &UScriptableTask_PlayDialogueGraph::HandleEnded);
	}

	// A start refused before playing never ends the instance, so nothing else would finish the task.
	if (!Instance || !Instance->Start(Entry))
	{
		if (Instance)
		{
			Instance->OnEnded.RemoveAll(this);
			Instance = nullptr;
		}
		Finish();
	}
}

void UScriptableTask_PlayDialogueGraph::StopTask()
{
	if (UKzDialogueGraphInstance* Playing = Instance)
	{
		Instance = nullptr;
		Playing->OnEnded.RemoveAll(this);
		Playing->Stop();
	}
}

void UScriptableTask_PlayDialogueGraph::HandleEnded(UKzDialogueGraphInstance* Ended, FName Exit, bool bInterrupted)
{
	Instance = nullptr;
	Finish();
}