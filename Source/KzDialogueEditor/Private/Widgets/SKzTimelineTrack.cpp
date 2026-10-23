// Copyright 2026 kirzo

#include "Widgets/SKzTimelineTrack.h"
#include "Widgets/KzWaveformPreview.h"
#include "KzDialogueTimeline.h"
#include "KzDialogueNotify.h"

#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "GenericPlatform/ICursor.h"
#include "Input/CursorReply.h"
#include "Layout/WidgetPath.h"
#include "Rendering/DrawElements.h"
#include "Sound/SoundBase.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SNullWidget.h"

#define LOCTEXT_NAMESPACE "SKzTimelineTrack"

namespace
{
	float GetNextTickSpacing(uint32 CurrentStep)
	{
		if (CurrentStep & 0x01)
		{
			return FMath::Pow(10.f, 0.5f * static_cast<float>(CurrentStep - 1) + 1.f);
		}
		return 0.5f * FMath::Pow(10.f, 0.5f * static_cast<float>(CurrentStep) + 1.f);
	}
}

float FKzTimelineView::TickSpacing(float PixelsPerSecond)
{
	const float MinTickPx = 5.f;
	const float MinSpacing = 0.001f;
	if (PixelsPerSecond <= 0.f) { return MinSpacing; }

	float Spacing = MinSpacing;
	uint32 Step = 0;
	while (Spacing * PixelsPerSecond < MinTickPx)
	{
		Spacing = MinSpacing * GetNextTickSpacing(Step);
		++Step;
	}
	return Spacing;
}

// ---------------------------------------------------------------------------------------
// SKzTimelineTrack
// ---------------------------------------------------------------------------------------

void SKzTimelineTrack::Construct(const FArguments& InArgs)
{
	Timeline = InArgs._Timeline;
	TrackIndex = InArgs._TrackIndex;
	Waveform = InArgs._Waveform;
	Duration = InArgs._Duration;
	AudioTimeOffset = InArgs._AudioTimeOffset;
	LineAudio = InArgs._LineAudio;
	SelectedEventAttr = InArgs._SelectedEvent;
	Playhead = InArgs._Playhead;
	SelectionStart = InArgs._SelectionStart;
	SelectionEnd = InArgs._SelectionEnd;
	OnSelect = InArgs._OnSelect;
	OnBeginRetime = InArgs._OnBeginRetime;
	OnRetime = InArgs._OnRetime;
	OnEndRetime = InArgs._OnEndRetime;
	OnInteractionEnd = InArgs._OnInteractionEnd;
	OnBuildAddMenu = InArgs._OnBuildAddMenu;
	OnBuildEventMenu = InArgs._OnBuildEventMenu;
	ViewStart = InArgs._ViewStart;
	ViewEnd = InArgs._ViewEnd;
	OnZoom = InArgs._OnZoom;
	OnPan = InArgs._OnPan;
	OnMoveToTrack = InArgs._OnMoveToTrack;
	OnDragRow = InArgs._OnDragRow;
	OnActivateEvent = InArgs._OnActivateEvent;
	IsDropTarget = InArgs._IsDropTarget;
	SetClipping(EWidgetClipping::ClipToBounds);

	// Over a marker it describes the marker; over empty space it lists the gestures nothing else shows.
	if (TrackIndex != INDEX_NONE)
	{
		SetToolTipText(TAttribute<FText>::CreateSP(this, &SKzTimelineTrack::GetHoverToolTip));
	}
}

