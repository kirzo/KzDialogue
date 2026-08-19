// Copyright 2026 kirzo

#include "KzWordAsset.h"

DEFINE_LOG_CATEGORY_STATIC(LogKzWord, Log, All);

FText UKzWordAsset::Resolve(EKzGender Gender) const
{
	if (const FText* Form = GenderForms.Find(Gender))
	{
		if (!KzIsTextSourceEmpty(*Form))
		{
			return *Form;
		}
	}
	return Text;
}

FText UKzWordAsset::ResolveName(FName Part) const
{
	if (Part.IsNone())
	{
		return Text;
	}

	const int64 Value = StaticEnum<EKzGender>()->GetValueByNameString(Part.ToString());
	if (Value == INDEX_NONE)
	{
		UE_LOG(LogKzWord, Warning, TEXT("'%s': unknown name part '%s' (expected a gender form); using the default form."), *GetName(), *Part.ToString());
		return Text;
	}
	return Resolve(static_cast<EKzGender>(Value));
}

FPrimaryAssetId UKzWordAsset::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(TEXT("KzWord"), GetFName());
}

#if WITH_EDITOR

TArray<FName> UKzWordAsset::GetNameParts() const
{
	// Every gender form name is addressable; unauthored forms fall back to Text by design.
	TArray<FName> Parts;
	const UEnum* Enum = StaticEnum<EKzGender>();
	for (int32 i = 0; i < Enum->NumEnums() - 1; ++i)
	{
		if (static_cast<EKzGender>(Enum->GetValueByIndex(i)) != EKzGender::Unspecified)
		{
			Parts.Add(*Enum->GetNameStringByIndex(i));
		}
	}
	return Parts;
}

FText UKzWordAsset::GetNamePartDescription(FName Part) const
{
	return FText::Format(NSLOCTEXT("KzWord", "PartGenderForm", "The {0} gender form of the word; unauthored forms fall back to Text."), FText::FromName(Part));
}

void UKzWordAsset::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	RefreshMetadata();
}

void UKzWordAsset::PostDuplicate(bool bDuplicateForPIE)
{
	Super::PostDuplicate(bDuplicateForPIE);
	if (bDuplicateForPIE) return;

	// A duplicate is a new word: fresh identity, re-anchored localization keys.
	AssetId = FGuid::NewGuid();
	RefreshMetadata();
}

void UKzWordAsset::PostLoad()
{
	Super::PostLoad();
	RefreshMetadata();
}

void UKzWordAsset::PostInitProperties()
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

void UKzWordAsset::RefreshMetadata()
{
	// AssetId safety net (PostInitProperties/PostDuplicate usually cover this).
	if (!AssetId.IsValid())
	{
		AssetId = FGuid::NewGuid();
		MarkPackageDirty();
	}

	// Texts whose SOURCE went empty keep no identity: a keyed empty can resolve a stale
	// translation as its display string and read as non-empty everywhere.
	auto ResetKeyedEmpty = [this](FText& InText)
	{
		if (FTextInspector::GetNamespace(InText).IsSet() && KzIsTextSourceEmpty(InText))
		{
			InText = FText::GetEmpty();
			MarkPackageDirty();
		}
	};
	ResetKeyedEmpty(Text);
	for (TPair<EKzGender, FText>& Form : GenderForms)
	{
		ResetKeyedEmpty(Form.Value);
	}

	RebindFTextKeys();
}

void UKzWordAsset::RebindFTextKeys()
{
	if (!AssetId.IsValid()) { return; }

	const FString Namespace = FString::Printf(TEXT("KzWord.%s"), *AssetId.ToString(EGuidFormats::Digits));

	// A text the user marked as non-localizable (culture invariant) keeps that choice:
	// ChangeKey would rebuild it as localizable, silently reverting the checkbox. Empty
	// texts stay keyless: a keyed empty can pick up a stale translation as its display
	// string and read as non-empty everywhere downstream.
	auto Rebind = [&Namespace](FText& InText, const FString& Key)
	{
		const FString* Source = FTextInspector::GetSourceString(InText);
		if (Source && !Source->IsEmpty() && !InText.IsCultureInvariant())
		{
			InText = FText::ChangeKey(Namespace, Key, InText);
		}
	};

	Rebind(Text, TEXT("Text"));
	for (TPair<EKzGender, FText>& Form : GenderForms)
	{
		Rebind(Form.Value, FString::Printf(TEXT("Text_%s"), *StaticEnum<EKzGender>()->GetNameStringByValue(static_cast<int64>(Form.Key))));
	}
}

#endif