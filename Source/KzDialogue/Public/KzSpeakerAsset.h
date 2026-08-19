// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "KzWordAsset.h"
#include "KzSpeakerAsset.generated.h"

/**
 * Identity and display data of a dialogue character. One asset per character; lines,
 * aliases, speaker components and notifies reference it directly, and the reference
 * itself IS the speaker identity (no tag indirection). A hidden persona ("???") is
 * simply another speaker asset listed in the world component's ExtraPersonas.
 *
 * Naming: DisplayName wins when set. Otherwise the structured parts are composed with
 * the per-culture format from UKzDialogueSettings (surname-first cultures, honorifics).
 * Every name field is a localizable text anchored to a stable GUID-derived key, so a
 * character's name is translated once here instead of per line. Structured parts may
 * also reference a shared UKzWordAsset (family names, honorifics: common vocabulary),
 * which then resolves through the speaker's Gender and localizes once for everyone.
 */
UCLASS(BlueprintType, Const)
class KZDIALOGUE_API UKzSpeakerAsset : public UKzNamedAsset
{
	GENERATED_BODY()

public:
	/** Stable identifier used as the localization namespace suffix ("KzSpeaker.<AssetId>"). Generated once on creation and preserved across renames. */
	UPROPERTY(VisibleAnywhere, Category = "Speaker")
	FGuid AssetId;

	/** Simple display name. When set it is used as-is and the structured parts are ignored. Always direct text: a character's name is its own. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Speaker")
	FText DisplayName;

	/** Grammatical gender, used to pick the matching form of referenced shared words ("Dr." resolves to Dr./Dra. in declining languages). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Speaker")
	EKzGender Gender = EKzGender::Unspecified;

	/** Given (first) name, composed per culture when DisplayName is empty. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Speaker|Structured Name")
	FKzWordText GivenName;

	/** Family name (surname). Cultures like ja/zh/hu order it first via the settings' name format. A shared word asset lets siblings localize the surname once. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Speaker|Structured Name")
	FKzWordText FamilyName;

	/** Second family-name component: the Hispanic second surname, a Portuguese double surname, a patronymic... Placed by the name format's {Family2}; addressable as "{Token:family2}". */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Speaker|Structured Name")
	FKzWordText SecondFamilyName;

	/** Optional honorific ("Dr.", "-san"). Placement is decided by the per-culture name format. Common vocabulary: prefer a shared word asset. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Speaker|Structured Name")
	FKzWordText Honorific;

	/** Optional qualifier describing the character variant ("Teen", "Young", "Ghost of"). Not a form of address; placement is decided by the per-culture name format. Common vocabulary: prefer a shared word asset. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Speaker|Structured Name")
	FKzWordText Qualifier;

	/** Informal address ("Bob" for Robert): how other characters call this one in dialogue, via "{Token:nick}". Not part of the default composed name; the {Nick} format arg exists for projects that want it composed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Speaker|Structured Name")
	FKzWordText NickName;

	/** CRC32 of DisplayName's source string. Updated automatically; used to detect translation drift. */
	UPROPERTY()
	uint32 SourceDisplayNameHash = 0;

	/** CRC32 of GivenName's source string. Same role as SourceDisplayNameHash. */
	UPROPERTY()
	uint32 SourceGivenNameHash = 0;

	/** CRC32 of FamilyName's source string. Same role as SourceDisplayNameHash. */
	UPROPERTY()
	uint32 SourceFamilyNameHash = 0;

	/** CRC32 of SecondFamilyName's source string. Same role as SourceDisplayNameHash. */
	UPROPERTY()
	uint32 SourceSecondFamilyNameHash = 0;

	/** CRC32 of Honorific's source string. Same role as SourceDisplayNameHash. */
	UPROPERTY()
	uint32 SourceHonorificHash = 0;

	/** CRC32 of Qualifier's source string. Same role as SourceDisplayNameHash. */
	UPROPERTY()
	uint32 SourceQualifierHash = 0;

	/** CRC32 of NickName's source string. Same role as SourceDisplayNameHash. */
	UPROPERTY()
	uint32 SourceNickNameHash = 0;

	/** Localized name for the active culture: DisplayName when set, else the structured parts composed with the per-culture format, else empty. */
	UFUNCTION(BlueprintPure, Category = "Speaker")
	FText GetResolvedDisplayName() const;

	/** The structured parts composed with the per-culture format; excluded parts contribute nothing. bWithFamily covers both surname components. */
	FText ComposeStructuredName(bool bWithHonorific, bool bWithQualifier, bool bWithFamily = true) const;

	//~ UKzNamedAsset: parts are given / family / family2 / nick / honorific / qualifier / gender / display / fullname (structured composition even when DisplayName is set) / no-honorific / no-qualifier / no-family (composition without either surname: "Teen Kirzo"); None or unknown = GetResolvedDisplayName.
	virtual FText ResolveName(FName Part = NAME_None) const override;

	//~ UObject
	virtual FPrimaryAssetId GetPrimaryAssetId() const override;
#if WITH_EDITOR
	virtual TArray<FName> GetNameParts() const override;
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