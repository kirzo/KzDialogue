// Copyright 2026 kirzo

#include "Validation/KzDialogueValidators.h"
#include "KzDialogueAsset.h"
#include "KzDialogueTimeline.h"
#include "KzDialogueNotify.h"
#include "KzNamedAsset.h"
#include "KzSpeakerAsset.h"
#include "KzWordAsset.h"
#include "Localization/KzL10N.h"
#include "Widgets/SKzDialogueTimeline.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundWave.h"

#include "LocTextHelper.h"

#define LOCTEXT_NAMESPACE "KzDialogueValidators"

namespace
{
	/** "12 '(Kirzo) Open the door...'": the line's number plus a peek at it, so a message reads without opening the asset. */
	FText DescribeLine(int32 Index, const FKzDialogueLine& Line)
	{
		return FText::Format(INVTEXT("{0} '{1}'"), FText::AsNumber(Index + 1), Line.GetDisplayLabel(40));
	}
}

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
				FText::Format(LOCTEXT("EmptyLine", "Line {0} has neither text nor audio."), DescribeLine(i, Line)),
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

		// The registry knows whether the wave exists without loading it.
		if (!IAssetRegistry::GetChecked().GetAssetByObjectPath(Line.Audio.ToSoftObjectPath()).IsValid())
		{
			FKzValidationIssue Issue = FKzValidationIssue::WithContextId(
				EKzValidationSeverity::Error,
				FText::Format(
					LOCTEXT("BrokenAudio", "Line {0} references audio that does not exist: {1}"),
					DescribeLine(i, Line),
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
		if (Line.Audio.IsNull())
		{
			// Hidden in the details once the audio is gone, but it still lengthens the line.
			if (Line.AudioStartDelay > 0.0f)
			{
				FKzValidationIssue Issue = FKzValidationIssue::WithContextId(
					EKzValidationSeverity::Warning,
					FText::Format(LOCTEXT("AudioDelayNoAudio", "Line {0}: AudioStartDelay ({1}s) is set but the line has no audio; it still lengthens the line."), DescribeLine(i, Line), Line.AudioStartDelay),
					Id,
					Line.LineId);
				Issue.ContextIndex = i;
				OutIssues.Add(Issue);
			}
			continue;
		}
		if (Line.AudioStartTime <= 0.0f && Line.AudioEndTime <= 0.0f) { continue; }

		if (Line.AudioEndTime > 0.0f && Line.AudioEndTime <= Line.AudioStartTime)
		{
			FKzValidationIssue Issue = FKzValidationIssue::WithContextId(
				EKzValidationSeverity::Error,
				FText::Format(LOCTEXT("AudioRangeEmpty", "Line {0}: AudioEndTime must be greater than AudioStartTime."), DescribeLine(i, Line)),
				Id,
				Line.LineId);
			Issue.ContextIndex = i;
			OutIssues.Add(Issue);
		}

		if (Line.bLocalizeAudio)
		{
			FKzValidationIssue Issue = FKzValidationIssue::WithContextId(
				EKzValidationSeverity::Warning,
				FText::Format(LOCTEXT("AudioRangeLocalized", "Line {0}: custom playback range on audio marked for localization; localized takes have their own timing. Uncheck Localize Audio or clear the range."), DescribeLine(i, Line)),
				Id,
				Line.LineId);
			Issue.ContextIndex = i;
			OutIssues.Add(Issue);
		}

		// The wave's length from its registry tag, so validating never loads audio.
		float AudioDuration = 0.0f;
		IAssetRegistry::GetChecked().GetAssetByObjectPath(Line.Audio.ToSoftObjectPath()).GetTagValue(GET_MEMBER_NAME_CHECKED(USoundBase, Duration), AudioDuration);
		if (AudioDuration > 0.0f && Line.AudioStartTime >= AudioDuration)
		{
			FKzValidationIssue Issue = FKzValidationIssue::WithContextId(
				EKzValidationSeverity::Warning,
				FText::Format(LOCTEXT("AudioRangePastEnd", "Line {0}: AudioStartTime ({1}s) is at or beyond the audio's length ({2}s)."), DescribeLine(i, Line), Line.AudioStartTime, AudioDuration),
				Id,
				Line.LineId);
			Issue.ContextIndex = i;
			OutIssues.Add(Issue);
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
					DescribeLine(i, Line)),
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
				FText::Format(LOCTEXT("InvalidGuid", "Line {0} has an invalid GUID."), DescribeLine(i, Line)),
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
					DescribeLine(i, Line), FText::AsNumber(*Earlier + 1)),
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
			if (!Dialogue->FindLine(LineId))
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
			const FKzDialogueLine* Line = Dialogue->FindLine(LineId);
			if (!Line) { continue; } // covered by InvalidLine validator

			if (!(Line->Speaker == Alias.Speaker))
			{
				FKzValidationIssue Issue = FKzValidationIssue::WithContextId(
					EKzValidationSeverity::Error,
					FText::Format(LOCTEXT("AliasSpeakerMismatch",
						"Alias '{0}' (speaker: {1}) references a line with a different speaker ({2})."),
						FText::FromName(Alias.AliasName),
						Alias.Speaker.GetEditorLabel(),
						Line->Speaker.GetEditorLabel()),
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

		const USoundBase* Audio = Line.Audio.IsNull() ? nullptr : Line.Audio.LoadSynchronous();
		const float LineDuration = Line.ResolveTotalDuration(Audio ? Audio->GetDuration() : 0.f);

		FKzDialogueTimeResolveContext ResolveCtx;
		ResolveCtx.LineDuration = LineDuration;
		ResolveCtx.Audio = Audio;
		ResolveCtx.AudioOffset = Line.GetAudioTimelineOffset();

		const FText LineLabel = DescribeLine(i, Line);
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
						float MarkerStart = 0.f;
						float MarkerEnd = 0.f;
						if (!Marker->FindMarker(Wave, MarkerStart, MarkerEnd))
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
	return Asset && Asset->IsA<UKzDialogueAsset>();
}

void UKzDialogueValidator_Localization::Validate_Implementation(const UObject* Asset, TArray<FKzValidationIssue>& OutIssues) const
{
	const UKzDialogueAsset* Dialogue = Cast<UKzDialogueAsset>(Asset);
	if (!Dialogue) { return; }

	const FName Id = GetValidatorId();

	auto AddIssue = [&](EKzValidationSeverity Severity, const FText& Message, const FGuid& LineId, int32 LineIndex)
	{
		FKzValidationIssue Issue = FKzValidationIssue::WithContextId(Severity, Message, Id, LineId);
		Issue.ContextIndex = LineIndex;
		OutIssues.Add(Issue);
	};

	// Every localizable FText must sit on its stable key, or gather produces throwaway keys and
	// existing translations orphan. Flags exactly what the resave rebind would change.
	for (int32 i = 0; i < Dialogue->Lines.Num(); ++i)
	{
		const FKzDialogueLine& Line = Dialogue->Lines[i];
		const TPair<FString, FString> TextId = Dialogue->GetLineTextId(Line);
		FText Anchored = Line.Text;
		if (KzAnchorText(Anchored, TextId.Key, TextId.Value))
		{
			AddIssue(EKzValidationSeverity::Warning,
				FText::Format(LOCTEXT("UnstableKey", "Line {0}: text is not anchored to its stable localization key; resave the asset to rebind it."), DescribeLine(i, Line)),
				Line.LineId, i);
		}
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
						FText::Format(LOCTEXT("UnknownNamedToken", "Line {0}: no named asset claims the token '{1}' referenced by '`{{2}`}'."), DescribeLine(i, Line), FText::FromString(TokenPart), FText::FromString(Arg)),
						Line.LineId, i);
					continue;
				}

				const UKzNamedAsset* Named = Cast<UKzNamedAsset>(Path->TryLoad());
				if (Named && !Named->GetNameParts().Contains(FName(*Modifier)))
				{
					AddIssue(EKzValidationSeverity::Warning,
						FText::Format(LOCTEXT("UnknownNamePart", "Line {0}: '{1}' is not a name part of '{2}' (see the token picker for the valid ones)."), DescribeLine(i, Line), FText::FromString(Modifier), FText::FromString(Named->GetName())),
						Line.LineId, i);
				}
			}
		}
	}

	// Translation-dependent checks need a localization target; without one there is nothing to compare against.
	FKzLocTargetInfo Target;
	FText TargetError;
	if (!KzDialogueL10N::ReadLocTargetInfo(Target, TargetError) || Target.ForeignCultures.IsEmpty()) { return; }

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

	auto JoinSorted = [](const TSet<FString>& Args)
	{
		TArray<FString> Sorted = Args.Array();
		Sorted.Sort();
		return FText::FromString(FString::Join(Sorted, TEXT(", ")));
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

			// Untranslated is the Localization tab's business, not a per-save warning.
			if (!Entry.IsValid() || Entry->Translation.Text.IsEmpty()) { continue; }

			const FString& Translation = Entry->Translation.Text;

			// Stale translation: the remaining checks would compare against the wrong baseline.
			if (Entry->Source.Text != *SourceString)
			{
				AddIssue(EKzValidationSeverity::Warning,
					FText::Format(LOCTEXT("StaleTranslation", "Line {0}: the '{1}' translation predates the current source text; needs review."), DescribeLine(i, Line), FText::FromString(Culture)),
					Line.LineId, i);
				continue;
			}

			if (Line.MaxCharacters > 0 && Translation.Len() > Line.MaxCharacters)
			{
				AddIssue(EKzValidationSeverity::Warning,
					FText::Format(LOCTEXT("MaxCharsExceeded", "Line {0}: the '{1}' translation is {2} characters long, over the line's MaxCharacters ({3})."), DescribeLine(i, Line), FText::FromString(Culture), FText::AsNumber(Translation.Len()), FText::AsNumber(Line.MaxCharacters)),
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
					FText::Format(LOCTEXT("PlaceholderMismatch", "Line {0}: the '{1}' translation's placeholders ({2}) do not match the source's ({3})."), DescribeLine(i, Line), FText::FromString(Culture), JoinSorted(TranslationArgs), JoinSorted(SourceArgs)),
					Line.LineId, i);
			}
		}
	}
}

