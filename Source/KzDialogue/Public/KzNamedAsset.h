// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "KzNamedAsset.generated.h"

/** True when the text has no SOURCE string. FText::IsEmpty checks the display string, which for a KEYED empty text can resolve a stale translation and lie. */
FORCEINLINE bool KzIsTextSourceEmpty(const FText& Text)
{
	const FString* Source = FTextInspector::GetSourceString(Text);
	return !Source || Source->IsEmpty();
}

/**
 * Something with a proper name that text can reference by token: a character, a place, an
 * object. A line saying "Open the door, {Kirzo}." resolves the token to this asset's
 * localized name when it plays; "{Kirzo:part}" picks a specific rendering (the subclass
 * defines its parts). The asset IS the registry: Token is exported as an asset registry
 * tag, so pickers, validators and the runtime lookup enumerate named assets without
 * loading them. Primary data asset so a single Asset Manager rule cooks token-only
 * referenced things.
 */
UCLASS(Abstract, BlueprintType)
class KZDIALOGUE_API UKzNamedAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Public token addressing this thing from text: "{Token}" / "{Token:part}" in dialogue lines and UI. None = not addressable. Two assets sharing a token is a validation error; the runtime keeps the first found. If cooked-tag filtering is ever enabled, this tag must be allowlisted. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Named Asset", AssetRegistrySearchable)
	FName Token;

	/** The localized name rendered for a "{Token:Part}" reference. None = the default rendering; unknown parts fall back to it. */
	UFUNCTION(BlueprintPure, Category = "Named Asset")
	virtual FText ResolveName(FName Part = NAME_None) const;

#if WITH_EDITOR
	DECLARE_MULTICAST_DELEGATE_TwoParams(FKzOnNamedTokenRenamed, UKzNamedAsset* /*Asset*/, FName /*OldToken*/);

	/** Fired after Token changes on an asset (old value included); the editor module rewrites the lines referencing the old token. */
	static FKzOnNamedTokenRenamed OnTokenRenamed;

	/** Valid ":part" modifiers of this type, feeding the token picker and the validator. */
	virtual TArray<FName> GetNameParts() const { return {}; }

	virtual void PreEditChange(FProperty* PropertyAboutToChange) override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

private:
	/** Token value captured before an edit, so the rename broadcast can carry the old name. */
	FName TokenBeforeEdit;
#endif
};