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

	/** Get (and lazily create) the player associated with a channel. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Subsystem", meta = (Categories = "Dialogue.Channel"))
	UKzDialoguePlayer* GetOrCreatePlayer(FGameplayTag InChannel);

	/** Get the player for a channel without creating one. */
	UFUNCTION(BlueprintPure, Category = "Dialogue|Subsystem", meta = (Categories = "Dialogue.Channel"))
	UKzDialoguePlayer* FindPlayer(FGameplayTag InChannel) const;

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

	/** Stop the dialogue on a channel (graceful, with exit animations). */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Subsystem", meta = (Categories = "Dialogue.Channel"))
	void StopChannel(FGameplayTag InChannel);

	/** Stop all channels. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Subsystem")
	void StopAll();

	/** Interrupt the dialogue on a channel (hard-stop). */
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

	/** Look up the channel definition in settings, or null if not declared. Logs a
	 *  warning the first time an undeclared channel is used. */
	const FKzDialogueChannelDefinition* FindChannelDefinition(const FGameplayTag& Tag) const;

	/** True if the dialogue currently playing on this player allows being interrupted. */
	bool IsActiveDialogueInterruptible(const UKzDialoguePlayer* Player) const;

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