// Copyright 2026 kirzo

#include "KzDialogueEditorStyle.h"
#include "Misc/Paths.h"

FLinearColor FKzDialogueEditorStyle::BrandColor = FColor::FromHex(TEXT("#4A6EB6"));

FKzDialogueEditorStyle::FKzDialogueEditorStyle()
	: TKzEditorStyle_Base(FName("KzDialogueEditorStyle"))
{
	SetupPluginResources(TEXT("KzDialogue"));

	const FVector2D IconSize(16.f, 16.f);
	const FVector2D ThumbnailSize(64.f, 64.f);
	auto Icon = [this](const TCHAR* File) { return RootToContentDir(FString(TEXT("Icons/")) + File, TEXT(".svg")); };

	// White line icon for lists, tabs and menus, colour figure for the Content Browser; SVG, which the base's PNG helpers do not load
	for (const TCHAR* const Asset : { TEXT("DialogueAsset"), TEXT("SpeakerAsset"), TEXT("WordAsset"), TEXT("NamedAsset") })
	{
		Set(*FString::Printf(TEXT("ClassIcon.Kz%s"), Asset), new FSlateVectorImageBrush(Icon(Asset), IconSize));
		Set(*FString::Printf(TEXT("ClassThumbnail.Kz%s"), Asset), new FSlateVectorImageBrush(Icon(*(FString(Asset) + TEXT("Color"))), ThumbnailSize));
	}

	// Components, the subtitle widget and the timeline notifies only show the small icon
	const TCHAR* const ClassIcons[][2] = {
		{ TEXT("KzDialogueSpeakerComponent"), TEXT("SpeakerAsset") },
		{ TEXT("KzBarkComponent"), TEXT("BarkComponent") },
		{ TEXT("KzSubtitleWidget"), TEXT("SubtitleWidget") },
		{ TEXT("KzDialogueNotify"), TEXT("DialogueNotify") },
		{ TEXT("KzDialogueNotifyState"), TEXT("DialogueNotifyState") },
		{ TEXT("KzDialogueNotify_CameraShake"), TEXT("CameraShake") },
		{ TEXT("KzDialogueNotifyState_SetTag"), TEXT("SetTag") },
	};
	for (const auto& Pair : ClassIcons)
	{
		Set(*FString::Printf(TEXT("ClassIcon.%s"), Pair[0]), new FSlateVectorImageBrush(Icon(Pair[1]), IconSize));
	}

	// Notifies that play an engine asset wear that asset's icon
	const FString EngineAssetIcons = FPaths::EngineContentDir() / TEXT("Editor/Slate/Starship/AssetIcons");
	const TCHAR* const EngineIcons[][2] = {
		{ TEXT("KzDialogueNotify_PlaySound"), TEXT("SoundCue_16") },
		{ TEXT("KzDialogueNotify_PlayMontage"), TEXT("AnimMontage_16") },
		{ TEXT("KzDialogueNotifyState_PlayMontage"), TEXT("AnimMontage_16") },
		{ TEXT("KzDialogueNotify_PlaySlotAnimation"), TEXT("AnimSequence_16") },
		{ TEXT("KzDialogueNotifyState_PlaySlotAnimation"), TEXT("AnimSequence_16") },
		{ TEXT("KzDialogueNotify_ForceFeedback"), TEXT("ForceFeedbackEffect_16") },
	};
	for (const auto& Pair : EngineIcons)
	{
		Set(*FString::Printf(TEXT("ClassIcon.%s"), Pair[0]), new FSlateVectorImageBrush(EngineAssetIcons / Pair[1] + TEXT(".svg"), IconSize));
	}

	Set("Kz.Dialogue.Icon", new FSlateVectorImageBrush(Icon(TEXT("DialogueAsset")), IconSize));
	Set("Kz.Dialogue.SpeakerIcon", new FSlateVectorImageBrush(Icon(TEXT("SpeakerAsset")), IconSize));
}