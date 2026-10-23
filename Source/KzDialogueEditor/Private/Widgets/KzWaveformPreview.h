// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"

class FSlateWindowElementList;
class USoundWave;
struct FGeometry;

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

TSharedRef<FKzWaveformPreview> KzBuildWaveformPreview(USoundWave* Wave);

/** Paints Preview as one min/max column per pixel of Geometry, from wave second StartSeconds at x = 0, advancing SecondsPerPixel. Columns reach HalfSpan above and below the center; ColorAt colors each one by its x. */
void KzPaintWaveform(const FKzWaveformPreview& Preview, const FGeometry& Geometry, FSlateWindowElementList& OutDrawElements, int32 LayerId, float HalfSpan, float StartSeconds, float SecondsPerPixel, TFunctionRef<FLinearColor(float X)> ColorAt);