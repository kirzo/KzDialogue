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

/** Resolves the value of one line template token (e.g. "PlayerName" for "{PlayerName}"). Bind to a function (BP function graphs or C++ UFUNCTIONs; BP custom events cannot return values). */
DECLARE_DYNAMIC_DELEGATE_RetVal_OneParam(FText, FKzDialogueTextArgumentResolver, const FKzDialogueLine&, Line);

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
	 * Pass InheritPriority to fall back to the asset hint / channel default.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Subsystem", meta = (Categories = "Dialogue.Channel", AdvancedDisplay = "bStartImmediately,AdvanceMode"))
	UKzDialoguePlayer* Play(UKzDialogueProvider* Provider, FGameplayTag InChannel, int32 Priority = -1, bool bStartImmediately = true, EKzDialogueAdvanceMode AdvanceMode = EKzDialogueAdvanceMode::Automatic);

	/**
	 * Play a whole dialogue asset, resolving EACH line's channel and chaining runs across channel changes.
	 * Returns a UKzDialogueAssetSession that completes once for the whole asset (wait on it via
	 * OnDialogueFinished / IsPlaying). A valid InChannel forces every line onto it (single-channel,
	 * single run). AdvanceMode defaults to the asset's.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Subsystem", meta = (Categories = "Dialogue.Channel", AdvancedDisplay = "bStartImmediately,AdvanceMode"))
	UKzDialogueAssetSession* PlayAsset(UKzDialogueAsset* Asset, FGameplayTag InChannel, bool bStartImmediately = true, EKzDialogueAdvanceMode AdvanceMode = EKzDialogueAdvanceMode::Inherit);

	/**
	 * Play an ordered subset of an asset's entries (lines or aliases) with the same per-entry channel
	 * resolution and run chaining as PlayAsset. Returns a session that completes once for the whole list.
	 * A valid InChannel forces every entry onto it (single run); InheritPriority falls back to the asset
	 * hint / channel default. BP callers go through UKzDialogueFunctionLibrary::PlayDialogueLineList.
	 */
	UKzDialogueAssetSession* PlayLineList(UKzDialogueAsset* Asset, const TArray<FGuid>& LineIds, FGameplayTag InChannel, int32 Priority = InheritPriority, bool bStartImmediately = true, EKzDialogueAdvanceMode AdvanceMode = EKzDialogueAdvanceMode::Inherit);

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
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Subsystem", meta = (Categories = "Dialogue.Channel", AdvancedDisplay = "bStartImmediately,AdvanceMode"))
	UKzDialoguePlayer* PlayAssetLineList(UKzDialogueAsset* Asset, const TArray<FGuid>& LineIds, FGameplayTag InChannel, int32 Priority = -1, bool bStartImmediately = true, EKzDialogueAdvanceMode AdvanceMode = EKzDialogueAdvanceMode::Automatic);

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

	/** Removes a finished asset session from the subsystem's keep-alive list. Called by the session itself. */
	void ReleaseAssetSession(UKzDialogueAssetSession* Session);

	/** True while the subsystem is tearing down (Deinitialize); in-flight asset sessions use it to stop chaining. */
	bool IsDeinitializing() const { return bDeinitializing; }

	/** Resolves the advance mode: explicit override > asset's mode > Automatic. */
	EKzDialogueAdvanceMode ResolveAdvanceMode(EKzDialogueAdvanceMode Override, const UKzDialogueAsset* Asset) const;

	/**
	 * Reset the cached playback state of a single alias. Next time the alias is
	 * resolved, it starts as if it had never been played.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dialogue")
	void ResetAliasState(FGuid AliasId);

	/** Reset cached state for every alias. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue")
	void ResetAllAliasStates();

	/**
	 * Register an ambient resolver for one template token: "PlayerName" resolves "{PlayerName}"
	 * in any line played through this subsystem. Called when a line enters, only if the line's
	 * pattern uses the token AND the play call did not already provide a value for it (explicit
	 * per-play arguments win over ambient resolvers). Re-registering a token replaces it.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Subsystem")
	void RegisterTextArgumentResolver(FName Token, FKzDialogueTextArgumentResolver Resolver);

	/** Remove the resolver registered for Token. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Subsystem")
	void UnregisterTextArgumentResolver(FName Token);

	/** Resolver registered for Token, or null. Consulted by players when a line enters. */
	const FKzDialogueTextArgumentResolver* FindTextArgumentResolver(FName Token) const;

	/**
	 * Resolve a named-asset text token ("Kirzo" or "Kirzo:given"): the thing's localized
	 * name, optionally narrowed by the ":part" modifier (speaker name parts, word gender
	 * forms). Tokens come from UKzNamedAsset::Token, discovered through the asset registry;
	 * the asset loads on first resolve. False when no named asset claims the token.
	 */
	bool TryResolveNamedText(const FString& TokenAndModifier, FText& OutText) const;

	/** Blueprint access to named-asset tokens for text outside dialogue lines (objectives, UI). Empty when no named asset claims the token. */
	UFUNCTION(BlueprintPure, Category = "Dialogue|Subsystem")
	FText ResolveNamedText(const FString& TokenAndModifier) const;

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

	/** Ambient token resolvers, keyed by template token name. Dynamic delegates bind weakly, so stale owners just unbind. */
	TMap<FName, FKzDialogueTextArgumentResolver> TextArgumentResolvers;

	/** Named-asset tokens gathered from the asset registry on first resolve (world lifetime cache; assets load lazily). */
	mutable TMap<FName, FSoftObjectPath> NamedAssetTokens;
	mutable bool bNamedAssetTokensBuilt = false;

	/** Asset path claiming Token, building the registry-scan cache on first use. Null when unclaimed. */
	const FSoftObjectPath* FindNamedAssetPath(FName Token) const;

	/**
	 * Resolve the priority to use given:
	 *   - RequestedPriority (caller's request, possibly InheritPriority sentinel)
	 *   - AssetHintPriority (asset's Priority field, or InheritPriority if not asset-backed)
	 *   - ChannelDef (channel definition from settings, or null if undeclared)
	 *
	 * Precedence: explicit caller > asset hint > channel default > 0.
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