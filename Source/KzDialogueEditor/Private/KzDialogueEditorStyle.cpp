// Copyright 2026 kirzo

#include "KzDialogueEditorStyle.h"

FLinearColor FKzDialogueEditorStyle::BrandColor = FColor::FromHex(TEXT("#4A6EB6"));

FKzDialogueEditorStyle::FKzDialogueEditorStyle()
	: TKzEditorStyle_Base(FName("KzDialogueEditorStyle"))
{
	SetupPluginResources(TEXT("KzDialogue"));

	AddClassIcon(TEXT("KzDialogueAsset"), TEXT("KzDialogueAsset_16x"));
	AddClassThumbnail(TEXT("KzDialogueAsset"), TEXT("KzDialogueAsset_64x"));

	const FString IconPath = RootToContentDir(TEXT("KzDialogueAsset_16x"), TEXT(".png"));
	const FVector2D IconSize(16.f, 16.f);

	Set("Kz.Dialogue.Icon", new FSlateImageBrush(IconPath, IconSize));
}