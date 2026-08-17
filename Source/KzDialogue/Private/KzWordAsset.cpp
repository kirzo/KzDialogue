// Copyright 2026 kirzo

#include "KzWordAsset.h"

FText UKzWordAsset::Resolve(EKzGender Gender) const
{
	if (const FText* Form = GenderForms.Find(Gender))
	{
		if (!Form->IsEmpty())
		{
			return *Form;
		}
	}
	return Text;
}

#if WITH_EDITOR

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

	RebindFTextKeys();
}

void UKzWordAsset::RebindFTextKeys()
{
	if (!AssetId.IsValid()) { return; }

	const FString Namespace = FString::Printf(TEXT("KzWord.%s"), *AssetId.ToString(EGuidFormats::Digits));

	// A text the user marked as non-localizable (culture invariant) keeps that choice:
	// ChangeKey would rebuild it as localizable, silently reverting the checkbox.
	auto Rebind = [&Namespace](FText& InText, const FString& Key)
	{
		if (!InText.IsCultureInvariant())
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