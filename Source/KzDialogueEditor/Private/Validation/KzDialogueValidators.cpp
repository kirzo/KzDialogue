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

#undef LOCTEXT_NAMESPACE