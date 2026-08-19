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
 * Reports broken custom audio playback ranges: an empty range (AudioEndTime at or before
 * AudioStartTime), a start beyond the wave, or a range on audio that is also marked for
 * localization (localized takes have their own timing, so a source-wave cut cannot apply).
 */
UCLASS()
class KZDIALOGUEEDITOR_API UKzDialogueValidator_AudioRange : public UKzAssetValidator
{
	GENERATED_BODY()
public:
	virtual bool CanValidate_Implementation(const UObject* Asset) const override;
	virtual void Validate_Implementation(const UObject* Asset, TArray<FKzValidationIssue>& OutIssues) const override;
};

/**
 * Reports voiced lines whose text changed after their audio was assigned: the recording no
 * longer matches the script and needs a re-record. Assigning a different take re-baselines
 * automatically; the Localization tab's Accept action clears the state for the same take.
 */
UCLASS()
class KZDIALOGUEEDITOR_API UKzDialogueValidator_StaleAudio : public UKzAssetValidator
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
 * MaxCharacters, placeholder mismatches between source and translation, and named-asset
 * token references ("{Token:part}" with no claiming asset or an unknown part). Also validates
 * named assets (duplicate tokens) and speaker assets (name field key anchoring + stale name
 * translations). Completeness (untranslated texts, missing localized audio) is the coverage
 * report's business, not a per-save warning. Archive-dependent checks are silently skipped
 * while the project has no localization target.
 */
UCLASS()
class KZDIALOGUEEDITOR_API UKzDialogueValidator_Localization : public UKzAssetValidator
{
	GENERATED_BODY()
public:
	virtual bool CanValidate_Implementation(const UObject* Asset) const override;
	virtual void Validate_Implementation(const UObject* Asset, TArray<FKzValidationIssue>& OutIssues) const override;

private:
	/** Speaker-asset branch: key anchoring and stale translations of the name fields. */
	void ValidateSpeakerAsset(const class UKzSpeakerAsset* Speaker, TArray<FKzValidationIssue>& OutIssues) const;

	/** Named-asset branch: another asset claiming the same token is an error (text references resolve to only one). */
	void ValidateNamedAssetToken(const class UKzNamedAsset* Named, TArray<FKzValidationIssue>& OutIssues) const;
};