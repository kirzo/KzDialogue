// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "KzDialogueTypes.h"
#include "KzDialogueAsset.generated.h"

class UKzDialogueTimeline;

/**
 * Authorable asset describing a linear dialogue: an ordered list of lines plus
 * metadata. Pluggable inside Sequencer tracks, dialogue subsystem, speaker components,
 * or any custom pipeline.
 */
UCLASS(BlueprintType)
class KZDIALOGUE_API UKzDialogueAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UKzDialogueAsset();

	/** Stable identifier used as the localization namespace suffix for this asset's FTexts. Generated once on creation and preserved across renames. */
	UPROPERTY(VisibleAnywhere, Category = "Dialogue")
	FGuid AssetId;

	/** Production-facing label shown by tooling (dialogue dashboard) instead of the asset name. Plain string on purpose: an internal label that must not enter the localization gather. */
	UPROPERTY(EditAnywhere, Category = "Dialogue")
	FString DisplayName;

	/**
	 * Logical identifier for the dialogue.
	 * Optional, useful for analytics or to reference dialogues by tag from gameplay.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue")
	FGameplayTag DialogueTag;

	/**
	 * Free-form tags shared by every line in this dialogue (mood, speaker, context, ...). They are merged
	 * into each line's own Tags when the line is resolved, so a view that only receives the resolved line
	 * sees the union (asset + line). Query it with UKzDialogueFunctionLibrary::LineHasTag and friends.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue")
	FGameplayTagContainer Tags;

	/** When false, no request takes this dialogue's channel while it plays, whatever its priority. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue")
	bool bInterruptible = true;

	/** Priority on its channel: an equal or higher one preempts the playing dialogue. -1 uses the channel default. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue", meta = (ClampMin = -1))
	int32 Priority = -1;

	/** Default channel suggestion (subsystem call may override). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue", meta = (Categories = "Dialogue.Channel"))
	FGameplayTag DefaultChannel;

	/** How this dialogue advances by default: auto-timed, or holding each line until Next() (RPG-style). The callsite may override. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue")
	EKzDialogueAdvanceMode AdvanceMode = EKzDialogueAdvanceMode::Automatic;

	/** The lines, in playback order. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue")
	TArray<FKzDialogueLine> Lines;

	/** Reusable named groups of lines selected randomly at playback time. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue")
	TArray<FKzDialogueAlias> Aliases;

	/** Per-line notify timelines, owned by the asset and keyed by line id. Managed by the line editor. */
	UPROPERTY(Instanced)
	TArray<TObjectPtr<UKzDialogueTimeline>> Timelines;

	/** Named assets referenced by this asset's line-text tokens ("{Kirzo}"), refreshed at save. Soft references on purpose: the cooker follows them, so token-only referenced things package without Asset Manager rules, while nothing force-loads at runtime. */
	UPROPERTY(VisibleAnywhere, AdvancedDisplay, Category = "Dialogue")
	TArray<TSoftObjectPtr<UObject>> TokenReferences;

	/** Find a line by its stable id. Returns INDEX_NONE if not found. */
	UFUNCTION(BlueprintPure, Category = "Dialogue|Asset")
	int32 IndexOfLine(const FGuid& LineId) const;

	/** Find the notify timeline owned by the given line id, or null. */
	UKzDialogueTimeline* FindTimelineForLine(const FGuid& LineId) const;

	/**
	 * Completes a resolved line copy with the asset's runtime data so a consumer (view) gets a full line
	 * without needing the asset: its per-line timeline, and the asset-wide Tags merged into the line's own.
	 * Call on every line handed out by a resolve path or the provider.
	 */
	void FinalizeResolvedLine(FKzDialogueLine& OutLine) const;

	/** Get a line by its stable id. */
	UFUNCTION(BlueprintPure, Category = "Dialogue|Asset")
	bool TryGetLineById(const FGuid& LineId, FKzDialogueLine& OutLine) const;

	/** Look up an alias by name. */
	UFUNCTION(BlueprintPure, Category = "Dialogue|Asset")
	bool TryGetAliasByName(FName AliasName, FKzDialogueAlias& OutAlias) const;

	/** Look up an alias by GUID. */
	UFUNCTION(BlueprintPure, Category = "Dialogue|Asset")
	bool TryGetAliasById(const FGuid& AliasId, FKzDialogueAlias& OutAlias) const;

	/**
	 * Resolve an alias to one of its lines (random pick).
	 * Returns false if the alias is unknown, empty, or all referenced LineIds are stale.
	 */
	bool TryResolveAlias(const FGuid& AliasId, FKzDialogueLine& OutLine) const;
	bool TryResolveAlias(FName AliasName, FKzDialogueLine& OutLine) const;

	/**
	 * Resolve a GUID to a concrete line. The GUID can be either a LineId or an
	 * AliasId; the asset checks both tables. Returns false if neither matches.
	 */
	bool TryResolveLineOrAlias(const FGuid& Id, FKzDialogueLine& OutLine) const;

	/**
	 * Resolve a reference (line or alias) to a concrete line. The same as
	 * TryGetLineById when the reference points to a line, or TryResolveAlias
	 * when it points to an alias.
	 */
	bool TryResolveReference(const FKzDialogueAssetReference& Reference, FKzDialogueLine& OutLine) const;

	//~ UObject
	virtual FPrimaryAssetId GetPrimaryAssetId() const override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent) override;
	virtual void PostDuplicate(bool bDuplicateForPIE) override;
	virtual void PostLoad() override;
	virtual void PostInitProperties() override;
	virtual void GetAssetRegistryTags(FAssetRegistryTagsContext Context) const override;

	/** Registry tag names exported by GetAssetRegistryTags; read by the dialogue dashboard without loading assets. */
	static const FName TagLineCount;
	static const FName TagVoicedCount;
	static const FName TagSpeakers;
	static const FName TagDisplayName;

	/** The (Namespace, Key) a line's Text anchors to: KzDialogue.Shared/<SharedTextId> when shared, else KzDialogue.<AssetId>/<LineId>-Text. */
	TPair<FString, FString> GetLineTextId(const FKzDialogueLine& Line) const;

	/** The line a localization identity belongs to, by its shared id or by its own key (still valid after a merge). INDEX_NONE if none. */
	int32 IndexOfLineByTextId(const FString& Namespace, const FString& Key) const;

private:
	/** Editor-only invariant pass: ensures unique GUIDs, anchors FText keys, and refreshes source text hashes. Idempotent. */
	void RefreshLineMetadata();

	/** Re-anchor every FText in the asset to its stable, GUID-derived (Namespace, Key). True when a text changed. */
	bool RebindFTextKeys();
#endif
};