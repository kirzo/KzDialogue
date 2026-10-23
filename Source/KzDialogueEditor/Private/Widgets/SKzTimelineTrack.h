// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"

class UKzDialogueTimeline;
class USoundBase;
struct FKzDialogueNotifyEvent;
struct FKzWaveformPreview;

/** The visible time window mapped onto a widget's width, shared by the tracks and the ruler. */
struct FKzTimelineView
{
	float Start;
	float Span;
	float Width;

	/** Grid spacing in seconds for a zoom level, on SSimpleTimeSlider's series (.001, .005, .01, .05, .1, .5, 1, ...) so the track grid falls on the ruler ticks. */
	static float TickSpacing(float PixelsPerSecond);

	FKzTimelineView(float InStart, float InEnd, float InWidth) : Start(InStart), Span(FMath::Max(InEnd - InStart, 0.0001f)), Width(InWidth) {}

	float End() const { return Start + Span; }
	bool Contains(float Seconds) const { return Seconds >= Start && Seconds <= End(); }
	float TimeToX(float Seconds) const { return (Seconds - Start) / Span * Width; }
	float XToTime(float X) const { return Start + X / FMath::Max(Width, 1.f) * Span; }
	float SecondsPerPixel() const { return Span / FMath::Max(Width, 1.f); }
	float Snap(float Seconds) const { const float Spacing = TickSpacing(Width / Span); return Spacing > 0.f ? FMath::RoundToFloat(Seconds / Spacing) * Spacing : Seconds; }

	/** Calls Func(Seconds, bMajor) for every grid tick in view; every tenth is major. */
	template <typename FuncType>
	void ForEachTick(FuncType&& Func) const
	{
		const float Spacing = TickSpacing(Width / Span);
		for (int32 Tick = FMath::FloorToInt(Start / Spacing); Spacing > 0.f; ++Tick)
		{
			const float Seconds = static_cast<float>(Tick) * Spacing;
			if (Seconds > End()) { break; }
			if (Seconds >= Start) { Func(Seconds, Tick % 10 == 0); }
		}
	}
};

/**
 * One lane's interactive notify track, mirroring the AnimMontage notify track. Paints point/state
 * markers along [0, duration], hit-tests them, and supports drag-to-retime (drag a state's edges to
 * resize), left-click to select, right-click on empty space to add a notify, and right-click on a
 * marker for its menu. INDEX_NONE as TrackIndex gives the decorative bands (waveform, group row).
 */
class SKzTimelineTrack : public SLeafWidget
{
public:
	DECLARE_DELEGATE_OneParam(FOnSelectEvent, int32 /*EventIndex*/);
	DECLARE_DELEGATE_ThreeParams(FOnRetimeEvent, int32 /*EventIndex*/, float /*StartSeconds*/, float /*EndSeconds*/);
	DECLARE_DELEGATE_RetVal_OneParam(TSharedRef<SWidget>, FOnBuildAddMenu, float /*TimeSeconds*/);
	DECLARE_DELEGATE_RetVal_OneParam(TSharedRef<SWidget>, FOnBuildEventMenu, int32 /*EventIndex*/);
	DECLARE_DELEGATE_ThreeParams(FOnZoom, float /*CursorSeconds*/, float /*WheelDelta*/, bool /*bPan*/);
	DECLARE_DELEGATE_OneParam(FOnPan, float /*DeltaSeconds*/);
	DECLARE_DELEGATE_TwoParams(FOnMoveToTrack, int32 /*EventIndex*/, int32 /*RowDelta*/);
	DECLARE_DELEGATE_OneParam(FOnDragRow, int32 /*RowDelta*/);
	DECLARE_DELEGATE_OneParam(FOnActivateEvent, int32 /*EventIndex*/);

	SLATE_BEGIN_ARGS(SKzTimelineTrack) : _Duration(1.f), _AudioTimeOffset(0.f), _LineAudio(nullptr), _ViewStart(0.f), _ViewEnd(1.f), _SelectionStart(-1.f), _SelectionEnd(-1.f), _SelectedEvent(INDEX_NONE), _Playhead(-1.f), _IsDropTarget(false) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UKzDialogueTimeline>, Timeline)
		SLATE_ARGUMENT(int32, TrackIndex)
		SLATE_ATTRIBUTE(TSharedPtr<FKzWaveformPreview>, Waveform)
		SLATE_ATTRIBUTE(float, Duration)
		SLATE_ATTRIBUTE(float, AudioTimeOffset)
		SLATE_ATTRIBUTE(USoundBase*, LineAudio)
		SLATE_ATTRIBUTE(float, ViewStart)
		SLATE_ATTRIBUTE(float, ViewEnd)
		SLATE_ATTRIBUTE(float, SelectionStart)
		SLATE_ATTRIBUTE(float, SelectionEnd)
		SLATE_ATTRIBUTE(int32, SelectedEvent)
		SLATE_ATTRIBUTE(float, Playhead)
		SLATE_ATTRIBUTE(bool, IsDropTarget)
		SLATE_EVENT(FOnSelectEvent, OnSelect)
		SLATE_EVENT(FSimpleDelegate, OnBeginRetime)
		SLATE_EVENT(FOnRetimeEvent, OnRetime)
		SLATE_EVENT(FSimpleDelegate, OnEndRetime)
		SLATE_EVENT(FSimpleDelegate, OnInteractionEnd)
		SLATE_EVENT(FOnBuildAddMenu, OnBuildAddMenu)
		SLATE_EVENT(FOnBuildEventMenu, OnBuildEventMenu)
		SLATE_EVENT(FOnZoom, OnZoom)
		SLATE_EVENT(FOnPan, OnPan)
		SLATE_EVENT(FOnMoveToTrack, OnMoveToTrack)
		SLATE_EVENT(FOnDragRow, OnDragRow)
		SLATE_EVENT(FOnActivateEvent, OnActivateEvent)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(100.f, 28.f); }

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override;
	virtual FReply OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FCursorReply OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const override;

