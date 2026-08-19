// Copyright 2026 kirzo

#include "KzNamedAsset.h"

FText UKzNamedAsset::ResolveName(FName /*Part*/) const
{
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

#endif