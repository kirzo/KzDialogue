// Copyright 2026 kirzo

#include "KzSpeakerAsset.h"

#include "Internationalization/Text.h"
#include "Misc/Crc.h"
#include "Settings/KzDialogueSettings.h"

#if WITH_EDITOR
namespace
{
	uint32 ComputeSourceHash(const FText& Text)
	{
		// GetSourceString returns the authored string, ignoring the active culture's
		// translation. Text.ToString() would hash the translation, which defeats the
		// whole point of drift detection.
		const FString* Source = FTextInspector::GetSourceString(Text);
		return Source ? FCrc::StrCrc32(**Source) : 0;
	}
}
#endif

FText UKzSpeakerAsset::GetResolvedDisplayName() const
{
	if (!DisplayName.IsEmpty())
	{
		return DisplayName;
	}

	if (GivenName.IsEmpty() && FamilyName.IsEmpty() && Honorific.IsEmpty())
	{
		return FText::GetEmpty();
	}

	static const FString FallbackFormat = TEXT("{Given} {Family}");
	const UKzDialogueSettings* Settings = UKzDialogueSettings::Get();
	const FString& Format = Settings ? Settings->GetActiveSpeakerNameFormat() : FallbackFormat;

	FFormatNamedArguments Args;
	Args.Add(TEXT("Given"), GivenName);
	Args.Add(TEXT("Family"), FamilyName);
	Args.Add(TEXT("Honorific"), Honorific);
	FString Composed = FText::Format(FTextFormat::FromString(Format), Args).ToString();

	// Collapse the gaps empty parts leave behind: runs of spaces inside, stray spaces at
	// the ends. Spaceless CJK patterns pass through untouched.
	while (Composed.ReplaceInline(TEXT("  "), TEXT(" ")) > 0) {}
	Composed.TrimStartAndEndInline();

	return FText::FromString(Composed);
}

FPrimaryAssetId UKzSpeakerAsset::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(TEXT("KzSpeaker"), GetFName());
}

#if WITH_EDITOR

void UKzSpeakerAsset::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	RefreshMetadata();
}

void UKzSpeakerAsset::PostDuplicate(bool bDuplicateForPIE)
{
	Super::PostDuplicate(bDuplicateForPIE);
	if (bDuplicateForPIE) return;

	// A duplicate is a new character: fresh identity, re-anchored localization keys.
	AssetId = FGuid::NewGuid();
	RefreshMetadata();
}

void UKzSpeakerAsset::PostLoad()
{
	Super::PostLoad();
	RefreshMetadata();
}

void UKzSpeakerAsset::PostInitProperties()
{
	Super::PostInitProperties();

	// Only generate for freshly-created instances, not for the CDO or for objects
	// currently being loaded (PostLoad handles those).
	if (!HasAnyFlags(RF_ClassDefaultObject | RF_NeedLoad | RF_NeedPostLoad))
	{
		if (!AssetId.IsValid())
		{
			AssetId = FGuid::NewGuid();
		}
	}
}

void UKzSpeakerAsset::RefreshMetadata()
{
	bool bDirty = false;

	// AssetId safety net (PostInitProperties/PostDuplicate usually cover this).
	if (!AssetId.IsValid())
	{
		AssetId = FGuid::NewGuid();
		bDirty = true;
	}

	RebindFTextKeys();

	auto RefreshHash = [&bDirty](const FText& Text, uint32& Hash)
	{
		const uint32 NewHash = ComputeSourceHash(Text);
		if (NewHash != Hash)
		{
			Hash = NewHash;
			bDirty = true;
		}
	};
	RefreshHash(DisplayName, SourceDisplayNameHash);
	RefreshHash(GivenName, SourceGivenNameHash);
	RefreshHash(FamilyName, SourceFamilyNameHash);
	RefreshHash(Honorific, SourceHonorificHash);

	if (bDirty)
	{
		MarkPackageDirty();
	}
}

void UKzSpeakerAsset::RebindFTextKeys()
{
	if (!AssetId.IsValid()) { return; }

	const FString Namespace = FString::Printf(TEXT("KzSpeaker.%s"), *AssetId.ToString(EGuidFormats::Digits));

	DisplayName = FText::ChangeKey(Namespace, TEXT("DisplayName"), DisplayName);
	GivenName = FText::ChangeKey(Namespace, TEXT("GivenName"), GivenName);
	FamilyName = FText::ChangeKey(Namespace, TEXT("FamilyName"), FamilyName);
	Honorific = FText::ChangeKey(Namespace, TEXT("Honorific"), Honorific);
}

#endif