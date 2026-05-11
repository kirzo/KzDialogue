// Copyright 2026 kirzo

#include "KzDialoguePlayer.h"
#include "KzDialogueProvider.h"

#include "KzDialogueAsset.h"

#include "Sound/SoundWave.h"
#include "Components/AudioComponent.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"

UKzDialoguePlayer::UKzDialoguePlayer()
{
}

UWorld* UKzDialoguePlayer::GetWorld() const
{
	// Route GetWorld through the outer chain. Subsystem-created players are outered to
	// the world, so this resolves correctly without any special handling.
	return GetOuter() ? GetOuter()->GetWorld() : nullptr;
}

void UKzDialoguePlayer::BeginDestroy()
{
	StopLineAudio(0.0f);
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(LineTimerHandle);
	}
	Super::BeginDestroy();
}

// ---------------------------------------------------------------------------------------
// Public commands
// ---------------------------------------------------------------------------------------

void UKzDialoguePlayer::Play(UKzDialogueProvider* InProvider)
{
	SetProvider(InProvider);
	StartDialogue();
}

void UKzDialoguePlayer::SetProvider(UKzDialogueProvider* InProvider)
{
	if (!IsValid(InProvider)) return;

	// Cleanly cancel anything currently playing before starting fresh.
	if (IsPlaying())
	{
		Abort();
	}

	Provider = InProvider;
	Provider->Reset();
}

void UKzDialoguePlayer::StartDialogue()
{
	// Only start if we have a provider and we are currently Idle
	if (!IsValid(Provider) || State != EKzDialogueState::Idle)
	{
		return;
	}

	OnDialogueStarted.Broadcast(this);
	Enter_Entering();
}

void UKzDialoguePlayer::Pause()
{
	if (State == EKzDialogueState::Idle || State == EKzDialogueState::Paused)
	{
		return;
	}

	StateBeforePause = State;
	State = EKzDialogueState::Paused;

	// Capture the time remaining on the line timer so we can resume cleanly.
	if (UWorld* World = GetWorld())
	{
		FTimerManager& TM = World->GetTimerManager();
		if (TM.IsTimerActive(LineTimerHandle))
		{
			PausedTimeRemaining = TM.GetTimerRemaining(LineTimerHandle);
			TM.PauseTimer(LineTimerHandle);
		}
	}

	if (IsValid(ActiveAudio))
	{
		ActiveAudio->SetPaused(true);
	}

	OnPaused.Broadcast(this);
}

void UKzDialoguePlayer::Resume()
{
	if (State != EKzDialogueState::Paused)
	{
		return;
	}

	State = StateBeforePause;
	StateBeforePause = EKzDialogueState::Idle;

	if (UWorld* World = GetWorld())
	{
		FTimerManager& TM = World->GetTimerManager();
		if (LineTimerHandle.IsValid())
		{
			TM.UnPauseTimer(LineTimerHandle);
		}
	}

	if (IsValid(ActiveAudio))
	{
		ActiveAudio->SetPaused(false);
	}

	OnResumed.Broadcast(this);
}

void UKzDialoguePlayer::Stop()
{
	if (State == EKzDialogueState::Idle)
	{
		return;
	}

	// From mid-line states, route through LineExiting -> Exiting (handled by transitions).
	if (State == EKzDialogueState::LineEntering || State == EKzDialogueState::LinePlaying)
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(LineTimerHandle);
		}
		Enter_LineExiting();
	}
	else if (State == EKzDialogueState::Entering)
	{
		// Skip the line phase entirely.
		Enter_Exiting();
	}
	// If already exiting, let it complete naturally.
}

void UKzDialoguePlayer::Abort()
{
	if (State == EKzDialogueState::Idle)
	{
		return;
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(LineTimerHandle);
	}
	StopLineAudio(0.0f);

	FinishWithReason(EKzDialogueFinishReason::Aborted);
}

void UKzDialoguePlayer::Interrupt()
{
	if (State == EKzDialogueState::Idle)
	{
		return;
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(LineTimerHandle);
	}
	StopLineAudio(0.1f);

	FinishWithReason(EKzDialogueFinishReason::Interrupted);
}

