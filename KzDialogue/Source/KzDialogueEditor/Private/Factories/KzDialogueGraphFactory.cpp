// Copyright 2026 kirzo

#include "Factories/KzDialogueGraphFactory.h"
#include "AssetToolsModule.h"
#include "Factories/KzDialogueAssetFactory.h"
#include "Graph/KzDialogueGraph.h"
#include "KzDialogueAsset.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"

UKzDialogueGraphFactory::UKzDialogueGraphFactory()
{
	SupportedClass = UKzDialogueGraph::StaticClass();
}

UObject* UKzDialogueGraphFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	UKzDialogueGraph* Graph = Cast<UKzDialogueGraph>(Super::FactoryCreateNew(Class, InParent, Name, Flags, Context, Warn));
	if (!Graph || !InParent || InParent->GetOutermost() == GetTransientPackage())
	{
		return Graph;
	}

	IAssetTools& AssetTools = FAssetToolsModule::GetModule().Get();
	const FString Path = FPackageName::GetLongPackagePath(InParent->GetOutermost()->GetName());
	FString PackageName;
	FString AssetName;
	AssetTools.CreateUniqueAssetName(Path / (Name.ToString() + TEXT("_Lines")), FString(), PackageName, AssetName);
	Graph->Lines = Cast<UKzDialogueAsset>(AssetTools.CreateAsset(AssetName, Path, UKzDialogueAsset::StaticClass(), NewObject<UKzDialogueAssetFactory>()));
	return Graph;
}