int32 SKzTimelineTrack::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const float W = AllottedGeometry.GetLocalSize().X;
	const float H = AllottedGeometry.GetLocalSize().Y;
	const FKzTimelineView View = MakeView(W);
	const FSlateBrush* Box = FAppStyle::GetBrush("WhiteBrush");

	// Lane background so an empty track still reads as a drop area; lit while a dragged marker would land here.
	FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), Box, ESlateDrawEffect::None, FLinearColor(0.f, 0.f, 0.f, 0.15f));
	if (IsDropTarget.Get(false))
	{
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), Box, ESlateDrawEffect::None, FLinearColor(0.98f, 0.86f, 0.36f, 0.15f));
	}

	// Grid, separator and bound/selection lines are drawn for every row -- including the
	// decorative "Notifies" group row (TrackIndex == INDEX_NONE), which has no events.
	const float Dur = FMath::Max(Duration.Get(1.f), UE_KINDA_SMALL_NUMBER);

	// Grid: vertical lines on the same ticks as the ruler, brighter every tenth, plus a
	// horizontal separator at the bottom of the row.
	if (W > 1.f)
	{
		View.ForEachTick([&](float Seconds, bool bMajor)
		{
			const FLinearColor GridColor(1.f, 1.f, 1.f, bMajor ? 0.10f : 0.04f);
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(FVector2f(1.f, H), FSlateLayoutTransform(FVector2f(View.TimeToX(Seconds), 0.f))), Box, ESlateDrawEffect::None, GridColor);
		});
	}
	// Audio waveform on the group band (when the line has a drawable USoundWave). Timeline
	// second 0 = this wave second, when the line plays a cut of its audio.
	if (const TSharedPtr<FKzWaveformPreview> Wave = Waveform.Get())
	{
		const FLinearColor WaveColor(0.40f, 0.62f, 0.88f, 0.55f);
		KzPaintWaveform(*Wave, AllottedGeometry, OutDrawElements, LayerId, FMath::Max(H * 0.5f - 2.f, 1.f), View.Start + AudioTimeOffset.Get(), View.SecondsPerPixel(),
			[&WaveColor](float) { return WaveColor; });
	}
	else if (Waveform.IsBound())
	{
		// A SoundCue or MetaSound has no single wave to draw.
		if (const USoundBase* Audio = LineAudio.Get(nullptr))
		{
			const FText Hint = FText::Format(LOCTEXT("NoWaveform", "No waveform for a {0}"), FText::FromString(Audio->GetClass()->GetName()));
			PaintHint(AllottedGeometry, OutDrawElements, LayerId + 1, Hint);
		}
	}

	FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(FVector2f(W, 1.f), FSlateLayoutTransform(FVector2f(0.f, H - 1.f))), Box, ESlateDrawEffect::None, FLinearColor(1.f, 1.f, 1.f, 0.08f));

	// Line bounds: a green line at the start (0) and a red one at the end, when in view.
	if (View.Contains(0.f))
	{
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(FVector2f(2.f, H), FSlateLayoutTransform(FVector2f(View.TimeToX(0.f), 0.f))), Box, ESlateDrawEffect::None, FLinearColor(0.20f, 0.85f, 0.25f, 0.9f));
	}
	if (View.Contains(Dur))
	{
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(FVector2f(2.f, H), FSlateLayoutTransform(FVector2f(View.TimeToX(Dur) - 2.f, 0.f))), Box, ESlateDrawEffect::None, FLinearColor(0.85f, 0.20f, 0.20f, 0.9f));
	}

	// Subtle line(s) at the selected event's time (start, plus end for a state).
	const FLinearColor SelLineColor(1.f, 1.f, 1.f, 0.4f);
	for (const float SelTime : { SelectionStart.Get(-1.f), SelectionEnd.Get(-1.f) })
	{
		if (View.Contains(SelTime))
		{
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(FVector2f(2.f, H), FSlateLayoutTransform(FVector2f(View.TimeToX(SelTime), 0.f))), Box, ESlateDrawEffect::None, SelLineColor);
		}
	}

	// Playhead: a bright vertical line across the row, on top of everything else.
	const float PlayheadSec = Playhead.Get(-1.f);
	if (View.Contains(PlayheadSec))
	{
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 5, AllottedGeometry.ToPaintGeometry(FVector2f(1.f, H), FSlateLayoutTransform(FVector2f(View.TimeToX(PlayheadSec), 0.f))), Box, ESlateDrawEffect::None, FLinearColor(1.f, 1.f, 1.f, 0.9f));
	}

	// Events only on a real track; the group row stops here (decoration only).
	UKzDialogueTimeline* T = Timeline.Get();
	if (!T || !T->Tracks.IsValidIndex(TrackIndex)) { return LayerId + 6; }
	const FKzDialogueNotifyTrack& Track = T->Tracks[TrackIndex];
	const int32 Sel = SelectedEventAttr.Get(INDEX_NONE);
	const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Regular", 8);
	const TSharedRef<FSlateFontMeasure> FontMeasure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();

	if (Track.Events.Num() == 0)
	{
		PaintHint(AllottedGeometry, OutDrawElements, LayerId + 1, LOCTEXT("EmptyTrackHint", "Right-click to add a notify"));
	}

	for (int32 e = 0; e < Track.Events.Num(); ++e)
	{
		const FKzDialogueNotifyEvent& Event = Track.Events[e];
		float StartSec, EndSec; bool bState;
		const bool bPastEnd = GetDrawnBounds(Event, Dur, StartSec, EndSec, bState);

		const float L = View.TimeToX(StartSec);
		const float R = View.TimeToX(EndSec);
		const bool bSelected = (e == Sel);
		// No notify, a start past the line end or a missing audio marker never fire as authored: red.
		const bool bBroken = !Event.Notify || bPastEnd || IsMarkerMissing(Event);
		FLinearColor Color = bSelected ? FLinearColor(0.98f, 0.86f, 0.36f) : (bBroken ? FLinearColor(0.85f, 0.20f, 0.20f) : Event.Notify->GetEditorColor());
		FLinearColor TextColor = Color.GetLuminance() > 0.5f ? FLinearColor::Black : FLinearColor::White;
		if (!Event.bEnabled)
		{
			Color.A *= 0.35f;
			TextColor.A *= 0.5f;
		}
		const FText Label = Event.Notify ? Event.Notify->GetNotifyName() : LOCTEXT("Empty", "(empty)");
		const FVector2D LabelExtent = FontMeasure->Measure(Label.ToString(), Font);

		// Anchored to an audio marker: a dot at the foot of its time, so it reads as not draggable.
		if (!IsRetimable(Event))
		{
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 3, AllottedGeometry.ToPaintGeometry(FVector2f(6.f, 6.f), FSlateLayoutTransform(FVector2f(L - 3.f, H - 7.f))), Box, ESlateDrawEffect::None, FLinearColor(1.f, 0.55f, 0.10f));
		}

		if (bState)
		{
			// Ranged notify: a solid bar spanning [start, end].
			const float BarWidth = FMath::Max(R - L, 3.f);
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1, AllottedGeometry.ToPaintGeometry(FVector2f(BarWidth, H - 4.f), FSlateLayoutTransform(FVector2f(L, 2.f))), Box, ESlateDrawEffect::None, Color);
			FSlateDrawElement::MakeText(OutDrawElements, LayerId + 2, AllottedGeometry.ToPaintGeometry(FVector2f(FMath::Max(BarWidth - 6.f, 8.f), static_cast<float>(LabelExtent.Y)), FSlateLayoutTransform(FVector2f(L + 4.f, 3.f))), Label, Font, ESlateDrawEffect::None, TextColor);
		}
		else
		{
			// Point notify, drawn like an anim notify: a labelled box at the top with a thin
			// handle line dropping to the bottom of the track to mark the exact time.
			const float BoxW = static_cast<float>(LabelExtent.X) + 8.f;
			const float BoxH = static_cast<float>(LabelExtent.Y) + 4.f;
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1, AllottedGeometry.ToPaintGeometry(FVector2f(2.f, H), FSlateLayoutTransform(FVector2f(L - 1.f, 0.f))), Box, ESlateDrawEffect::None, Color);
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 2, AllottedGeometry.ToPaintGeometry(FVector2f(BoxW, BoxH), FSlateLayoutTransform(FVector2f(L, 1.f))), Box, ESlateDrawEffect::None, Color);
			FSlateDrawElement::MakeText(OutDrawElements, LayerId + 3, AllottedGeometry.ToPaintGeometry(FVector2f(static_cast<float>(LabelExtent.X), static_cast<float>(LabelExtent.Y)), FSlateLayoutTransform(FVector2f(L + 4.f, 3.f))), Label, Font, ESlateDrawEffect::None, TextColor);
		}
	}

	// The dragged marker's time, next to the cursor.
	if (bDragStarted && Track.Events.IsValidIndex(DragIndex) && IsRetimable(Track.Events[DragIndex]))
	{
		float StartSec, EndSec; bool bState;
		GetDrawnBounds(Track.Events[DragIndex], Dur, StartSec, EndSec, bState);
		const FText TimeText = FText::FromString(bState ? FString::Printf(TEXT("%.2f - %.2f s"), StartSec, EndSec) : FString::Printf(TEXT("%.2f s"), StartSec));
		const FVector2D Extent = FontMeasure->Measure(TimeText.ToString(), Font);
		const FVector2f Pos(FMath::Min(DragCursorX + 12.f, W - static_cast<float>(Extent.X) - 4.f), H - static_cast<float>(Extent.Y) - 2.f);
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 4, AllottedGeometry.ToPaintGeometry(FVector2f(static_cast<float>(Extent.X) + 4.f, static_cast<float>(Extent.Y)), FSlateLayoutTransform(Pos - FVector2f(2.f, 0.f))), Box, ESlateDrawEffect::None, FLinearColor(0.f, 0.f, 0.f, 0.7f));
		FSlateDrawElement::MakeText(OutDrawElements, LayerId + 5, AllottedGeometry.ToPaintGeometry(FVector2f(static_cast<float>(Extent.X), static_cast<float>(Extent.Y)), FSlateLayoutTransform(Pos)), TimeText, Font, ESlateDrawEffect::None, FLinearColor::White);
	}
	return LayerId + 6;
}

