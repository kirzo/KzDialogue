// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "GameplayTagContainer.h"
#include "KzDialogueTypes.h"
#include "UObject/SoftObjectPath.h"
#include "KzDialogueSubsystem.generated.h"

class AActor;
class UKzDialoguePlayer;
class UKzDialogueProvider;
class UKzDialogueAsset;
class UKzDialogueAssetSession;
class UKzDialogueGraphInstance;
class UKzDialogueChoiceListener;
class AKzDialogueNetActor;
class UKzDialogueNetComponent;
class APlayerController;
struct FKzDialogueChannelDefinition;
struct FKzDialogueNetEntry;
struct FKzDialogueNetEntries;

/** A queued request (RequestId) is about to start on its channel, or was dropped: expired, displaced by Replace, or cleared by a stop. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FKzOnQueuedDialogueRequest, FGameplayTag, Channel, FGuid, RequestId);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FKzOnDialogueGraphStarted, UKzDialogueGraphInstance*, Instance);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FKzOnDialogueGraphEventSent, UKzDialogueGraphInstance*, Instance, FGameplayTag, Event);

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

	/** The request's label in the dialogue history. */
	FString HistoryLabel;
};

/** A request, queue event or ending the subsystem remembers, so "why didn't this line play" has an answer (Kz.Dialogue.History, Kz.Dialogue.Debug 2). */
struct FKzDialogueHistoryEntry
{
	/** World time. */
	double Time = 0.0;
	FGameplayTag Channel;
	int32 Priority = 0;
	/** What was asked for or what ended: the asset and its line or alias, an ad-hoc line, a provider class. */
	FString Request;
	/** Started, Queued, a rejection with what holds the channel, an ending (Interrupted by ...), Dropped with its cause. */
	FString Outcome;

	/** The player's owner on an owned channel, by name; empty on a global one. */
	FString Owner;
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
 * World-level manager of dialogue playback. Owns a UKzDialoguePlayer per channel, or per channel and
 * owner on owned channels (FKzDialogueChannelDefinition::Scope), and arbitrates between requests using
 * priority. Channels let independent dialogue streams coexist (story, barks, system, tutorial...)
 * without interfering; owners let each NPC bark and each local player talk on their own.
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

	/** Fired whenever a channel player is lazily created, BEFORE anything plays on it. Lets views bind to players that don't exist yet (e.g. a new NPC's barks). */
	UPROPERTY(BlueprintAssignable, Category = "Dialogue|Subsystem")
	FKzOnDialoguePlayerCreated OnPlayerCreated;

	/** Fired when an owned player goes away with its owner; views unbind here. */
	UPROPERTY(BlueprintAssignable, Category = "Dialogue|Subsystem")
	FKzOnDialoguePlayerRemoved OnPlayerRemoved;

	/** Get (and lazily create) the player of a channel, for Owner on an owned channel (see ResolveOwner). */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Subsystem", meta = (Categories = "Dialogue.Channel", AdvancedDisplay = "Owner"))
	UKzDialoguePlayer* GetOrCreatePlayer(FGameplayTag InChannel, AActor* Owner = nullptr);

	/** Get the player of a channel, for Owner on an owned channel, without creating one. */
	UFUNCTION(BlueprintPure, Category = "Dialogue|Subsystem", meta = (Categories = "Dialogue.Channel", AdvancedDisplay = "Owner"))
	UKzDialoguePlayer* FindPlayer(FGameplayTag InChannel, AActor* Owner = nullptr) const;

	/**
	 * Whose player of the channel a request with Owner plays on, by the channel's scope. Global: nobody. Per speaker:
	 * Owner, else the actor of Speaker, else nobody (a shared player). Per listener: Owner's player controller (a pawn
	 * gives its controller), else the first local player.
	 */
	UFUNCTION(BlueprintPure, Category = "Dialogue|Subsystem", meta = (Categories = "Dialogue.Channel", AdvancedDisplay = "Speaker"))
	AActor* ResolveOwner(FGameplayTag InChannel, AActor* Owner, const UKzSpeakerAsset* Speaker = nullptr) const;

