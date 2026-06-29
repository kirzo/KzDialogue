// Copyright 2026 kirzo

#include "KzBarkComponent.h"
#include "KzDialogueSubsystem.h"
#include "KzDialoguePlayer.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogKzBark, Log, All);

UKzBarkComponent::UKzBarkComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UKzBarkComponent::BeginPlay()
{
	Super::BeginPlay();
	if (bAutoStart)
	{
		StartBarking();
	}
}

void UKzBarkComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopBarking();
	Super::EndPlay(EndPlayReason);
}

UKzDialogueSubsystem* UKzBarkComponent::GetSubsystem() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetSubsystem<UKzDialogueSubsystem>() : nullptr;
}

void UKzBarkComponent::StartBarking()
{
	if (bBarking)
	{
		return;
	}

	if (!Channel.IsValid() || !Bark.IsValid() || !GetSubsystem())
	{
		UE_LOG(LogKzBark, Warning, TEXT("UKzBarkComponent on %s: can't start — needs a valid Channel, Bark and dialogue subsystem."), *GetNameSafe(GetOwner()));
		return;
	}

	bBarking = true;
	bPaused = false;
	bBarkInFlight = false;
	BindChannelPlayer();

	if (bFirstBarkImmediate)
	{
		PlayBark();
	}
	else
	{
		ScheduleNextBark();
	}
}

void UKzBarkComponent::StopBarking()
{
	const bool bWasActive = bBarking || bBarkInFlight;

	bBarking = false;
	bPaused = false;
	bBarkInFlight = false;

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(BarkTimer);
	}

	UnbindChannelPlayer();

	// Cut a bark that's currently playing. Only when we were actually running, and only if the subsystem
	// is still around (it may be gone during teardown).
	if (bWasActive)
	{
		if (UKzDialogueSubsystem* Subsystem = GetSubsystem())
		{
			Subsystem->StopChannel(Channel);
		}
	}
}

void UKzBarkComponent::PauseBarking()
{
	if (!bBarking || bPaused)
	{
		return;
	}
	bPaused = true;

	// Mid-wait: the timer pauses with its remaining time. Mid-bark: no timer to pause, and
	// HandleBarkFinished holds off rescheduling while paused.
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().PauseTimer(BarkTimer);
	}
}

void UKzBarkComponent::ResumeBarking()
{
	if (!bBarking || !bPaused)
	{
		return;
	}
	bPaused = false;

	UWorld* World = GetWorld();
	if (World && World->GetTimerManager().IsTimerPaused(BarkTimer))
	{
		World->GetTimerManager().UnPauseTimer(BarkTimer); // resume the interrupted wait
	}
	else if (!bBarkInFlight)
	{
		ScheduleNextBark(); // the bark finished during the pause — restart the cadence
	}
	// else: a bark is still playing; its finish reschedules now that we're unpaused.
}

void UKzBarkComponent::BarkNow()
{
	if (bBarkInFlight || !Channel.IsValid() || !Bark.IsValid() || !GetSubsystem())
	{
		return;
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(BarkTimer); // cancel any pending wait
	}
	BindChannelPlayer(); // so a one-off bark (loop off) still routes its finish through us
	PlayBark();
}

void UKzBarkComponent::ScheduleNextBark()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const float Delay = FMath::Max(0.01f, FMath::FRandRange(FMath::Min(MinInterval, MaxInterval), FMath::Max(MinInterval, MaxInterval)));
	World->GetTimerManager().SetTimer(BarkTimer, this, &UKzBarkComponent::PlayBark, Delay, false);
}

void UKzBarkComponent::PlayBark()
{
	if (bBarkInFlight)
	{
		return;
	}

	UKzDialogueSubsystem* Subsystem = GetSubsystem();
	if (!Subsystem)
	{
		return;
	}

	const TArray<FKzDialogueLineRef> Refs = { Bark };
	UKzDialoguePlayer* Player = Subsystem->PlayLineRefs(Refs, Channel, Priority, true);
	if (!Player)
	{
		// Rejected (priority / cooldown / unresolvable) — try again after another interval.
		ScheduleNextBark();
		return;
	}

	// Set in-flight only AFTER the play request: launching may interrupt prior content on the channel,
	// which fires OnDialogueFinished synchronously here — we must ignore that, not treat it as our bark.
	bBarkInFlight = true;
	OnBarkPlayed.Broadcast(Player);
	// No scheduling here: HandleBarkFinished reschedules when this bark ends.
}

void UKzBarkComponent::HandleBarkFinished(UKzDialoguePlayer* Player, EKzDialogueFinishReason Reason)
{
	if (!bBarkInFlight)
	{
		return; // a finish we didn't initiate (e.g. the interrupt of prior content during our launch)
	}
	bBarkInFlight = false;
	OnBarkFinished.Broadcast(Reason);

	if (!bBarking || bPaused)
	{
		return; // stopped, or paused (resume will reschedule)
	}
	ScheduleNextBark(); // reschedule on ANY end reason — the loop survives cancel / interrupt
}

void UKzBarkComponent::BindChannelPlayer()
{
	UKzDialogueSubsystem* Subsystem = GetSubsystem();
	if (!Subsystem || !Channel.IsValid())
	{
		return;
	}
	UKzDialoguePlayer* Player = Subsystem->GetOrCreatePlayer(Channel);
	if (!Player || Player == BoundPlayer.Get())
	{
		return;
	}
	UnbindChannelPlayer();
	Player->OnDialogueFinished.AddDynamic(this, &UKzBarkComponent::HandleBarkFinished);
	BoundPlayer = Player;
}

void UKzBarkComponent::UnbindChannelPlayer()
{
	if (UKzDialoguePlayer* Player = BoundPlayer.Get())
	{
		Player->OnDialogueFinished.RemoveDynamic(this, &UKzBarkComponent::HandleBarkFinished);
	}
	BoundPlayer = nullptr;
}