// Copyright 2026 kirzo

#include "KzDialogueAssetSession.h"
#include "KzDialogueSubsystem.h"
#include "KzDialoguePlayer.h"
#include "KzDialogueAsset.h"

void UKzDialogueAssetSession::Start(UKzDialogueSubsystem* InSubsystem, UKzDialogueAsset* InAsset, FGameplayTag ExplicitChannel, bool bInStartImmediately, EKzDialogueAdvanceMode InAdvanceMode)
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
	StartEntries(InSubsystem, InAsset, EntryIds, ExplicitChannel, UKzDialogueSubsystem::InheritPriority, bInStartImmediately, InAdvanceMode);
}

void UKzDialogueAssetSession::StartEntries(UKzDialogueSubsystem* InSubsystem, UKzDialogueAsset* InAsset, const TArray<FGuid>& EntryIds, FGameplayTag ExplicitChannel, int32 InPriority, bool bInStartImmediately, EKzDialogueAdvanceMode InAdvanceMode)
{
	Subsystem = InSubsystem;
	Asset = InAsset;
	Priority = InPriority;
	bStartImmediately = bInStartImmediately;
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

	// Pass the run's channel EXPLICITLY (bypasses the internal first-entry resolution, so playback channel
	// == grouping channel) and the caller's priority (InheritPriority by default -> the asset's Priority
	// hint). Chained runs start immediately; run 0 honours bStartImmediately.
	const bool bStart = (RunIndex == 0) ? bStartImmediately : true;
	UKzDialoguePlayer* Player = Subsystem->PlayAssetLineList(Asset, Run.LineIds, Run.Channel, Priority, bStart, ResolvedAdvanceMode);

	if (!Player)
	{
		// Refused (a higher-priority dialogue holds this run's channel) or nothing resolved: terminal,
		// so the session can't stall waiting for a finish that will never come.
		FinishSession(EKzDialogueFinishReason::Interrupted);
		return;
	}

	CurrentPlayer = Player;

	// Bind AFTER launching: preempting external content broadcasts OnDialogueFinished during the play call.
	Player->OnDialogueFinished.AddDynamic(this, &UKzDialogueAssetSession::HandleRunFinished);
}

void UKzDialogueAssetSession::HandleRunFinished(UKzDialoguePlayer* Player, EKzDialogueFinishReason Reason)
{
	if (Player)
	{
		Player->OnDialogueFinished.RemoveDynamic(this, &UKzDialogueAssetSession::HandleRunFinished);
	}

	// Chain the next run only if THIS run reached its natural end, on OUR current player, and we are not
	// being torn down or in world teardown. Stop()/StopChannel/Deinitialize all emit Completed too, so
	// player identity + the teardown flag are what separate "ran to its end" from "someone stopped it".
	const bool bChain = (Reason == EKzDialogueFinishReason::Completed)
		&& (Player == CurrentPlayer)
		&& !bTearingDown
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

	bTearingDown = true;
	if (CurrentPlayer)
	{
		// Player Stop() emits Completed (sync without a view, async with one). We keep the binding: the
		// teardown flag makes HandleRunFinished resolve the session instead of chaining when it fires.
		CurrentPlayer->Stop();
	}
	else
	{
		FinishSession(EKzDialogueFinishReason::Completed);
	}
}

void UKzDialogueAssetSession::Interrupt()
{
	if (!bActive || bFinished) { return; }

	bTearingDown = true;
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
	UKzDialogueSubsystem* Sub = Subsystem;

	// Broadcast while still alive, then release LAST and touch nothing after (mirrors FinishWithReason).
	OnDialogueFinished.Broadcast(LastPlayer, Reason);

	if (Sub)
	{
		Sub->ReleaseAssetSession(this);
	}
}
