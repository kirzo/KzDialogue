// Copyright 2026 kirzo

#include "KzSpeakerAsset.h"

#include "KzDialogueTypes.h"
#include "Internationalization/Text.h"
#include "Settings/KzDialogueSettings.h"

DEFINE_LOG_CATEGORY_STATIC(LogKzSpeaker, Log, All);

FText UKzSpeakerAsset::GetResolvedDisplayName() const
{
	// Source-based emptiness: a keyed empty DisplayName can display a stale translation and
	// would otherwise shadow the structured composition.
	if (!KzIsTextSourceEmpty(DisplayName))
	{
		return DisplayName;
	}
	return ComposeStructuredName(/*bWithHonorific=*/true, /*bWithQualifier=*/true);
}

FText UKzSpeakerAsset::ComposeStructuredName(bool bWithHonorific, bool bWithQualifier, bool bWithFamily) const
{
	if (GivenName.IsEmpty() && FamilyName.IsEmpty() && SecondFamilyName.IsEmpty() && Honorific.IsEmpty() && Qualifier.IsEmpty() && NickName.IsEmpty())
	{
		return FText::GetEmpty();
	}

	static const FString FallbackFormat = TEXT("{Given} {Family}");
	const UKzDialogueSettings* Settings = UKzDialogueSettings::Get();
	const FString& Format = Settings ? Settings->GetActiveSpeakerNameFormat() : FallbackFormat;

	FFormatNamedArguments Args;
	Args.Add(TEXT("Given"), GivenName.Resolve(Gender));
	Args.Add(TEXT("Family"), bWithFamily ? FamilyName.Resolve(Gender) : FText::GetEmpty());
	Args.Add(TEXT("Family2"), bWithFamily ? SecondFamilyName.Resolve(Gender) : FText::GetEmpty());
	Args.Add(TEXT("Nick"), NickName.Resolve(Gender));
	Args.Add(TEXT("Honorific"), bWithHonorific ? Honorific.Resolve(Gender) : FText::GetEmpty());
	Args.Add(TEXT("Qualifier"), bWithQualifier ? Qualifier.Resolve(Gender) : FText::GetEmpty());
	FString Composed = FText::Format(FTextFormat::FromString(Format), Args).ToString();

	// Collapse the gaps empty parts leave behind: runs of spaces inside, stray spaces at
	// the ends. Spaceless CJK patterns pass through untouched.
	while (Composed.ReplaceInline(TEXT("  "), TEXT(" ")) > 0) {}
	Composed.TrimStartAndEndInline();

	return FText::FromString(Composed);
}

FText UKzSpeakerAsset::ResolveName(FName Part) const
{
	if (Part.IsNone()) { return GetResolvedDisplayName(); }
	if (Part == TEXT("given")) { return GivenName.Resolve(Gender); }
	if (Part == TEXT("family")) { return FamilyName.Resolve(Gender); }
	if (Part == TEXT("family2")) { return SecondFamilyName.Resolve(Gender); }
	if (Part == TEXT("nick")) { return NickName.Resolve(Gender); }
	if (Part == TEXT("honorific")) { return Honorific.Resolve(Gender); }
	if (Part == TEXT("qualifier")) { return Qualifier.Resolve(Gender); }
	if (Part == TEXT("gender")) { return FText::FromString(StaticEnum<EKzGender>()->GetNameStringByValue(static_cast<int64>(Gender))); }
	if (Part == TEXT("display")) { return DisplayName; }
	if (Part == TEXT("fullname")) { return ComposeStructuredName(true, true); }
	if (Part == TEXT("no-honorific")) { return ComposeStructuredName(false, true); }
	if (Part == TEXT("no-qualifier")) { return ComposeStructuredName(true, false); }
	if (Part == TEXT("no-family")) { return ComposeStructuredName(true, true, false); }

	UE_LOG(LogKzSpeaker, Warning, TEXT("'%s': unknown name part '%s'; using the default name."), *GetName(), *Part.ToString());
	return GetResolvedDisplayName();
}

FPrimaryAssetId UKzSpeakerAsset::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(TEXT("KzSpeaker"), GetFName());
}

#if WITH_EDITOR

TArray<FName> UKzSpeakerAsset::GetNameParts() const
{
	return { TEXT("given"), TEXT("family"), TEXT("family2"), TEXT("nick"), TEXT("honorific"), TEXT("qualifier"), TEXT("gender"), TEXT("display"), TEXT("fullname"), TEXT("no-honorific"), TEXT("no-qualifier"), TEXT("no-family") };
}

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

	// Texts whose SOURCE went empty keep no identity: a keyed empty can resolve a stale
	// translation as its display string and read as non-empty everywhere. Self-heals assets
	// keyed back when the rebind did not skip empties yet.
	auto ResetKeyedEmpty = [&bDirty](FText& InText)
	{
		if (FTextInspector::GetNamespace(InText).IsSet() && KzIsTextSourceEmpty(InText))
		{
			InText = FText::GetEmpty();
			bDirty = true;
		}
	};
	ResetKeyedEmpty(DisplayName);
	ResetKeyedEmpty(GivenName.Text);
	ResetKeyedEmpty(FamilyName.Text);
	ResetKeyedEmpty(SecondFamilyName.Text);
	ResetKeyedEmpty(Honorific.Text);
	ResetKeyedEmpty(Qualifier.Text);
	ResetKeyedEmpty(NickName.Text);

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
	EnforceWordWins(SecondFamilyName);
	EnforceWordWins(Honorific);
	EnforceWordWins(Qualifier);
	EnforceWordWins(NickName);

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
	RefreshHash(SecondFamilyName.Text, SourceSecondFamilyNameHash);
	RefreshHash(Honorific.Text, SourceHonorificHash);
	RefreshHash(Qualifier.Text, SourceQualifierHash);
	RefreshHash(NickName.Text, SourceNickNameHash);

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
	Rebind(SecondFamilyName.Text, TEXT("SecondFamilyName"));
	Rebind(Honorific.Text, TEXT("Honorific"));
	Rebind(Qualifier.Text, TEXT("Qualifier"));
	Rebind(NickName.Text, TEXT("NickName"));
}

#endif