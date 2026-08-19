// Copyright 2026 kirzo

#include "KzNamedTokenSubsystem.h"

#include "KzNamedAsset.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY_STATIC(LogKzNamedToken, Log, All);

const FKzNamedTokenOverride* UKzNamedTokenSubsystem::FindOverrideFor(const UObject* WorldContextObject, FName Token)
{
	if (!WorldContextObject || Token.IsNone()) { return nullptr; }

	const UWorld* World = WorldContextObject->GetWorld();
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	const UKzNamedTokenSubsystem* Store = GameInstance ? GameInstance->GetSubsystem<UKzNamedTokenSubsystem>() : nullptr;
	return Store ? Store->FindOverride(Token) : nullptr;
}

bool UKzNamedTokenSubsystem::SetNamedTokenGender(FName Token, EKzGender Gender)
{
	if (!IsTokenClaimed(Token))
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
	if (!IsTokenClaimed(Token))
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

bool UKzNamedTokenSubsystem::IsTokenClaimed(FName Token) const
{
	if (Token.IsNone()) { return false; }

	// Set calls are rare (character creation, load), so a registry scan per call is fine.
	const IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	TArray<FAssetData> NamedAssets;
	Registry.GetAssetsByClass(UKzNamedAsset::StaticClass()->GetClassPathName(), NamedAssets, /*bSearchSubClasses=*/true);
	for (const FAssetData& Data : NamedAssets)
	{
		FName AssetToken;
		if (Data.GetTagValue(GET_MEMBER_NAME_CHECKED(UKzNamedAsset, Token), AssetToken) && AssetToken == Token)
		{
			return true;
		}
	}
	return false;
}