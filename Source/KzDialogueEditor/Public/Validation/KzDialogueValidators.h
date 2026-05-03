// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "Validation/KzAssetValidator.h"
#include "KzDialogueValidators.generated.h"

class UKzDialogueAsset;

/** Reports lines that have neither text nor audio. */
UCLASS()
class KZDIALOGUEEDITOR_API UKzDialogueValidator_EmptyLines : public UKzAssetValidator
{
	GENERATED_BODY()
public:
	virtual bool CanValidate_Implementation(const UObject* Asset) const override;
	virtual void Validate_Implementation(const UObject* Asset, TArray<FKzValidationIssue>& OutIssues) const override;
};

/** Reports lines whose Audio soft pointer cannot be resolved (asset moved/deleted). */
UCLASS()
class KZDIALOGUEEDITOR_API UKzDialogueValidator_BrokenAudio : public UKzAssetValidator
{
	GENERATED_BODY()
public:
	virtual bool CanValidate_Implementation(const UObject* Asset) const override;
	virtual void Validate_Implementation(const UObject* Asset, TArray<FKzValidationIssue>& OutIssues) const override;
};

/**
 * Reports lines that share the same LineId, which would make Sequencer references
 * ambiguous and break the asset's GUID invariants.
 */
UCLASS()
class KZDIALOGUEEDITOR_API UKzDialogueValidator_DuplicateLineIds : public UKzAssetValidator
{
	GENERATED_BODY()
public:
	virtual bool CanValidate_Implementation(const UObject* Asset) const override;
	virtual void Validate_Implementation(const UObject* Asset, TArray<FKzValidationIssue>& OutIssues) const override;
};