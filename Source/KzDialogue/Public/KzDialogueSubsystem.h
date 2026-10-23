// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GameplayTagContainer.h"
#include "KzDialogueTypes.h"
#include "UObject/SoftObjectPath.h"
#include "KzDialogueSubsystem.generated.h"

class UKzDialoguePlayer;
class UKzDialogueProvider;
class UKzDialogueAsset;
class UKzDialogueAssetSession;
struct FKzDialogueChannelDefinition;

/** A queued request (RequestId) is about to start on its channel, or was dropped: expired, displaced by Replace, or cleared by a stop. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FKzOnQueuedDialogueRequest, FGameplayTag, Channel, FGuid, RequestId);

/** A request waiting for its busy channel (FKzDialogueChannelDefinition::BusyPolicy), already resolved to a provider. */
USTRUCT()
struct FKzQueuedDialogue
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	TObjectPtr<UKzDialogueProvider> Provider = nullptr;

	FGuid RequestId;
	int32 Priority = 0;
	bool bInterruptible = true;
	EKzDialogueAdvanceMode AdvanceMode = EKzDialogueAdvanceMode::Automatic;

	/** World time it was queued at, for the channel's QueueTimeout. */
	double QueuedAt = 0.0;
};

/** One channel's waiting requests, in playing order. */
USTRUCT()
struct FKzDialogueChannelQueue
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	TArray<FKzQueuedDialogue> Entries;
};

/**
 * World-level manager of dialogue playback. Owns a UKzDialoguePlayer per channel and
 * arbitrates between requests using priority. Channels let independent dialogue streams
 * coexist (story, barks, system, tutorial...) without interfering.
 */
UCLASS()
class KZDIALOGUE_API UKzDialogueSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

	friend class FKzDialogueDebugOverlay;