FReply SKzTimelineTrack::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	const float W = MyGeometry.GetLocalSize().X;
	const float LocalX = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()).X;

	// Middle button pans from any row, the waveform and Notifies bands included.
	if (MouseEvent.GetEffectingButton() == EKeys::MiddleMouseButton)
	{
		bPanning = true;
		PanLastX = LocalX;
		return FReply::Handled().CaptureMouse(SharedThis(this));
	}

	UKzDialogueTimeline* T = Timeline.Get();
	if (!T || !T->Tracks.IsValidIndex(TrackIndex)) { return FReply::Unhandled(); }

	const float Dur = FMath::Max(Duration.Get(1.f), UE_KINDA_SMALL_NUMBER);

	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		EDragMode Mode;
		const int32 Hit = HitTest(LocalX, W, Dur, Mode);
		OnSelect.ExecuteIfBound(Hit);
		Invalidate(EInvalidateWidgetReason::Paint);

		if (Hit != INDEX_NONE && T->Tracks[TrackIndex].Events.IsValidIndex(Hit))
		{
			float StartSec, EndSec; bool bState;
			GetDrawnBounds(T->Tracks[TrackIndex].Events[Hit], Dur, StartSec, EndSec, bState);
			const float CursorSec = MakeView(W).XToTime(LocalX);

			// Anchor-based events cannot be retimed or resized; the drag stays alive only
			// for the vertical move-to-track gesture.
			if (!IsRetimable(T->Tracks[TrackIndex].Events[Hit])) { Mode = EDragMode::Move; }

			DragIndex = Hit;
			DragMode = Mode;
			DragGrabOffsetSec = CursorSec - StartSec;
			DragOrigLenSec = EndSec - StartSec;
			DragStartLocal = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
			bDragStarted = false;
		}
		return FReply::Handled().CaptureMouse(SharedThis(this));
	}

	if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FReply SKzTimelineTrack::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	const float W = MyGeometry.GetLocalSize().X;
	const FVector2f Local = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	const float LocalX = Local.X;
	HoverX = LocalX;
	HoverWidth = W;

	if (bPanning && MouseEvent.IsMouseButtonDown(EKeys::MiddleMouseButton))
	{
		const float DeltaX = LocalX - PanLastX;
		PanLastX = LocalX;
		if (W > 0.f) { OnPan.ExecuteIfBound(-(DeltaX / W) * MakeView(W).Span); }
		return FReply::Handled();
	}

	if (DragIndex == INDEX_NONE || !MouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton)) { return FReply::Unhandled(); }
	UKzDialogueTimeline* T = Timeline.Get();
	if (!T || !T->Tracks.IsValidIndex(TrackIndex) || !T->Tracks[TrackIndex].Events.IsValidIndex(DragIndex)) { return FReply::Unhandled(); }

	const float Dur = FMath::Max(Duration.Get(1.f), UE_KINDA_SMALL_NUMBER);

	// Anchor-based events keep their time; the drag only serves move-to-track on release.
	const bool bRetimable = IsRetimable(T->Tracks[TrackIndex].Events[DragIndex]);

	// Measured in both axes, so a purely vertical drag (move to another track) starts too.
	if (!bDragStarted)
	{
		if (FVector2f::Distance(Local, DragStartLocal) < DragThreshold) { return FReply::Handled(); }
		if (bRetimable) { OnBeginRetime.ExecuteIfBound(); }
		bDragStarted = true;
	}

	const float H = MyGeometry.GetLocalSize().Y;
	if (H > 0.f) { OnDragRow.ExecuteIfBound(FMath::FloorToInt(Local.Y / H)); }
	DragCursorX = LocalX;

	if (!bRetimable) { return FReply::Handled(); }

	const FKzTimelineView View = MakeView(W);
	const float CursorSec = View.XToTime(LocalX);

	float CurStart, CurEnd; bool bState;
	GetDrawnBounds(T->Tracks[TrackIndex].Events[DragIndex], Dur, CurStart, CurEnd, bState);

	float NewStart, NewEnd;
	switch (DragMode)
	{
	case EDragMode::ResizeStart:
		NewStart = CursorSec;
		NewEnd = CurEnd;
		break;
	case EDragMode::ResizeEnd:
		NewStart = CurStart;
		NewEnd = CursorSec;
		break;
	default:
		NewStart = CursorSec - DragGrabOffsetSec;
		NewEnd = NewStart + DragOrigLenSec;
		break;
	}

	// Hold Shift to snap the dragged edge (or the whole marker) to the grid.
	if (MouseEvent.IsShiftDown())
	{
		if (DragMode == EDragMode::ResizeStart) { NewStart = View.Snap(NewStart); }
		else if (DragMode == EDragMode::ResizeEnd) { NewEnd = View.Snap(NewEnd); }
		else
		{
			const float Snapped = View.Snap(NewStart);
			NewEnd += Snapped - NewStart;
			NewStart = Snapped;
		}
	}

	OnRetime.ExecuteIfBound(DragIndex, NewStart, NewEnd);
	Invalidate(EInvalidateWidgetReason::Paint);
	return FReply::Handled();
}

