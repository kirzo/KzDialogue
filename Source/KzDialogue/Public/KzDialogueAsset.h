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

	/** Find a line by its stable id. Returns INDEX_NONE if not found. */
	UFUNCTION(BlueprintPure, Category = "Dialogue")
	int32 IndexOfLine(const FGuid& LineId) const;

	/** Get a line by its stable id. */
	UFUNCTION(BlueprintPure, Category = "Dialogue")
	bool TryGetLineById(const FGuid& LineId, FKzDialogueLine& OutLine) const;

	//~ UObject
	virtual FPrimaryAssetId GetPrimaryAssetId() const override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent) override;
	virtual void PostDuplicate(bool bDuplicateForPIE) override;
	virtual void PostLoad() override;

	/** Editor helpers. Used by the custom asset editor. */
	void EnsureLineGuids();
#endif
};