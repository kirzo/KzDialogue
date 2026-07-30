// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/EngineTypes.h"
#include "GameplayTagContainer.h"
#include "KzDialogueTypes.h"
#include "KzBarkComponent.generated.h"

class UKzDialogueSubsystem;
class UKzDialoguePlayer;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FKzOnBarkPlayed, UKzDialoguePlayer*, Player);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FKzOnBarkFinished, EKzDialogueFinishReason, Reason);

/**
 * Periodically plays a bark (a line or alias from a dialogue asset) on a channel, with a random gap
 * [MinInterval, MaxInterval] measured end-to-end, plus start / stop / pause / resume. Reschedules after
 * EVERY bark ends — finished OR cancelled / interrupted — so the loop survives a bark being pre-empted by
 * higher-priority dialogue. Gate it from the outside with Start / Stop or Pause / Resume.
 *
 * Line variety and per-line cooldown come from the dialogue alias itself (SelectionMode / CooldownSeconds),
 * so point Bark at an alias and the asset decides which variation plays. Use a dedicated channel per barker
 * (e.g. Dialogue.Channel.Bark) so stop / pause only affect this one's lines.
 */
UCLASS(ClassGroup = (Dialogue), meta = (BlueprintSpawnableComponent))
class KZDIALOGUE_API UKzBarkComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UKzBarkComponent();

	/** Line or alias to bark. Point it at an alias to get variety + cooldown from the asset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bark")
	FKzDialogueLineRef Bark;

	/** Channel to bark on. Use a dedicated per-barker channel so stop / pause only affect this one. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bark", meta = (Categories = "Dialogue.Channel"))
	FGameplayTag Channel;

	/** Minimum gap between barks (seconds), measured from the end of one to the start of the next. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bark", meta = (ClampMin = "0.0", Units = "Seconds"))
	float MinInterval = 8.0f;

	/** Maximum gap between barks (seconds). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bark", meta = (ClampMin = "0.0", Units = "Seconds"))
	float MaxInterval = 16.0f;

	/** Start barking automatically on BeginPlay. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bark")
	bool bAutoStart = false;

	/** Play the first bark immediately on start instead of waiting a first interval. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bark")
	bool bFirstBarkImmediate = false;

	/** Dialogue priority for the barks (-1 = inherit asset / channel default). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bark", AdvancedDisplay)
	int32 Priority = -1;

	/** Fired when a bark actually starts playing (carries the channel player). */
	UPROPERTY(BlueprintAssignable, Category = "Bark")
	FKzOnBarkPlayed OnBarkPlayed;

	/** Fired when a bark ends, with the reason (Completed / Stopped / Aborted / Interrupted). */
	UPROPERTY(BlueprintAssignable, Category = "Bark")
	FKzOnBarkFinished OnBarkFinished;

	/** Begin the bark loop (no-op if already barking). */
	UFUNCTION(BlueprintCallable, Category = "Bark")
	void StartBarking();

	/** Stop the loop and stop any bark currently playing on the channel (graceful). */
	UFUNCTION(BlueprintCallable, Category = "Bark")
	void StopBarking();

	/** Pause the loop, keeping the remaining wait; a bark already playing is left to finish. */
	UFUNCTION(BlueprintCallable, Category = "Bark")
	void PauseBarking();

	/** Resume a paused loop. */
	UFUNCTION(BlueprintCallable, Category = "Bark")
	void ResumeBarking();

	/** Force a bark right now (ignores the pending wait). No-op if one is already playing. */
	UFUNCTION(BlueprintCallable, Category = "Bark")
	void BarkNow();

	/** True between StartBarking and StopBarking. */
	UFUNCTION(BlueprintPure, Category = "Bark")
	bool IsBarking() const { return bBarking; }

	/** True while paused. */
	UFUNCTION(BlueprintPure, Category = "Bark")
	bool IsPaused() const { return bPaused; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UKzDialogueSubsystem* GetSubsystem() const;

	void ScheduleNextBark();
	void PlayBark();

	UFUNCTION()
	void HandleBarkFinished(UKzDialoguePlayer* Player, EKzDialogueFinishReason Reason);

	void BindChannelPlayer();
	void UnbindChannelPlayer();

	FTimerHandle BarkTimer;

	/** Channel player we're bound to for finish callbacks. */
	TWeakObjectPtr<UKzDialoguePlayer> BoundPlayer;

	bool bBarking = false;
	bool bPaused = false;

	/** A bark is currently playing (between the play request and its finish). */
	bool bBarkInFlight = false;
};