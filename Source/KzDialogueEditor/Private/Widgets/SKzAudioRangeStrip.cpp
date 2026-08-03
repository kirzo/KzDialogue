// Copyright 2026 kirzo

#include "Widgets/SKzAudioRangeStrip.h"
#include "Widgets/KzWaveformPreview.h"

#include "PropertyHandle.h"
#include "Rendering/DrawElements.h"
#include "Styling/AppStyle.h"

namespace
{
	constexpr float KzEdgeGrabPx = 6.f;
	constexpr float KzMinRange = 0.05f;

	const FLinearColor KzStripBackground(0.02f, 0.02f, 0.02f);
	const FLinearColor KzWaveDim(0.32f, 0.32f, 0.32f);
	const FLinearColor KzWaveInRange(0.25f, 0.6f, 0.9f);
	const FLinearColor KzStartEdge(0.3f, 0.8f, 0.3f);
	const FLinearColor KzEndEdge(0.9f, 0.35f, 0.3f);
}

void SKzAudioRangeStrip::Construct(const FArguments& InArgs, USoundWave* Wave)
{
	StartHandle = InArgs._StartHandle;
	EndHandle = InArgs._EndHandle;
	Preview = KzBuildWaveformPreview(Wave);
}

FVector2D SKzAudioRangeStrip::ComputeDesiredSize(float) const
{
	return FVector2D(100.f, 48.f);
}

float SKzAudioRangeStrip::GetDuration() const
{
	return Preview.IsValid() && Preview->Duration > 0.f ? Preview->Duration : 1.f;
}

float SKzAudioRangeStrip::GetStartTime() const
{
	if (Drag == EDragEdge::Start) { return DragTime; }
	float Value = 0.f;
	if (StartHandle.IsValid()) { StartHandle->GetValue(Value); }
	return FMath::Clamp(Value, 0.f, GetDuration());
}

float SKzAudioRangeStrip::GetEndTime() const
{
	if (Drag == EDragEdge::End) { return DragTime; }
	float Value = 0.f;
	if (EndHandle.IsValid()) { EndHandle->GetValue(Value); }
	// 0 = play to the natural end.
	return Value > 0.f ? FMath::Min(Value, GetDuration()) : GetDuration();
}

float SKzAudioRangeStrip::TimeToX(float Time, float Width) const
{
	return FMath::Clamp(Time / GetDuration(), 0.f, 1.f) * Width;
}

float SKzAudioRangeStrip::XToTime(float X, float Width) const
{
	return FMath::Clamp(X / FMath::Max(1.f, Width), 0.f, 1.f) * GetDuration();
}

SKzAudioRangeStrip::EDragEdge SKzAudioRangeStrip::HitTestEdge(const FGeometry& MyGeometry, const FVector2D& ScreenPos) const
{
	const float Width = MyGeometry.GetLocalSize().X;
	const float LocalX = MyGeometry.AbsoluteToLocal(ScreenPos).X;
	const float StartX = TimeToX(GetStartTime(), Width);
	const float EndX = TimeToX(GetEndTime(), Width);

	// Topmost-ish rule: when the edges overlap, prefer the one whose side has room.
	const bool bOnStart = FMath::Abs(LocalX - StartX) <= KzEdgeGrabPx;
	const bool bOnEnd = FMath::Abs(LocalX - EndX) <= KzEdgeGrabPx;
	if (bOnStart && bOnEnd) { return LocalX < (StartX + EndX) * 0.5f ? EDragEdge::Start : EDragEdge::End; }
	if (bOnStart) { return EDragEdge::Start; }
	if (bOnEnd) { return EDragEdge::End; }
	return EDragEdge::None;
}

