// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "Sound/SoundWave.h"

/**
 * Downsampled min/max envelope of a sound wave, drawn along a time axis. Shared by the
 * notify timeline's waveform band and the audio range strip; cache it on the owning widget
 * and rebuild only when the wave changes (decoding is not free).
 */
struct FKzWaveformPreview
{
	TWeakObjectPtr<USoundWave> Wave;
	TArray<FVector2f> Peaks;
	float Duration = 0.f;
	float Gain = 1.f;
};

inline TSharedRef<FKzWaveformPreview> KzBuildWaveformPreview(USoundWave* Wave)
{
	TSharedRef<FKzWaveformPreview> Preview = MakeShared<FKzWaveformPreview>();
	Preview->Wave = Wave;

#if WITH_EDITOR
	TArray<uint8> RawPCM;
	uint32 SampleRate = 0;
	uint16 NumChannels = 0;
	if (Wave && Wave->GetImportedSoundWaveData(RawPCM, SampleRate, NumChannels) && NumChannels > 0 && SampleRate > 0 && RawPCM.Num() >= static_cast<int32>(sizeof(int16)))
	{
		const int16* Samples = reinterpret_cast<const int16*>(RawPCM.GetData());
		const int32 NumFrames = (RawPCM.Num() / sizeof(int16)) / NumChannels;
		const int32 BucketCount = FMath::Clamp(NumFrames, 1, 4096);
		Preview->Peaks.SetNumUninitialized(BucketCount);

		float Peak = 0.f;
		for (int32 Bucket = 0; Bucket < BucketCount; ++Bucket)
		{
			const int32 FrameStart = static_cast<int32>(static_cast<int64>(Bucket) * NumFrames / BucketCount);
			const int32 FrameEnd = FMath::Max(FrameStart + 1, static_cast<int32>(static_cast<int64>(Bucket + 1) * NumFrames / BucketCount));
			float Mn = 0.f;
			float Mx = 0.f;
			for (int32 Frame = FrameStart; Frame < FrameEnd && Frame < NumFrames; ++Frame)
			{
				const float S = Samples[Frame * NumChannels] / 32768.0f;
				Mn = FMath::Min(Mn, S);
				Mx = FMath::Max(Mx, S);
			}
			Preview->Peaks[Bucket] = FVector2f(Mn, Mx);
			Peak = FMath::Max(Peak, FMath::Max(-Mn, Mx));
		}
		Preview->Duration = static_cast<float>(NumFrames) / static_cast<float>(SampleRate);
		Preview->Gain = Peak > UE_KINDA_SMALL_NUMBER ? FMath::Min(1.0f / Peak, 10.0f) : 1.0f;
	}
#endif

	return Preview;
}