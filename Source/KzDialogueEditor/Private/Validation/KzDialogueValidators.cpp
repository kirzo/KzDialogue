// Copyright 2026 kirzo

#include "Validation/KzDialogueValidators.h"
#include "KzDialogueAsset.h"
#include "Sound/SoundBase.h"

#define LOCTEXT_NAMESPACE "KzDialogueValidators"

// =======================================================================================
// Empty lines
// =======================================================================================

bool UKzDialogueValidator_EmptyLines::CanValidate_Implementation(const UObject* Asset) const
{
	return Asset && Asset->IsA<UKzDialogueAsset>();
}

void UKzDialogueValidator_EmptyLines::Validate_Implementation(const UObject* Asset, TArray<FKzValidationIssue>& OutIssues) const
{
	const UKzDialogueAsset* Dialogue = Cast<UKzDialogueAsset>(Asset);
	if (!Dialogue) { return; }

	const FName Id = GetValidatorId();
	for (int32 i = 0; i < Dialogue->Lines.Num(); ++i)
	{
		const FKzDialogueLine& Line = Dialogue->Lines[i];
		const bool bHasText = !Line.Text.IsEmpty();
		const bool bHasAudio = !Line.Audio.IsNull();

		if (!bHasText && !bHasAudio)
		{
			FKzValidationIssue Issue = FKzValidationIssue::WithContextId(
				EKzValidationSeverity::Warning,
				FText::Format(LOCTEXT("EmptyLine", "Line {0} has neither text nor audio."), FText::AsNumber(i + 1)),
				Id,
				Line.LineId);
			Issue.ContextIndex = i;
			OutIssues.Add(Issue);
		}
	}
}

// =======================================================================================
// Broken audio
// =======================================================================================

bool UKzDialogueValidator_BrokenAudio::CanValidate_Implementation(const UObject* Asset) const
{
	return Asset && Asset->IsA<UKzDialogueAsset>();
}

void UKzDialogueValidator_BrokenAudio::Validate_Implementation(const UObject* Asset, TArray<FKzValidationIssue>& OutIssues) const
{
	const UKzDialogueAsset* Dialogue = Cast<UKzDialogueAsset>(Asset);
	if (!Dialogue) { return; }

	const FName Id = GetValidatorId();
	for (int32 i = 0; i < Dialogue->Lines.Num(); ++i)
	{
		const FKzDialogueLine& Line = Dialogue->Lines[i];
		if (Line.Audio.IsNull()) { continue; }

		// LoadSynchronous returns null when the soft pointer can't be resolved.
		if (!Line.Audio.LoadSynchronous())
		{
			FKzValidationIssue Issue = FKzValidationIssue::WithContextId(
				EKzValidationSeverity::Error,
				FText::Format(
					LOCTEXT("BrokenAudio", "Line {0} references audio that failed to load: {1}"),
					FText::AsNumber(i + 1),
					FText::FromString(Line.Audio.ToString())),
				Id,
				Line.LineId);
			Issue.ContextIndex = i;
			OutIssues.Add(Issue);
		}
	}
}

// =======================================================================================
// Duplicate LineIds
// =======================================================================================

bool UKzDialogueValidator_DuplicateLineIds::CanValidate_Implementation(const UObject* Asset) const
{
	return Asset && Asset->IsA<UKzDialogueAsset>();
}