void UKzDialoguePlayer::Skip()
{
	if (State == EKzDialogueState::LineEntering || State == EKzDialogueState::LinePlaying)
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(LineTimerHandle);
		}
		Enter_LineExiting();
	}
}

// ---------------------------------------------------------------------------------------
// View notifications
// ---------------------------------------------------------------------------------------

void UKzDialoguePlayer::NotifyEnterFinished()
{
	if (State == EKzDialogueState::Entering)
	{
		Enter_LineEntering();
	}
}

void UKzDialoguePlayer::NotifyExitFinished()
{
	if (State == EKzDialogueState::Exiting)
	{
		FinishWithReason(EKzDialogueFinishReason::Completed);
	}
}

void UKzDialoguePlayer::NotifyLineEnterFinished()
{
	if (State == EKzDialogueState::LineEntering)
	{
		Enter_LinePlaying();
	}
}

void UKzDialoguePlayer::NotifyLineExitFinished()
{
	if (State == EKzDialogueState::LineExiting)
	{
		OnLineFinished.Broadcast(this, CurrentLine);
		StopLineAudio();

		// Decide whether to play another line or wrap up.
		if (IsValid(Provider) && Provider->HasNext())
		{
			Enter_LineEntering();
		}
		else
		{
			Enter_Exiting();
		}
	}
}

// ---------------------------------------------------------------------------------------
// State entries
// ---------------------------------------------------------------------------------------

void UKzDialoguePlayer::Enter_Entering()
{
	State = EKzDialogueState::Entering;
	OnRequestDialogueEnter.Broadcast(this);

	// If no view is driving fade animations, skip to the first line immediately.
	if (!bUsesFadeAnimations)
	{
		Enter_LineEntering();
	}
}

void UKzDialoguePlayer::Enter_LineEntering()
{
	if (!IsValid(Provider) || !Provider->HasNext())
	{
		Enter_Exiting();
		return;
	}

	State = EKzDialogueState::LineEntering;
	CurrentLine = Provider->Advance();

	OnLineStarted.Broadcast(this, CurrentLine);
	DispatchSpecificLineEvent(SpecificLineStartedBindings, CurrentLine);

	if (bStartAudioOnLineEnter)
	{
		StartLineAudio();
	}

	OnRequestLineEnter.Broadcast(this, CurrentLine);

	if (!bUsesFadeAnimations)
	{
		Enter_LinePlaying();
	}
}

void UKzDialoguePlayer::Enter_LinePlaying()
{
	State = EKzDialogueState::LinePlaying;

	if (!bStartAudioOnLineEnter)
	{
		StartLineAudio();
	}

	const float Duration = (PausedTimeRemaining > 0.0f) ? PausedTimeRemaining : ResolveLineDuration(CurrentLine);
	PausedTimeRemaining = 0.0f;

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(LineTimerHandle, this,
			&UKzDialoguePlayer::HandleLineTimerElapsed, Duration, /*loop=*/false);
	}
	else
	{
		// No world (unlikely in normal use): advance immediately.
		HandleLineTimerElapsed();
	}
}

void UKzDialoguePlayer::Enter_LineExiting()
{
	State = EKzDialogueState::LineExiting;
	OnRequestLineExit.Broadcast(this, CurrentLine);
	DispatchSpecificLineEvent(SpecificLineFinishedBindings, CurrentLine);

	if (!bUsesFadeAnimations)
	{
		NotifyLineExitFinished();
	}
}

void UKzDialoguePlayer::Enter_Exiting()
{
	State = EKzDialogueState::Exiting;
	StopLineAudio();
	OnRequestDialogueExit.Broadcast(this);

	if (!bUsesFadeAnimations)
	{
		FinishWithReason(EKzDialogueFinishReason::Completed);
	}
}

// ---------------------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------------------

void UKzDialoguePlayer::HandleLineTimerElapsed()
{
	if (State == EKzDialogueState::LinePlaying)
	{
		Enter_LineExiting();
	}
}

void UKzDialoguePlayer::FinishWithReason(EKzDialogueFinishReason Reason)
{
	UKzDialogueProvider* PreviousProvider = Provider;

	State = EKzDialogueState::Idle;
	StateBeforePause = EKzDialogueState::Idle;
	PausedTimeRemaining = 0.0f;
	Provider = nullptr;
	CurrentLine = FKzDialogueLine{};

	OnDialogueFinished.Broadcast(this, Reason);

	// We deliberately keep PreviousProvider alive only inside this scope; it'll be GC'd
	// when its outer goes out of scope.
	(void)PreviousProvider;
}