FReply SKzTimelineTrack::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::MiddleMouseButton)
	{
		const bool bHadCapture = HasMouseCapture();
		bPanning = false;
		return bHadCapture ? FReply::Handled().ReleaseMouseCapture() : FReply::Handled();
	}

	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		const bool bHadCapture = HasMouseCapture();
		const bool bWasRetiming = (DragIndex != INDEX_NONE && bDragStarted);
		const int32 DraggedEvent = DragIndex;

		// Released over another row? Move the dragged notify there; the host maps rows to visible tracks.
		int32 RowDelta = 0;
		if (bWasRetiming)
		{
			const float H = MyGeometry.GetLocalSize().Y;
			const float LocalY = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()).Y;
			if (H > 0.f) { RowDelta = FMath::FloorToInt(LocalY / H); }
		}

		DragIndex = INDEX_NONE;
		DragMode = EDragMode::None;

		const FReply Reply = bHadCapture ? FReply::Handled().ReleaseMouseCapture() : FReply::Handled();
		if (bWasRetiming)
		{
			OnEndRetime.ExecuteIfBound();
			if (RowDelta != 0) { OnMoveToTrack.ExecuteIfBound(DraggedEvent, RowDelta); }
		}
		else
		{
			OnInteractionEnd.ExecuteIfBound();
		}
		return Reply;
	}

	if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		const float W = MyGeometry.GetLocalSize().X;
		const float Dur = FMath::Max(Duration.Get(1.f), UE_KINDA_SMALL_NUMBER);
		const float LocalX = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()).X;
		EDragMode Mode;
		const int32 Hit = HitTest(LocalX, W, Dur, Mode);

		TSharedRef<SWidget> MenuContent = SNullWidget::NullWidget;
		if (Hit != INDEX_NONE && OnBuildEventMenu.IsBound())
		{
			OnSelect.ExecuteIfBound(Hit);
			Invalidate(EInvalidateWidgetReason::Paint);
			MenuContent = OnBuildEventMenu.Execute(Hit);
		}
		else if (OnBuildAddMenu.IsBound())
		{
			MenuContent = OnBuildAddMenu.Execute(MakeView(W).XToTime(LocalX));
		}

		// Nothing to add/edit here (e.g. the decorative group row): no menu.
		if (MenuContent == SNullWidget::NullWidget) { return FReply::Unhandled(); }

		FSlateApplication::Get().PushMenu(SharedThis(this), FWidgetPath(), MenuContent, MouseEvent.GetScreenSpacePosition(), FPopupTransitionEffect(FPopupTransitionEffect::ContextMenu));
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

