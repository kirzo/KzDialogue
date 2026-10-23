// Copyright 2026 kirzo

#include "KzDialogueAssetSession.h"
#include "KzDialogueSubsystem.h"
#include "KzDialoguePlayer.h"
#include "KzDialogueAsset.h"

void UKzDialogueAssetSession::Start(UKzDialogueSubsystem* InSubsystem, UKzDialogueAsset* InAsset, FGameplayTag ExplicitChannel, EKzDialogueAdvanceMode InAdvanceMode)
{
	TArray<FGuid> EntryIds;
	if (InAsset)
	{
		EntryIds.Reserve(InAsset->Lines.Num());
		for (const FKzDialogueLine& Line : InAsset->Lines)
		{
			EntryIds.Add(Line.LineId);
		}
	}
	StartEntries(InSubsystem, InAsset, EntryIds, ExplicitChannel, UKzDialogueSubsystem::InheritPriority, InAdvanceMode);
}

void UKzDialogueAssetSession::StartEntries(UKzDialogueSubsystem* InSubsystem, UKzDialogueAsset* InAsset, const TArray<FGuid>& EntryIds, FGameplayTag ExplicitChannel, int32 InPriority, EKzDialogueAdvanceMode InAdvanceMode)
{
	Subsystem = InSubsystem;
	Asset = InAsset;
	Priority = InPriority;
	ResolvedAdvanceMode = (InSubsystem && InAsset) ? InSubsystem->ResolveAdvanceMode(InAdvanceMode, InAsset) : EKzDialogueAdvanceMode::Automatic;

	// Split the entries into maximal consecutive runs sharing a resolved channel. A valid explicit
	// channel short-circuits per-entry resolution, so every entry lands on it -> a single run (identical
	// to the old single-session play).
	if (Asset && Subsystem)
	{
		FGameplayTag PrevChannel;
		bool bFirst = true;
		for (const FGuid& EntryId : EntryIds)
		{
			const FGameplayTag Ch = Subsystem->ResolveChannelForEntry(ExplicitChannel, Asset, EntryId);
			if (bFirst || Ch != PrevChannel)
			{
				FRun& NewRun = Runs.AddDefaulted_GetRef();
				NewRun.Channel = Ch;
				PrevChannel = Ch;
				bFirst = false;
			}
			Runs.Last().LineIds.Add(EntryId);
		}
	}

	if (Runs.Num() == 0)
	{
		// Empty asset / nothing to play: resolve now. Callers see IsPlaying()==false and GetFinishReason().
		FinishSession(EKzDialogueFinishReason::Completed);
		return;
	}

	bActive = true;
	LaunchRun(0);
}

void UKzDialogueAssetSession::LaunchRun(int32 RunIndex)
{
	if (!Subsystem || !Runs.IsValidIndex(RunIndex))
	{
		FinishSession(EKzDialogueFinishReason::Aborted);
		return;
	}

	CurrentRunIndex = RunIndex;
	const FRun& Run = Runs[RunIndex];
	CurrentChannel = Run.Channel;

	UKzDialoguePlayer* Player = Subsystem->GetOrCreatePlayer(Run.Channel);
	if (!Player)
	{
		FinishSession(EKzDialogueFinishReason::Aborted);
		return;
	}

	// Bound before launching, so a first line presented inside the play call is forwarded. The finish of the
	// content this run preempts also lands inside the call, before our OnDialogueStarted, and is ignored.
	CurrentPlayer = Player;
	bRunStarted = false;
	Player->OnDialogueStarted.AddDynamic(this, &UKzDialogueAssetSession::HandleRunStarted);
	Player->OnDialogueFinished.AddDynamic(this, &UKzDialogueAssetSession::HandleRunFinished);
	Player->OnLineStarted.AddDynamic(this, &UKzDialogueAssetSession::HandleLineStarted);
	Player->OnLineFinished.AddDynamic(this, &UKzDialogueAssetSession::HandleLineFinished);

	// The run's channel goes EXPLICITLY (playback channel == grouping channel), with the caller's priority
	// (InheritPriority by default -> the asset's Priority hint).
	if (!Subsystem->PlayAssetLineList(Asset, Run.LineIds, Run.Channel, Priority, ResolvedAdvanceMode))
	{
		// Refused (a higher-priority dialogue holds this run's channel) or nothing resolved: terminal,
		// so the session can't stall waiting for a finish that will never come.
		FinishSession(EKzDialogueFinishReason::Interrupted);
	}
}