int32 SKzAudioRangeStrip::OnPaint(const FPaintArgs& /*Args*/, const FGeometry& AllottedGeometry, const FSlateRect& /*MyCullingRect*/, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& /*InWidgetStyle*/, bool /*bParentEnabled*/) const
{
	const FSlateBrush* Brush = FAppStyle::GetBrush("WhiteBrush");
	const FVector2f Size(AllottedGeometry.GetLocalSize());
	const float Width = Size.X;
	const float Height = Size.Y;

	FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
		AllottedGeometry.ToPaintGeometry(FVector2f(Width, Height), FSlateLayoutTransform(FVector2f(0.f, 0.f))),
		Brush, ESlateDrawEffect::None, KzStripBackground);

	const float StartX = TimeToX(GetStartTime(), Width);
	const float EndX = TimeToX(GetEndTime(), Width);

	// Waveform: one min/max column per pixel, brighter inside the active range.
	if (Preview.IsValid() && Preview->Peaks.Num() > 0)
	{
		const float Center = Height * 0.5f;
		const float HalfSpan = Height * 0.45f;
		for (int32 X = 0; X < FMath::FloorToInt(Width); ++X)
		{
			const int32 Bucket = FMath::Clamp(static_cast<int32>(static_cast<float>(X) / Width * Preview->Peaks.Num()), 0, Preview->Peaks.Num() - 1);
			const FVector2f MinMax = Preview->Peaks[Bucket] * Preview->Gain;
			const float Top = Center - MinMax.Y * HalfSpan;
			const float Bottom = Center - MinMax.X * HalfSpan;
			const bool bInRange = X >= StartX && X <= EndX;

			FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1,
				AllottedGeometry.ToPaintGeometry(FVector2f(1.f, FMath::Max(1.f, Bottom - Top)), FSlateLayoutTransform(FVector2f(static_cast<float>(X), Top))),
				Brush, ESlateDrawEffect::None, bInRange ? KzWaveInRange : KzWaveDim);
		}
	}

	// Range edges: full-height lines with a small grab block at the top.
	auto DrawEdge = [&](float X, const FLinearColor& Color)
	{
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 2,
			AllottedGeometry.ToPaintGeometry(FVector2f(1.f, Height), FSlateLayoutTransform(FVector2f(X, 0.f))),
			Brush, ESlateDrawEffect::None, Color);
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 2,
			AllottedGeometry.ToPaintGeometry(FVector2f(5.f, 8.f), FSlateLayoutTransform(FVector2f(X - 2.f, 0.f))),
			Brush, ESlateDrawEffect::None, Color);
	};
	DrawEdge(StartX, KzStartEdge);
	DrawEdge(EndX, KzEndEdge);

	return LayerId + 3;
}

FReply SKzAudioRangeStrip::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}

	const EDragEdge Edge = HitTestEdge(MyGeometry, MouseEvent.GetScreenSpacePosition());
	if (Edge == EDragEdge::None)
	{
		return FReply::Unhandled();
	}

	Drag = Edge;
	DragTime = Edge == EDragEdge::Start ? GetStartTime() : GetEndTime();
	return FReply::Handled().CaptureMouse(SharedThis(this));
}

FReply SKzAudioRangeStrip::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (Drag == EDragEdge::None || !HasMouseCapture())
	{
		return FReply::Unhandled();
	}

	const float Width = MyGeometry.GetLocalSize().X;
	const float Time = XToTime(MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()).X, Width);

	// Preview only; the property (and its transaction + asset-side clamping) commits on release.
	if (Drag == EDragEdge::Start)
	{
		float End = 0.f;
		if (EndHandle.IsValid()) { EndHandle->GetValue(End); }
		const float MaxStart = (End > 0.f ? End : GetDuration()) - KzMinRange;
		DragTime = FMath::Clamp(Time, 0.f, FMath::Max(0.f, MaxStart));
	}
	else
	{
		float Start = 0.f;
		if (StartHandle.IsValid()) { StartHandle->GetValue(Start); }
		DragTime = FMath::Clamp(Time, Start + KzMinRange, GetDuration());
	}
	return FReply::Handled();
}

FReply SKzAudioRangeStrip::OnMouseButtonUp(const FGeometry& /*MyGeometry*/, const FPointerEvent& MouseEvent)
{
	if (Drag == EDragEdge::None || MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}

	const EDragEdge Edge = Drag;
	const float Time = DragTime;
	Drag = EDragEdge::None;

	if (Edge == EDragEdge::Start && StartHandle.IsValid())
	{
		StartHandle->SetValue(Time);
	}
	else if (Edge == EDragEdge::End && EndHandle.IsValid())
	{
		// Reaching the wave's end means "no cut": store the 0 sentinel so no stop timer arms.
		const float Committed = Time >= GetDuration() - 0.02f ? 0.f : Time;
		EndHandle->SetValue(Committed);
	}

	return FReply::Handled().ReleaseMouseCapture();
}

FCursorReply SKzAudioRangeStrip::OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const
{
	if (Drag != EDragEdge::None || HitTestEdge(MyGeometry, CursorEvent.GetScreenSpacePosition()) != EDragEdge::None)
	{
		return FCursorReply::Cursor(EMouseCursor::ResizeLeftRight);
	}
	return FCursorReply::Unhandled();
}