private:
	enum class EDragMode { None, Move, ResizeStart, ResizeEnd };

	FKzTimelineView MakeView(float W) const { return FKzTimelineView(ViewStart.Get(0.f), ViewEnd.Get(1.f), W); }

	void GetEventBounds(const FKzDialogueNotifyEvent& Event, float Dur, float& OutStartSec, float& OutEndSec, bool& bOutState) const;

	/** Bounds as drawn and hit: a start past the line end is pinned to the end so the marker stays visible. True when pinned. */
	bool GetDrawnBounds(const FKzDialogueNotifyEvent& Event, float Dur, float& OutStartSec, float& OutEndSec, bool& bOutState) const;

	/** Only Relative sources are draggable; anchor-based ones take their time from the audio. */
	static bool IsRetimable(const FKzDialogueNotifyEvent& Event);

	/** An audio-marker source whose marker the line's audio lacks: it fires at its fallback time. */
	bool IsMarkerMissing(const FKzDialogueNotifyEvent& Event) const;

	/** Dim text at the lane's left edge, for states the lane cannot otherwise show. */
	static void PaintHint(const FGeometry& Geometry, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FText& Hint);

	FText GetHoverToolTip() const;
	float PointBoxWidth(const FKzDialogueNotifyEvent& Event) const;
	int32 HitTest(float LocalX, float W, float Dur, EDragMode& OutMode) const;

	static constexpr float EdgeGrab = 6.f;
	static constexpr float DragThreshold = 4.f;

	TWeakObjectPtr<UKzDialogueTimeline> Timeline;
	int32 TrackIndex = INDEX_NONE;
	TAttribute<TSharedPtr<FKzWaveformPreview>> Waveform;
	TAttribute<float> Duration;
	TAttribute<float> AudioTimeOffset;
	TAttribute<USoundBase*> LineAudio;
	TAttribute<int32> SelectedEventAttr;
	TAttribute<float> Playhead;
	TAttribute<float> SelectionStart;
	TAttribute<float> SelectionEnd;
	FOnSelectEvent OnSelect;
	FSimpleDelegate OnBeginRetime;
	FOnRetimeEvent OnRetime;
	FSimpleDelegate OnEndRetime;
	FSimpleDelegate OnInteractionEnd;
	FOnBuildAddMenu OnBuildAddMenu;
	FOnBuildEventMenu OnBuildEventMenu;
	TAttribute<float> ViewStart;
	TAttribute<float> ViewEnd;
	FOnZoom OnZoom;
	FOnPan OnPan;
	FOnMoveToTrack OnMoveToTrack;
	FOnDragRow OnDragRow;
	FOnActivateEvent OnActivateEvent;
	TAttribute<bool> IsDropTarget;
	bool bPanning = false;
	float PanLastX = 0.f;

	/** Last cursor position over the lane, for the tooltip. */
	float HoverX = -1.f;
	float HoverWidth = 1.f;

	int32 DragIndex = INDEX_NONE;
	EDragMode DragMode = EDragMode::None;
	float DragGrabOffsetSec = 0.f;
	float DragOrigLenSec = 0.f;
	FVector2f DragStartLocal = FVector2f::ZeroVector;
	float DragCursorX = 0.f;
	bool bDragStarted = false;
};

/** The time ruler: ticks and second labels on the track grid's spacing; left-drag scrubs the playhead, middle-drag pans. */
class SKzTimelineRuler : public SLeafWidget
{
public:
	DECLARE_DELEGATE_ThreeParams(FOnZoom, float /*CursorSeconds*/, float /*WheelDelta*/, bool /*bPan*/);
	DECLARE_DELEGATE_OneParam(FOnPan, float /*DeltaSeconds*/);
	DECLARE_DELEGATE_OneParam(FOnScrub, float /*Seconds*/);

	SLATE_BEGIN_ARGS(SKzTimelineRuler) : _ViewStart(0.f), _ViewEnd(1.f), _Duration(1.f), _SelectionStart(-1.f), _SelectionEnd(-1.f), _Playhead(-1.f) {}
		SLATE_ATTRIBUTE(float, ViewStart)
		SLATE_ATTRIBUTE(float, ViewEnd)
		SLATE_ATTRIBUTE(float, Duration)
		SLATE_ATTRIBUTE(float, SelectionStart)
		SLATE_ATTRIBUTE(float, SelectionEnd)
		SLATE_ATTRIBUTE(float, Playhead)
		SLATE_EVENT(FOnZoom, OnZoom)
		SLATE_EVENT(FOnPan, OnPan)
		SLATE_EVENT(FOnScrub, OnScrub)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(100.f, 24.f); }

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FReply OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override;

private:
	FKzTimelineView MakeView(float W) const { return FKzTimelineView(ViewStart.Get(0.f), ViewEnd.Get(1.f), W); }

	TAttribute<float> ViewStart;
	TAttribute<float> ViewEnd;
	TAttribute<float> Duration;
	TAttribute<float> SelectionStart;
	TAttribute<float> SelectionEnd;
	FOnZoom OnZoom;
	FOnPan OnPan;
	FOnScrub OnScrub;
	TAttribute<float> Playhead;
	bool bPanning = false;
	bool bScrubbing = false;
	float PanLastX = 0.f;
};