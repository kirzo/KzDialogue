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

/** Reports aliases without a defined speaker (warning — likely a setup mistake). */
UCLASS()
class KZDIALOGUEEDITOR_API UKzDialogueValidator_AliasMissingSpeaker : public UKzAssetValidator
{
	GENERATED_BODY()
public:
	virtual bool CanValidate_Implementation(const UObject* Asset) const override;
	virtual void Validate_Implementation(const UObject* Asset, TArray<FKzValidationIssue>& OutIssues) const override;
};

/** Reports aliases that reference no lines. */
UCLASS()
class KZDIALOGUEEDITOR_API UKzDialogueValidator_AliasEmpty : public UKzAssetValidator
{
	GENERATED_BODY()
public:
	virtual bool CanValidate_Implementation(const UObject* Asset) const override;
	virtual void Validate_Implementation(const UObject* Asset, TArray<FKzValidationIssue>& OutIssues) const override;
};

/** Reports aliases referencing LineIds that no longer exist in the asset. */
UCLASS()
class KZDIALOGUEEDITOR_API UKzDialogueValidator_AliasInvalidLine : public UKzAssetValidator
{
	GENERATED_BODY()
public:
	virtual bool CanValidate_Implementation(const UObject* Asset) const override;
	virtual void Validate_Implementation(const UObject* Asset, TArray<FKzValidationIssue>& OutIssues) const override;
};

/** Reports lines referenced by an alias that don't share the alias's speaker. */
UCLASS()
class KZDIALOGUEEDITOR_API UKzDialogueValidator_AliasSpeakerMismatch : public UKzAssetValidator
{
	GENERATED_BODY()
public:
	virtual bool CanValidate_Implementation(const UObject* Asset) const override;
	virtual void Validate_Implementation(const UObject* Asset, TArray<FKzValidationIssue>& OutIssues) const override;
};

/** Reports two or more aliases sharing the same name. */
UCLASS()
class KZDIALOGUEEDITOR_API UKzDialogueValidator_DuplicateAliasName : public UKzAssetValidator
{
	GENERATED_BODY()
public:
	virtual bool CanValidate_Implementation(const UObject* Asset) const override;
	virtual void Validate_Implementation(const UObject* Asset, TArray<FKzValidationIssue>& OutIssues) const override;
};

/**
 * Reports notify-timeline problems that fail silently at runtime: events with no notify or
 * no time source, point notifies placed past the line, and per-notify config errors (an unset
 * montage / sound / etc., reported via each notify's ValidateNotify).
 */
UCLASS()
class KZDIALOGUEEDITOR_API UKzDialogueValidator_Timelines : public UKzAssetValidator
{
	GENERATED_BODY()
public:
	virtual bool CanValidate_Implementation(const UObject* Asset) const override;
	virtual void Validate_Implementation(const UObject* Asset, TArray<FKzValidationIssue>& OutIssues) const override;
};

/**
 * Localization correctness checks: texts not anchored to their stable GUID-derived key, stale
 * translations (source text changed after they were made), translations exceeding the line's
 * MaxCharacters, and placeholder mismatches between source and translation. Completeness
 * (untranslated texts, missing localized audio) is the coverage report's business, not a
 * per-save warning. Archive-dependent checks are silently skipped while the project has no
 * localization target.
 */
UCLASS()
class KZDIALOGUEEDITOR_API UKzDialogueValidator_Localization : public UKzAssetValidator
{
	GENERATED_BODY()
public:
	virtual bool CanValidate_Implementation(const UObject* Asset) const override;
	virtual void Validate_Implementation(const UObject* Asset, TArray<FKzValidationIssue>& OutIssues) const override;
};