void UKzDialogueAssetSession::UnbindPlayer(UKzDialoguePlayer* Player)
{
	if (!Player) { return; }

	Player->OnDialogueStarted.RemoveDynamic(this, &UKzDialogueAssetSession::HandleRunStarted);
	Player->OnDialogueFinished.RemoveDynamic(this, &UKzDialogueAssetSession::HandleRunFinished);
	Player->OnLineStarted.RemoveDynamic(this, &UKzDialogueAssetSession::HandleLineStarted);
	Player->OnLineFinished.RemoveDynamic(this, &UKzDialogueAssetSession::HandleLineFinished);
}

void UKzDialogueAssetSession::HandleRunStarted(UKzDialoguePlayer* Player)
{
	bRunStarted = true;
}

void UKzDialogueAssetSession::HandleLineStarted(UKzDialoguePlayer* Player, const FKzDialogueLine& Line)
{
	OnLineStarted.Broadcast(Player, Line);
}

void UKzDialogueAssetSession::HandleLineFinished(UKzDialoguePlayer* Player, const FKzDialogueLine& Line)
{
	OnLineFinished.Broadcast(Player, Line);
}

void UKzDialogueAssetSession::HandleRunFinished(UKzDialoguePlayer* Player, EKzDialogueFinishReason Reason)
{
	if (!bRunStarted) { return; }

	UnbindPlayer(Player);

	// Chain only when THIS run reached its natural end on OUR player and the world is not tearing down.
	const bool bChain = (Reason == EKzDialogueFinishReason::Completed)
		&& (Player == CurrentPlayer)
		&& Subsystem && !Subsystem->IsDeinitializing();

	CurrentPlayer = nullptr;

	if (!bChain)
	{
		FinishSession(Reason);
		return;
	}

	if (Runs.IsValidIndex(CurrentRunIndex + 1))
	{
		LaunchRun(CurrentRunIndex + 1);
	}
	else
	{
		FinishSession(EKzDialogueFinishReason::Completed);
	}
}

void UKzDialogueAssetSession::Stop()
{
	if (!bActive || bFinished) { return; }

	if (CurrentPlayer)
	{
		// Finishes as Stopped once the exit animations end (synchronously without views): HandleRunFinished resolves the session.
		CurrentPlayer->Stop();
	}
	else
	{
		FinishSession(EKzDialogueFinishReason::Stopped);
	}
}

void UKzDialogueAssetSession::Interrupt()
{
	if (!bActive || bFinished) { return; }

	if (CurrentPlayer)
	{
		// Synchronous: fires HandleRunFinished(Interrupted) inside this call -> FinishSession(Interrupted).
		CurrentPlayer->Interrupt();
	}
	else
	{
		FinishSession(EKzDialogueFinishReason::Interrupted);
	}
}

void UKzDialogueAssetSession::FinishSession(EKzDialogueFinishReason Reason)
{
	if (bFinished) { return; }
	bFinished = true;
	bActive = false;
	FinishReason = Reason;

	UKzDialoguePlayer* LastPlayer = CurrentPlayer;
	CurrentPlayer = nullptr;
	UnbindPlayer(LastPlayer);
	UKzDialogueSubsystem* Sub = Subsystem;

	// Broadcast while still alive, then release LAST and touch nothing after (mirrors FinishWithReason).
	OnDialogueFinished.Broadcast(LastPlayer, Reason);

	if (Sub)
	{
		Sub->ReleaseAssetSession(this);
	}
}