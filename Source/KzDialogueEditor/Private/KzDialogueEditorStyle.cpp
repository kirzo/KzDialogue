// Copyright 2026 kirzo

#include "KzDialogueEditorStyle.h"

FLinearColor FKzDialogueEditorStyle::BrandColor = FColor::FromHex(TEXT("#4A6EB6"));

FKzDialogueEditorStyle::FKzDialogueEditorStyle()
	: TKzEditorStyle_Base(FName("KzDialogueEditorStyle"))
{
	SetupPluginResources(TEXT("KzDialogue"));

	AddClassIcon(TEXT("KzDialogueAsset"), TEXT("KzDialogueAsset_16x"));
	AddClassThumbnail(TEXT("KzDialogueAsset"), TEXT("KzDialogueAsset_64x"));

	AddClassIcon(TEXT("KzSpeakerAsset"), TEXT("KzDialogueSpeaker_16x"));
	AddClassThumbnail(TEXT("KzSpeakerAsset"), TEXT("KzDialogueSpeaker_64x"));

	const FVector2D IconSize(16.f, 16.f);

	Set("Kz.Dialogue.Icon", new FSlateImageBrush(RootToContentDir(TEXT("KzDialogueAsset_16x"), TEXT(".png")), IconSize));
	Set("Kz.Dialogue.SpeakerIcon", new FSlateImageBrush(RootToContentDir(TEXT("KzDialogueSpeaker_16x"), TEXT(".png")), IconSize));
}