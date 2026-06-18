// Copyright 2026 kirzo

#include "Validation/KzDialogueValidators.h"
#include "KzDialogueAsset.h"
#include "KzDialogueTimeline.h"
#include "KzDialogueNotify.h"
#include "Widgets/SKzDialogueTimeline.h"
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

// =======================================================================================
// Notify timelines
// =======================================================================================

bool UKzDialogueValidator_Timelines::CanValidate_Implementation(const UObject* Asset) const
{
	return Asset && Asset->IsA<UKzDialogueAsset>();
}

void UKzDialogueValidator_Timelines::Validate_Implementation(const UObject* Asset, TArray<FKzValidationIssue>& OutIssues) const
{
	const UKzDialogueAsset* Dialogue = Cast<UKzDialogueAsset>(Asset);
	if (!Dialogue) { return; }

	const FName Id = GetValidatorId();
	for (int32 i = 0; i < Dialogue->Lines.Num(); ++i)
	{
		const FKzDialogueLine& Line = Dialogue->Lines[i];
		const UKzDialogueTimeline* Timeline = Dialogue->FindTimelineForLine(Line.LineId);
		if (!Timeline) { continue; }

		// Best-effort effective duration for the "past the line" check; skipped if unknown.
		float LineDuration = Line.Duration;
		const USoundBase* Audio = Line.Audio.IsNull() ? nullptr : Line.Audio.LoadSynchronous();
		if (LineDuration <= 0.f && Audio) { LineDuration = Audio->GetDuration(); }

		FKzDialogueTimeResolveContext ResolveCtx;
		ResolveCtx.LineDuration = LineDuration;
		ResolveCtx.Audio = Audio;

		const FText LineLabel = FText::AsNumber(i + 1);
		const FGuid LineId = Line.LineId;

		for (int32 t = 0; t < Timeline->Tracks.Num(); ++t)
		{
			const FKzDialogueNotifyTrack& Track = Timeline->Tracks[t];
			const FText TrackLabel = FText::FromName(Track.Name);
			for (int32 e = 0; e < Track.Events.Num(); ++e)
			{
				const FKzDialogueNotifyEvent& Event = Track.Events[e];

				// Activating an issue jumps to the line (ContextId) and then selects this exact notify.
				auto AddIssue = [&](EKzValidationSeverity Severity, const FText& Message)
				{
					FKzValidationIssue Issue = FKzValidationIssue::WithContextId(Severity, Message, Id, LineId);
					Issue.ContextIndex = i;
					Issue.OnActivate = [LineId, t, e]() { SKzDialogueTimeline::RequestNotifySelection(LineId, t, e); };
					OutIssues.Add(Issue);
				};

				if (!Event.Notify)
				{
					AddIssue(EKzValidationSeverity::Error, FText::Format(LOCTEXT("NotifyUnset", "Line {0}, track '{1}': an event has no notify assigned."), LineLabel, TrackLabel));
					continue;
				}

				const FText NotifyName = Event.Notify->GetNotifyName();

				const FKzDialogueTimeSource* Source = Event.TimeSource.GetPtr<FKzDialogueTimeSource>();
				if (!Source)
				{
					AddIssue(EKzValidationSeverity::Error, FText::Format(LOCTEXT("TimeSourceUnset", "Line {0}, track '{1}', {2}: no time source set, so it will not fire."), LineLabel, TrackLabel, NotifyName));
					continue;
				}

				TArray<FText> NotifyErrors;
				Event.Notify->ValidateNotify(NotifyErrors);
				for (const FText& Err : NotifyErrors)
				{
					AddIssue(EKzValidationSeverity::Error, FText::Format(LOCTEXT("NotifyConfig", "Line {0}, track '{1}', {2}: {3}"), LineLabel, TrackLabel, NotifyName, Err));
				}

				// A point notify placed past the line never fires (a state ending past the line is
				// the legitimate "hold to the end" case, so it is left alone).
				if (LineDuration > 0.f && !Event.Notify->IsA<UKzDialogueNotifyState>())
				{
					float Start = 0.f;
					float End = 0.f;
					Source->Resolve(ResolveCtx, Start, End);
					if (Start > LineDuration + KINDA_SMALL_NUMBER)
					{
						AddIssue(EKzValidationSeverity::Warning, FText::Format(LOCTEXT("PointPastLine", "Line {0}, track '{1}', {2}: fires at {3}s, past the line duration ({4}s); it will never play."), LineLabel, TrackLabel, NotifyName, FText::AsNumber(Start), FText::AsNumber(LineDuration)));
					}
				}
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE