// Copyright 2026 kirzo

#include "KzTokenText.h"

#include "Serialization/StructuredArchiveAdapters.h"
#include "UObject/PropertyTag.h"

#if WITH_EDITOR
#include "KzNamedAsset.h"
#include "AssetRegistry/AssetRegistryModule.h"
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
	// Same scheme as UKzDialogueAsset::TokenReferences: tokens come from the asset registry
	// tag, like the runtime lookup; unresolved tokens simply contribute nothing. While the
	// startup scan is still running the registry is incomplete, so the saved references are
	// kept.
	const IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	if (Registry.IsLoadingAssets()) { return false; }

	TSet<FName> UsedTokens;
	const FString* Source = FTextInspector::GetSourceString(Text);
	if (Source && !Source->IsEmpty())
	{
		TArray<FString> ArgumentNames;
		FTextFormat::FromString(*Source).GetFormatArgumentNames(ArgumentNames);
		for (const FString& ArgumentName : ArgumentNames)
		{
			FString Token = ArgumentName;
			FString Modifier;
			ArgumentName.Split(TEXT(":"), &Token, &Modifier);
			if (!Token.IsEmpty()) { UsedTokens.Add(FName(*Token)); }
		}
	}

	TArray<TSoftObjectPtr<UObject>> NewReferences;
	if (!UsedTokens.IsEmpty())
	{
		TArray<FAssetData> NamedAssets;
		Registry.GetAssetsByClass(UKzNamedAsset::StaticClass()->GetClassPathName(), NamedAssets, /*bSearchSubClasses=*/true);
		for (const FAssetData& Data : NamedAssets)
		{
			FName AssetToken;
			if (Data.GetTagValue(GET_MEMBER_NAME_CHECKED(UKzNamedAsset, Token), AssetToken) && UsedTokens.Contains(AssetToken))
			{
				NewReferences.Emplace(Data.ToSoftObjectPath());
			}
		}
		NewReferences.Sort([](const TSoftObjectPtr<UObject>& A, const TSoftObjectPtr<UObject>& B) { return A.ToString() < B.ToString(); });
	}

	if (NewReferences != TokenReferences)
	{
		TokenReferences = MoveTemp(NewReferences);
		return true;
	}
	return false;
}

#endif