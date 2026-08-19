// Copyright 2026 kirzo

#include "Validation/KzDialogueValidators.h"
#include "KzDialogueAsset.h"
#include "KzDialogueTimeline.h"
#include "KzDialogueNotify.h"
#include "KzNamedAsset.h"
#include "KzSpeakerAsset.h"
#include "Localization/KzDialogueTranslationCsv.h"
#include "Widgets/SKzDialogueTimeline.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundWave.h"

#include "LocTextHelper.h"

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
// Custom audio playback ranges
// =======================================================================================

bool UKzDialogueValidator_AudioRange::CanValidate_Implementation(const UObject* Asset) const
{
	return Asset && Asset->IsA<UKzDialogueAsset>();
}

void UKzDialogueValidator_AudioRange::Validate_Implementation(const UObject* Asset, TArray<FKzValidationIssue>& OutIssues) const
{
	const UKzDialogueAsset* Dialogue = Cast<UKzDialogueAsset>(Asset);
	if (!Dialogue) { return; }

	const FName Id = GetValidatorId();
	for (int32 i = 0; i < Dialogue->Lines.Num(); ++i)
	{
		const FKzDialogueLine& Line = Dialogue->Lines[i];
		const bool bHasRange = Line.AudioStartTime > 0.0f || Line.AudioEndTime > 0.0f;
		if (Line.Audio.IsNull() || !bHasRange) { continue; }

		if (Line.AudioEndTime > 0.0f && Line.AudioEndTime <= Line.AudioStartTime)
		{
			FKzValidationIssue Issue = FKzValidationIssue::WithContextId(
				EKzValidationSeverity::Error,
				FText::Format(LOCTEXT("AudioRangeEmpty", "Line {0}: AudioEndTime must be greater than AudioStartTime."), FText::AsNumber(i + 1)),
				Id,
				Line.LineId);
			Issue.ContextIndex = i;
			OutIssues.Add(Issue);
		}

		if (Line.bLocalizeAudio)
		{
			FKzValidationIssue Issue = FKzValidationIssue::WithContextId(
				EKzValidationSeverity::Warning,
				FText::Format(LOCTEXT("AudioRangeLocalized", "Line {0}: custom playback range on audio marked for localization; localized takes have their own timing. Uncheck Localize Audio or clear the range."), FText::AsNumber(i + 1)),
				Id,
				Line.LineId);
			Issue.ContextIndex = i;
			OutIssues.Add(Issue);
		}

		if (const USoundBase* Sound = Line.Audio.LoadSynchronous())
		{
			if (Line.AudioStartTime >= Sound->GetDuration())
			{
				FKzValidationIssue Issue = FKzValidationIssue::WithContextId(
					EKzValidationSeverity::Warning,
					FText::Format(LOCTEXT("AudioRangePastEnd", "Line {0}: AudioStartTime ({1}s) is at or beyond the audio's length ({2}s)."), FText::AsNumber(i + 1), Line.AudioStartTime, Sound->GetDuration()),
					Id,
					Line.LineId);
				Issue.ContextIndex = i;
				OutIssues.Add(Issue);
			}
		}
	}
}

// =======================================================================================
// Stale audio (text changed after recording)
// =======================================================================================

bool UKzDialogueValidator_StaleAudio::CanValidate_Implementation(const UObject* Asset) const
{
	return Asset && Asset->IsA<UKzDialogueAsset>();
}

