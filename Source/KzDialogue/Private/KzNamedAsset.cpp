// Copyright 2026 kirzo

#include "KzNamedAsset.h"

#include "KzNamedTokenSubsystem.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#endif

FText UKzNamedAsset::ResolveName(FName Part, const FKzNamedTokenOverride* Overrides) const
{
	if (Overrides && !Part.IsNone())
	{
		if (const FText* Pinned = Overrides->Parts.Find(Part))
		{
			return *Pinned;
		}
	}
	return FText::GetEmpty();
}

#if WITH_EDITOR

UKzNamedAsset::FKzOnNamedTokenRenamed UKzNamedAsset::OnTokenRenamed;

void UKzNamedAsset::GatherTextTokens(const FText& Text, TSet<FName>& OutTokens)
{
	const FString* Source = FTextInspector::GetSourceString(Text);
	if (!Source || Source->IsEmpty()) { return; }

	TArray<FString> ArgumentNames;
	FTextFormat::FromString(*Source).GetFormatArgumentNames(ArgumentNames);
	for (const FString& ArgumentName : ArgumentNames)
	{
		FString Token = ArgumentName;
		FString Modifier;
		ArgumentName.Split(TEXT(":"), &Token, &Modifier);
		if (!Token.IsEmpty()) { OutTokens.Add(FName(*Token)); }
	}
}

bool UKzNamedAsset::RefreshTokenReferences(const TSet<FName>& Tokens, TArray<TSoftObjectPtr<UObject>>& References)
{
	const IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	if (Registry.IsLoadingAssets()) { return false; }

	TArray<TSoftObjectPtr<UObject>> NewReferences;
	if (!Tokens.IsEmpty())
	{
		TArray<FAssetData> NamedAssets;
		Registry.GetAssetsByClass(StaticClass()->GetClassPathName(), NamedAssets, /*bSearchSubClasses=*/true);
		for (const FAssetData& Data : NamedAssets)
		{
			FName AssetToken;
			if (Data.GetTagValue(GET_MEMBER_NAME_CHECKED(UKzNamedAsset, Token), AssetToken) && Tokens.Contains(AssetToken))
			{
				NewReferences.Emplace(Data.ToSoftObjectPath());
			}
		}
		NewReferences.Sort([](const TSoftObjectPtr<UObject>& A, const TSoftObjectPtr<UObject>& B) { return A.ToString() < B.ToString(); });
	}

	if (NewReferences == References) { return false; }
	References = MoveTemp(NewReferences);
	return true;
}

void UKzNamedAsset::PreEditChange(FProperty* PropertyAboutToChange)
{
	Super::PreEditChange(PropertyAboutToChange);
	TokenBeforeEdit = Token;
}

void UKzNamedAsset::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UKzNamedAsset, Token) && Token != TokenBeforeEdit && !TokenBeforeEdit.IsNone())
	{
		OnTokenRenamed.Broadcast(this, TokenBeforeEdit);
	}
}

void UKzNamedAsset::PreEditUndo()
{
	Super::PreEditUndo();
	TokenBeforeEdit = Token;
}

void UKzNamedAsset::PostEditUndo()
{
	Super::PostEditUndo();

	// Undoing or redoing a rename is a rename too: the transaction restores the lines, the listeners the archives.
	if (Token != TokenBeforeEdit && !TokenBeforeEdit.IsNone())
	{
		OnTokenRenamed.Broadcast(this, TokenBeforeEdit);
	}
}

#endif