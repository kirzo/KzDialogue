// Copyright 2026 kirzo

#include "KzDialogueNotify.h"

FText UKzDialogueNotifyBase::GetNotifyName() const
{
	FString RawName = GetClass()->GetName();
	RawName.RemoveFromEnd(TEXT("_C"));
	return FText::FromString(FName::NameToDisplayString(RawName, false));
}