	/**
	 * Fill OutPlayers with every player whose channel matches Scope hierarchically, of every owner: the
	 * scope Dialogue.Channel.Bark matches Dialogue.Channel.Bark.Combat (and itself). Pass a leaf tag for
	 * an exact lookup.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Subsystem", meta = (Categories = "Dialogue.Channel"))
	void GetPlayersInScope(FGameplayTag Scope, TArray<UKzDialoguePlayer*>& OutPlayers) const;

	/** True while a dialogue with Speaker on its current line plays on any channel other than ExceptChannel. */
	UFUNCTION(BlueprintPure, Category = "Dialogue|Subsystem", meta = (Categories = "Dialogue.Channel"))
	bool IsSpeakerTalking(const UKzSpeakerAsset* Speaker, FGameplayTag ExceptChannel) const;

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

	/** The last HistorySize requests, queue events and endings, oldest first. */
	const TArray<FKzDialogueHistoryEntry>& GetHistory() const { return History; }
	static constexpr int32 HistorySize = 64;

	/** Drops a waiting request (OnQueuedRequestDropped fires). False when it is not waiting. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Subsystem")
	bool CancelQueuedRequest(FGuid RequestId);

	/**
	 * Play a provider on a channel. An equal or higher priority takes the channel unless the channel or
	 * the playing dialogue refuse interruption; a channel only playing its exit animations takes anything.
	 * A refused request follows the channel's busy policy. Returns the player (also when queued: it plays
	 * there later), or nullptr when rejected; GetLastPlayResult says which.
	 * @param Priority -1 (InheritPriority) uses the channel default.
	 * @param Owner On an owned channel, whose player it plays on (see ResolveOwner).
	 */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Subsystem", meta = (Categories = "Dialogue.Channel", AdvancedDisplay = "AdvanceMode,bInterruptible,Owner"))
	UKzDialoguePlayer* Play(UKzDialogueProvider* Provider, FGameplayTag InChannel, int32 Priority = -1, EKzDialogueAdvanceMode AdvanceMode = EKzDialogueAdvanceMode::Automatic, bool bInterruptible = true, AActor* Owner = nullptr);

	/** A dialogue graph started playing; views of its options bind to it here. */
	UPROPERTY(BlueprintAssignable, Category = "Dialogue|Subsystem")
	FKzOnDialogueGraphStarted OnDialogueGraphStarted;

	/** An Event node of any dialogue graph fired, for listeners of every graph (quests, achievements). */
	UPROPERTY(BlueprintAssignable, Category = "Dialogue|Subsystem")
	FKzOnDialogueGraphEventSent OnDialogueGraphEvent;

	/** Resolves a line, or one pick of an alias (which counts against its selection and cooldown), with its timeline. False when it resolves to nothing. */
	bool ResolveLineRef(const FKzDialogueLineRef& Ref, FKzDialogueLine& OutLine);

	/** Keeps a running dialogue graph alive, or lets a finished one go. Called by the instances. */
	void AddGraphInstance(UKzDialogueGraphInstance* Instance);
	void RemoveGraphInstance(UKzDialogueGraphInstance* Instance);
	const TArray<TObjectPtr<UKzDialogueGraphInstance>>& GetGraphInstances() const { return GraphInstances; }

	/** Keeps a choice listener alive while whoever created it lives, or until it stops listening. Called by the listeners. */
	void AddChoiceListener(UKzDialogueChoiceListener* Listener);
	void RemoveChoiceListener(UKzDialogueChoiceListener* Listener);

	/** A server of a networked game: it decides every dialogue its clients show. */
	bool IsNetServer() const;

	/** A client of a networked game: its players copy the server's (UKzDialoguePlayer::IsProxy). */
	bool IsNetClient() const;

	/** On a server, sends the player's state to the clients that show it; NewLine is the line it just took. Called by the players. */
	void PublishPlayer(const UKzDialoguePlayer& Player, const FKzDialogueLine* NewLine = nullptr);

	/** On a client, a server player arrived or changed: its copy here follows it. Called by the net entries. */
	void ReceiveNetEntry(const FKzDialogueNetEntry& Entry);

	/** On a client, a server player went away, and its copy with it. */
	void ReleaseNetEntry(const FKzDialogueNetEntry& Entry);

	/** On a client, asks the server to advance the player that Player copies. */
	void SendNext(const UKzDialoguePlayer& Player);

	/** On a server, a client asks to advance a player: they may when it is their own or a shared one. */
	void HandleNetNext(APlayerController* From, FGameplayTag InChannel, AActor* Owner);