void UKzDialogueValidator_DuplicateLineIds::Validate_Implementation(const UObject* Asset, TArray<FKzValidationIssue>& OutIssues) const
{
	const UKzDialogueAsset* Dialogue = Cast<UKzDialogueAsset>(Asset);
	if (!Dialogue) { return; }

	const FName Id = GetValidatorId();
	TMap<FGuid, int32> FirstOccurrence;
	for (int32 i = 0; i < Dialogue->Lines.Num(); ++i)
	{
		const FKzDialogueLine& Line = Dialogue->Lines[i];

		if (!Line.LineId.IsValid())
		{
			FKzValidationIssue Issue = FKzValidationIssue::WithContextIndex(
				EKzValidationSeverity::Error,
				FText::Format(LOCTEXT("InvalidGuid", "Line {0} has an invalid GUID."), FText::AsNumber(i + 1)),
				Id, i);
			OutIssues.Add(Issue);
			continue;
		}

		if (const int32* Earlier = FirstOccurrence.Find(Line.LineId))
		{
			FKzValidationIssue Issue = FKzValidationIssue::WithContextId(
				EKzValidationSeverity::Error,
				FText::Format(
					LOCTEXT("DuplicateGuid", "Line {0} shares its GUID with line {1}."),
					FText::AsNumber(i + 1), FText::AsNumber(*Earlier + 1)),
				Id, Line.LineId);
			Issue.ContextIndex = i;
			OutIssues.Add(Issue);
		}
		else
		{
			FirstOccurrence.Add(Line.LineId, i);
		}
	}
}

// =======================================================================================
// Alias validators
// =======================================================================================

bool UKzDialogueValidator_AliasMissingSpeaker::CanValidate_Implementation(const UObject* Asset) const
{
	return Asset && Asset->IsA<UKzDialogueAsset>();
}

void UKzDialogueValidator_AliasMissingSpeaker::Validate_Implementation(const UObject* Asset, TArray<FKzValidationIssue>& OutIssues) const
{
	const UKzDialogueAsset* Dialogue = Cast<UKzDialogueAsset>(Asset);
	if (!Dialogue) { return; }

	const FName Id = GetValidatorId();
	for (int32 i = 0; i < Dialogue->Aliases.Num(); ++i)
	{
		const FKzDialogueAlias& Alias = Dialogue->Aliases[i];
		// "No speaker" is valid (narration aliases). We only flag when the array is
		// non-empty but lines reference different speakers and the alias defines none.
		// That case is handled by the SpeakerMismatch validator. Here we simply skip.
		(void)Alias; (void)i; (void)Id;
	}
}

bool UKzDialogueValidator_AliasEmpty::CanValidate_Implementation(const UObject* Asset) const
{
	return Asset && Asset->IsA<UKzDialogueAsset>();
}

void UKzDialogueValidator_AliasEmpty::Validate_Implementation(const UObject* Asset, TArray<FKzValidationIssue>& OutIssues) const
{
	const UKzDialogueAsset* Dialogue = Cast<UKzDialogueAsset>(Asset);
	if (!Dialogue) { return; }

	const FName Id = GetValidatorId();
	for (int32 i = 0; i < Dialogue->Aliases.Num(); ++i)
	{
		const FKzDialogueAlias& Alias = Dialogue->Aliases[i];
		if (Alias.LineIds.Num() == 0)
		{
			FKzValidationIssue Issue = FKzValidationIssue::WithContextId(
				EKzValidationSeverity::Warning,
				FText::Format(LOCTEXT("AliasEmpty", "Alias '{0}' has no referenced lines."),
					FText::FromName(Alias.AliasName)),
				Id, Alias.AliasId);
			Issue.ContextIndex = i;
			OutIssues.Add(Issue);
		}
	}
}

bool UKzDialogueValidator_AliasInvalidLine::CanValidate_Implementation(const UObject* Asset) const
{
	return Asset && Asset->IsA<UKzDialogueAsset>();
}

