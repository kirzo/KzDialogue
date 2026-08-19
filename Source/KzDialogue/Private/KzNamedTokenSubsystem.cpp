// Copyright 2026 kirzo

#include "KzNamedTokenSubsystem.h"

#include "KzNamedAsset.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY_STATIC(LogKzNamedToken, Log, All);

UKzNamedTokenSubsystem* UKzNamedTokenSubsystem::Get(const UObject* WorldContextObject)
{
	if (!WorldContextObject) { return nullptr; }

	const UWorld* World = WorldContextObject->GetWorld();
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UKzNamedTokenSubsystem>() : nullptr;
}

const FKzNamedTokenOverride* UKzNamedTokenSubsystem::FindOverrideFor(const UObject* WorldContextObject, FName Token)
{
	const UKzNamedTokenSubsystem* Store = Get(WorldContextObject);
	return (Store && !Token.IsNone()) ? Store->FindOverride(Token) : nullptr;
}

bool UKzNamedTokenSubsystem::SetNamedTokenGender(FName Token, EKzGender Gender)
{
	if (!FindNamedAssetPath(Token))
	{
		UE_LOG(LogKzNamedToken, Warning, TEXT("SetNamedTokenGender: no named asset claims token '%s'; create the asset first (it is the token's declaration and fallback)."), *Token.ToString());
		return false;
	}

	FKzNamedTokenOverride& Override = Overrides.Tokens.FindOrAdd(Token);
	Override.bOverrideGender = true;
	Override.Gender = Gender;
	return true;
}

bool UKzNamedTokenSubsystem::SetNamedTokenPart(FName Token, FName Part, FText Text)
{
	if (!FindNamedAssetPath(Token))
	{
		UE_LOG(LogKzNamedToken, Warning, TEXT("SetNamedTokenPart: no named asset claims token '%s'; create the asset first (it is the token's declaration and fallback)."), *Token.ToString());
		return false;
	}

	Overrides.Tokens.FindOrAdd(Token).Parts.Add(Part, Text);
	return true;
}

void UKzNamedTokenSubsystem::ClearNamedTokenOverride(FName Token)
{
	Overrides.Tokens.Remove(Token);
}

void UKzNamedTokenSubsystem::ClearAllNamedTokenOverrides()
{
	Overrides.Tokens.Reset();
}

const FSoftObjectPath* UKzNamedTokenSubsystem::FindNamedAssetPath(FName Token) const
{
	if (Token.IsNone()) { return nullptr; }

	if (!bNamedAssetTokensBuilt)
	{
		bNamedAssetTokensBuilt = true;

		const IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
		TArray<FAssetData> Assets;
		Registry.GetAssetsByClass(UKzNamedAsset::StaticClass()->GetClassPathName(), Assets, /*bSearchSubClasses=*/true);

		for (const FAssetData& Asset : Assets)
		{
			FName AssetToken;
			if (!Asset.GetTagValue(GET_MEMBER_NAME_CHECKED(UKzNamedAsset, Token), AssetToken) || AssetToken.IsNone()) { continue; }

			if (const FSoftObjectPath* Existing = NamedAssetTokens.Find(AssetToken))
			{
				UE_LOG(LogKzNamedToken, Warning, TEXT("Named-asset token '%s' is claimed by both '%s' and '%s'; keeping the first."), *AssetToken.ToString(), *Existing->ToString(), *Asset.GetObjectPathString());
				continue;
			}
			NamedAssetTokens.Add(AssetToken, Asset.ToSoftObjectPath());
		}
	}

	return NamedAssetTokens.Find(Token);
}