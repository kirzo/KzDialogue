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

/** Where a bark component barks in a networked game. */
UENUM(BlueprintType)
enum class EKzBarkNetworkMode : uint8
{
	/** The server barks, and every player hears the same bark at once. Bark Now is the server's to call. */
	Shared,
	/** Each machine barks on its own, without the network (none on a dedicated server): background chatter, where each player may hear another line. */
	PerMachine
};

/**
 * Periodically plays a bark (a line or alias from a dialogue asset) on a channel, with a random gap
 * [MinInterval, MaxInterval] measured end-to-end, plus start / stop / pause / resume. Reschedules after
 * EVERY bark ends (finished OR cancelled / interrupted), so the loop survives a bark being pre-empted by
 * higher-priority dialogue. Gate it from the outside with Start / Stop or Pause / Resume.
 *
 * Line variety and per-line cooldown come from the dialogue alias itself (SelectionMode / CooldownSeconds),
 * so point Bark at an alias and the asset decides which variation plays. On a per speaker channel (such as
 * Dialogue.Channel.Bark) each barker plays on its own actor's player, so barkers neither cut each other nor
 * get stopped by each other.
 *
 * The Rules skip a bark that would not make sense now, and the loop waits another interval. BarkNow
 * honors them too, except Probability, which only thins the loop.
 *
 * In a networked game, NetworkMode picks who barks: the server for everyone, or each machine on its own.
 */
UCLASS(ClassGroup = (Dialogue), meta = (BlueprintSpawnableComponent))
class KZDIALOGUE_API UKzBarkComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UKzBarkComponent();

	/** Line or alias to bark. Point it at an alias to get variety + cooldown from the asset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Bark")
	FKzDialogueLineRef Bark;

	/** Channel to bark on; empty resolves like any line (line, asset, settings). A per speaker channel gives this actor its own player. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Bark", meta = (Categories = "Dialogue.Channel"))
	FGameplayTag Channel;

	/** Minimum gap between barks (seconds), measured from the end of one to the start of the next. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Bark", meta = (ClampMin = "0.0", Units = "Seconds"))
	float MinInterval = 8.0f;

	/** Maximum gap between barks (seconds). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Bark", meta = (ClampMin = "0.0", Units = "Seconds"))
	float MaxInterval = 16.0f;

	/** Start barking automatically on BeginPlay. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Bark")
	bool bAutoStart = false;

	/** Play the first bark immediately on start instead of waiting a first interval. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Bark")
	bool bFirstBarkImmediate = false;

	/** Dialogue priority for the barks (-1 = inherit asset / channel default). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Bark", AdvancedDisplay)
	int32 Priority = -1;

	/** In a networked game: the server barks for everyone, or each machine on its own. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Bark")
	EKzBarkNetworkMode NetworkMode = EKzBarkNetworkMode::Shared;

	/** Chance each scheduled bark plays. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Bark|Rules", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Probability = 1.0f;

	/** Skip barks farther than this from the nearest player: a local player's audio listener, or on a server where a remote player looks from. 0 = any distance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Bark|Rules", meta = (ClampMin = "0.0", Units = "Centimeters"))
	float MaxListenerDistance = 0.0f;

	/** Skip barks while the owner has not been rendered lately (off screen or culled). A dedicated server renders nothing, so there it does not apply. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Bark|Rules")
	bool bRequireRecentlyRendered = false;

	/** Skip barks while the bark's speaker (the line's, or the alias's) talks on another channel. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Bark|Rules")
	bool bSkipWhileSpeakerTalking = true;

	/** Fired when a bark actually starts playing (carries the channel player). */
	UPROPERTY(BlueprintAssignable, Category = "Dialogue|Bark")
	FKzOnBarkPlayed OnBarkPlayed;

	/** Fired when a bark ends, with the reason (Completed / Stopped / Aborted / Interrupted). */
	UPROPERTY(BlueprintAssignable, Category = "Dialogue|Bark")
	FKzOnBarkFinished OnBarkFinished;

	/** Begin the bark loop (no-op if already barking). */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Bark")
	void StartBarking();

	/** Stop the loop and stop any bark currently playing on the channel (graceful). */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Bark")
	void StopBarking();

	/** Pause the loop, keeping the remaining wait; a bark already playing is left to finish. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Bark")
	void PauseBarking();

	/** Resume a paused loop. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Bark")
	void ResumeBarking();

	/** Bark right now, ignoring the pending wait and Probability; the other rules still apply. No-op if one is already playing. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Bark")
	void BarkNow();

	/** True between StartBarking and StopBarking. */
	UFUNCTION(BlueprintPure, Category = "Dialogue|Bark")
	bool IsBarking() const { return bBarking; }

	/** True while paused. */
	UFUNCTION(BlueprintPure, Category = "Dialogue|Bark")
	bool IsPaused() const { return bPaused; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UKzDialogueSubsystem* GetSubsystem() const;

	/** Channel the barks play on: Channel when set, else the one Bark resolves to. */
	FGameplayTag ResolveBarkChannel() const;

	void ScheduleNextBark();

	/** Waits another interval unless the loop is stopped or paused (a one-off BarkNow must not start it). */
	void RescheduleIfLooping();

	void PlayBark(bool bRollProbability);
	bool PassesRules(const UKzDialogueSubsystem& Subsystem, bool bRollProbability) const;
	bool IsNearListener() const;

	/** Whether this machine barks, by the network mode: the server for a shared one, every machine but a dedicated server for a per machine one. */
	bool BarksHere() const;

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