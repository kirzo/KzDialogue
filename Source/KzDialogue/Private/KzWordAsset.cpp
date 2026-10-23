// Copyright 2026 kirzo

#include "KzWordAsset.h"
#include "KzDialogue.h"

#include "KzNamedTokenSubsystem.h"

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

FText UKzWordAsset::ResolveName(FName Part, const FKzNamedTokenOverride* Overrides) const
{
	// A pinned part wins; the default form is addressed under its field name "Text".
	if (Overrides)
	{
		if (const FText* Pinned = Overrides->Parts.Find(Part.IsNone() ? FName(TEXT("Text")) : Part)) { return *Pinned; }
	}

	if (Part.IsNone())
	{
		return Text;
	}

	const int64 Value = StaticEnum<EKzGender>()->GetValueByNameString(Part.ToString());
	if (Value == INDEX_NONE)
	{
		UE_LOG(LogKzDialogue, Warning, TEXT("'%s': unknown name part '%s' (expected a gender form); using the default form."), *GetName(), *Part.ToString());
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

TArray<FName> UKzWordAsset::GetOverridablePartNames() const
{
	// The default form is pinnable too, under its field name (the ":part" modifiers only
	// cover the gender forms).
	TArray<FName> Parts;
	Parts.Add(TEXT("Text"));
	Parts.Append(GetNameParts());
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

	if (RebindFTextKeys())
	{
		MarkPackageDirty();
	}
}

bool UKzWordAsset::RebindFTextKeys()
{
	if (!AssetId.IsValid()) { return false; }

	const FString Namespace = FString::Printf(TEXT("KzWord.%s"), *AssetId.ToString(EGuidFormats::Digits));

	bool bChanged = KzAnchorText(Text, Namespace, TEXT("Text"));
	for (TPair<EKzGender, FText>& Form : GenderForms)
	{
		bChanged |= KzAnchorText(Form.Value, Namespace, FString::Printf(TEXT("Text_%s"), *StaticEnum<EKzGender>()->GetNameStringByValue(static_cast<int64>(Form.Key))));
	}
	return bChanged;
}

#endif