// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "KzDialogueTypes.h"
#include "KzDialogueAsset.generated.h"

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

	/**
	 * Logical identifier for the dialogue.
	 * Optional, useful for analytics or to reference dialogues by tag from gameplay.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue")
	FGameplayTag DialogueTag;

	/** When true, a higher-priority dialogue may interrupt this one mid-line. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue")
	bool bInterruptible = true;

	/** Higher numbers preempt lower-numbered dialogues on the same channel. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue", meta = (ClampMin = 0))
	int32 Priority = 0;

	/** Default channel suggestion (subsystem call may override). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue", meta = (Categories = "Dialogue.Channel"))
	FGameplayTag DefaultChannel;

	/** The lines, in playback order. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue")
	TArray<FKzDialogueLine> Lines;

	/** Reusable named groups of lines selected randomly at playback time. */
	UPROPERTY(EditAnywhere, Category = "Dialogue")
	TArray<FKzDialogueAlias> Aliases;

	/** Find a line by its stable id. Returns INDEX_NONE if not found. */
	UFUNCTION(BlueprintPure, Category = "Dialogue")
	int32 IndexOfLine(const FGuid& LineId) const;

	/** Get a line by its stable id. */
	UFUNCTION(BlueprintPure, Category = "Dialogue")
	bool TryGetLineById(const FGuid& LineId, FKzDialogueLine& OutLine) const;

	/** Look up an alias by name. */
	UFUNCTION(BlueprintPure, Category = "Dialogue")
	bool TryGetAliasByName(FName AliasName, FKzDialogueAlias& OutAlias) const;

	/** Look up an alias by GUID. */
	UFUNCTION(BlueprintPure, Category = "Dialogue")
	bool TryGetAliasById(const FGuid& AliasId, FKzDialogueAlias& OutAlias) const;

	/**
	 * Resolve an alias to one of its lines (random pick).
	 * Returns false if the alias is unknown, empty, or all referenced LineIds are stale.
	 */
	bool TryResolveAlias(const FGuid& AliasId, FKzDialogueLine& OutLine) const;
	bool TryResolveAlias(FName AliasName, FKzDialogueLine& OutLine) const;

	/**
	 * Resolve a GUID to a concrete line. The GUID can be either a LineId or an
	 * AliasId — the asset checks both tables. Returns false if neither matches.
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

private:
	/** Editor-only invariant pass: ensures unique GUIDs, anchors FText keys, and refreshes source text hashes. Idempotent. */
	void RefreshLineMetadata();

	/** Re-anchor every FText in the asset to its stable, GUID-derived (Namespace, Key). Idempotent. */
	void RebindFTextKeys();
#endif
};