float UKzDialoguePlayer::ResolveLineDuration(const FKzDialogueLine& Line) const
{
	float Duration;
	if (Line.Duration > UE_KINDA_SMALL_NUMBER)
	{
		Duration = Line.Duration;
	}
	else if (USoundBase* Sound = Line.Audio.Get())
	{
		const float SoundDuration = Sound->GetDuration();
		Duration = (SoundDuration > UE_KINDA_SMALL_NUMBER) ? SoundDuration : DefaultDuration;
	}
	else
	{
		Duration = DefaultDuration;
	}

	// Apply TimeScale, but clamp to a sane minimum so we never set a 0-duration timer.
	const float Scaled = Duration / FMath::Max(0.1f, TimeScale);
	return FMath::Max(0.1f, Scaled);
}

void UKzDialoguePlayer::StartLineAudio()
{
	StopLineAudio(0.05f);

	USoundBase* Sound = CurrentLine.Audio.IsNull() ? nullptr : CurrentLine.Audio.LoadSynchronous();
	if (!Sound)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// Spawn 2D regardless of speaker if explicitly requested or if no speaker actor is
	// present in the world. Otherwise we'd need attachment, which is a per-speaker concern;
	// a SpeakerComponent attaches when needed (see UKzDialogueSpeakerComponent::Speak()).
	ActiveAudio = UGameplayStatics::SpawnSound2D(World, Sound);
	if (!IsValid(ActiveAudio))
	{
		return;
	}
	ActiveAudio->bAutoDestroy = true;

	// Apply audio params from the property bag. Each value is forwarded to the audio
	// component using the type-appropriate setter.
	const FInstancedPropertyBag& Bag = CurrentLine.AudioParams;
	if (const UPropertyBag* Desc = Bag.GetPropertyBagStruct())
	{
		for (const FPropertyBagPropertyDesc& Prop : Desc->GetPropertyDescs())
		{
			const FName ParamName = Prop.Name;
			if (ParamName.IsNone()) { continue; }

			switch (Prop.ValueType)
			{
			case EPropertyBagPropertyType::Bool:
			{
				if (auto Result = Bag.GetValueBool(ParamName); Result.HasValue())
				{
					ActiveAudio->SetBoolParameter(ParamName, Result.GetValue());
				}
				break;
			}
			case EPropertyBagPropertyType::Int32:
			{
				if (auto Result = Bag.GetValueInt32(ParamName); Result.HasValue())
				{
					ActiveAudio->SetIntParameter(ParamName, Result.GetValue());
				}
				break;
			}
			case EPropertyBagPropertyType::Float:
			case EPropertyBagPropertyType::Double:
			{
				if (auto Result = Bag.GetValueDouble(ParamName); Result.HasValue())
				{
					ActiveAudio->SetFloatParameter(ParamName, static_cast<float>(Result.GetValue()));
				}
				break;
			}
			case EPropertyBagPropertyType::Name:
			{
				if (auto Result = Bag.GetValueName(ParamName); Result.HasValue())
				{
					ActiveAudio->SetStringParameter(ParamName, Result.GetValue().ToString());
				}
				break;
			}
			case EPropertyBagPropertyType::Object:
			{
				if (auto Result = Bag.GetValueObject(ParamName); Result.HasValue())
				{
					if (USoundWave* Wave = Cast<USoundWave>(Result.GetValue()))
					{
						ActiveAudio->SetWaveParameter(ParamName, Wave);
					}
					else
					{
						ActiveAudio->SetObjectParameter(ParamName, Result.GetValue());
					}
				}
				break;
			}
			default:
				break;
			}
		}
	}

	ActiveAudio->Play();
}

void UKzDialoguePlayer::StopLineAudio(float FadeTime)
{
	if (IsValid(ActiveAudio))
	{
		if (FadeTime > 0.0f) { ActiveAudio->FadeOut(FadeTime, 0.0f); }
		else { ActiveAudio->Stop(); }
	}
	ActiveAudio = nullptr;
}

