// Copyright 2026 kirzo

#include "Widgets/KzWaveformPreview.h"

#include "Layout/Geometry.h"
#include "Rendering/DrawElements.h"
#include "Sound/SoundWave.h"
#include "Styling/AppStyle.h"

TSharedRef<FKzWaveformPreview> KzBuildWaveformPreview(USoundWave* Wave)
{
	TSharedRef<FKzWaveformPreview> Preview = MakeShared<FKzWaveformPreview>();
	Preview->Wave = Wave;

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

	return Preview;
}

void KzPaintWaveform(const FKzWaveformPreview& Preview, const FGeometry& Geometry, FSlateWindowElementList& OutDrawElements, int32 LayerId, float HalfSpan, float StartSeconds, float SecondsPerPixel, TFunctionRef<FLinearColor(float X)> ColorAt)
{
	const int32 N = Preview.Peaks.Num();
	if (N == 0 || Preview.Duration <= UE_KINDA_SMALL_NUMBER) { return; }

	const FSlateBrush* Brush = FAppStyle::GetBrush("WhiteBrush");
	const FVector2f Size(Geometry.GetLocalSize());
	const float CenterY = Size.Y * 0.5f;
	for (int32 Px = 0; Px < FMath::FloorToInt(Size.X); ++Px)
	{
		const float TLeft = StartSeconds + Px * SecondsPerPixel;
		const float TRight = TLeft + SecondsPerPixel;
		if (TRight <= 0.f || TLeft >= Preview.Duration) { continue; }

		// Every bucket under the pixel counts, so peaks survive any zoom.
		const int32 B0 = FMath::Clamp(static_cast<int32>(FMath::Max(TLeft, 0.f) / Preview.Duration * N), 0, N - 1);
		const int32 B1 = FMath::Clamp(FMath::CeilToInt(FMath::Min(TRight, Preview.Duration) / Preview.Duration * N), B0 + 1, N);
		float Mn = 0.f;
		float Mx = 0.f;
		for (int32 B = B0; B < B1; ++B)
		{
			Mn = FMath::Min(Mn, Preview.Peaks[B].X);
			Mx = FMath::Max(Mx, Preview.Peaks[B].Y);
		}

		const float Top = CenterY - Mx * Preview.Gain * HalfSpan;
		const float ColumnHeight = FMath::Max((Mx - Mn) * Preview.Gain * HalfSpan, 1.f);
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId, Geometry.ToPaintGeometry(FVector2f(1.f, ColumnHeight), FSlateLayoutTransform(FVector2f(static_cast<float>(Px), Top))), Brush, ESlateDrawEffect::None, ColorAt(static_cast<float>(Px)));
	}
}