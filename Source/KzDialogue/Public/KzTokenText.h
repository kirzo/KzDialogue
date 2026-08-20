// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "KzTokenText.generated.h"

/**
 * A localizable text that may reference named-asset tokens ("Open the door, {Kirzo}.").
 * Same runtime data as a plain FText; the type exists so every property of it gets the
 * token-aware editor (the "{}" browser, the inline "{" autocomplete, the localization
 * toggle) and resolves through UKzDialogueFunctionLibrary::ResolveTokenText. Replacing an
 * existing C++ FText property with this struct keeps its saved data: the struct loads
 * from TextProperty tags.
 */
USTRUCT(BlueprintType)
struct KZDIALOGUE_API FKzTokenText
{
	GENERATED_BODY()

	/** The authored text, tokens unresolved. Localizable like any FText; tokens survive into translations and resolve at display time. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text")
	FText Text;

	/** Named assets the text references by token, as soft references so token-only referenced things cook. Refreshed by the editor customization whenever the text changes. */
	UPROPERTY()
	TArray<TSoftObjectPtr<UObject>> TokenReferences;

	bool SerializeFromMismatchedTag(const struct FPropertyTag& Tag, FStructuredArchive::FSlot Slot);

#if WITH_EDITOR
	/** Rescan Text for named-asset tokens and rebuild TokenReferences. Kept as-is while the registry startup scan runs. True when the list changed. */
	bool RefreshTokenReferences();
#endif
};

template<>
struct TStructOpsTypeTraits<FKzTokenText> : public TStructOpsTypeTraitsBase2<FKzTokenText>
{
	enum
	{
		WithStructuredSerializeFromMismatchedTag = true,
	};
};