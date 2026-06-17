// Copyright 2026 kirzo

#include "KzDialogueNotify.h"

FText UKzDialogueNotifyBase::GetNotifyName() const
{
	return FText::FromString(FName::NameToDisplayString(GetClass()->GetName(), false));
}