// Copyright 2026 kirzo

#include "Tasks/ScriptableTask_PlayDialogueAsset.h"
#include "KzDialogueAsyncActions.h"

void UScriptableTask_PlayDialogueAsset::BeginTask()
{
	Action = UKzAsyncPlayDialogueAsset::PlayDialogueAsset(this, Asset, Channel, AdvanceMode, Owner);
	Action->Finished.AddDynamic(this, &UScriptableTask_PlayDialogueAsset::HandleEnded);
	Action->Cancelled.AddDynamic(this, &UScriptableTask_PlayDialogueAsset::HandleEnded);
	Action->Activate();
}

void UScriptableTask_PlayDialogueAsset::StopTask()
{
	if (UKzAsyncPlayDialogueAsset* Playing = Action)
	{
		Action = nullptr;
		Playing->Finished.RemoveAll(this);
		Playing->Cancelled.RemoveAll(this);
		Playing->Stop();
	}
}

void UScriptableTask_PlayDialogueAsset::HandleEnded(UKzDialoguePlayer* Player, const FKzDialogueLine& EndedLine)
{
	Action = nullptr;
	Finish();
}