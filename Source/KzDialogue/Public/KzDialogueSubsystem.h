// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GameplayTagContainer.h"
#include "KzDialogueTypes.h"
#include "KzDialogueSubsystem.generated.h"

class UKzDialoguePlayer;
class UKzDialogueProvider;
class UKzDialogueAsset;
struct FKzDialogueChannelDefinition;

/**
 * World-level manager of dialogue playback. Owns a UKzDialoguePlayer per channel and
 * arbitrates between requests using priority. Channels let independent dialogue streams
 * coexist (story, barks, system, tutorial...) without interfering.
 */
UCLASS()
class KZDIALOGUE_API UKzDialogueSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

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
	 * Channel resolution chain for a concrete line: explicit > line default > audio SoundClass
	 * mapping (see UKzDialogueSettings::SoundClassChannels) > asset default > project settings
	 * default. Deterministic and state-free. Asset may be null (manual lines).
	 */
	UFUNCTION(BlueprintPure, Category = "Dialogue|Subsystem", meta = (Categories = "Dialogue.Channel"))
	FGameplayTag ResolveChannel(FGameplayTag ExplicitChannel, const FKzDialogueLine& Line, const UKzDialogueAsset* Asset) const;

	/**
	 * Channel resolution chain for an asset entry by GUID. Line entries: explicit > line >
	 * audio SoundClass mapping > asset > settings. Alias entries: explicit > alias > lines'
	 * unanimous DefaultChannel (only when ALL the alias lines agree) > asset > settings.
	 * Deterministic and state-free, so callers can resolve the channel before playing — the
	 * play paths use the same chain and land on the same player.
	 */
	UFUNCTION(BlueprintPure, Category = "Dialogue|Subsystem", meta = (Categories = "Dialogue.Channel"))
	FGameplayTag ResolveChannelForEntry(FGameplayTag ExplicitChannel, const UKzDialogueAsset* Asset, FGuid EntryId) const;

	/** Sentinel meaning "no explicit priority"; falls back to asset, then channel default. */
	static constexpr int32 InheritPriority = -1;

	/**
	 * Play a provider on a given channel, respecting priority.
	 * Returns the player that is now playing it, or nullptr if rejected (lower priority
	 * than current and channel/asset don't allow interruption).
	 *
	 * Pass InheritPriority to fall back to the asset hint / channel default. Otherwise
	 * the priority is clamped to the channel's [MinPriority, MaxPriority] range.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Subsystem", meta = (Categories = "Dialogue.Channel", AdvancedDisplay = "bStartImmediately"))
	UKzDialoguePlayer* Play(UKzDialogueProvider* Provider, FGameplayTag InChannel, int32 Priority = -1, bool bStartImmediately = true);

	/** Convenience wrapper: build an asset provider and play it. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Subsystem", meta = (Categories = "Dialogue.Channel", AdvancedDisplay = "bStartImmediately"))
	UKzDialoguePlayer* PlayAsset(UKzDialogueAsset* Asset, FGameplayTag InChannel, bool bStartImmediately = true);

	/** Convenience wrapper: build a manual single-line provider and play it. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Subsystem", meta = (Categories = "Dialogue.Channel", AdvancedDisplay = "bStartImmediately"))
	UKzDialoguePlayer* PlayLine(const FKzDialogueLine& Line, FGameplayTag InChannel, int32 Priority = -1, bool bStartImmediately = true);

	/**
	 * Play a line or alias from a dialogue asset by GUID. The asset checks both
	 * Lines and Aliases tables internally, so the same call site works for either.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Subsystem", meta = (Categories = "Dialogue.Channel", AdvancedDisplay = "bStartImmediately"))
	UKzDialoguePlayer* PlayAssetLine(UKzDialogueAsset* Asset, FGuid LineId, FGameplayTag InChannel, int32 Priority = -1, bool bStartImmediately = true);

	/**
	 * Play several lines or aliases from one dialogue asset as a SINGLE sequential dialogue.
	 * Aliases are resolved at launch (stateful: shuffle bags, cooldowns...); entries that fail
	 * to resolve are skipped. Line events fire per entry and OnDialogueFinished once at the end.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Subsystem", meta = (Categories = "Dialogue.Channel", AdvancedDisplay = "bStartImmediately"))
	UKzDialoguePlayer* PlayAssetLineList(UKzDialogueAsset* Asset, const TArray<FGuid>& LineIds, FGameplayTag InChannel, int32 Priority = -1, bool bStartImmediately = true);

	/**
	 * Play an array of line references (possibly spanning multiple assets) as a SINGLE
	 * sequential dialogue. Aliases are resolved at launch (stateful); entries that fail to
	 * resolve are skipped. No asset priority hint applies: explicit Priority, else channel default.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Subsystem", meta = (Categories = "Dialogue.Channel", AdvancedDisplay = "bStartImmediately"))
	UKzDialoguePlayer* PlayLineRefs(const TArray<FKzDialogueLineRef>& Refs, FGameplayTag InChannel, int32 Priority = -1, bool bStartImmediately = true);

	/** Stop the dialogue on every channel matching the scope (graceful, with exit animations). E.g. Dialogue.Channel.Bark stops all bark channels; a leaf tag stops just that one. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Subsystem", meta = (Categories = "Dialogue.Channel"))
	void StopChannel(FGameplayTag InChannel);

	/** Stop all channels. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Subsystem")
	void StopAll();

	/** Interrupt the dialogue on every channel matching the scope (hard-stop). */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Subsystem", meta = (Categories = "Dialogue.Channel"))
	void InterruptChannel(FGameplayTag InChannel);

	/** Interrupt all channels. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Subsystem")
	void InterruptAll();

	/**
	 * Fill OutPlayers with every player currently held by the subsystem.
	 * Order is not guaranteed. Useful for debug commands and overlays. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Subsystem")
	void GetAllPlayers(TArray<UKzDialoguePlayer*>& OutPlayers) const;

	/**
	 * Reset the cached playback state of a single alias. Next time the alias is
	 * resolved, it starts as if it had never been played.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dialogue")
	void ResetAliasState(FGuid AliasId);

	/** Reset cached state for every alias. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue")
	void ResetAllAliasStates();

	//~ USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

private:
	UPROPERTY(Transient)
	TMap<FGameplayTag, TObjectPtr<UKzDialoguePlayer>> Players;

	/**
	 * Resolve the priority to use given:
	 *   - RequestedPriority (caller's request, possibly InheritPriority sentinel)
	 *   - AssetHintPriority (asset's Priority field, or InheritPriority if not asset-backed)
	 *   - ChannelDef (channel definition from settings, or null if undeclared)
	 *
	 * Precedence: explicit caller > asset hint > channel default > 0.
	 * The result is clamped to the channel's [MinPriority, MaxPriority] range when defined.
	 */
	int32 ResolvePriority(int32 RequestedPriority, int32 AssetHintPriority, const FKzDialogueChannelDefinition* ChannelDef) const;

	/** Look up the channel definition in settings, walking up the tag hierarchy so children
	 *  inherit their closest declared ancestor (declare Dialogue.Channel.Bark once and every
	 *  Bark.* channel uses it). Null and a warning when no ancestor is declared either. */
	const FKzDialogueChannelDefinition* FindChannelDefinition(const FGameplayTag& Tag) const;

	/** True if the dialogue currently playing on this player allows being interrupted. */
	bool IsActiveDialogueInterruptible(const UKzDialoguePlayer* Player) const;

	/** Resolves a line-or-alias id inside Asset to a concrete line (stateful alias selection). */
	bool ResolveAssetEntry(UKzDialogueAsset* Asset, const FGuid& LineId, FKzDialogueLine& OutLine);

	/**
	 * Per-alias playback state. Keyed by AliasId. Created lazily on first resolve.
	 *
	 * Holds whatever the alias's selection mode needs to remember between resolves
	 * (last picked line for RandomNoRepeat, shuffle order + cursor for ShuffleBag,
	 * cursor for Sequential, etc.).
	 */
	struct FAliasPlaybackState
	{
		/**
		 * Last line returned.
		 * Used by RandomNoRepeat and the cross-bag check in ShuffleBag.
		 * Invalid means "nothing played yet".
		 */
		FGuid LastPickedLineId;

		/** ShuffleBag: current bag of LineIds in shuffle order. Drained as lines play. */
		TArray<FGuid> ShuffleBag;

		/** ShuffleBag: index of the next line to take from the bag. */
		int32 ShuffleCursor = 0;

		/**
		 * ShuffleBag: snapshot of the alias's LineIds when the bag was built.
		 * If the alias's LineIds change (asset edited, hot reload), the bag
		 * is rebuilt.
		 */
		TArray<FGuid> ShuffleBagSourceIds;

		/** Sequential: index of the next line to play. */
		int32 SequentialCursor = 0;

		/**
		 * World time of the last successful resolution. Used to gate
		 * further resolutions while CooldownSeconds hasn't elapsed.
		 *
		 * Negative means "never resolved" — the cooldown check always passes on first use.
		 */
		double LastResolvedWorldTime = -1.0;
	};

	TMap<FGuid /*AliasId*/, FAliasPlaybackState> AliasStates;

	/**
	 * Resolve an alias to a concrete LineId according to its SelectionMode, updating
	 * the cached state. Returns an invalid FGuid if the alias has no lines.
	 */
	FGuid ResolveAliasInternal(const FKzDialogueAlias& Alias);
};