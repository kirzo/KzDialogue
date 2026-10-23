// Copyright 2026 kirzo

#include "KzTokenText.h"

#include "Serialization/StructuredArchiveAdapters.h"
#include "UObject/PropertyTag.h"

#if WITH_EDITOR
#include "KzNamedAsset.h"
#endif

bool FKzTokenText::SerializeFromMismatchedTag(const FPropertyTag& Tag, FStructuredArchive::FSlot Slot)
{
	// Drop-in replacement for a plain FText property: old assets load their TextProperty
	// payload straight into Text.
	if (Tag.Type == NAME_TextProperty)
	{
		Slot << Text;
		return true;
	}
	return false;
}

#if WITH_EDITOR

bool FKzTokenText::RefreshTokenReferences()
{
	TSet<FName> UsedTokens;
	UKzNamedAsset::GatherTextTokens(Text, UsedTokens);
	return UKzNamedAsset::RefreshTokenReferences(UsedTokens, TokenReferences);
}

#endif