// ---------------------------------------------------------------------------------------
// Specific line bindings
// ---------------------------------------------------------------------------------------

TSet<FGuid> UKzDialoguePlayer::ResolveLineRefToMatchSet(const FKzDialogueLineRef& LineRef) const
{
	TSet<FGuid> Result;

	if (!LineRef.IsValid()) { return Result; }

	UKzDialogueAsset* Asset = LineRef.Asset.LoadSynchronous();
	if (!Asset) { return Result; }

	// If the GUID is a line, the match set is just that line.
	if (Asset->IndexOfLine(LineRef.LineId) != INDEX_NONE)
	{
		Result.Add(LineRef.LineId);
		return Result;
	}

	// Otherwise try as alias and expand to all referenced line ids.
	FKzDialogueAlias Alias;
	if (Asset->TryGetAliasById(LineRef.LineId, Alias))
	{
		Result.Reserve(Alias.LineIds.Num());
		for (const FGuid& Id : Alias.LineIds)
		{
			Result.Add(Id);
		}
	}

	return Result;
}

FGuid UKzDialoguePlayer::BindOnSpecificLineStarted(const FKzDialogueLineRef& LineRef, const FKzOnDialogueLineSingleEvent& Callback, bool bAutoUnbind)
{
	if (!Callback.IsBound()) { return FGuid(); }

	TSet<FGuid> Matches = ResolveLineRefToMatchSet(LineRef);
	if (Matches.Num() == 0) { return FGuid(); }

	FSpecificLineBinding& Binding = SpecificLineStartedBindings.AddDefaulted_GetRef();
	Binding.Handle = FGuid::NewGuid();
	Binding.MatchingLineIds = MoveTemp(Matches);
	Binding.Callback = Callback;
	Binding.bAutoUnbind = bAutoUnbind;
	return Binding.Handle;
}

FGuid UKzDialoguePlayer::BindOnSpecificLineFinished(const FKzDialogueLineRef& LineRef, const FKzOnDialogueLineSingleEvent& Callback, bool bAutoUnbind)
{
	if (!Callback.IsBound()) { return FGuid(); }

	TSet<FGuid> Matches = ResolveLineRefToMatchSet(LineRef);
	if (Matches.Num() == 0) { return FGuid(); }

	FSpecificLineBinding& Binding = SpecificLineFinishedBindings.AddDefaulted_GetRef();
	Binding.Handle = FGuid::NewGuid();
	Binding.MatchingLineIds = MoveTemp(Matches);
	Binding.Callback = Callback;
	Binding.bAutoUnbind = bAutoUnbind;
	return Binding.Handle;
}

void UKzDialoguePlayer::UnbindSpecificLine(FGuid BindingHandle)
{
	if (!BindingHandle.IsValid()) { return; }

	// Same handle can't be in both arrays (each Bind generates a fresh one), but we
	// don't track which one for simplicity. Removing by predicate is O(n) in both
	// arrays; binding count is expected to be small (handful per player).
	auto MatchesHandle = [BindingHandle](const FSpecificLineBinding& Binding) -> bool
		{
			return Binding.Handle == BindingHandle;
		};

	SpecificLineStartedBindings.RemoveAll(MatchesHandle);
	SpecificLineFinishedBindings.RemoveAll(MatchesHandle);
}

void UKzDialoguePlayer::DispatchSpecificLineEvent(TArray<FSpecificLineBinding>& Bindings, const FKzDialogueLine& Line)
{
	if (Bindings.Num() == 0) { return; }

	// Two-pass to safely handle auto-unbind without mutating during iteration.
	TArray<FGuid, TInlineAllocator<4>> HandlesToRemove;

	for (const FSpecificLineBinding& Binding : Bindings)
	{
		if (!Binding.MatchingLineIds.Contains(Line.LineId)) { continue; }

		Binding.Callback.ExecuteIfBound(this, Line);

		if (Binding.bAutoUnbind)
		{
			HandlesToRemove.Add(Binding.Handle);
		}
	}

	if (HandlesToRemove.Num() > 0)
	{
		Bindings.RemoveAll([&HandlesToRemove](const FSpecificLineBinding& Binding)
			{
				return HandlesToRemove.Contains(Binding.Handle);
			});
	}
}