public:
	/** Default channel used when callers don't pass one. */
	UPROPERTY(BlueprintReadOnly, Category = "Dialogue|Subsystem")
	FGameplayTag DefaultChannel;

	/** Fired whenever a channel player is lazily created, BEFORE anything plays on it. Lets views bind to channels that don't exist yet (e.g. per-character bark channels). */
	UPROPERTY(BlueprintAssignable, Category = "Dialogue|Subsystem")
	FKzOnDialoguePlayerCreated OnPlayerCreated;

	/** Get (and lazily create) the player associated with a channel. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Subsystem", meta = (Categories = "Dialogue.Channel"))
	UKzDialoguePlayer* GetOrCreatePlayer(FGameplayTag InChannel);

	/** Get the player for a channel without creating one. */
	UFUNCTION(BlueprintPure, Category = "Dialogue|Subsystem", meta = (Categories = "Dialogue.Channel"))
	UKzDialoguePlayer* FindPlayer(FGameplayTag InChannel) const;

	/**
	 * Fill OutPlayers with every player whose channel matches Scope hierarchically: the scope
	 * Dialogue.Channel.Bark matches Dialogue.Channel.Bark.MyCharacter (and itself). Pass a leaf
	 * tag for an exact lookup.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Subsystem", meta = (Categories = "Dialogue.Channel"))
	void GetPlayersInScope(FGameplayTag Scope, TArray<UKzDialoguePlayer*>& OutPlayers) const;

	/**
	 * Channel resolution chain for an asset entry by GUID. Line entries: explicit > line >
	 * audio SoundClass mapping (UKzDialogueSettings::SoundClassChannels) > asset > settings.
	 * Alias entries: explicit > alias > lines' unanimous DefaultChannel (only when ALL the alias
	 * lines agree) > asset > settings. Deterministic and state-free, so callers can resolve the
	 * channel before playing: the play paths use the same chain and land on the same player.
	 */
	UFUNCTION(BlueprintPure, Category = "Dialogue|Subsystem", meta = (Categories = "Dialogue.Channel"))
	FGameplayTag ResolveChannelForEntry(FGameplayTag ExplicitChannel, const UKzDialogueAsset* Asset, FGuid EntryId) const;

	/** Sentinel meaning "no explicit priority"; falls back to asset, then channel default. */
	static constexpr int32 InheritPriority = -1;

	/** Outcome of the last play request on this subsystem: why a play returned nothing, or that it was queued. */
	UFUNCTION(BlueprintPure, Category = "Dialogue|Subsystem")
	EKzDialoguePlayResult GetLastPlayResult() const { return LastPlayResult; }

	/** Id of the last request that was queued; valid only while GetLastPlayResult is Queued. Matches the queued request events. */
	UFUNCTION(BlueprintPure, Category = "Dialogue|Subsystem")
	FGuid GetLastQueuedRequestId() const { return LastQueuedRequestId; }

	/** A queued request is about to start on its channel. */
	UPROPERTY(BlueprintAssignable, Category = "Dialogue|Subsystem")
	FKzOnQueuedDialogueRequest OnQueuedRequestStarted;

	/** A queued request will never play: it expired, Replace displaced it, it was cancelled, or its channel was stopped or interrupted. */
	UPROPERTY(BlueprintAssignable, Category = "Dialogue|Subsystem")
	FKzOnQueuedDialogueRequest OnQueuedRequestDropped;

	/** Drops a waiting request (OnQueuedRequestDropped fires). False when it is not waiting. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Subsystem")
	bool CancelQueuedRequest(FGuid RequestId);

	/**
	 * Play a provider on a channel. An equal or higher priority takes the channel unless the channel or
	 * the playing dialogue refuse interruption; a channel only playing its exit animations takes anything.
	 * A refused request follows the channel's busy policy. Returns the player (also when queued: it plays
	 * there later), or nullptr when rejected; GetLastPlayResult says which.
	 * @param Priority -1 (InheritPriority) uses the channel default.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Subsystem", meta = (Categories = "Dialogue.Channel", AdvancedDisplay = "AdvanceMode"))
	UKzDialoguePlayer* Play(UKzDialogueProvider* Provider, FGameplayTag InChannel, int32 Priority = -1, EKzDialogueAdvanceMode AdvanceMode = EKzDialogueAdvanceMode::Automatic);

	// The entry points below are C++ only: Blueprints reach them through UKzDialogueFunctionLibrary and the async nodes.

	/**
	 * Play a whole dialogue asset, resolving EACH line's channel and chaining runs across channel changes.
	 * Returns a UKzDialogueAssetSession that completes once for the whole asset (wait on it via
	 * OnDialogueFinished / IsPlaying). A valid InChannel forces every line onto it (single-channel,
	 * single run). AdvanceMode defaults to the asset's.
	 */
	UKzDialogueAssetSession* PlayAsset(UKzDialogueAsset* Asset, FGameplayTag InChannel, EKzDialogueAdvanceMode AdvanceMode = EKzDialogueAdvanceMode::Inherit);

	/**
	 * Play an ordered subset of an asset's entries (lines or aliases) with the same per-entry channel
	 * resolution and run chaining as PlayAsset. Returns a session that completes once for the whole list.
	 * A valid InChannel forces every entry onto it (single run); InheritPriority falls back to the asset
	 * hint / channel default.
	 */
	UKzDialogueAssetSession* PlayLineList(UKzDialogueAsset* Asset, const TArray<FGuid>& LineIds, FGameplayTag InChannel, int32 Priority = InheritPriority, EKzDialogueAdvanceMode AdvanceMode = EKzDialogueAdvanceMode::Inherit);

	/** Convenience wrapper: build a manual single-line provider and play it. */
	UKzDialoguePlayer* PlayLine(const FKzDialogueLine& Line, FGameplayTag InChannel, int32 Priority = -1);

	/**
	 * Play a line or alias from a dialogue asset by GUID. The asset checks both
	 * Lines and Aliases tables internally, so the same call site works for either.
	 */
	UKzDialoguePlayer* PlayAssetLine(UKzDialogueAsset* Asset, FGuid LineId, FGameplayTag InChannel, int32 Priority = -1);

	/**
	 * Play several lines or aliases from one dialogue asset as a SINGLE sequential dialogue.
	 * Aliases are resolved at launch (stateful: shuffle bags, cooldowns...); entries that fail
	 * to resolve are skipped. Line events fire per entry and OnDialogueFinished once at the end.
	 */
	UKzDialoguePlayer* PlayAssetLineList(UKzDialogueAsset* Asset, const TArray<FGuid>& LineIds, FGameplayTag InChannel, int32 Priority = -1, EKzDialogueAdvanceMode AdvanceMode = EKzDialogueAdvanceMode::Automatic);

	/**
	 * Play an array of line references (possibly spanning multiple assets) as a SINGLE
	 * sequential dialogue. Aliases are resolved at launch (stateful); entries that fail to
	 * resolve are skipped. The first ref's asset gives the channel, priority hint and interruptibility.
	 */
	UKzDialoguePlayer* PlayLineRefs(const TArray<FKzDialogueLineRef>& Refs, FGameplayTag InChannel, int32 Priority = -1);

	/** Stop the dialogue on every channel matching the scope (graceful, with exit animations), dropping their queued requests. E.g. Dialogue.Channel.Bark stops all bark channels; a leaf tag stops just that one. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Subsystem", meta = (Categories = "Dialogue.Channel"))
	void StopChannel(FGameplayTag InChannel);

	/** Stop all channels and drop every queued request. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Subsystem")
	void StopAll();

	/** Interrupt the dialogue on every channel matching the scope (hard-stop), dropping their queued requests. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Subsystem", meta = (Categories = "Dialogue.Channel"))
	void InterruptChannel(FGameplayTag InChannel);

	/** Interrupt all channels and drop every queued request. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Subsystem")
	void InterruptAll();

	/**
	 * Fill OutPlayers with every player currently held by the subsystem.
	 * Order is not guaranteed. Useful for debug commands and overlays. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Subsystem")
	void GetAllPlayers(TArray<UKzDialoguePlayer*>& OutPlayers) const;

	/** Removes a finished asset session from the subsystem's keep-alive list. Called by the session itself. */
	void ReleaseAssetSession(UKzDialogueAssetSession* Session);

	/** True while the subsystem is tearing down (Deinitialize); in-flight asset sessions use it to stop chaining. */
	bool IsDeinitializing() const { return bDeinitializing; }

	/** Resolves the advance mode: explicit override > asset's mode > Automatic. */
	EKzDialogueAdvanceMode ResolveAdvanceMode(EKzDialogueAdvanceMode Override, const UKzDialogueAsset* Asset) const;

	/**
	 * Reset the playback state of a single alias, its remembered picks and its cooldown. Next time the
	 * alias is resolved, it starts as if it had never been played.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Subsystem")
	void ResetAliasState(FGuid AliasId);

	/** Reset cached state for every alias. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Subsystem")
	void ResetAllAliasStates();

	//~ USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

private:
	UPROPERTY(Transient)
	TMap<FGameplayTag, TObjectPtr<UKzDialoguePlayer>> Players;

	/** Asset sessions kept alive while they run; each removes itself on finish via ReleaseAssetSession. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UKzDialogueAssetSession>> AssetSessions;

	/** Set in Deinitialize so in-flight sessions don't chain another run during world teardown. */
	bool bDeinitializing = false;

	/** Requests waiting for their busy channel, by player channel. */
	UPROPERTY(Transient)
	TMap<FGameplayTag, FKzDialogueChannelQueue> Queues;

	EKzDialoguePlayResult LastPlayResult = EKzDialoguePlayResult::Started;
	FGuid LastQueuedRequestId;

	/**
	 * Resolve the priority to use given:
	 *   - RequestedPriority (caller's request, possibly InheritPriority sentinel)
	 *   - AssetHintPriority (asset's Priority field, or InheritPriority if not asset-backed)
	 *   - ChannelDef (channel definition from settings, or null if undeclared)
	 *
	 * Precedence: explicit caller > asset hint > channel default > 0.
	 */
	int32 ResolvePriority(int32 RequestedPriority, int32 AssetHintPriority, const FKzDialogueChannelDefinition* ChannelDef) const;

	/** The single gate every play request passes BEFORE anything with side effects (alias state, providers) runs: Started when it may play now, Queued when the channel's busy policy keeps it, else the rejection. */
	EKzDialoguePlayResult Admit(const UKzDialoguePlayer& Player, int32 Priority, const FKzDialogueChannelDefinition* ChannelDef) const;

	/** Records Result as the last play result; returns Player, or null for a rejection. */
	UKzDialoguePlayer* Record(EKzDialoguePlayResult Result, UKzDialoguePlayer* Player);

	/** Files an admitted-to-wait request in Player's queue, by priority then arrival; Replace first drops what was waiting. */
	UKzDialoguePlayer* Enqueue(UKzDialoguePlayer& Player, UKzDialogueProvider* Provider, int32 Priority, bool bInterruptible, EKzDialogueAdvanceMode AdvanceMode, const FKzDialogueChannelDefinition* ChannelDef);

	/** A channel finished: its next queued request starts on the next tick, once every finish listener has run. */
	UFUNCTION()
	void HandlePlayerFinished(UKzDialoguePlayer* Player, EKzDialogueFinishReason Reason);

	/** Starts the next live request waiting for Channel, if the channel is free. */
	void ProcessQueue(FGameplayTag Channel);
	void ScheduleQueue(FGameplayTag Channel);

	/** Drops the requests waiting for Channel, reporting each one. */
	void DropQueue(FGameplayTag Channel);

	/** Ends whatever occupies the admitted player and starts Provider on it. Null when a listener of that ending took the channel first. */
	UKzDialoguePlayer* StartOnPlayer(UKzDialoguePlayer& Player, UKzDialogueProvider* Provider, int32 Priority, bool bInterruptible, EKzDialogueAdvanceMode AdvanceMode);

	/** Starts Provider now when Admission is Started, else files it in the queue; records the outcome. */
	UKzDialoguePlayer* StartOrEnqueue(UKzDialoguePlayer& Player, EKzDialoguePlayResult Admission, UKzDialogueProvider* Provider, int32 Priority, bool bInterruptible, EKzDialogueAdvanceMode AdvanceMode, const FKzDialogueChannelDefinition* ChannelDef);

	/** Plays one resolved line on an admitted player: hot-swaps a playing manual line, else starts a fresh manual provider. */
	UKzDialoguePlayer* CommitLine(UKzDialoguePlayer& Player, const FKzDialogueLine& Line, int32 Priority, bool bInterruptible);

	/** Resolves a line-or-alias id inside Asset to a concrete line (stateful alias selection). */
	bool ResolveAssetEntry(UKzDialogueAsset* Asset, const FGuid& LineId, FKzDialogueLine& OutLine);

	/** World time each alias last resolved at, for its cooldown. Level-local on purpose; what the alias remembers between picks lives in UKzDialogueMemorySubsystem. */
	TMap<FGuid /*AliasId*/, double> AliasCooldownStarts;

	/**
	 * Resolve an alias to a concrete LineId according to its SelectionMode, updating
	 * the cached state. Returns an invalid FGuid if the alias has no lines.
	 */
	FGuid ResolveAliasInternal(const FKzDialogueAlias& Alias);
};