void SKzTimelineTrack::OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent)
{
	// A drag cut short (window switch, another button) must still close its retime transaction.
	const bool bWasRetiming = DragIndex != INDEX_NONE && bDragStarted;
	bPanning = false;
	DragIndex = INDEX_NONE;
	DragMode = EDragMode::None;
	bDragStarted = false;
	if (bWasRetiming) { OnEndRetime.ExecuteIfBound(); }
}

FReply SKzTimelineTrack::OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton) { return FReply::Unhandled(); }

	const float W = MyGeometry.GetLocalSize().X;
	const float Dur = FMath::Max(Duration.Get(1.f), UE_KINDA_SMALL_NUMBER);
	const float LocalX = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()).X;
	EDragMode Mode;
	const int32 Hit = HitTest(LocalX, W, Dur, Mode);
	if (Hit == INDEX_NONE) { return FReply::Unhandled(); }

	OnSelect.ExecuteIfBound(Hit);
	Invalidate(EInvalidateWidgetReason::Paint);
	OnActivateEvent.ExecuteIfBound(Hit);
	return FReply::Handled();
}

FReply SKzTimelineTrack::OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!OnZoom.IsBound()) { return FReply::Unhandled(); }
	const float W = MyGeometry.GetLocalSize().X;
	const float LocalX = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()).X;
	OnZoom.Execute(MakeView(W).XToTime(LocalX), MouseEvent.GetWheelDelta(), MouseEvent.IsShiftDown());
	return FReply::Handled();
}

FCursorReply SKzTimelineTrack::OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const
{
	const float W = MyGeometry.GetLocalSize().X;
	const float Dur = FMath::Max(Duration.Get(1.f), UE_KINDA_SMALL_NUMBER);
	const float LocalX = MyGeometry.AbsoluteToLocal(CursorEvent.GetScreenSpacePosition()).X;
	EDragMode Mode;
	const int32 Hit = HitTest(LocalX, W, Dur, Mode);
	if (Hit != INDEX_NONE)
	{
		// Anchor-based events read as non-draggable: their time comes from the audio marker.
		UKzDialogueTimeline* T = Timeline.Get();
		if (T && T->Tracks.IsValidIndex(TrackIndex) && T->Tracks[TrackIndex].Events.IsValidIndex(Hit) && !IsRetimable(T->Tracks[TrackIndex].Events[Hit]))
		{
			return FCursorReply::Cursor(EMouseCursor::Default);
		}

		if (Mode == EDragMode::ResizeStart || Mode == EDragMode::ResizeEnd) { return FCursorReply::Cursor(EMouseCursor::ResizeLeftRight); }
		return FCursorReply::Cursor(EMouseCursor::CardinalCross);
	}
	return FCursorReply::Unhandled();
}

