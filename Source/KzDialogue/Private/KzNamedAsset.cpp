// Copyright 2026 kirzo

#include "KzNamedAsset.h"

#include "KzNamedTokenSubsystem.h"

FText UKzNamedAsset::ResolveName(FName Part, const FKzNamedTokenOverride* Overrides) const
{
	if (Overrides && !Part.IsNone())
	{
		if (const FText* Pinned = Overrides->Parts.Find(Part))
		{
			return *Pinned;
		}
	}
	return FText::GetEmpty();
}

#if WITH_EDITOR

UKzNamedAsset::FKzOnNamedTokenRenamed UKzNamedAsset::OnTokenRenamed;

void UKzNamedAsset::PreEditChange(FProperty* PropertyAboutToChange)
{
	Super::PreEditChange(PropertyAboutToChange);
	TokenBeforeEdit = Token;
}

void UKzNamedAsset::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UKzNamedAsset, Token) && Token != TokenBeforeEdit && !TokenBeforeEdit.IsNone())
	{
		OnTokenRenamed.Broadcast(this, TokenBeforeEdit);
	}
}

void UKzNamedAsset::PreEditUndo()
{
	Super::PreEditUndo();
	TokenBeforeEdit = Token;
}

void UKzNamedAsset::PostEditUndo()
{
	Super::PostEditUndo();

	// Undoing or redoing a rename is a rename too: the transaction restores the lines, the listeners the archives.
	if (Token != TokenBeforeEdit && !TokenBeforeEdit.IsNone())
	{
		OnTokenRenamed.Broadcast(this, TokenBeforeEdit);
	}
}

#endif