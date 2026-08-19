// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "KzNamedAsset.h"
#include "KzWordAsset.generated.h"

/** Grammatical gender of a speaker or word form; target languages decline shared words by it. */
UENUM(BlueprintType)
enum class EKzGender : uint8
{
	Unspecified,
	Masculine,
	Feminine
};

/**
 * A shared, localizable word ("Dr.", "Smith", "Teen"): common vocabulary that no single
 * owner should hold. Every referencer shares the SAME localization entry by construction,
 * so the word is translated exactly once. Optional gender forms cover languages whose
 * target words decline (es: Dr./Dra.) even when the source language spells them the same;
 * a missing or empty form falls back to Text.
 */
UCLASS(BlueprintType, Const)
class KZDIALOGUE_API UKzWordAsset : public UKzNamedAsset
{
	GENERATED_BODY()

public:
	/** Stable identifier used as the localization namespace suffix ("KzWord.<AssetId>"). Generated once on creation and preserved across renames. */
	UPROPERTY(VisibleAnywhere, Category = "Word")
	FGuid AssetId;

	/** Default form, used when the requested gender has no authored form. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Word")
	FText Text;

	/** Gendered forms. Author one even when the source spelling equals Text: each form is its own localization entry, so target languages can decline it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Word")
	TMap<EKzGender, FText> GenderForms;

	/** The word for the given gender: its authored form, or Text as fallback. */
	UFUNCTION(BlueprintPure, Category = "Word")
	FText Resolve(EKzGender Gender = EKzGender::Unspecified) const;

	//~ UKzNamedAsset: parts are gender form names ("{Gate:Feminine}"); None or unknown = Text.
	virtual FText ResolveName(FName Part = NAME_None) const override;

	//~ UObject
	virtual FPrimaryAssetId GetPrimaryAssetId() const override;
#if WITH_EDITOR
	virtual TArray<FName> GetNameParts() const override;
	virtual FText GetNamePartDescription(FName Part) const override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostDuplicate(bool bDuplicateForPIE) override;
	virtual void PostLoad() override;
	virtual void PostInitProperties() override;

private:
	/** Editor-only invariant pass: AssetId and stable FText keys. Idempotent. */
	void RefreshMetadata();

	/** Re-anchor every localizable FText to its stable GUID-derived (Namespace, Key). Idempotent. */
	void RebindFTextKeys();
#endif
};

/**
 * A localizable text authored either inline or through a shared UKzWordAsset. A referenced
 * word wins, and the owner's metadata refresh EMPTIES the inline text so the localization
 * gather only ever sees one of the two.
 */
USTRUCT(BlueprintType)
struct KZDIALOGUE_API FKzWordText
{
	GENERATED_BODY()

	/** Inline text, used when no word asset is referenced. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Text")
	FText Text;

	/** Shared word: one localization entry no matter how many owners reference it; gendered forms resolve by the owner's gender. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Text")
	TObjectPtr<UKzWordAsset> Word;

	bool IsEmpty() const { return !Word && KzIsTextSourceEmpty(Text); }

	/** The final text: the word asset's gender form when referenced, the inline text otherwise. */
	FText Resolve(EKzGender Gender = EKzGender::Unspecified) const { return Word ? Word->Resolve(Gender) : Text; }
};