void SKzTimelineTrack::GetEventBounds(const FKzDialogueNotifyEvent& Event, float Dur, float& OutStartSec, float& OutEndSec, bool& bOutState) const
{
	const FKzDialogueTimeSource_Relative* Rel = Event.TimeSource.GetPtr<FKzDialogueTimeSource_Relative>();
	bOutState = Event.Notify && Event.Notify->IsA<UKzDialogueNotifyState>();
	if (Rel)
	{
		OutStartSec = Rel->bNormalized ? Rel->Time * Dur : Rel->Time;
		const float Len = Rel->bNormalized ? Rel->Duration * Dur : Rel->Duration;
		OutEndSec = OutStartSec + (bOutState ? Len : 0.f);
	}
	else if (const FKzDialogueTimeSource* Source = Event.TimeSource.GetPtr<FKzDialogueTimeSource>())
	{
		// Anchor-style sources (audio markers) resolve generically against the line's audio,
		// so the widget shows the same times the runtime will bake.
		FKzDialogueTimeResolveContext Context;
		Context.LineDuration = Dur;
		Context.Audio = LineAudio.Get(nullptr);
		Source->Resolve(Context, OutStartSec, OutEndSec);
		if (!bOutState) { OutEndSec = OutStartSec; }
	}
	else
	{
		OutStartSec = 0.f;
		OutEndSec = 0.f;
	}
}

bool SKzTimelineTrack::GetDrawnBounds(const FKzDialogueNotifyEvent& Event, float Dur, float& OutStartSec, float& OutEndSec, bool& bOutState) const
{
	GetEventBounds(Event, Dur, OutStartSec, OutEndSec, bOutState);
	const bool bPastEnd = OutStartSec > Dur + UE_KINDA_SMALL_NUMBER;
	OutStartSec = FMath::Min(OutStartSec, Dur);
	OutEndSec = FMath::Clamp(OutEndSec, OutStartSec, Dur);
	return bPastEnd;
}

bool SKzTimelineTrack::IsRetimable(const FKzDialogueNotifyEvent& Event)
{
	return Event.TimeSource.GetPtr<FKzDialogueTimeSource_Relative>() != nullptr;
}

bool SKzTimelineTrack::IsMarkerMissing(const FKzDialogueNotifyEvent& Event) const
{
	const FKzDialogueTimeSource_AudioMarker* Marker = Event.TimeSource.GetPtr<FKzDialogueTimeSource_AudioMarker>();
	float Start = 0.f;
	float End = 0.f;
	return Marker && !Marker->FindMarker(LineAudio.Get(nullptr), Start, End);
}

void SKzTimelineTrack::PaintHint(const FGeometry& Geometry, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FText& Hint)
{
	const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Italic", 8);
	const float Y = FMath::Max(0.f, (Geometry.GetLocalSize().Y - 12.f) * 0.5f);
	FSlateDrawElement::MakeText(OutDrawElements, LayerId, Geometry.ToPaintGeometry(FVector2f(Geometry.GetLocalSize().X, 12.f), FSlateLayoutTransform(FVector2f(6.f, Y))), Hint, Font, ESlateDrawEffect::None, FLinearColor(1.f, 1.f, 1.f, 0.35f));
}

FText SKzTimelineTrack::GetHoverToolTip() const
{
	UKzDialogueTimeline* T = Timeline.Get();
	const float Dur = FMath::Max(Duration.Get(1.f), UE_KINDA_SMALL_NUMBER);
	EDragMode Mode;
	const int32 Hit = (T && T->Tracks.IsValidIndex(TrackIndex)) ? HitTest(HoverX, HoverWidth, Dur, Mode) : INDEX_NONE;
	if (Hit == INDEX_NONE)
	{
		return LOCTEXT("TrackGestures", "Right-click: add a notify here, or open a marker's menu.\nDrag a marker to retime it (a state's edges resize it); hold Shift to snap to the grid.\nDrag it onto another row to move it to that track; Esc cancels the drag.\nDouble-click a Blueprint notify to open it.\nWheel zooms; Shift+wheel or middle-drag pans.\nCtrl+C copies, Ctrl+V pastes at the playhead, Ctrl+D duplicates, Delete removes.");
	}

	const FKzDialogueNotifyEvent& Event = T->Tracks[TrackIndex].Events[Hit];
	float StartSec, EndSec; bool bState;
	GetEventBounds(Event, Dur, StartSec, EndSec, bState);

	const FText Name = Event.Notify ? FText::Format(INVTEXT("{0} ({1})"), Event.Notify->GetNotifyName(), FText::FromString(Event.Notify->GetClass()->GetName())) : LOCTEXT("NoNotifyTip", "No notify assigned: it does nothing.");
	const FText Time = FText::FromString(bState ? FString::Printf(TEXT("%.2f - %.2f s"), StartSec, EndSec) : FString::Printf(TEXT("%.2f s"), StartSec));

	FText Source = LOCTEXT("NoSourceTip", "no time source");
	if (const FKzDialogueTimeSource_Relative* Rel = Event.TimeSource.GetPtr<FKzDialogueTimeSource_Relative>())
	{
		Source = Rel->bNormalized ? LOCTEXT("RelativeNormalizedTip", "relative, scaled to the line") : LOCTEXT("RelativeTip", "relative");
	}
	else if (const FKzDialogueTimeSource_AudioMarker* Marker = Event.TimeSource.GetPtr<FKzDialogueTimeSource_AudioMarker>())
	{
		Source = IsMarkerMissing(Event)
			? FText::Format(LOCTEXT("MarkerMissingTip", "audio marker '{0}', not in the line's audio: fires at its fallback time"), FText::FromName(Marker->MarkerName))
			: FText::Format(LOCTEXT("MarkerTip", "audio marker '{0}'"), FText::FromName(Marker->MarkerName));
	}
	else if (const UScriptStruct* SourceStruct = Event.TimeSource.GetScriptStruct())
	{
		Source = SourceStruct->GetDisplayNameText();
	}

	FText Tip = FText::Format(INVTEXT("{0}\n{1}, {2}"), Name, Time, Source);
	if (StartSec > Dur + UE_KINDA_SMALL_NUMBER)
	{
		Tip = FText::Format(LOCTEXT("PastEndTip", "{0}\nStarts past the line end ({1} s): it never fires."), Tip, FText::AsNumber(Dur));
	}
	if (!Event.bEnabled)
	{
		Tip = FText::Format(LOCTEXT("DisabledTip", "{0}\nDisabled."), Tip);
	}
	return Tip;
}

