// Copyright 2026 kirzo

#include "KzDialogueAudition.h"
#include "KzDialogueTypes.h"

#include "Components/AudioComponent.h"
#include "Containers/Ticker.h"
#include "Editor.h"
#include "Sound/SoundBase.h"

namespace
{
	// The editor preview component is shared with other tools: ours only while it still plays our sound.
	TWeakObjectPtr<UAudioComponent> GAudio;
	TWeakObjectPtr<USoundBase> GSound;
	FString GKey;
	FTSTicker::FDelegateHandle GCutTicker;

	UAudioComponent* OwnedAudio()
	{
		UAudioComponent* Audio = GAudio.Get();
		return Audio && GSound.IsValid() && Audio->Sound == GSound.Get() ? Audio : nullptr;
	}

	FString LineKey(const FGuid& LineId)
	{
		return LineId.ToString(EGuidFormats::Digits);
	}
}

void KzDialogueAudition::Play(USoundBase* Sound, const FString& Key, float Start, float End)
{
	Stop();
	if (!Sound || !GEditor) { return; }

	UAudioComponent* Audio = GEditor->PlayPreviewSound(Sound);
	if (!Audio) { return; }
	if (Start > 0.0f)
	{
		Audio->Stop();
		Audio->Play(Start);
	}

	GAudio = Audio;
	GSound = Sound;
	GKey = Key;

	if (End > 0.0f)
	{
		GCutTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Key](float)
		{
			GCutTicker.Reset();
			if (IsPlaying(Key)) { OwnedAudio()->FadeOut(0.1f, 0.0f); }
			return false;
		}), FMath::Max(0.05f, End - Start));
	}
}

void KzDialogueAudition::PlayLine(const FKzDialogueLine& Line)
{
	Play(Line.Audio.LoadSynchronous(), LineKey(Line.LineId), FMath::Max(0.0f, Line.AudioStartTime), Line.AudioEndTime);
}

void KzDialogueAudition::Stop(const FString& Key)
{
	if (!Key.IsEmpty() && Key != GKey) { return; }

	if (GCutTicker.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(GCutTicker);
		GCutTicker.Reset();
	}
	if (UAudioComponent* Audio = OwnedAudio())
	{
		Audio->Stop();
	}
	GAudio = nullptr;
	GSound = nullptr;
	GKey.Reset();
}

bool KzDialogueAudition::IsPlaying(const FString& Key)
{
	const UAudioComponent* Audio = OwnedAudio();
	return !Key.IsEmpty() && Key == GKey && Audio && Audio->IsPlaying();
}

bool KzDialogueAudition::IsPlayingLine(const FGuid& LineId)
{
	return IsPlaying(LineKey(LineId));
}

void KzDialogueAudition::ToggleLine(const FKzDialogueLine& Line)
{
	if (IsPlayingLine(Line.LineId)) { Stop(LineKey(Line.LineId)); }
	else { PlayLine(Line); }
}