// =======================================================================================
// Named assets
// =======================================================================================

bool UKzDialogueValidator_NamedAsset::CanValidate_Implementation(const UObject* Asset) const
{
	return Asset && Asset->IsA<UKzNamedAsset>();
}

void UKzDialogueValidator_NamedAsset::Validate_Implementation(const UObject* Asset, TArray<FKzValidationIssue>& OutIssues) const
{
	const UKzNamedAsset* Named = Cast<UKzNamedAsset>(Asset);
	if (!Named) { return; }

	if (const UKzSpeakerAsset* Speaker = Cast<UKzSpeakerAsset>(Named))
	{
		ValidateSpeakerAsset(Speaker, OutIssues);
	}

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

void UKzDialogueValidator_NamedAsset::ValidateSpeakerAsset(const UKzSpeakerAsset* Speaker, TArray<FKzValidationIssue>& OutIssues) const
{
	const FName Id = GetValidatorId();
	const FString ExpectedNamespace = FString::Printf(TEXT("KzSpeaker.%s"), *Speaker->AssetId.ToString(EGuidFormats::Digits));

	// Flags exactly what the resave rebind would change.
	Speaker->ForEachLocalizedField([&](const TCHAR* Key, const FText& Text, uint32, const FKzWordText*)
	{
		FText Anchored = Text;
		if (KzAnchorText(Anchored, ExpectedNamespace, Key))
		{
			OutIssues.Add(FKzValidationIssue(EKzValidationSeverity::Warning,
				FText::Format(LOCTEXT("SpeakerUnstableKey", "{0} is not anchored to its stable localization key; resave the asset to rebind it."), FText::FromString(Key)),
				Id));
		}
	});

	// Stale name translations, against the localization target archives.
	FKzLocTargetInfo Target;
	FText TargetError;
	if (!KzDialogueL10N::ReadLocTargetInfo(Target, TargetError) || Target.ForeignCultures.IsEmpty()) { return; }

	FLocTextHelper LocHelper(Target.TargetPath, Target.ManifestName, Target.ArchiveName, Target.NativeCulture, Target.ForeignCultures, nullptr);
	if (!LocHelper.LoadManifest(ELocTextHelperLoadFlags::Load, nullptr)) { return; }

	for (const FString& Culture : Target.ForeignCultures)
	{
		if (!LocHelper.LoadForeignArchive(Culture, ELocTextHelperLoadFlags::Load, nullptr)) { continue; }

		Speaker->ForEachLocalizedField([&](const TCHAR* Key, const FText& Text, uint32, const FKzWordText*)
		{
			const FString* SourceString = FTextInspector::GetSourceString(Text);
			if (Text.IsEmpty() || !SourceString) { return; }

			const TSharedPtr<FArchiveEntry> Entry = LocHelper.FindTranslation(Culture, FLocKey(ExpectedNamespace), FLocKey(Key), nullptr);
			if (Entry.IsValid() && !Entry->Translation.Text.IsEmpty() && Entry->Source.Text != *SourceString)
			{
				OutIssues.Add(FKzValidationIssue(EKzValidationSeverity::Warning,
					FText::Format(LOCTEXT("SpeakerStaleTranslation", "{0}: the '{1}' translation predates the current name; needs review."), FText::FromString(Key), FText::FromString(Culture)),
					Id));
			}
		});
	}
}

// =======================================================================================
// Identity validator
// =======================================================================================

namespace
{
	FGuid GetLocalizationAssetId(const UObject* Asset)
	{
		if (const UKzDialogueAsset* Dialogue = Cast<UKzDialogueAsset>(Asset)) { return Dialogue->AssetId; }
		if (const UKzSpeakerAsset* Speaker = Cast<UKzSpeakerAsset>(Asset)) { return Speaker->AssetId; }
		if (const UKzWordAsset* Word = Cast<UKzWordAsset>(Asset)) { return Word->AssetId; }
		return FGuid();
	}
}

bool UKzDialogueValidator_DuplicateAssetId::CanValidate_Implementation(const UObject* Asset) const
{
	return GetLocalizationAssetId(Asset).IsValid();
}

void UKzDialogueValidator_DuplicateAssetId::Validate_Implementation(const UObject* Asset, TArray<FKzValidationIssue>& OutIssues) const
{
	const FGuid AssetId = GetLocalizationAssetId(Asset);

	// Packages saved before AssetId became a registry tag carry none and are not compared.
	TArray<FAssetData> Others;
	IAssetRegistry::GetChecked().GetAssetsByClass(Asset->GetClass()->GetClassPathName(), Others);
	for (const FAssetData& Other : Others)
	{
		FString OtherIdString;
		FGuid OtherId;
		if (Other.ToSoftObjectPath() != FSoftObjectPath(Asset) && Other.GetTagValue(GET_MEMBER_NAME_CHECKED(UKzDialogueAsset, AssetId), OtherIdString)
			&& FGuid::Parse(OtherIdString, OtherId) && OtherId == AssetId)
		{
			OutIssues.Add(FKzValidationIssue(EKzValidationSeverity::Error,
				FText::Format(LOCTEXT("DuplicateAssetId", "AssetId is shared with {0}, a copy made outside the editor: both write their texts into the same localization namespace. Delete the copy and duplicate the original inside the editor, which gives it a fresh identity."), FText::FromString(Other.GetObjectPathString())),
				GetValidatorId()));
		}
	}
}
#undef LOCTEXT_NAMESPACE