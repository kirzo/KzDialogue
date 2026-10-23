// Copyright 2026 kirzo

#include "KzDialogueTypes.h"
#include "KzDialogueAsset.h"
#include "KzNamedTokenSubsystem.h"
#include "KzSpeakerAsset.h"
#include "Settings/KzDialogueSettings.h"

FText FKzDialogueLine::GetFormattedText() const
{
	return FormatArguments.Num() > 0 ? FText::Format(FTextFormat(Text), FormatArguments) : Text;
}

float FKzDialogueLine::ResolveReadingDuration() const
{
	const UKzDialogueSettings* Settings = UKzDialogueSettings::Get();
	const float Speed = Settings ? Settings->GetActiveReadingSpeed() * UKzDialogueSettings::GetReadingSpeedScale() : 0.0f;
	if (Speed <= 0.0f) { return Settings ? Settings->DefaultDuration : 2.5f; }

	return FMath::Max(Settings->MinReadingDuration, GetFormattedText().ToString().TrimStartAndEnd().Len() / Speed);
}

float FKzDialogueLine::ResolveTotalDuration(float FullAudioDuration) const
{
	// Trimmed range when the line cuts a section out of the wave; full length otherwise.
	float End = FullAudioDuration;
	if (AudioEndTime > 0.0f)
	{
		End = FullAudioDuration > 0.0f ? FMath::Min(AudioEndTime, FullAudioDuration) : AudioEndTime;
	}
	const float AudioLength = FMath::Max(0.0f, End - FMath::Max(0.0f, AudioStartTime));

	float Length = 0.0f;
	switch (DurationMode)
	{
	case EKzLineDurationMode::Override:
		Length = Duration;
		break;
	case EKzLineDurationMode::ExtendAudio:
		Length = AudioLength + Duration;
		break;
	default:
		Length = Duration > 0.0f ? Duration : (AudioLength > 0.0f ? AudioLength : ResolveReadingDuration());
		break;
	}

	// The audio lead extends the line: the text lives its resolved time plus the delay.
	return Length + FMath::Max(0.0f, AudioStartDelay);
}

FText FKzDialogueSpeaker::GetSubtitleName(const UObject* WorldContextObject) const
{
	if (!Asset)
	{
		return FText::GetEmpty();
	}

	const FKzNamedTokenOverride* Override = UKzNamedTokenSubsystem::FindOverrideFor(WorldContextObject, Asset->Token);
	const FText Resolved = Asset->ResolveName(NAME_None, Override);
	if (!Resolved.IsEmpty())
	{
		return Resolved;
	}

	// A freshly created asset with no name authored yet.
	return FText::FromString(FName::NameToDisplayString(Asset->GetName(), false));
}

FText FKzDialogueSpeaker::GetEditorLabel() const
{
	return Asset ? GetSubtitleName(nullptr) : NSLOCTEXT("KzDialogue", "Narration", "<Narration>");
}

bool FKzDialogueLineRef::TryResolve(FKzDialogueLine& OutLine) const
{
	if (!IsValid()) { return false; }

	UKzDialogueAsset* Loaded = Asset.LoadSynchronous();
	if (!Loaded) { return false; }

	return Loaded->TryResolveLineOrAlias(LineId, OutLine);
}

bool FKzDialogueLineList::TryResolve(int32 Index, FKzDialogueLine& OutLine) const
{
	if (!LineIds.IsValidIndex(Index)) { return false; }

	UKzDialogueAsset* Loaded = Asset.LoadSynchronous();
	if (!Loaded) { return false; }

	return Loaded->TryResolveLineOrAlias(LineIds[Index], OutLine);
}

bool FKzDialogueLineList::TryResolveAll(TArray<FKzDialogueLine>& OutLines) const
{
	OutLines.Reset();
	if (!Asset.IsValid() && Asset.IsNull()) { return false; }

	UKzDialogueAsset* Loaded = Asset.LoadSynchronous();
	if (!Loaded) { return false; }

	OutLines.Reserve(LineIds.Num());
	for (const FGuid& Id : LineIds)
	{
		FKzDialogueLine Line;
		if (Loaded->TryResolveLineOrAlias(Id, Line))
		{
			OutLines.Add(MoveTemp(Line));
		}
	}
	return OutLines.Num() > 0;
}

void FKzDialogueLineList::GetLineRefs(TArray<FKzDialogueLineRef>& OutRefs) const
{
	OutRefs.Reset();
	OutRefs.Reserve(LineIds.Num());
	for (const FGuid& Id : LineIds)
	{
		FKzDialogueLineRef Ref;
		Ref.Asset = Asset;
		Ref.LineId = Id;
		OutRefs.Add(MoveTemp(Ref));
	}
}