	// The entry points below are C++ only: Blueprints reach them through UKzDialogueFunctionLibrary and the async nodes.
	// Owner, on an owned channel, picks whose player they play on (see ResolveOwner); without one, the line's speaker decides.

	/**
	 * Play a whole dialogue asset, resolving EACH line's channel and chaining runs across channel changes.
	 * Returns a UKzDialogueAssetSession that completes once for the whole asset (wait on it via
	 * OnDialogueFinished / IsPlaying). A valid InChannel forces every line onto it (single-channel,
	 * single run). AdvanceMode defaults to the asset's.
	 */
	UKzDialogueAssetSession* PlayAsset(UKzDialogueAsset* Asset, FGameplayTag InChannel, EKzDialogueAdvanceMode AdvanceMode = EKzDialogueAdvanceMode::Inherit, AActor* Owner = nullptr);

	/**
	 * Play an ordered subset of an asset's entries (lines or aliases) with the same per-entry channel
	 * resolution and run chaining as PlayAsset. Returns a session that completes once for the whole list.
	 * A valid InChannel forces every entry onto it (single run); InheritPriority falls back to the asset
	 * hint / channel default.
	 */
	UKzDialogueAssetSession* PlayLineList(UKzDialogueAsset* Asset, const TArray<FGuid>& LineIds, FGameplayTag InChannel, int32 Priority = InheritPriority, EKzDialogueAdvanceMode AdvanceMode = EKzDialogueAdvanceMode::Inherit, AActor* Owner = nullptr);

	/** Convenience wrapper: build a manual single-line provider and play it. */
	UKzDialoguePlayer* PlayLine(const FKzDialogueLine& Line, FGameplayTag InChannel, int32 Priority = -1, AActor* Owner = nullptr);

	/**
	 * Play a line or alias from a dialogue asset by GUID. The asset checks both
	 * Lines and Aliases tables internally, so the same call site works for either.
	 */
	UKzDialoguePlayer* PlayAssetLine(UKzDialogueAsset* Asset, FGuid LineId, FGameplayTag InChannel, int32 Priority = -1, AActor* Owner = nullptr);

	/**
	 * Play several lines or aliases from one dialogue asset as a SINGLE sequential dialogue.
	 * Aliases are resolved at launch (stateful: shuffle bags, cooldowns...); entries that fail
	 * to resolve are skipped. Line events fire per entry and OnDialogueFinished once at the end.
	 */
	UKzDialoguePlayer* PlayAssetLineList(UKzDialogueAsset* Asset, const TArray<FGuid>& LineIds, FGameplayTag InChannel, int32 Priority = -1, EKzDialogueAdvanceMode AdvanceMode = EKzDialogueAdvanceMode::Automatic, AActor* Owner = nullptr);

	/**
	 * Play an array of line references (possibly spanning multiple assets) as a SINGLE
	 * sequential dialogue. Aliases are resolved at launch (stateful); entries that fail to
	 * resolve are skipped. The first ref's asset gives the channel, priority hint and interruptibility.
	 */
	UKzDialoguePlayer* PlayLineRefs(const TArray<FKzDialogueLineRef>& Refs, FGameplayTag InChannel, int32 Priority = -1, AActor* Owner = nullptr);

	/** Stop the dialogue on every channel matching the scope (graceful, with exit animations), dropping their queued requests. E.g. Dialogue.Channel.Bark stops all bark channels; a leaf tag stops just that one. With Owner, only that owner's players. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Subsystem", meta = (Categories = "Dialogue.Channel", AdvancedDisplay = "Owner"))
	void StopChannel(FGameplayTag InChannel, AActor* Owner = nullptr);

	/** Stop all channels and drop every queued request. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Subsystem")
	void StopAll();

	/** Interrupt the dialogue on every channel matching the scope (hard-stop), dropping their queued requests. With Owner, only that owner's players. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Subsystem", meta = (Categories = "Dialogue.Channel", AdvancedDisplay = "Owner"))
	void InterruptChannel(FGameplayTag InChannel, AActor* Owner = nullptr);

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
	/** One per channel, or per channel and owner on owned channels; looked up by both. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UKzDialoguePlayer>> Players;

	/** Asset sessions kept alive while they run; each removes itself on finish via ReleaseAssetSession. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UKzDialogueAssetSession>> AssetSessions;

	/** Dialogue graphs kept alive while they run; each removes itself when it ends. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UKzDialogueGraphInstance>> GraphInstances;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UKzDialogueChoiceListener>> ChoiceListeners;

	/** Set in Deinitialize so in-flight sessions don't chain another run during world teardown. */
	bool bDeinitializing = false;