void UKzDialogueValidator_AliasInvalidLine::Validate_Implementation(const UObject* Asset, TArray<FKzValidationIssue>& OutIssues) const
{
	const UKzDialogueAsset* Dialogue = Cast<UKzDialogueAsset>(Asset);
	if (!Dialogue) { return; }

	const FName Id = GetValidatorId();
	for (int32 i = 0; i < Dialogue->Aliases.Num(); ++i)
	{
		const FKzDialogueAlias& Alias = Dialogue->Aliases[i];
		for (const FGuid& LineId : Alias.LineIds)
		{
			FKzDialogueLine Found;
			if (!Dialogue->TryGetLineById(LineId, Found))
			{
				FKzValidationIssue Issue = FKzValidationIssue::WithContextId(
					EKzValidationSeverity::Error,
					FText::Format(LOCTEXT("AliasInvalidLine",
						"Alias '{0}' references missing line with id {1}."),
						FText::FromName(Alias.AliasName),
						FText::FromString(LineId.ToString(EGuidFormats::DigitsWithHyphens))),
					Id, Alias.AliasId);
				Issue.ContextIndex = i;
				OutIssues.Add(Issue);
			}
		}
	}
}

bool UKzDialogueValidator_AliasSpeakerMismatch::CanValidate_Implementation(const UObject* Asset) const
{
	return Asset && Asset->IsA<UKzDialogueAsset>();
}

void UKzDialogueValidator_AliasSpeakerMismatch::Validate_Implementation(const UObject* Asset, TArray<FKzValidationIssue>& OutIssues) const
{
	const UKzDialogueAsset* Dialogue = Cast<UKzDialogueAsset>(Asset);
	if (!Dialogue) { return; }

	const FName Id = GetValidatorId();
	for (int32 i = 0; i < Dialogue->Aliases.Num(); ++i)
	{
		const FKzDialogueAlias& Alias = Dialogue->Aliases[i];
		const FGameplayTag& AliasSpeaker = Alias.Speaker.SpeakerTag;

		for (const FGuid& LineId : Alias.LineIds)
		{
			FKzDialogueLine Line;
			if (!Dialogue->TryGetLineById(LineId, Line)) { continue; } // covered by InvalidLine validator

			if (Line.Speaker.SpeakerTag != AliasSpeaker)
			{
				FKzValidationIssue Issue = FKzValidationIssue::WithContextId(
					EKzValidationSeverity::Error,
					FText::Format(LOCTEXT("AliasSpeakerMismatch",
						"Alias '{0}' (speaker: {1}) references a line with a different speaker ({2})."),
						FText::FromName(Alias.AliasName),
						FText::FromString(AliasSpeaker.ToString()),
						FText::FromString(Line.Speaker.SpeakerTag.ToString())),
					Id, Alias.AliasId);
				Issue.ContextIndex = i;
				OutIssues.Add(Issue);
			}
		}
	}
}

bool UKzDialogueValidator_DuplicateAliasName::CanValidate_Implementation(const UObject* Asset) const
{
	return Asset && Asset->IsA<UKzDialogueAsset>();
}

void UKzDialogueValidator_DuplicateAliasName::Validate_Implementation(const UObject* Asset, TArray<FKzValidationIssue>& OutIssues) const
{
	const UKzDialogueAsset* Dialogue = Cast<UKzDialogueAsset>(Asset);
	if (!Dialogue) { return; }

	const FName Id = GetValidatorId();
	TMap<FName, int32> FirstOccurrence;
	for (int32 i = 0; i < Dialogue->Aliases.Num(); ++i)
	{
		const FKzDialogueAlias& Alias = Dialogue->Aliases[i];
		if (Alias.AliasName.IsNone()) { continue; }

		if (const int32* Earlier = FirstOccurrence.Find(Alias.AliasName))
		{
			FKzValidationIssue Issue = FKzValidationIssue::WithContextId(
				EKzValidationSeverity::Error,
				FText::Format(LOCTEXT("DuplicateAliasName",
					"Alias '{0}' is defined twice (also at index {1})."),
					FText::FromName(Alias.AliasName), FText::AsNumber(*Earlier + 1)),
				Id, Alias.AliasId);
			Issue.ContextIndex = i;
			OutIssues.Add(Issue);
		}
		else
		{
			FirstOccurrence.Add(Alias.AliasName, i);
		}
	}
}

#undef LOCTEXT_NAMESPACE