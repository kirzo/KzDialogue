// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "KzDialogueTypes.h"
#include "KzGraph.h"
#include "KzDialogueGraph.generated.h"

class UKzDialogueAsset;

/**
 * A dialogue with branches: lines, choices, conditions and events in a graph that a play walks from one of its entries.
 * Its lines live in dialogue assets, so their audio, timelines and localization work as everywhere else.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Dialogue Graph"))
class KZDIALOGUE_API UKzDialogueGraph : public UKzGraph
{
	GENERATED_BODY()

public:
	/** The channel every line of a play goes through, whatever channel the line has. Empty takes the default channel. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue Graph", meta = (Categories = "Dialogue.Channel"))
	FGameplayTag Channel;

	/** -1 takes the channel's default priority. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue Graph")
	int32 Priority = -1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue Graph")
	bool bInterruptible = true;

	/** Automatic: lines advance after their duration. Manual: each line holds until Next. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue Graph")
	EKzDialogueAdvanceMode AdvanceMode = EKzDialogueAdvanceMode::Automatic;

	/** What the dialogue memory files this graph's visits under. Kept across renames, new for a duplicate. */
	UPROPERTY(VisibleAnywhere, AdvancedDisplay, Category = "Dialogue Graph")
	FGuid GraphId;

	virtual void PostInitProperties() override;
#if WITH_EDITOR
	virtual void PostDuplicate(bool bDuplicateForPIE) override;
#endif

#if WITH_EDITORONLY_DATA
	/** The dialogue asset new Line nodes write their lines to. */
	UPROPERTY(EditAnywhere, Category = "Dialogue Graph")
	TSoftObjectPtr<UKzDialogueAsset> Lines;
#endif

#if WITH_EDITOR
	virtual const UScriptStruct* GetNodeStruct() const override;
	virtual EKzGraphFlow GetFlow() const override { return EKzGraphFlow::Vertical; }
	virtual void GetDefaultNodes(TArray<const UScriptStruct*>& OutNodes) const override;
	virtual void ValidateGraph(const FKzGraphData& InData, TArray<FKzValidationIssue>& OutIssues) const override;

	/** Another dialogue graph dropped on this one makes a Sub Graph of it. */
	virtual bool MakeNodeForAsset(UObject& Asset, TInstancedStruct<FKzGraphNode>& OutNode) const override;

	/** Its lines asset, and those its nodes pick lines from: lines are written from the graph. */
	virtual void GetAssetsSavedWith(TArray<UObject*>& OutAssets) const override;
#endif
};