float SKzTimelineTrack::PointBoxWidth(const FKzDialogueNotifyEvent& Event) const
{
	const FText Label = Event.Notify ? Event.Notify->GetNotifyName() : LOCTEXT("Empty", "(empty)");
	const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Regular", 8);
	const double TextWidth = FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Label.ToString(), Font).X;
	return static_cast<float>(TextWidth) + 8.f;
}

int32 SKzTimelineTrack::HitTest(float LocalX, float W, float Dur, EDragMode& OutMode) const
{
	OutMode = EDragMode::None;
	UKzDialogueTimeline* T = Timeline.Get();
	if (!T || !T->Tracks.IsValidIndex(TrackIndex)) { return INDEX_NONE; }
	const FKzDialogueNotifyTrack& Track = T->Tracks[TrackIndex];
	const FKzTimelineView View = MakeView(W);
	for (int32 e = Track.Events.Num() - 1; e >= 0; --e)
	{
		float StartSec, EndSec; bool bState;
		GetDrawnBounds(Track.Events[e], Dur, StartSec, EndSec, bState);
		const float L = View.TimeToX(StartSec);
		const float R = View.TimeToX(EndSec);
		if (bState)
		{
			if (LocalX >= L - 2.f && LocalX <= R + 2.f)
			{
				if (LocalX <= L + EdgeGrab) { OutMode = EDragMode::ResizeStart; }
				else if (LocalX >= R - EdgeGrab) { OutMode = EDragMode::ResizeEnd; }
				else { OutMode = EDragMode::Move; }
				return e;
			}
		}
		else
		{
			// Point: the labelled box extends right from the time; grab anywhere on it.
			const float BoxW = PointBoxWidth(Track.Events[e]);
			if (LocalX >= L - 3.f && LocalX <= L + BoxW + 2.f)
			{
				OutMode = EDragMode::Move;
				return e;
			}
		}
	}
	return INDEX_NONE;
}

// ---------------------------------------------------------------------------------------
// SKzTimelineRuler
// ---------------------------------------------------------------------------------------

void SKzTimelineRuler::Construct(const FArguments& InArgs)
{
	ViewStart = InArgs._ViewStart;
	ViewEnd = InArgs._ViewEnd;
	Duration = InArgs._Duration;
	SelectionStart = InArgs._SelectionStart;
	SelectionEnd = InArgs._SelectionEnd;
	OnZoom = InArgs._OnZoom;
	OnPan = InArgs._OnPan;
	OnScrub = InArgs._OnScrub;
	Playhead = InArgs._Playhead;
	SetClipping(EWidgetClipping::ClipToBounds);
}

