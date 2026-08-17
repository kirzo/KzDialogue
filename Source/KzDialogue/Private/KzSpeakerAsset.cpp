// Copyright 2026 kirzo

#include "KzSpeakerAsset.h"

#include "KzDialogueTypes.h"
#include "Internationalization/Text.h"
#include "Settings/KzDialogueSettings.h"

FText UKzSpeakerAsset::GetResolvedDisplayName() const
{
	if (!DisplayName.IsEmpty())
	{
		return DisplayName;
	}

	if (GivenName.IsEmpty() && FamilyName.IsEmpty() && Honorific.IsEmpty() && Qualifier.IsEmpty())
	{
		return FText::GetEmpty();
	}

	static const FString FallbackFormat = TEXT("{Given} {Family}");
	const UKzDialogueSettings* Settings = UKzDialogueSettings::Get();
	const FString& Format = Settings ? Settings->GetActiveSpeakerNameFormat() : FallbackFormat;

	FFormatNamedArguments Args;
	Args.Add(TEXT("Given"), GivenName.Resolve(Gender));
	Args.Add(TEXT("Family"), FamilyName.Resolve(Gender));
	Args.Add(TEXT("Honorific"), Honorific.Resolve(Gender));
	Args.Add(TEXT("Qualifier"), Qualifier.Resolve(Gender));
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

	// A referenced shared word wins: the inline text empties so the localization gather
	// only ever sees one of the two. Emptiness is judged by SOURCE string and identity, not
	// FText::IsEmpty: a keyed empty text can display a stale translation and lie.
	auto EnforceWordWins = [&bDirty](FKzWordText& Field)
	{
		const FString* Source = FTextInspector::GetSourceString(Field.Text);
		if (Field.Word && ((Source && !Source->IsEmpty()) || FTextInspector::GetNamespace(Field.Text).IsSet()))
		{
			Field.Text = FText::GetEmpty();
			bDirty = true;
		}
	};
	EnforceWordWins(GivenName);
	EnforceWordWins(FamilyName);
	EnforceWordWins(Honorific);
	EnforceWordWins(Qualifier);

	RebindFTextKeys();

	auto RefreshHash = [&bDirty](const FText& Text, uint32& Hash)
	{
		const uint32 NewHash = KzComputeSourceTextHash(Text);
		if (NewHash != Hash)
		{
			Hash = NewHash;
			bDirty = true;
		}
	};
	RefreshHash(DisplayName, SourceDisplayNameHash);
	RefreshHash(GivenName.Text, SourceGivenNameHash);
	RefreshHash(FamilyName.Text, SourceFamilyNameHash);
	RefreshHash(Honorific.Text, SourceHonorificHash);
	RefreshHash(Qualifier.Text, SourceQualifierHash);

	if (bDirty)
	{
		MarkPackageDirty();
	}
}

void UKzSpeakerAsset::RebindFTextKeys()
{
	if (!AssetId.IsValid()) { return; }

	const FString Namespace = FString::Printf(TEXT("KzSpeaker.%s"), *AssetId.ToString(EGuidFormats::Digits));

	// A text the user marked as non-localizable (culture invariant) keeps that choice:
	// ChangeKey would rebuild it as localizable, silently reverting the checkbox. Empty
	// texts stay keyless: a keyed empty can pick up a stale translation as its display
	// string and read as non-empty everywhere downstream.
	auto Rebind = [&Namespace](FText& Text, const TCHAR* Key)
	{
		const FString* Source = FTextInspector::GetSourceString(Text);
		if (Source && !Source->IsEmpty() && !Text.IsCultureInvariant())
		{
			Text = FText::ChangeKey(Namespace, Key, Text);
		}
	};
	Rebind(DisplayName, TEXT("DisplayName"));
	Rebind(GivenName.Text, TEXT("GivenName"));
	Rebind(FamilyName.Text, TEXT("FamilyName"));
	Rebind(Honorific.Text, TEXT("Honorific"));
	Rebind(Qualifier.Text, TEXT("Qualifier"));
}

#endif