	/** Requests waiting for their busy player. */
	UPROPERTY(Transient)
	TMap<TObjectPtr<UKzDialoguePlayer>, FKzDialogueChannelQueue> Queues;

	EKzDialoguePlayResult LastPlayResult = EKzDialoguePlayResult::Started;
	FGuid LastQueuedRequestId;

	/** The request being admitted now, guarded per entry point so a nested one restores it; Record files it, and an interruption meanwhile names it. */
	FKzDialogueHistoryEntry PendingRequest;

	/** What each player plays, by history label, for its ending and for the rejections it causes. */
	TMap<const UKzDialoguePlayer*, FString> PlayingRequests;

	TArray<FKzDialogueHistoryEntry> History;

	void AddHistory(FGameplayTag Channel, const FString& Owner, int32 Priority, const FString& Request, const FString& Outcome);

	/** The player's owner as the history and the debug name it; empty without one. */
	static FString DescribeOwner(const UKzDialoguePlayer* Player);

	/** Looks a player up by channel and an already resolved owner. */
	UKzDialoguePlayer* FindResolvedPlayer(FGameplayTag InChannel, const AActor* ResolvedOwner) const;

	/** Gets or creates the player a request plays on, resolving its owner, and names both in the pending request. */
	UKzDialoguePlayer* GetPlayerForRequest(FGameplayTag InChannel, AActor* Owner, const UKzSpeakerAsset* Speaker);

	/** An owner's players go with it. */
	UFUNCTION()
	void HandleOwnerEndPlay(AActor* Actor, EEndPlayReason::Type EndPlayReason);

	/** Takes a player out: its queue dropped, its dialogue interrupted, OnPlayerRemoved, and on a server its net entry. */
	void RemovePlayer(UKzDialoguePlayer& Player);

	/** Carries the shared players to the clients; spawned the first time one is published. */
	UPROPERTY(Transient)
	TObjectPtr<AKzDialogueNetActor> NetActor;

	FDelegateHandle ActorSpawnedHandle;

	/** On a server, gives each player controller its net component as it spawns, so its client can send requests. */
	void HandleActorSpawned(AActor* Actor);

	UKzDialogueNetComponent* FindOrAddNetComponent(APlayerController* Controller);

	/** Where the player's net entry travels: its player's net component for a player's own dialogue, the net actor for a shared one. Null when no client shows it. */
	FKzDialogueNetEntries* FindNetEntries(const UKzDialoguePlayer& Player, bool bCreate);

	/** Whether other owners already fill the channel's MaxConcurrent, so Player may not start. */
	bool IsAtConcurrencyLimit(const UKzDialoguePlayer& Player, const FKzDialogueChannelDefinition* ChannelDef) const;

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
	EKzDialoguePlayResult Admit(UKzDialoguePlayer& Player, int32 Priority, const FKzDialogueChannelDefinition* ChannelDef) const;

	/** Records Result as the last play result; returns Player, or null for a rejection. */
	UKzDialoguePlayer* Record(EKzDialoguePlayResult Result, UKzDialoguePlayer* Player);

	/** Files an admitted-to-wait request in Player's queue, by priority then arrival; Replace first drops what was waiting. */
	UKzDialoguePlayer* Enqueue(UKzDialoguePlayer& Player, UKzDialogueProvider* Provider, int32 Priority, bool bInterruptible, EKzDialogueAdvanceMode AdvanceMode, const FKzDialogueChannelDefinition* ChannelDef);

	/** A player finished: its next queued request starts on the next tick, once every finish listener has run, and so do those waiting for a concurrency slot of its channel. */
	UFUNCTION()
	void HandlePlayerFinished(UKzDialoguePlayer* Player, EKzDialogueFinishReason Reason);

	/** Starts the next live request waiting for Player, if it is free and its channel has a slot. */
	void ProcessQueue(UKzDialoguePlayer* Player);
	void ScheduleQueue(UKzDialoguePlayer* Player);

	/** Drops the requests waiting for Player, reporting each one; Cause goes to the history. */
	void DropQueue(UKzDialoguePlayer* Player, const TCHAR* Cause);

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