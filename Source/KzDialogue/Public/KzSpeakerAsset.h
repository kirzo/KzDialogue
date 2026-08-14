// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "KzSpeakerAsset.generated.h"

/**
 * Identity and display data of a dialogue character. One asset per character; lines,
 * aliases, speaker components and notifies reference it directly, and the reference
 * itself IS the speaker identity (no tag indirection). A hidden persona ("???") is
 * simply another speaker asset listed in the world component's ExtraPersonas.
 *
 * Naming: DisplayName wins when set. Otherwise the structured parts are composed with
 * the per-culture format from UKzDialogueSettings (surname-first cultures, honorifics).
 * Every name field is a localizable FText anchored to a stable GUID-derived key, so a
 * character's name is translated once here instead of per line.
 */
UCLASS(BlueprintType, Const)
class KZDIALOGUE_API UKzSpeakerAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Stable identifier used as the localization namespace suffix ("KzSpeaker.<AssetId>"). Generated once on creation and preserved across renames. */
	UPROPERTY(VisibleAnywhere, Category = "Speaker")
	FGuid AssetId;

	/** Simple display name. When set it is used as-is and the structured parts are ignored. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Speaker")
	FText DisplayName;

	/** Given (first) name, composed per culture when DisplayName is empty. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Speaker|Structured Name")
	FText GivenName;

	/** Family name (surname). Cultures like ja/zh/hu order it first via the settings' name format. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Speaker|Structured Name")
	FText FamilyName;

	/** Optional honorific ("Dr.", "-san"). Placement is decided by the per-culture name format. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Speaker|Structured Name")
	FText Honorific;

	/** Optional qualifier describing the character variant ("Teen", "Young", "Ghost of"). Not a form of address; placement is decided by the per-culture name format. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Speaker|Structured Name")
	FText Qualifier;

	/** CRC32 of DisplayName's source string. Updated automatically; used to detect translation drift. */
	UPROPERTY()
	uint32 SourceDisplayNameHash = 0;

	/** CRC32 of GivenName's source string. Same role as SourceDisplayNameHash. */
	UPROPERTY()
	uint32 SourceGivenNameHash = 0;

	/** CRC32 of FamilyName's source string. Same role as SourceDisplayNameHash. */
	UPROPERTY()
	uint32 SourceFamilyNameHash = 0;

	/** CRC32 of Honorific's source string. Same role as SourceDisplayNameHash. */
	UPROPERTY()
	uint32 SourceHonorificHash = 0;

	/** CRC32 of Qualifier's source string. Same role as SourceDisplayNameHash. */
	UPROPERTY()
	uint32 SourceQualifierHash = 0;

	/** Localized name for the active culture: DisplayName when set, else the structured parts composed with the per-culture format, else empty. */
	UFUNCTION(BlueprintPure, Category = "Speaker")
	FText GetResolvedDisplayName() const;

	//~ UObject
	virtual FPrimaryAssetId GetPrimaryAssetId() const override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostDuplicate(bool bDuplicateForPIE) override;
	virtual void PostLoad() override;
	virtual void PostInitProperties() override;

private:
	/** Editor-only invariant pass: AssetId, stable FText keys, source hashes. Idempotent. */
	void RefreshMetadata();

	/** Re-anchor every localizable FText to its stable GUID-derived (Namespace, Key). Idempotent. */
	void RebindFTextKeys();
#endif
};