void UKzDialogueValidator_StaleAudio::Validate_Implementation(const UObject* Asset, TArray<FKzValidationIssue>& OutIssues) const
{
	const UKzDialogueAsset* Dialogue = Cast<UKzDialogueAsset>(Asset);
	if (!Dialogue) { return; }

	const FName Id = GetValidatorId();
	for (int32 i = 0; i < Dialogue->Lines.Num(); ++i)
	{
		const FKzDialogueLine& Line = Dialogue->Lines[i];
		if (Line.Audio.IsNull() || Line.RecordedTextHash == 0) { continue; }

		if (Line.RecordedTextHash != Line.SourceTextHash)
		{
			FKzValidationIssue Issue = FKzValidationIssue::WithContextId(
				EKzValidationSeverity::Warning,
				FText::Format(
					LOCTEXT("StaleAudio", "Line {0}: text changed after its audio was recorded. Assign the new take, or accept the current text from the Localization tab."),
					FText::AsNumber(i + 1)),
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

		for (const FGuid& LineId : Alias.LineIds)
		{
			FKzDialogueLine Line;
			if (!Dialogue->TryGetLineById(LineId, Line)) { continue; } // covered by InvalidLine validator

			if (!(Line.Speaker == Alias.Speaker))
			{
				FKzValidationIssue Issue = FKzValidationIssue::WithContextId(
					EKzValidationSeverity::Error,
					FText::Format(LOCTEXT("AliasSpeakerMismatch",
						"Alias '{0}' (speaker: {1}) references a line with a different speaker ({2})."),
						FText::FromName(Alias.AliasName),
						Alias.Speaker.GetDisplayLabel(),
						Line.Speaker.GetDisplayLabel()),
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

				// Audio-marker sources: an unset name never resolves; a label missing from the source
				// wave silently uses FallbackTime (localized waves are the loc team's checklist, not this one).
				if (const FKzDialogueTimeSource_AudioMarker* Marker = Event.TimeSource.GetPtr<FKzDialogueTimeSource_AudioMarker>())
				{
					if (Marker->MarkerName.IsNone())
					{
						AddIssue(EKzValidationSeverity::Error, FText::Format(LOCTEXT("MarkerUnset", "Line {0}, track '{1}', {2}: audio marker time source with no marker name; it will always use the fallback time."), LineLabel, TrackLabel, NotifyName));
					}
					else if (const USoundWave* Wave = Cast<USoundWave>(ResolveCtx.Audio))
					{
						const FString MarkerLabel = Marker->MarkerName.ToString();
						const bool bFound = Wave->GetCuePoints().ContainsByPredicate([&MarkerLabel](const FSoundWaveCuePoint& Cue) { return Cue.Label == MarkerLabel; });
						if (!bFound)
						{
							AddIssue(EKzValidationSeverity::Warning, FText::Format(LOCTEXT("MarkerNotFound", "Line {0}, track '{1}', {2}: marker '{3}' not found in the line's wave; it will use the fallback time."), LineLabel, TrackLabel, NotifyName, FText::FromName(Marker->MarkerName)));
						}
					}
					else if (ResolveCtx.Audio)
					{
						AddIssue(EKzValidationSeverity::Warning, FText::Format(LOCTEXT("MarkerNotAWave", "Line {0}, track '{1}', {2}: audio marker time sources need a SoundWave; this line's audio is a {3}, so the fallback time applies."), LineLabel, TrackLabel, NotifyName, FText::FromString(ResolveCtx.Audio->GetClass()->GetName())));
					}
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
					if (Start > LineDuration + UE_KINDA_SMALL_NUMBER)
					{
						AddIssue(EKzValidationSeverity::Warning, FText::Format(LOCTEXT("PointPastLine", "Line {0}, track '{1}', {2}: fires at {3}s, past the line duration ({4}s); it will never play."), LineLabel, TrackLabel, NotifyName, FText::AsNumber(Start), FText::AsNumber(LineDuration)));
					}
				}
			}
		}
	}
}

// =======================================================================================
// Localization
// =======================================================================================

bool UKzDialogueValidator_Localization::CanValidate_Implementation(const UObject* Asset) const
{
	return Asset && (Asset->IsA<UKzDialogueAsset>() || Asset->IsA<UKzNamedAsset>());
}

void UKzDialogueValidator_Localization::Validate_Implementation(const UObject* Asset, TArray<FKzValidationIssue>& OutIssues) const
{
	if (const UKzNamedAsset* Named = Cast<UKzNamedAsset>(Asset))
	{
		ValidateNamedAssetToken(Named, OutIssues);
		if (const UKzSpeakerAsset* SpeakerAsset = Cast<UKzSpeakerAsset>(Asset))
		{
			ValidateSpeakerAsset(SpeakerAsset, OutIssues);
		}
		return;
	}

	const UKzDialogueAsset* Dialogue = Cast<UKzDialogueAsset>(Asset);
	if (!Dialogue) { return; }

	const FName Id = GetValidatorId();

	auto AddIssue = [&](EKzValidationSeverity Severity, const FText& Message, const FGuid& LineId, int32 LineIndex)
	{
		FKzValidationIssue Issue = FKzValidationIssue::WithContextId(Severity, Message, Id, LineId);
		Issue.ContextIndex = LineIndex;
		OutIssues.Add(Issue);
	};

	// Every localizable FText must sit on its stable GUID-derived key, or gather produces
	// throwaway keys and existing translations orphan. Self-heals on resave.
	const FString ExpectedNamespace = FString::Printf(TEXT("KzDialogue.%s"), *Dialogue->AssetId.ToString(EGuidFormats::Digits));
	for (int32 i = 0; i < Dialogue->Lines.Num(); ++i)
	{
		const FKzDialogueLine& Line = Dialogue->Lines[i];
		auto CheckKey = [&](const FText& Source, const TCHAR* Suffix)
		{
			// Culture-invariant = deliberately non-localizable; it carries no key on purpose.
			if (Source.IsEmpty() || Source.IsCultureInvariant()) { return; }
			const TOptional<FString> Namespace = FTextInspector::GetNamespace(Source);
			const TOptional<FString> Key = FTextInspector::GetKey(Source);
			const FString ExpectedKey = Line.LineId.ToString(EGuidFormats::Digits) + Suffix;
			if (!Namespace.IsSet() || !Key.IsSet() || Namespace.GetValue() != ExpectedNamespace || Key.GetValue() != ExpectedKey)
			{
				AddIssue(EKzValidationSeverity::Warning,
					FText::Format(LOCTEXT("UnstableKey", "Line {0}: text is not anchored to its stable localization key; resave the asset to rebind it."), FText::AsNumber(i + 1)),
					Line.LineId, i);
			}
		};
		CheckKey(Line.Text, TEXT("-Text"));
	}

	// Named-asset token references. A ":part" modifier states the intent unambiguously, so a
	// missing token or unknown part there is a real mistake; plain tokens may be code-registered
	// ambient resolvers and cannot be judged from data. The token map also whitelists the
	// tokens translations may legitimately ADD (the gender mechanism lives only there).
	TMap<FName, FSoftObjectPath> NamedTokens;
	{
		const IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
		TArray<FAssetData> NamedAssets;
		Registry.GetAssetsByClass(UKzNamedAsset::StaticClass()->GetClassPathName(), NamedAssets, /*bSearchSubClasses=*/true);
		for (const FAssetData& Data : NamedAssets)
		{
			FName AssetToken;
			if (Data.GetTagValue(GET_MEMBER_NAME_CHECKED(UKzNamedAsset, Token), AssetToken) && !AssetToken.IsNone() && !NamedTokens.Contains(AssetToken))
			{
				NamedTokens.Add(AssetToken, Data.ToSoftObjectPath());
			}
		}

		for (int32 i = 0; i < Dialogue->Lines.Num(); ++i)
		{
			const FKzDialogueLine& Line = Dialogue->Lines[i];
			const FString* SourceString = FTextInspector::GetSourceString(Line.Text);
			if (!SourceString || SourceString->IsEmpty()) { continue; }

			TArray<FString> Args;
			FTextFormat::FromString(*SourceString).GetFormatArgumentNames(Args);
			for (const FString& Arg : Args)
			{
				FString TokenPart;
				FString Modifier;
				if (!Arg.Split(TEXT(":"), &TokenPart, &Modifier)) { continue; }

				const FSoftObjectPath* Path = NamedTokens.Find(FName(*TokenPart));
				if (!Path)
				{
					AddIssue(EKzValidationSeverity::Warning,
						FText::Format(LOCTEXT("UnknownNamedToken", "Line {0}: no named asset claims the token '{1}' referenced by '`{{2}`}'."), FText::AsNumber(i + 1), FText::FromString(TokenPart), FText::FromString(Arg)),
						Line.LineId, i);
					continue;
				}

				const UKzNamedAsset* Named = Cast<UKzNamedAsset>(Path->TryLoad());
				if (Named && !Named->GetNameParts().Contains(FName(*Modifier)))
				{
					AddIssue(EKzValidationSeverity::Warning,
						FText::Format(LOCTEXT("UnknownNamePart", "Line {0}: '{1}' is not a name part of '{2}' (see the token picker for the valid ones)."), FText::AsNumber(i + 1), FText::FromString(Modifier), FText::FromString(Named->GetName())),
						Line.LineId, i);
				}
			}
		}
	}

	// Translation-dependent checks need a localization target; without one there is nothing to compare against.
	FKzLocTargetInfo Target;
	FText TargetError;
	if (!FKzDialogueTranslationCsv::ReadLocTargetInfo(Target, TargetError) || Target.ForeignCultures.IsEmpty()) { return; }

	// Archives are re-read on every validation; fine at this project's asset counts.
	FLocTextHelper LocHelper(Target.TargetPath, Target.ManifestName, Target.ArchiveName, Target.NativeCulture, Target.ForeignCultures, nullptr);
	if (!LocHelper.LoadManifest(ELocTextHelperLoadFlags::Load, nullptr)) { return; }

	TArray<FString> Cultures;
	for (const FString& Culture : Target.ForeignCultures)
	{
		if (LocHelper.LoadForeignArchive(Culture, ELocTextHelperLoadFlags::Load, nullptr))
		{
			Cultures.Add(Culture);
		}
	}
	if (Cultures.IsEmpty()) { return; }

	auto GetFormatArgs = [](const FString& Pattern)
	{
		TArray<FString> Args;
		FTextFormat::FromString(Pattern).GetFormatArgumentNames(Args);
		return TSet<FString>(Args);
	};

	for (int32 i = 0; i < Dialogue->Lines.Num(); ++i)
	{
		const FKzDialogueLine& Line = Dialogue->Lines[i];

		const FText& Source = Line.Text;
		if (Source.IsEmpty()) { continue; }
		const TOptional<FString> Namespace = FTextInspector::GetNamespace(Source);
		const TOptional<FString> Key = FTextInspector::GetKey(Source);
		const FString* SourceString = FTextInspector::GetSourceString(Source);
		if (!Namespace.IsSet() || !Key.IsSet() || !SourceString) { continue; }

		const TSet<FString> SourceArgs = GetFormatArgs(*SourceString);

		for (const FString& Culture : Cultures)
		{
			const TSharedPtr<FArchiveEntry> Entry = LocHelper.FindTranslation(Culture, FLocKey(Namespace.GetValue()), FLocKey(Key.GetValue()), nullptr);

			// Untranslated is the coverage report's business, not a per-save warning.
			if (!Entry.IsValid() || Entry->Translation.Text.IsEmpty()) { continue; }

			const FString& Translation = Entry->Translation.Text;

			// Stale translation: the remaining checks would compare against the wrong baseline.
			if (Entry->Source.Text != *SourceString)
			{
				AddIssue(EKzValidationSeverity::Warning,
					FText::Format(LOCTEXT("StaleTranslation", "Line {0}: the '{1}' translation predates the current source text; needs review."), FText::AsNumber(i + 1), FText::FromString(Culture)),
					Line.LineId, i);
				continue;
			}

			if (Line.MaxCharacters > 0 && Translation.Len() > Line.MaxCharacters)
			{
				AddIssue(EKzValidationSeverity::Warning,
					FText::Format(LOCTEXT("MaxCharsExceeded", "Line {0}: the '{1}' translation is {2} characters long, over the line's MaxCharacters ({3})."), FText::AsNumber(i + 1), FText::FromString(Culture), FText::AsNumber(Translation.Len()), FText::AsNumber(Line.MaxCharacters)),
					Line.LineId, i);
			}

			// Every source placeholder must survive into the translation. EXTRA translation
			// placeholders are fine when they are named-asset tokens: the gender mechanism
			// ("{player:gender}|gender(...)") exists only in translations by design.
			const TSet<FString> TranslationArgs = GetFormatArgs(Translation);
			TSet<FString> Problems;
			for (const FString& Arg : SourceArgs)
			{
				if (!TranslationArgs.Contains(Arg)) { Problems.Add(Arg); }
			}
			for (const FString& Arg : TranslationArgs)
			{
				if (SourceArgs.Contains(Arg)) { continue; }

				FString Token = Arg;
				FString Modifier;
				Arg.Split(TEXT(":"), &Token, &Modifier);
				if (!NamedTokens.Contains(FName(*Token))) { Problems.Add(Arg); }
			}
			if (Problems.Num() > 0)
			{
				AddIssue(EKzValidationSeverity::Error,
					FText::Format(LOCTEXT("PlaceholderMismatch", "Line {0}: the '{1}' translation's placeholders ({2}) do not match the source's ({3})."), FText::AsNumber(i + 1), FText::FromString(Culture), FText::FromString(FString::Join(TranslationArgs, TEXT(", "))), FText::FromString(FString::Join(SourceArgs, TEXT(", ")))),
					Line.LineId, i);
			}
		}
	}
}

void UKzDialogueValidator_Localization::ValidateNamedAssetToken(const UKzNamedAsset* Named, TArray<FKzValidationIssue>& OutIssues) const
{
	if (Named->Token.IsNone()) { return; }

	const IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	TArray<FAssetData> NamedAssets;
	Registry.GetAssetsByClass(UKzNamedAsset::StaticClass()->GetClassPathName(), NamedAssets, /*bSearchSubClasses=*/true);
	for (const FAssetData& Other : NamedAssets)
	{
		if (Other.ToSoftObjectPath() == FSoftObjectPath(Named)) { continue; }

		FName OtherToken;
		if (Other.GetTagValue(GET_MEMBER_NAME_CHECKED(UKzNamedAsset, Token), OtherToken) && OtherToken == Named->Token)
		{
			OutIssues.Add(FKzValidationIssue(EKzValidationSeverity::Error,
				FText::Format(LOCTEXT("DuplicateNamedToken", "Token '{0}' is also claimed by {1}; text references resolve to only one of them."), FText::FromName(Named->Token), FText::FromString(Other.GetObjectPathString())),
				GetValidatorId()));
		}
	}
}

void UKzDialogueValidator_Localization::ValidateSpeakerAsset(const UKzSpeakerAsset* Speaker, TArray<FKzValidationIssue>& OutIssues) const
{
	const FName Id = GetValidatorId();
	const FString ExpectedNamespace = FString::Printf(TEXT("KzSpeaker.%s"), *Speaker->AssetId.ToString(EGuidFormats::Digits));

	struct FKzNamedField { const TCHAR* Key; const FText& Text; };
	const FKzNamedField Fields[] = {
		{ TEXT("DisplayName"), Speaker->DisplayName },
		{ TEXT("GivenName"), Speaker->GivenName.Text },
		{ TEXT("FamilyName"), Speaker->FamilyName.Text },
		{ TEXT("SecondFamilyName"), Speaker->SecondFamilyName.Text },
		{ TEXT("Honorific"), Speaker->Honorific.Text },
		{ TEXT("Qualifier"), Speaker->Qualifier.Text },
		{ TEXT("NickName"), Speaker->NickName.Text },
	};

	// Key anchoring. Self-heals on resave. Culture-invariant fields are deliberately
	// non-localizable and carry no key on purpose.
	for (const FKzNamedField& Field : Fields)
	{
		if (Field.Text.IsEmpty() || Field.Text.IsCultureInvariant()) { continue; }
		const TOptional<FString> Namespace = FTextInspector::GetNamespace(Field.Text);
		const TOptional<FString> Key = FTextInspector::GetKey(Field.Text);
		if (!Namespace.IsSet() || !Key.IsSet() || Namespace.GetValue() != ExpectedNamespace || Key.GetValue() != Field.Key)
		{
			OutIssues.Add(FKzValidationIssue(EKzValidationSeverity::Warning,
				FText::Format(LOCTEXT("SpeakerUnstableKey", "{0} is not anchored to its stable localization key; resave the asset to rebind it."), FText::FromString(Field.Key)),
				Id));
		}
	}

	// Stale name translations, against the localization target archives.
	FKzLocTargetInfo Target;
	FText TargetError;
	if (!FKzDialogueTranslationCsv::ReadLocTargetInfo(Target, TargetError) || Target.ForeignCultures.IsEmpty()) { return; }

	FLocTextHelper LocHelper(Target.TargetPath, Target.ManifestName, Target.ArchiveName, Target.NativeCulture, Target.ForeignCultures, nullptr);
	if (!LocHelper.LoadManifest(ELocTextHelperLoadFlags::Load, nullptr)) { return; }

	for (const FString& Culture : Target.ForeignCultures)
	{
		if (!LocHelper.LoadForeignArchive(Culture, ELocTextHelperLoadFlags::Load, nullptr)) { continue; }

		for (const FKzNamedField& Field : Fields)
		{
			if (Field.Text.IsEmpty()) { continue; }
			const FString* SourceString = FTextInspector::GetSourceString(Field.Text);
			if (!SourceString) { continue; }

			const TSharedPtr<FArchiveEntry> Entry = LocHelper.FindTranslation(Culture, FLocKey(ExpectedNamespace), FLocKey(Field.Key), nullptr);
			if (Entry.IsValid() && !Entry->Translation.Text.IsEmpty() && Entry->Source.Text != *SourceString)
			{
				OutIssues.Add(FKzValidationIssue(EKzValidationSeverity::Warning,
					FText::Format(LOCTEXT("SpeakerStaleTranslation", "{0}: the '{1}' translation predates the current name; needs review."), FText::FromString(Field.Key), FText::FromString(Culture)),
					Id));
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE