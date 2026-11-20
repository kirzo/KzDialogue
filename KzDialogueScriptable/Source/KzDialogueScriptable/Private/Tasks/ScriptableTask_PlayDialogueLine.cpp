// Copyright 2026 kirzo

#include "Tasks/ScriptableTask_PlayDialogueLine.h"
#include "KzDialogueAsyncActions.h"

void UScriptableTask_PlayDialogueLine::BeginTask()
{
	Action = UKzAsyncPlayDialogueLine::PlayDialogueLine(this, Line, Channel, Priority, Owner);
	Action->Finished.AddDynamic(this, &UScriptableTask_PlayDialogueLine::HandleEnded);
	Action->Cancelled.AddDynamic(this, &UScriptableTask_PlayDialogueLine::HandleEnded);
	Action->Activate();
}

void UScriptableTask_PlayDialogueLine::StopTask()
{
	if (UKzAsyncPlayDialogueLine* Playing = Action)
	{
		Action = nullptr;
		Playing->Finished.RemoveAll(this);
		Playing->Cancelled.RemoveAll(this);
		Playing->Stop();
	}
}

void UScriptableTask_PlayDialogueLine::HandleEnded(UKzDialoguePlayer* Player, const FKzDialogueLine& EndedLine)
{
	Action = nullptr;
	Finish();
}