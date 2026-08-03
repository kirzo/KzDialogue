// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"

class IPropertyHandle;
class USoundWave;
struct FKzWaveformPreview;

/**
 * Visual editor for a line's custom audio playback range (AudioStartTime / AudioEndTime):
 * the wave's envelope with the active range highlighted, and draggable start/end edges that
 * write back through the property handles (so the asset's clamping and transactions apply).
 * Dragging the end edge to the wave's end commits 0 ("play to the natural end").
 */
class KZDIALOGUEEDITOR_API SKzAudioRangeStrip : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SKzAudioRangeStrip) {}
		SLATE_ARGUMENT(TSharedPtr<IPropertyHandle>, StartHandle)
		SLATE_ARGUMENT(TSharedPtr<IPropertyHandle>, EndHandle)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, USoundWave* Wave);

	//~ SWidget
	virtual FVector2D ComputeDesiredSize(float) const override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FCursorReply OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const override;

private:
	TSharedPtr<FKzWaveformPreview> Preview;
	TSharedPtr<IPropertyHandle> StartHandle;
	TSharedPtr<IPropertyHandle> EndHandle;

	enum class EDragEdge : uint8 { None, Start, End };
	EDragEdge Drag = EDragEdge::None;
	/** Provisional time of the dragged edge; committed to the property on mouse release. */
	float DragTime = 0.f;

	float GetDuration() const;
	/** Current values straight from the handles (the dragged edge reads DragTime instead). */
	float GetStartTime() const;
	float GetEndTime() const;

	float TimeToX(float Time, float Width) const;
	float XToTime(float X, float Width) const;
	EDragEdge HitTestEdge(const FGeometry& MyGeometry, const FVector2D& ScreenPos) const;
};