int32 SKzTimelineRuler::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const float W = AllottedGeometry.GetLocalSize().X;
	const float H = AllottedGeometry.GetLocalSize().Y;
	if (W <= 1.f) { return LayerId; }

	const FKzTimelineView View = MakeView(W);
	const FSlateBrush* Box = FAppStyle::GetBrush("WhiteBrush");
	const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Regular", 8);
	const TSharedRef<FSlateFontMeasure> FontMeasure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	const FLinearColor TickColor(1.f, 1.f, 1.f, 0.35f);
	const FLinearColor LabelColor(1.f, 1.f, 1.f, 0.6f);

	View.ForEachTick([&](float Seconds, bool bMajor)
	{
		const float X = View.TimeToX(Seconds);
		const float TickH = bMajor ? 7.f : 4.f;
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1, AllottedGeometry.ToPaintGeometry(FVector2f(1.f, TickH), FSlateLayoutTransform(FVector2f(X, H - TickH))), Box, ESlateDrawEffect::None, TickColor);
		if (bMajor)
		{
			const FString TickLabel = FString::Printf(TEXT("%g"), Seconds);
			const FVector2D TextSize = FontMeasure->Measure(TickLabel, Font);
			FSlateDrawElement::MakeText(OutDrawElements, LayerId + 2, AllottedGeometry.ToPaintGeometry(FVector2f(static_cast<float>(TextSize.X), static_cast<float>(TextSize.Y)), FSlateLayoutTransform(FVector2f(X + 2.f, 1.f))), FText::FromString(TickLabel), Font, ESlateDrawEffect::None, LabelColor);
		}
	});

	// Start (green) / end (red) bounds and the selected event's line(s), so they reach the
	// numbers and connect with the track lines below.
	auto DrawMarkLine = [&](float TimeSeconds, float LineWidth, const FLinearColor& Color, bool bAnchorRight)
	{
		if (!View.Contains(TimeSeconds)) { return; }
		const float X = View.TimeToX(TimeSeconds) - (bAnchorRight ? LineWidth : 0.f);
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 3, AllottedGeometry.ToPaintGeometry(FVector2f(LineWidth, H), FSlateLayoutTransform(FVector2f(X, 0.f))), Box, ESlateDrawEffect::None, Color);
	};
	DrawMarkLine(0.f, 2.f, FLinearColor(0.20f, 0.85f, 0.25f, 0.9f), false);
	DrawMarkLine(Duration.Get(1.f), 2.f, FLinearColor(0.85f, 0.20f, 0.20f, 0.9f), true);
	DrawMarkLine(SelectionStart.Get(-1.f), 2.f, FLinearColor(1.f, 1.f, 1.f, 0.4f), false);
	DrawMarkLine(SelectionEnd.Get(-1.f), 2.f, FLinearColor(1.f, 1.f, 1.f, 0.4f), false);

	// Playhead: a white line plus a small handle at the top, so the ruler reads as a scrubber.
	const float PlayheadSec = Playhead.Get(-1.f);
	if (View.Contains(PlayheadSec))
	{
		const float PX = View.TimeToX(PlayheadSec);
		// Native scrub handle (mirrors the anim timeline): a 13px box brush draws the head + line.
		const FSlateBrush* ScrubHandle = FAppStyle::GetBrush("Sequencer.Timeline.VanillaScrubHandleDown");
		// Center the 13px handle on the playhead line (its 1px center column sits at +6.5).
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 3, AllottedGeometry.ToPaintGeometry(FVector2f(13.f, H), FSlateLayoutTransform(FVector2f(PX - 6.f, 0.f))), ScrubHandle, ESlateDrawEffect::None, FLinearColor(1.f, 0.2f, 0.1f, 0.75f));
	}

	return LayerId + 4;
}

FReply SKzTimelineRuler::OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!OnZoom.IsBound()) { return FReply::Unhandled(); }
	const float LocalX = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()).X;
	OnZoom.Execute(MakeView(MyGeometry.GetLocalSize().X).XToTime(LocalX), MouseEvent.GetWheelDelta(), MouseEvent.IsShiftDown());
	return FReply::Handled();
}

FReply SKzTimelineRuler::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	const float W = MyGeometry.GetLocalSize().X;
	const float LocalX = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()).X;

	// Left button scrubs the playhead; middle button pans the view.
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		bScrubbing = true;
		OnScrub.ExecuteIfBound(MakeView(W).XToTime(LocalX));
		return FReply::Handled().CaptureMouse(SharedThis(this));
	}
	if (MouseEvent.GetEffectingButton() == EKeys::MiddleMouseButton)
	{
		bPanning = true;
		PanLastX = LocalX;
		return FReply::Handled().CaptureMouse(SharedThis(this));
	}
	return FReply::Unhandled();
}

FReply SKzTimelineRuler::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	const float W = MyGeometry.GetLocalSize().X;
	const float LocalX = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()).X;

	if (bScrubbing && MouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton))
	{
		OnScrub.ExecuteIfBound(MakeView(W).XToTime(LocalX));
		return FReply::Handled();
	}
	if (bPanning && MouseEvent.IsMouseButtonDown(EKeys::MiddleMouseButton))
	{
		const float DeltaX = LocalX - PanLastX;
		PanLastX = LocalX;
		if (W > 0.f) { OnPan.ExecuteIfBound(-(DeltaX / W) * MakeView(W).Span); }
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FReply SKzTimelineRuler::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (bScrubbing && MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		bScrubbing = false;
		return FReply::Handled().ReleaseMouseCapture();
	}
	if (bPanning && MouseEvent.GetEffectingButton() == EKeys::MiddleMouseButton)
	{
		bPanning = false;
		return FReply::Handled().ReleaseMouseCapture();
	}
	return FReply::Unhandled();
}

void SKzTimelineRuler::OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent)
{
	bPanning = false;
	bScrubbing = false;
}

#undef LOCTEXT_NAMESPACE