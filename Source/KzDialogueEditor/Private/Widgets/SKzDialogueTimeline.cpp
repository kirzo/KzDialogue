// Copyright 2026 kirzo

#include "Widgets/SKzDialogueTimeline.h"
#include "KzDialogueTimeline.h"
#include "KzDialogueNotify.h"

#include "UObject/UObjectHash.h"

#include "Modules/ModuleManager.h"
#include "PropertyCustomizationHelpers.h"
#include "ClassViewerModule.h"
#include "ClassViewerFilter.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Factories/BlueprintFactory.h"
#include "Engine/Blueprint.h"
#include "Editor.h"
#include "Subsystems/AssetEditorSubsystem.h"

#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Layout/WidgetPath.h"
#include "Styling/AppStyle.h"

#include "Widgets/SBoxPanel.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/SLeafWidget.h"
#include "Styling/SlateTypes.h"

#include "PropertyEditorModule.h"
#include "IStructureDetailsView.h"
#include "UObject/StructOnScope.h"

#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "ScopedTransaction.h"
#include "Input/CursorReply.h"
#include "GenericPlatform/ICursor.h"

#define LOCTEXT_NAMESPACE "SKzDialogueTimeline"

namespace
{
	// Class-viewer filter for the add menu: concrete subclasses of a notify base. Handles both
	// loaded classes and unloaded blueprints (the class viewer discovers them).
	class FKzNotifyClassFilter : public IClassViewerFilter
	{
	public:
		UClass* BaseClass = nullptr;

		virtual bool IsClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const UClass* InClass, TSharedRef<FClassViewerFilterFuncs> InFilterFuncs) override
		{
			return BaseClass && InClass->IsChildOf(BaseClass) && !InClass->HasAnyClassFlags(CLASS_Hidden | CLASS_HideDropDown | CLASS_Deprecated | CLASS_Abstract);
		}

		virtual bool IsUnloadedClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const TSharedRef<const IUnloadedBlueprintData> InUnloadedClassData, TSharedRef<FClassViewerFilterFuncs> InFilterFuncs) override
		{
			return BaseClass && InUnloadedClassData->IsChildOf(BaseClass) && !InUnloadedClassData->HasAnyClassFlags(CLASS_Hidden | CLASS_HideDropDown | CLASS_Deprecated | CLASS_Abstract);
		}
	};

	// Tick spacing, copied from SSimpleTimeSlider, so the track grid lines fall on the same
	// times as the ruler ticks above them. Series: .001, .005, .01, .05, .1, .5, 1, ...
	float GetNextTickSpacing(uint32 CurrentStep)
	{
		if (CurrentStep & 0x01)
		{
			return FMath::Pow(10.f, 0.5f * static_cast<float>(CurrentStep - 1) + 1.f);
		}
		return 0.5f * FMath::Pow(10.f, 0.5f * static_cast<float>(CurrentStep) + 1.f);
	}

	float DetermineTickSpacing(float PixelsPerSecond)
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
}

// ---------------------------------------------------------------------------------------
// SKzTimelineTrack: one lane's interactive notify track, mirroring the AnimMontage notify
// track. Paints point/state markers along [0, duration], hit-tests them, and supports
// drag-to-retime (drag a state's edges to resize), left-click to select, right-click on
// empty space to add a notify, and right-click on a marker to delete it.
// ---------------------------------------------------------------------------------------

class SKzTimelineTrack : public SLeafWidget
{
public:
	DECLARE_DELEGATE_OneParam(FOnSelectEvent, int32 /*EventIndex*/);
	DECLARE_DELEGATE_ThreeParams(FOnRetimeEvent, int32 /*EventIndex*/, float /*StartSeconds*/, float /*EndSeconds*/);
	DECLARE_DELEGATE_RetVal_OneParam(TSharedRef<SWidget>, FOnBuildAddMenu, float /*TimeSeconds*/);
	DECLARE_DELEGATE_RetVal_OneParam(TSharedRef<SWidget>, FOnBuildEventMenu, int32 /*EventIndex*/);
	DECLARE_DELEGATE_ThreeParams(FOnZoom, float /*CursorSeconds*/, float /*WheelDelta*/, bool /*bPan*/);
	DECLARE_DELEGATE_OneParam(FOnPan, float /*DeltaSeconds*/);
	DECLARE_DELEGATE_TwoParams(FOnMoveToTrack, int32 /*EventIndex*/, int32 /*TargetTrack*/);
	DECLARE_DELEGATE_OneParam(FOnActivateEvent, int32 /*EventIndex*/);

	SLATE_BEGIN_ARGS(SKzTimelineTrack) : _Duration(1.f), _ViewStart(0.f), _ViewEnd(1.f), _SelectionStart(-1.f), _SelectionEnd(-1.f), _SelectedEvent(INDEX_NONE) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UKzDialogueTimeline>, Timeline)
		SLATE_ARGUMENT(int32, TrackIndex)
		SLATE_ATTRIBUTE(float, Duration)
		SLATE_ATTRIBUTE(float, ViewStart)
		SLATE_ATTRIBUTE(float, ViewEnd)
		SLATE_ATTRIBUTE(float, SelectionStart)
		SLATE_ATTRIBUTE(float, SelectionEnd)
		SLATE_ATTRIBUTE(int32, SelectedEvent)
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
		SLATE_EVENT(FOnActivateEvent, OnActivateEvent)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		Timeline = InArgs._Timeline;
		TrackIndex = InArgs._TrackIndex;
		Duration = InArgs._Duration;
		SelectedEventAttr = InArgs._SelectedEvent;
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
		OnActivateEvent = InArgs._OnActivateEvent;
		SetClipping(EWidgetClipping::ClipToBounds);
	}

	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(100.f, 28.f); }

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
	{
		const float W = AllottedGeometry.GetLocalSize().X;
		const float H = AllottedGeometry.GetLocalSize().Y;
		const FSlateBrush* Box = FAppStyle::GetBrush("WhiteBrush");

		// Lane background so an empty track still reads as a drop area.
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), Box, ESlateDrawEffect::None, FLinearColor(0.f, 0.f, 0.f, 0.15f));

		// Grid, separator and bound/selection lines are drawn for every row -- including the
		// decorative "Notifies" group row (TrackIndex == INDEX_NONE), which has no events.
		const float Dur = FMath::Max(Duration.Get(1.f), KINDA_SMALL_NUMBER);

		// Grid: vertical lines on the same ticks as the ruler, brighter every tenth, plus a
		// horizontal separator at the bottom of the row.
		if (W > 1.f)
		{
			const float ViewMin = ViewStart.Get(0.f);
			const float ViewMax = ViewMin + ViewSpan();
			const float Spacing = DetermineTickSpacing(W / ViewSpan());
			for (int32 Tick = FMath::FloorToInt(ViewMin / Spacing); Spacing > 0.f; ++Tick)
			{
				const float Seconds = static_cast<float>(Tick) * Spacing;
				if (Seconds > ViewMax) { break; }
				if (Seconds < ViewMin) { continue; }
				const float GridX = TimeToX(Seconds, W);
				const FLinearColor GridColor(1.f, 1.f, 1.f, (Tick % 10 == 0) ? 0.10f : 0.04f);
				FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(FVector2f(1.f, H), FSlateLayoutTransform(FVector2f(GridX, 0.f))), Box, ESlateDrawEffect::None, GridColor);
			}
		}
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(FVector2f(W, 1.f), FSlateLayoutTransform(FVector2f(0.f, H - 1.f))), Box, ESlateDrawEffect::None, FLinearColor(1.f, 1.f, 1.f, 0.08f));

		// Line bounds: a green line at the start (0) and a red one at the end, when in view.
		{
			const float ViewMin = ViewStart.Get(0.f);
			const float ViewMaxLocal = ViewMin + ViewSpan();
			if (0.f >= ViewMin && 0.f <= ViewMaxLocal)
			{
				FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(FVector2f(2.f, H), FSlateLayoutTransform(FVector2f(TimeToX(0.f, W), 0.f))), Box, ESlateDrawEffect::None, FLinearColor(0.20f, 0.85f, 0.25f, 0.9f));
			}
			if (Dur >= ViewMin && Dur <= ViewMaxLocal)
			{
				FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(FVector2f(2.f, H), FSlateLayoutTransform(FVector2f(TimeToX(Dur, W) - 2.f, 0.f))), Box, ESlateDrawEffect::None, FLinearColor(0.85f, 0.20f, 0.20f, 0.9f));
			}

			// Subtle line(s) at the selected event's time (start, plus end for a state).
			const float SelStart = SelectionStart.Get(-1.f);
			const float SelEnd = SelectionEnd.Get(-1.f);
			const FLinearColor SelLineColor(1.f, 1.f, 1.f, 0.4f);
			if (SelStart >= ViewMin && SelStart <= ViewMaxLocal)
			{
				FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(FVector2f(1.f, H), FSlateLayoutTransform(FVector2f(TimeToX(SelStart, W), 0.f))), Box, ESlateDrawEffect::None, SelLineColor);
			}
			if (SelEnd >= ViewMin && SelEnd <= ViewMaxLocal)
			{
				FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(FVector2f(1.f, H), FSlateLayoutTransform(FVector2f(TimeToX(SelEnd, W), 0.f))), Box, ESlateDrawEffect::None, SelLineColor);
			}
		}

		// Events only on a real track; the group row stops here (decoration only).
		UKzDialogueTimeline* T = Timeline.Get();
		if (!T || !T->Tracks.IsValidIndex(TrackIndex)) { return LayerId + 4; }
		const FKzDialogueNotifyTrack& Track = T->Tracks[TrackIndex];
		const int32 Sel = SelectedEventAttr.Get(INDEX_NONE);
		const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Regular", 8);
		const TSharedRef<FSlateFontMeasure> FontMeasure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();

		for (int32 e = 0; e < Track.Events.Num(); ++e)
		{
			float StartSec, EndSec; bool bState;
			GetEventBounds(Track.Events[e], Dur, StartSec, EndSec, bState);

			const float L = TimeToX(StartSec, W);
			const float R = TimeToX(EndSec, W);
			const bool bSelected = (e == Sel);
			const FLinearColor Color = bSelected ? FLinearColor(0.98f, 0.86f, 0.36f) : (Track.Events[e].Notify ? Track.Events[e].Notify->GetEditorColor() : FLinearColor(0.46f, 0.62f, 0.85f));
			const FLinearColor TextColor = Color.GetLuminance() > 0.5f ? FLinearColor::Black : FLinearColor::White;
			const FText Label = Track.Events[e].Notify ? Track.Events[e].Notify->GetNotifyName() : LOCTEXT("Empty", "(empty)");
			const FVector2D LabelExtent = FontMeasure->Measure(Label.ToString(), Font);

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
		return LayerId + 4;
	}

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		UKzDialogueTimeline* T = Timeline.Get();
		if (!T || !T->Tracks.IsValidIndex(TrackIndex)) { return FReply::Unhandled(); }

		const float W = MyGeometry.GetLocalSize().X;
		const float Dur = FMath::Max(Duration.Get(1.f), KINDA_SMALL_NUMBER);
		const float LocalX = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()).X;

		// Middle button anywhere pans the view.
		if (MouseEvent.GetEffectingButton() == EKeys::MiddleMouseButton)
		{
			bPanning = true;
			PanLastX = LocalX;
			return FReply::Handled().CaptureMouse(SharedThis(this));
		}

		if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
		{
			EDragMode Mode;
			const int32 Hit = HitTest(LocalX, W, Dur, Mode);
			OnSelect.ExecuteIfBound(Hit);
			Invalidate(EInvalidateWidgetReason::Paint);

			if (Hit != INDEX_NONE && T->Tracks[TrackIndex].Events.IsValidIndex(Hit))
			{
				float StartSec, EndSec; bool bState;
				GetEventBounds(T->Tracks[TrackIndex].Events[Hit], Dur, StartSec, EndSec, bState);
				const float CursorSec = XToTime(LocalX, W);

				DragIndex = Hit;
				DragMode = Mode;
				DragGrabOffsetSec = CursorSec - StartSec;
				DragOrigLenSec = EndSec - StartSec;
				DragStartLocalX = LocalX;
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

	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		const float W = MyGeometry.GetLocalSize().X;
		const float LocalX = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()).X;

		if (bPanning && MouseEvent.IsMouseButtonDown(EKeys::MiddleMouseButton))
		{
			const float DeltaX = LocalX - PanLastX;
			PanLastX = LocalX;
			if (W > 0.f) { OnPan.ExecuteIfBound(-(DeltaX / W) * ViewSpan()); }
			return FReply::Handled();
		}

		if (DragIndex == INDEX_NONE || !MouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton)) { return FReply::Unhandled(); }
		UKzDialogueTimeline* T = Timeline.Get();
		if (!T || !T->Tracks.IsValidIndex(TrackIndex) || !T->Tracks[TrackIndex].Events.IsValidIndex(DragIndex)) { return FReply::Unhandled(); }

		const float Dur = FMath::Max(Duration.Get(1.f), KINDA_SMALL_NUMBER);

		if (!bDragStarted)
		{
			if (FMath::Abs(LocalX - DragStartLocalX) < DragThreshold) { return FReply::Handled(); }
			OnBeginRetime.ExecuteIfBound();
			bDragStarted = true;
		}

		const float CursorSec = XToTime(LocalX, W);

		float CurStart, CurEnd; bool bState;
		GetEventBounds(T->Tracks[TrackIndex].Events[DragIndex], Dur, CurStart, CurEnd, bState);

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
			if (DragMode == EDragMode::ResizeStart) { NewStart = SnapToGrid(NewStart, W); }
			else if (DragMode == EDragMode::ResizeEnd) { NewEnd = SnapToGrid(NewEnd, W); }
			else
			{
				const float Snapped = SnapToGrid(NewStart, W);
				NewEnd += Snapped - NewStart;
				NewStart = Snapped;
			}
		}

		OnRetime.ExecuteIfBound(DragIndex, NewStart, NewEnd);
		Invalidate(EInvalidateWidgetReason::Paint);
		return FReply::Handled();
	}

	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
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

			// Released over another row? Move the dragged notify to that track.
			int32 TargetTrack = TrackIndex;
			if (bWasRetiming)
			{
				const float H = MyGeometry.GetLocalSize().Y;
				const float LocalY = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()).Y;
				if (H > 0.f) { TargetTrack = TrackIndex + FMath::FloorToInt(LocalY / H); }
			}

			DragIndex = INDEX_NONE;
			DragMode = EDragMode::None;

			const FReply Reply = bHadCapture ? FReply::Handled().ReleaseMouseCapture() : FReply::Handled();
			if (bWasRetiming)
			{
				OnEndRetime.ExecuteIfBound();
				if (TargetTrack != TrackIndex) { OnMoveToTrack.ExecuteIfBound(DraggedEvent, TargetTrack); }
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
			const float Dur = FMath::Max(Duration.Get(1.f), KINDA_SMALL_NUMBER);
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
				MenuContent = OnBuildAddMenu.Execute(XToTime(LocalX, W));
			}

			// Nothing to add/edit here (e.g. the decorative group row): no menu.
			if (MenuContent == SNullWidget::NullWidget) { return FReply::Unhandled(); }

			FSlateApplication::Get().PushMenu(SharedThis(this), FWidgetPath(), MenuContent, MouseEvent.GetScreenSpacePosition(), FPopupTransitionEffect(FPopupTransitionEffect::ContextMenu));
			return FReply::Handled();
		}
		return FReply::Unhandled();
	}

	virtual FReply OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton) { return FReply::Unhandled(); }

		const float W = MyGeometry.GetLocalSize().X;
		const float Dur = FMath::Max(Duration.Get(1.f), KINDA_SMALL_NUMBER);
		const float LocalX = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()).X;
		EDragMode Mode;
		const int32 Hit = HitTest(LocalX, W, Dur, Mode);
		if (Hit == INDEX_NONE) { return FReply::Unhandled(); }

		OnSelect.ExecuteIfBound(Hit);
		Invalidate(EInvalidateWidgetReason::Paint);
		OnActivateEvent.ExecuteIfBound(Hit);
		return FReply::Handled();
	}

	virtual FReply OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (!OnZoom.IsBound()) { return FReply::Unhandled(); }
		const float W = MyGeometry.GetLocalSize().X;
		const float LocalX = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()).X;
		OnZoom.Execute(XToTime(LocalX, W), MouseEvent.GetWheelDelta(), MouseEvent.IsShiftDown());
		return FReply::Handled();
	}

	virtual FCursorReply OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const override
	{
		const float W = MyGeometry.GetLocalSize().X;
		const float Dur = FMath::Max(Duration.Get(1.f), KINDA_SMALL_NUMBER);
		const float LocalX = MyGeometry.AbsoluteToLocal(CursorEvent.GetScreenSpacePosition()).X;
		EDragMode Mode;
		const int32 Hit = HitTest(LocalX, W, Dur, Mode);
		if (Hit != INDEX_NONE)
		{
			if (Mode == EDragMode::ResizeStart || Mode == EDragMode::ResizeEnd) { return FCursorReply::Cursor(EMouseCursor::ResizeLeftRight); }
			return FCursorReply::Cursor(EMouseCursor::CardinalCross);
		}
		return FCursorReply::Unhandled();
	}

private:
	enum class EDragMode { None, Move, ResizeStart, ResizeEnd };

	float ViewSpan() const { return FMath::Max(ViewEnd.Get(1.f) - ViewStart.Get(0.f), 0.0001f); }
	float TimeToX(float TimeSeconds, float W) const { return (TimeSeconds - ViewStart.Get(0.f)) / ViewSpan() * W; }
	float XToTime(float LocalX, float W) const { return ViewStart.Get(0.f) + (LocalX / FMath::Max(W, 1.f)) * ViewSpan(); }
	float SnapToGrid(float TimeSeconds, float W) const { const float Spacing = DetermineTickSpacing(W / ViewSpan()); return Spacing > 0.f ? FMath::RoundToFloat(TimeSeconds / Spacing) * Spacing : TimeSeconds; }

	void GetEventBounds(const FKzDialogueNotifyEvent& Event, float Dur, float& OutStartSec, float& OutEndSec, bool& bOutState) const
	{
		const FKzDialogueTimeSource_Relative* Rel = Event.TimeSource.GetPtr<FKzDialogueTimeSource_Relative>();
		bOutState = Event.Notify && Event.Notify->IsA<UKzDialogueNotifyState>();
		if (Rel)
		{
			OutStartSec = Rel->bNormalized ? Rel->Time * Dur : Rel->Time;
			const float Len = Rel->bNormalized ? Rel->Duration * Dur : Rel->Duration;
			OutEndSec = OutStartSec + (bOutState ? Len : 0.f);
		}
		else
		{
			OutStartSec = 0.f;
			OutEndSec = 0.f;
		}
	}

	float PointBoxWidth(const FKzDialogueNotifyEvent& Event) const
	{
		const FText Label = Event.Notify ? Event.Notify->GetNotifyName() : LOCTEXT("Empty", "(empty)");
		const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Regular", 8);
		const double TextWidth = FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Label.ToString(), Font).X;
		return static_cast<float>(TextWidth) + 8.f;
	}

	int32 HitTest(float LocalX, float W, float Dur, EDragMode& OutMode) const
	{
		OutMode = EDragMode::None;
		UKzDialogueTimeline* T = Timeline.Get();
		if (!T || !T->Tracks.IsValidIndex(TrackIndex)) { return INDEX_NONE; }
		const FKzDialogueNotifyTrack& Track = T->Tracks[TrackIndex];
		for (int32 e = Track.Events.Num() - 1; e >= 0; --e)
		{
			float StartSec, EndSec; bool bState;
			GetEventBounds(Track.Events[e], Dur, StartSec, EndSec, bState);
			const float L = TimeToX(StartSec, W);
			const float R = TimeToX(EndSec, W);
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

	static constexpr float EdgeGrab = 6.f;
	static constexpr float DragThreshold = 4.f;

	TWeakObjectPtr<UKzDialogueTimeline> Timeline;
	int32 TrackIndex = INDEX_NONE;
	TAttribute<float> Duration;
	TAttribute<int32> SelectedEventAttr;
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
	FOnActivateEvent OnActivateEvent;
	bool bPanning = false;
	float PanLastX = 0.f;

	int32 DragIndex = INDEX_NONE;
	EDragMode DragMode = EDragMode::None;
	float DragGrabOffsetSec = 0.f;
	float DragOrigLenSec = 0.f;
	float DragStartLocalX = 0.f;
	bool bDragStarted = false;
};

// ---------------------------------------------------------------------------------------
// SKzTimelineRuler: the time ruler. Draws ticks and second labels on the same spacing as the
// track grid, with no scrub handle (a dialogue line has no transport).
// ---------------------------------------------------------------------------------------

class SKzTimelineRuler : public SLeafWidget
{
public:
	DECLARE_DELEGATE_ThreeParams(FOnZoom, float /*CursorSeconds*/, float /*WheelDelta*/, bool /*bPan*/);
	DECLARE_DELEGATE_OneParam(FOnPan, float /*DeltaSeconds*/);

	SLATE_BEGIN_ARGS(SKzTimelineRuler) : _ViewStart(0.f), _ViewEnd(1.f), _Duration(1.f), _SelectionStart(-1.f), _SelectionEnd(-1.f) {}
		SLATE_ATTRIBUTE(float, ViewStart)
		SLATE_ATTRIBUTE(float, ViewEnd)
		SLATE_ATTRIBUTE(float, Duration)
		SLATE_ATTRIBUTE(float, SelectionStart)
		SLATE_ATTRIBUTE(float, SelectionEnd)
		SLATE_EVENT(FOnZoom, OnZoom)
		SLATE_EVENT(FOnPan, OnPan)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		ViewStart = InArgs._ViewStart;
		ViewEnd = InArgs._ViewEnd;
		Duration = InArgs._Duration;
		SelectionStart = InArgs._SelectionStart;
		SelectionEnd = InArgs._SelectionEnd;
		OnZoom = InArgs._OnZoom;
		OnPan = InArgs._OnPan;
		SetClipping(EWidgetClipping::ClipToBounds);
	}

	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(100.f, 24.f); }

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
	{
		const float W = AllottedGeometry.GetLocalSize().X;
		const float H = AllottedGeometry.GetLocalSize().Y;
		if (W <= 1.f) { return LayerId; }

		const float ViewMin = ViewStart.Get(0.f);
		const float Span = FMath::Max(ViewEnd.Get(1.f) - ViewMin, 0.0001f);
		const float ViewMax = ViewMin + Span;
		const FSlateBrush* Box = FAppStyle::GetBrush("WhiteBrush");
		const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Regular", 8);
		const TSharedRef<FSlateFontMeasure> FontMeasure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
		const FLinearColor TickColor(1.f, 1.f, 1.f, 0.35f);
		const FLinearColor LabelColor(1.f, 1.f, 1.f, 0.6f);

		const float Spacing = DetermineTickSpacing(W / Span);
		for (int32 Tick = FMath::FloorToInt(ViewMin / Spacing); Spacing > 0.f; ++Tick)
		{
			const float Seconds = static_cast<float>(Tick) * Spacing;
			if (Seconds > ViewMax) { break; }
			if (Seconds < ViewMin) { continue; }
			const float X = (Seconds - ViewMin) / Span * W;
			const bool bMajor = (Tick % 10 == 0);
			const float TickH = bMajor ? 7.f : 4.f;
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1, AllottedGeometry.ToPaintGeometry(FVector2f(1.f, TickH), FSlateLayoutTransform(FVector2f(X, H - TickH))), Box, ESlateDrawEffect::None, TickColor);
			if (bMajor)
			{
				const FString TickLabel = FString::Printf(TEXT("%g"), Seconds);
				const FVector2D TextSize = FontMeasure->Measure(TickLabel, Font);
				FSlateDrawElement::MakeText(OutDrawElements, LayerId + 2, AllottedGeometry.ToPaintGeometry(FVector2f(static_cast<float>(TextSize.X), static_cast<float>(TextSize.Y)), FSlateLayoutTransform(FVector2f(X + 2.f, 1.f))), FText::FromString(TickLabel), Font, ESlateDrawEffect::None, LabelColor);
			}
		}

		// Start (green) / end (red) bounds and the selected event's line(s), so they reach the
		// numbers and connect with the track lines below.
		auto DrawMarkLine = [&](float TimeSeconds, float LineWidth, const FLinearColor& Color, bool bAnchorRight)
		{
			if (TimeSeconds < ViewMin || TimeSeconds > ViewMax) { return; }
			const float X = (TimeSeconds - ViewMin) / Span * W - (bAnchorRight ? LineWidth : 0.f);
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 3, AllottedGeometry.ToPaintGeometry(FVector2f(LineWidth, H), FSlateLayoutTransform(FVector2f(X, 0.f))), Box, ESlateDrawEffect::None, Color);
		};
		DrawMarkLine(0.f, 2.f, FLinearColor(0.20f, 0.85f, 0.25f, 0.9f), false);
		DrawMarkLine(Duration.Get(1.f), 2.f, FLinearColor(0.85f, 0.20f, 0.20f, 0.9f), true);
		DrawMarkLine(SelectionStart.Get(-1.f), 1.f, FLinearColor(1.f, 1.f, 1.f, 0.4f), false);
		DrawMarkLine(SelectionEnd.Get(-1.f), 1.f, FLinearColor(1.f, 1.f, 1.f, 0.4f), false);

		return LayerId + 4;
	}

	virtual FReply OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (!OnZoom.IsBound()) { return FReply::Unhandled(); }
		const float W = MyGeometry.GetLocalSize().X;
		const float ViewMin = ViewStart.Get(0.f);
		const float Span = FMath::Max(ViewEnd.Get(1.f) - ViewMin, 0.0001f);
		const float LocalX = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()).X;
		const float CursorTime = ViewMin + (LocalX / FMath::Max(W, 1.f)) * Span;
		OnZoom.Execute(CursorTime, MouseEvent.GetWheelDelta(), MouseEvent.IsShiftDown());
		return FReply::Handled();
	}

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		// Drag the time bar (left or middle button) to pan.
		if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton || MouseEvent.GetEffectingButton() == EKeys::MiddleMouseButton)
		{
			bPanning = true;
			PanLastX = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()).X;
			return FReply::Handled().CaptureMouse(SharedThis(this));
		}
		return FReply::Unhandled();
	}

	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (!bPanning || !(MouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton) || MouseEvent.IsMouseButtonDown(EKeys::MiddleMouseButton)))
		{
			return FReply::Unhandled();
		}
		const float W = MyGeometry.GetLocalSize().X;
		const float LocalX = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()).X;
		const float DeltaX = LocalX - PanLastX;
		PanLastX = LocalX;
		const float Span = FMath::Max(ViewEnd.Get(1.f) - ViewStart.Get(0.f), 0.0001f);
		if (W > 0.f) { OnPan.ExecuteIfBound(-(DeltaX / W) * Span); }
		return FReply::Handled();
	}

	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (bPanning && (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton || MouseEvent.GetEffectingButton() == EKeys::MiddleMouseButton))
		{
			bPanning = false;
			return FReply::Handled().ReleaseMouseCapture();
		}
		return FReply::Unhandled();
	}

private:
	TAttribute<float> ViewStart;
	TAttribute<float> ViewEnd;
	TAttribute<float> Duration;
	TAttribute<float> SelectionStart;
	TAttribute<float> SelectionEnd;
	FOnZoom OnZoom;
	FOnPan OnPan;
	bool bPanning = false;
	float PanLastX = 0.f;
};

// ---------------------------------------------------------------------------------------
// SKzDialogueTimeline
// ---------------------------------------------------------------------------------------

void SKzDialogueTimeline::Construct(const FArguments& InArgs, UKzDialogueTimeline* InTimeline)
{
	Timeline = InTimeline;
	DisplayDuration = InArgs._DisplayDuration;
	OnModified = InArgs._OnModified;

	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FDetailsViewArgs DetailsArgs;
	DetailsArgs.bAllowSearch = false;
	DetailsArgs.bShowOptions = false;
	DetailsArgs.bHideSelectionTip = true;
	DetailsArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	FStructureDetailsViewArgs StructArgs;
	StructArgs.bShowObjects = true;
	EventDetailsView = PropertyModule.CreateStructureDetailView(DetailsArgs, StructArgs, nullptr, FText::GetEmpty());
	EventDetailsView->GetOnFinishedChangingPropertiesDelegate().AddSP(this, &SKzDialogueTimeline::OnEventDetailsChanged);

	Rebuild();
}

SKzDialogueTimeline::~SKzDialogueTimeline() = default;

float SKzDialogueTimeline::Duration() const
{
	const float D = DisplayDuration.Get(0.f);
	return D > KINDA_SMALL_NUMBER ? D : 1.f;
}

float SKzDialogueTimeline::GetViewStart() const
{
	if (ViewEnd <= ViewStart) { return 0.f; }
	return FMath::Clamp(ViewStart, 0.f, Duration());
}

float SKzDialogueTimeline::GetViewEnd() const
{
	const float Dur = Duration();
	if (ViewEnd <= ViewStart) { return Dur; }
	return FMath::Clamp(ViewEnd, GetViewStart() + 0.001f, Dur);
}

float SKzDialogueTimeline::GetSelectionStart() const
{
	const FKzDialogueNotifyEvent* Event = SelectedEvent();
	const FKzDialogueTimeSource_Relative* Rel = Event ? Event->TimeSource.GetPtr<FKzDialogueTimeSource_Relative>() : nullptr;
	if (!Rel) { return -1.f; }
	return Rel->bNormalized ? Rel->Time * Duration() : Rel->Time;
}

float SKzDialogueTimeline::GetSelectionEnd() const
{
	const FKzDialogueNotifyEvent* Event = SelectedEvent();
	if (!Event || !Event->Notify || !Event->Notify->IsA<UKzDialogueNotifyState>()) { return -1.f; }
	const FKzDialogueTimeSource_Relative* Rel = Event->TimeSource.GetPtr<FKzDialogueTimeSource_Relative>();
	if (!Rel) { return -1.f; }
	const float Start = Rel->bNormalized ? Rel->Time * Duration() : Rel->Time;
	const float Len = Rel->bNormalized ? Rel->Duration * Duration() : Rel->Duration;
	return Start + Len;
}

void SKzDialogueTimeline::ZoomView(float CursorTimeSeconds, float WheelDelta, bool bPan)
{
	const float Dur = Duration();
	float Start = GetViewStart();
	float End = GetViewEnd();
	const float Size = End - Start;

	if (bPan)
	{
		const float Shift = Size * 0.15f * (WheelDelta > 0.f ? 1.f : -1.f);
		Start += Shift;
		End += Shift;
	}
	else
	{
		const float Factor = (WheelDelta > 0.f) ? 0.8f : 1.25f;
		const float NewSize = FMath::Clamp(Size * Factor, FMath::Min(0.05f, Dur), Dur);
		const float Frac = Size > KINDA_SMALL_NUMBER ? (CursorTimeSeconds - Start) / Size : 0.5f;
		Start = CursorTimeSeconds - Frac * NewSize;
		End = Start + NewSize;
	}

	// Keep the window size and slide it back inside [0, Dur].
	const float WindowSize = FMath::Min(End - Start, Dur);
	if (Start < 0.f) { Start = 0.f; }
	End = Start + WindowSize;
	if (End > Dur) { End = Dur; Start = FMath::Max(0.f, End - WindowSize); }

	ViewStart = Start;
	ViewEnd = End;
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SKzDialogueTimeline::PanView(float DeltaSeconds)
{
	const float Dur = Duration();
	float Start = GetViewStart();
	const float Size = GetViewEnd() - Start;
	Start += DeltaSeconds;
	float End = Start + Size;
	if (Start < 0.f) { Start = 0.f; End = Start + Size; }
	if (End > Dur) { End = Dur; Start = FMath::Max(0.f, End - Size); }
	ViewStart = Start;
	ViewEnd = End;
	Invalidate(EInvalidateWidgetReason::Paint);
}

FKzDialogueNotifyEvent* SKzDialogueTimeline::SelectedEvent() const
{
	UKzDialogueTimeline* T = Timeline.Get();
	if (!T || !T->Tracks.IsValidIndex(SelTrack)) { return nullptr; }
	FKzDialogueNotifyTrack& Track = T->Tracks[SelTrack];
	return Track.Events.IsValidIndex(SelEvent) ? &Track.Events[SelEvent] : nullptr;
}

void SKzDialogueTimeline::Modified()
{
	OnModified.ExecuteIfBound();
}

FReply SKzDialogueTimeline::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Delete && SelectedEvent())
	{
		RemoveSelected();
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

void SKzDialogueTimeline::Rebuild()
{
	UKzDialogueTimeline* T = Timeline.Get();

	// Two synchronized columns, like the AnimMontage notify panel: the left outliner (filter,
	// the "Notifies" group, one row per track) and the right track area (ruler, a group band,
	// one painted track per row). Matching row heights keep the columns aligned.
	TSharedRef<SVerticalBox> OutlinerColumn = SNew(SVerticalBox);
	TSharedRef<SVerticalBox> TrackAreaColumn = SNew(SVerticalBox);

	// Row 0: outliner filter | ruler.
	OutlinerColumn->AddSlot().AutoHeight()
	[
		SNew(SBox).HeightOverride(HeaderRowHeight).VAlign(VAlign_Center).Padding(FMargin(2.f, 0.f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(SSearchBox)
					.HintText(LOCTEXT("FilterTracksHint", "Filter"))
					.OnTextChanged_Lambda([this](const FText& InText) { FilterText = InText; Rebuild(); })
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2, 0, 0, 0)
			[
				SNew(SButton)
					.ToolTipText(LOCTEXT("ResetZoomTip", "Reset zoom (show the whole line)"))
					.ContentPadding(FMargin(4.f, 1.f))
					.OnClicked_Lambda([this]() { ViewStart = 0.f; ViewEnd = 0.f; Invalidate(EInvalidateWidgetReason::Paint); return FReply::Handled(); })
					[
						SNew(STextBlock).Text(LOCTEXT("ResetZoom", "Fit")).Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
					]
			]
		]
	];
	TrackAreaColumn->AddSlot().AutoHeight()
	[
		SNew(SBox).HeightOverride(HeaderRowHeight)[ BuildRuler() ]
	];

	// "Notifies" group header | group band.
	OutlinerColumn->AddSlot().AutoHeight()[ BuildNotifiesHeaderRow() ];
	TrackAreaColumn->AddSlot().AutoHeight()
	[
		SNew(SBox).HeightOverride(HeaderRowHeight)
		[
			// Decorative group row: draws the grid / bounds / selection (and zooms/pans) but
			// has no events of its own (INDEX_NONE) and no right-click menu.
			SNew(SKzTimelineTrack)
				.Timeline(Timeline)
				.TrackIndex(INDEX_NONE)
				.Duration_Lambda([this]() { return Duration(); })
				.ViewStart_Lambda([this]() { return GetViewStart(); })
				.ViewEnd_Lambda([this]() { return GetViewEnd(); })
				.SelectionStart_Lambda([this]() { return GetSelectionStart(); })
				.SelectionEnd_Lambda([this]() { return GetSelectionEnd(); })
				.OnZoom_Lambda([this](float CursorSeconds, float WheelDelta, bool bPan) { ZoomView(CursorSeconds, WheelDelta, bPan); })
				.OnPan_Lambda([this](float DeltaSeconds) { PanView(DeltaSeconds); })
		]
	];

	// One row per (filtered) track, when the group is expanded.
	if (T && bNotifiesExpanded)
	{
		for (int32 i = 0; i < T->Tracks.Num(); ++i)
		{
			if (!PassesFilter(T->Tracks[i].Name)) { continue; }
			OutlinerColumn->AddSlot().AutoHeight()[ BuildOutlinerTrackRow(i) ];
			TrackAreaColumn->AddSlot().AutoHeight()[ BuildTrackAreaRow(i) ];
		}
	}

	SyncEventDetails();

	ChildSlot
	[
		SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			.Padding(4)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SSplitter)
					.Orientation(Orient_Horizontal)
				+ SSplitter::Slot()
					.Value(MakeAttributeLambda([this]() { return ColumnFillCoefficients[0]; }))
					.OnSlotResized(SSplitter::FOnSlotResized::CreateLambda([this](float V) { ColumnFillCoefficients[0] = V; ColumnFillCoefficients[1] = 1.f - V; }))
					[
						SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(0)[ OutlinerColumn ]
					]
				+ SSplitter::Slot()
					.Value(MakeAttributeLambda([this]() { return ColumnFillCoefficients[1]; }))
					.OnSlotResized(SSplitter::FOnSlotResized::CreateLambda([this](float V) { ColumnFillCoefficients[1] = V; ColumnFillCoefficients[0] = 1.f - V; }))
					[
						SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(0)[ TrackAreaColumn ]
					]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)
			[
				EventDetailsView.IsValid() ? EventDetailsView->GetWidget().ToSharedRef() : SNullWidget::NullWidget
			]
		]
	];
}

TSharedRef<SWidget> SKzDialogueTimeline::BuildRuler()
{
	return SNew(SKzTimelineRuler)
		.ViewStart_Lambda([this]() { return GetViewStart(); })
		.ViewEnd_Lambda([this]() { return GetViewEnd(); })
		.Duration_Lambda([this]() { return Duration(); })
		.SelectionStart_Lambda([this]() { return GetSelectionStart(); })
		.SelectionEnd_Lambda([this]() { return GetSelectionEnd(); })
		.OnZoom_Lambda([this](float CursorSeconds, float WheelDelta, bool bPan) { ZoomView(CursorSeconds, WheelDelta, bPan); })
		.OnPan_Lambda([this](float DeltaSeconds) { PanView(DeltaSeconds); });
}

TSharedRef<SWidget> SKzDialogueTimeline::BuildNotifiesHeaderRow()
{
	// The "Notifies" group: expander + label + the "+ Track" dropdown, styled like the
	// AnimMontage outliner header (FAnimTimelineTrack_Notifies::GenerateContainerWidgetForOutliner).
	return SNew(SBox).HeightOverride(HeaderRowHeight)
	[
		SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("Sequencer.Section.BackgroundTint"))
			.BorderBackgroundColor(FAppStyle::GetColor("AnimTimeline.Outliner.HeaderColor"))
			.VAlign(VAlign_Center)
			.Padding(0)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0)
				[
					SNew(SButton)
						.ButtonStyle(FAppStyle::Get(), "NoBorder")
						.ContentPadding(0)
						.OnClicked_Lambda([this]() { bNotifiesExpanded = !bNotifiesExpanded; Rebuild(); return FReply::Handled(); })
						[
							SNew(SImage)
								.ColorAndOpacity(FSlateColor::UseForeground())
								.Image_Lambda([this]() { return FAppStyle::GetBrush(bNotifiesExpanded ? "TreeArrow_Expanded" : "TreeArrow_Collapsed"); })
						]
				]
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(2, 0)
				[
					SNew(STextBlock)
						.TextStyle(&FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>("AnimTimeline.Outliner.Label"))
						.Text(LOCTEXT("NotifiesRoot", "Notifies"))
				]
				+ SHorizontalBox::Slot().AutoWidth().HAlign(HAlign_Right).VAlign(VAlign_Center).Padding(4, 1)
				[
					PropertyCustomizationHelpers::MakeAddButton(FSimpleDelegate::CreateSP(this, &SKzDialogueTimeline::AddTrack), LOCTEXT("AddTrackTip", "Add a notify track"))
				]
			]
	];
}

TSharedRef<SWidget> SKzDialogueTimeline::BuildOutlinerTrackRow(int32 TrackIndex)
{
	UKzDialogueTimeline* T = Timeline.Get();
	if (!T || !T->Tracks.IsValidIndex(TrackIndex)) { return SNullWidget::NullWidget; }
	const FKzDialogueNotifyTrack& Track = T->Tracks[TrackIndex];

	return SNew(SBox).HeightOverride(RowHeight)
	[
		SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("Sequencer.Section.BackgroundTint"))
			.BorderBackgroundColor(FAppStyle::GetColor("AnimTimeline.Outliner.ItemColor"))
			.VAlign(VAlign_Center)
			.Padding(0)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()[ SNew(SSpacer).Size(FVector2D(22.f, 1.f)) ]
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(2, 0)
				[
					SNew(SEditableTextBox)
						.Text(FText::FromName(Track.Name))
						.HintText(LOCTEXT("TrackNameHint", "Track"))
						.OnTextCommitted_Lambda([this, TrackIndex](const FText& New, ETextCommit::Type) { RenameTrack(TrackIndex, New); })
				]
				+ SHorizontalBox::Slot().AutoWidth().HAlign(HAlign_Right).VAlign(VAlign_Center).Padding(4, 1)
				[
					PropertyCustomizationHelpers::MakeDeleteButton(FSimpleDelegate::CreateSP(this, &SKzDialogueTimeline::RemoveTrack, TrackIndex), LOCTEXT("DeleteTrackTip", "Delete this track and its notifies"), TAttribute<bool>::CreateLambda([this]() { UKzDialogueTimeline* T = Timeline.Get(); return T && T->Tracks.Num() > 1; }))
				]
			]
	];
}

TSharedRef<SWidget> SKzDialogueTimeline::BuildTrackAreaRow(int32 TrackIndex)
{
	// No wrapping border here: the track paints its own background and must share the ruler's
	// exact left edge and width so markers line up with the ticks.
	return SNew(SBox).HeightOverride(RowHeight)
	[
		SNew(SKzTimelineTrack)
			.Timeline(Timeline)
			.TrackIndex(TrackIndex)
			.Duration_Lambda([this]() { return Duration(); })
			.ViewStart_Lambda([this]() { return GetViewStart(); })
			.ViewEnd_Lambda([this]() { return GetViewEnd(); })
			.SelectionStart_Lambda([this]() { return GetSelectionStart(); })
			.SelectionEnd_Lambda([this]() { return GetSelectionEnd(); })
			.SelectedEvent_Lambda([this, TrackIndex]() { return (TrackIndex == SelTrack) ? SelEvent : INDEX_NONE; })
			.OnSelect_Lambda([this, TrackIndex](int32 EventIndex) { SetSelection(TrackIndex, EventIndex); })
			.OnBeginRetime_Lambda([this]() { BeginRetime(); })
			.OnRetime_Lambda([this, TrackIndex](int32 EventIndex, float StartSeconds, float EndSeconds) { RetimeEvent(TrackIndex, EventIndex, StartSeconds, EndSeconds); })
			.OnEndRetime_Lambda([this]() { EndRetime(); })
			.OnInteractionEnd_Lambda([this]() { Rebuild(); })
			.OnBuildAddMenu_Lambda([this, TrackIndex](float TimeSeconds) { return MakeAddMenuForTrack(TrackIndex, TimeSeconds); })
			.OnBuildEventMenu_Lambda([this, TrackIndex](int32 EventIndex) { return MakeEventMenuForTrack(TrackIndex, EventIndex); })
			.OnZoom_Lambda([this](float CursorSeconds, float WheelDelta, bool bPan) { ZoomView(CursorSeconds, WheelDelta, bPan); })
			.OnPan_Lambda([this](float DeltaSeconds) { PanView(DeltaSeconds); })
			.OnMoveToTrack_Lambda([this, TrackIndex](int32 EventIndex, int32 ToTrack) { MoveEventToTrack(TrackIndex, EventIndex, ToTrack); })
			.OnActivateEvent_Lambda([this, TrackIndex](int32 EventIndex) { OpenEventNotifyAsset(TrackIndex, EventIndex); })
	];
}

void SKzDialogueTimeline::OpenEventNotifyAsset(int32 TrackIndex, int32 EventIndex)
{
	UKzDialogueTimeline* T = Timeline.Get();
	if (!T || !T->Tracks.IsValidIndex(TrackIndex) || !T->Tracks[TrackIndex].Events.IsValidIndex(EventIndex)) { return; }

	const UKzDialogueNotifyBase* Notify = T->Tracks[TrackIndex].Events[EventIndex].Notify;
	if (!IsValid(Notify)) { return; }

	// Only Blueprint notifies have an asset to open; native ones have nowhere to go.
	if (UBlueprint* Blueprint = UBlueprint::GetBlueprintFromClass(Notify->GetClass()))
	{
		GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Blueprint);
	}
}

TSharedRef<SWidget> SKzDialogueTimeline::MakeAddMenuForTrack(int32 TrackIndex, float TimeSeconds)
{
	FMenuBuilder Menu(true, nullptr);

	Menu.BeginSection(NAME_None, LOCTEXT("AddSection", "Add"));
	{
		Menu.AddSubMenu(
			LOCTEXT("AddNotify", "Add Notify..."),
			LOCTEXT("AddNotifyTip", "Add a point notify at this time."),
			FNewMenuDelegate::CreateLambda([this, TrackIndex, TimeSeconds](FMenuBuilder& SubMenu)
			{
				SubMenu.AddWidget(MakeNotifyClassPicker(UKzDialogueNotify::StaticClass(), TrackIndex, TimeSeconds), FText::GetEmpty(), true);
			}));
		Menu.AddSubMenu(
			LOCTEXT("AddNotifyState", "Add Notify State..."),
			LOCTEXT("AddNotifyStateTip", "Add a ranged state notify at this time."),
			FNewMenuDelegate::CreateLambda([this, TrackIndex, TimeSeconds](FMenuBuilder& SubMenu)
			{
				SubMenu.AddWidget(MakeNotifyClassPicker(UKzDialogueNotifyState::StaticClass(), TrackIndex, TimeSeconds), FText::GetEmpty(), true);
			}));
	}
	Menu.EndSection();

	Menu.BeginSection(NAME_None, LOCTEXT("CreateSection", "Create"));
	{
		Menu.AddMenuEntry(
			LOCTEXT("NewNotify", "New Notify..."),
			LOCTEXT("NewNotifyTip", "Create a new notify Blueprint and add it here."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([this, TrackIndex, TimeSeconds]() { CreateNewNotify(TrackIndex, UKzDialogueNotify::StaticClass(), TimeSeconds); })));
		Menu.AddMenuEntry(
			LOCTEXT("NewNotifyState", "New Notify State..."),
			LOCTEXT("NewNotifyStateTip", "Create a new notify state Blueprint and add it here."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([this, TrackIndex, TimeSeconds]() { CreateNewNotify(TrackIndex, UKzDialogueNotifyState::StaticClass(), TimeSeconds); })));
	}
	Menu.EndSection();

	return Menu.MakeWidget();
}

TSharedRef<SWidget> SKzDialogueTimeline::MakeNotifyClassPicker(UClass* BaseClass, int32 TrackIndex, float TimeSeconds)
{
	TSharedRef<FKzNotifyClassFilter> Filter = MakeShared<FKzNotifyClassFilter>();
	Filter->BaseClass = BaseClass;

	FClassViewerInitializationOptions Options;
	Options.Mode = EClassViewerMode::ClassPicker;
	Options.bShowObjectRootClass = false;
	Options.bShowUnloadedBlueprints = true;
	Options.bShowNoneOption = false;
	Options.bEnableClassDynamicLoading = true;
	Options.bExpandRootNodes = true;
	Options.NameTypeToDisplay = EClassViewerNameTypeToDisplay::DisplayName;
	Options.ClassFilters.Add(Filter);

	FClassViewerModule& ClassViewerModule = FModuleManager::LoadModuleChecked<FClassViewerModule>("ClassViewer");
	return SNew(SBox)
		.MinDesiredWidth(280.f)
		.MaxDesiredHeight(400.f)
		[
			ClassViewerModule.CreateClassViewer(Options, FOnClassPicked::CreateLambda([this, TrackIndex, TimeSeconds](UClass* PickedClass)
			{
				FSlateApplication::Get().DismissAllMenus();
				AddEventAt(TrackIndex, PickedClass, TimeSeconds);
			}))
		];
}

void SKzDialogueTimeline::CreateNewNotify(int32 TrackIndex, UClass* ParentClass, float TimeSeconds)
{
	if (!ParentClass) { return; }

	UBlueprintFactory* Factory = NewObject<UBlueprintFactory>();
	Factory->ParentClass = ParentClass;

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	UObject* NewAsset = AssetTools.CreateAssetWithDialog(UBlueprint::StaticClass(), Factory);
	if (const UBlueprint* Blueprint = Cast<UBlueprint>(NewAsset))
	{
		if (UClass* GeneratedClass = Blueprint->GeneratedClass)
		{
			AddEventAt(TrackIndex, GeneratedClass, TimeSeconds);
		}
	}
}

TSharedRef<SWidget> SKzDialogueTimeline::MakeEventMenuForTrack(int32 TrackIndex, int32 EventIndex)
{
	FMenuBuilder Menu(true, nullptr);
	Menu.BeginSection(NAME_None, LOCTEXT("NotifyActions", "Notify"));
	{
		Menu.AddMenuEntry(
			LOCTEXT("DeleteNotify", "Delete"),
			LOCTEXT("DeleteNotifyTip", "Remove this notify from the track."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([this, TrackIndex, EventIndex]() { RemoveEvent(TrackIndex, EventIndex); })));
	}
	Menu.EndSection();
	return Menu.MakeWidget();
}

void SKzDialogueTimeline::SyncEventDetails()
{
	if (!EventDetailsView.IsValid()) { return; }

	FKzDialogueNotifyEvent* LiveEvent = SelectedEvent();
	if (!LiveEvent)
	{
		EventScope.Reset();
		ScopeTrack = INDEX_NONE;
		ScopeEvent = INDEX_NONE;
		EventDetailsView->SetStructureData(nullptr);
		return;
	}

	// A fresh scope (a copy of the event) on a new selection; otherwise keep the scope the view
	// is editing and just refresh it from the live event (covers drags and other edits).
	if (!EventScope.IsValid() || ScopeTrack != SelTrack || ScopeEvent != SelEvent)
	{
		EventScope = MakeShared<FStructOnScope>(FKzDialogueNotifyEvent::StaticStruct());
		ScopeTrack = SelTrack;
		ScopeEvent = SelEvent;
		EventDetailsView->SetStructureData(EventScope);
	}
	FKzDialogueNotifyEvent::StaticStruct()->CopyScriptStruct(EventScope->GetStructMemory(), LiveEvent);
}

bool SKzDialogueTimeline::PassesFilter(FName TrackName) const
{
	if (FilterText.IsEmpty()) { return true; }
	return TrackName.ToString().Contains(FilterText.ToString());
}

FName SKzDialogueTimeline::GetNewTrackName() const
{
	UKzDialogueTimeline* T = Timeline.Get();
	TArray<FName> Names;
	if (T)
	{
		for (const FKzDialogueNotifyTrack& Track : T->Tracks) { Names.Add(Track.Name); }
	}

	FName Candidate;
	int32 Index = 1;
	do { Candidate = *FString::FromInt(Index++); } while (Names.Contains(Candidate));
	return Candidate;
}

void SKzDialogueTimeline::AddTrack()
{
	UKzDialogueTimeline* T = Timeline.Get();
	if (!T) { return; }
	const FScopedTransaction Transaction(LOCTEXT("AddTrackTransaction", "Add Notify Track"));
	T->Modify();

	FKzDialogueNotifyTrack NewTrack;
	NewTrack.Name = GetNewTrackName();
	T->Tracks.Add(NewTrack);
	bNotifiesExpanded = true;
	Modified();
	Rebuild();
}

void SKzDialogueTimeline::RemoveTrack(int32 TrackIndex)
{
	UKzDialogueTimeline* T = Timeline.Get();
	// Keep at least one track; deleting the last one is what "Delete timeline" is for.
	if (!T || !T->Tracks.IsValidIndex(TrackIndex) || T->Tracks.Num() <= 1) { return; }
	const FScopedTransaction Transaction(LOCTEXT("RemoveTrackTransaction", "Delete Notify Track"));
	T->Modify();
	T->Tracks.RemoveAt(TrackIndex);
	if (SelTrack == TrackIndex) { SelTrack = INDEX_NONE; SelEvent = INDEX_NONE; }
	else if (SelTrack > TrackIndex) { --SelTrack; }
	Modified();
	Rebuild();
}

void SKzDialogueTimeline::RenameTrack(int32 TrackIndex, const FText& NewName)
{
	UKzDialogueTimeline* T = Timeline.Get();
	if (!T || !T->Tracks.IsValidIndex(TrackIndex)) { return; }
	T->Modify();
	T->Tracks[TrackIndex].Name = FName(*NewName.ToString());
	Modified();
}

void SKzDialogueTimeline::AddEventAt(int32 TrackIndex, UClass* NotifyClass, float TimeSeconds)
{
	UKzDialogueTimeline* T = Timeline.Get();
	if (!T || !T->Tracks.IsValidIndex(TrackIndex) || !NotifyClass) { return; }
	T->Modify();

	FKzDialogueNotifyEvent NewEvent;
	NewEvent.Notify = NewObject<UKzDialogueNotifyBase>(T, NotifyClass, NAME_None, RF_Transactional);
	if (FKzDialogueTimeSource_Relative* Rel = NewEvent.TimeSource.GetMutablePtr<FKzDialogueTimeSource_Relative>())
	{
		Rel->Time = FMath::Max(0.f, TimeSeconds);

		// State notifies span a window, so give them a non-zero default duration that fits.
		if (NotifyClass->IsChildOf(UKzDialogueNotifyState::StaticClass()))
		{
			const float Remaining = FMath::Max(0.f, Duration() - Rel->Time);
			Rel->Duration = FMath::Clamp(0.5f, 0.1f, FMath::Max(0.1f, Remaining));
		}
	}

	const int32 NewIndex = T->Tracks[TrackIndex].Events.Add(NewEvent);
	SelTrack = TrackIndex;
	SelEvent = NewIndex;
	Modified();
	Rebuild();
}

void SKzDialogueTimeline::RemoveSelected()
{
	RemoveEvent(SelTrack, SelEvent);
}

void SKzDialogueTimeline::RemoveEvent(int32 TrackIndex, int32 EventIndex)
{
	UKzDialogueTimeline* T = Timeline.Get();
	if (!T || !T->Tracks.IsValidIndex(TrackIndex) || !T->Tracks[TrackIndex].Events.IsValidIndex(EventIndex)) { return; }
	const FScopedTransaction Transaction(LOCTEXT("DeleteEventTransaction", "Delete Dialogue Notify"));
	T->Modify();
	T->Tracks[TrackIndex].Events.RemoveAt(EventIndex);
	if (SelTrack == TrackIndex)
	{
		if (SelEvent == EventIndex) { SelEvent = INDEX_NONE; }
		else if (SelEvent > EventIndex) { --SelEvent; }
	}
	Modified();
	Rebuild();
}

void SKzDialogueTimeline::SetSelection(int32 TrackIndex, int32 EventIndex)
{
	// No rebuild: the track reads selection as an attribute and repaints itself, so the
	// mouse capture that drives a drag survives. The inline editor refreshes on Rebuild,
	// fired from the track's OnInteractionEnd when the mouse is released.
	SelTrack = TrackIndex;
	SelEvent = EventIndex;

	// Take keyboard focus so the Delete key can remove the selected event.
	if (EventIndex != INDEX_NONE)
	{
		FSlateApplication::Get().SetKeyboardFocus(SharedThis(this), EFocusCause::Mouse);
	}
}

void SKzDialogueTimeline::BeginRetime()
{
	UKzDialogueTimeline* T = Timeline.Get();
	if (!T) { return; }
	DragTransaction = MakeUnique<FScopedTransaction>(LOCTEXT("RetimeEventTransaction", "Retime Dialogue Notify"));
	T->Modify();
}

void SKzDialogueTimeline::RetimeEvent(int32 TrackIndex, int32 EventIndex, float StartSeconds, float EndSeconds)
{
	UKzDialogueTimeline* T = Timeline.Get();
	if (!T || !T->Tracks.IsValidIndex(TrackIndex) || !T->Tracks[TrackIndex].Events.IsValidIndex(EventIndex)) { return; }
	FKzDialogueTimeSource_Relative* Rel = T->Tracks[TrackIndex].Events[EventIndex].TimeSource.GetMutablePtr<FKzDialogueTimeSource_Relative>();
	if (!Rel) { return; }

	const float Dur = Duration();
	StartSeconds = FMath::Clamp(StartSeconds, 0.f, Dur);
	EndSeconds = FMath::Clamp(EndSeconds, StartSeconds, Dur);
	const float LenSeconds = EndSeconds - StartSeconds;

	if (Rel->bNormalized)
	{
		Rel->Time = Dur > KINDA_SMALL_NUMBER ? StartSeconds / Dur : 0.f;
		Rel->Duration = Dur > KINDA_SMALL_NUMBER ? LenSeconds / Dur : 0.f;
	}
	else
	{
		Rel->Time = StartSeconds;
		Rel->Duration = LenSeconds;
	}
	Modified();
}

void SKzDialogueTimeline::EndRetime()
{
	DragTransaction.Reset();
	Rebuild();
}

void SKzDialogueTimeline::MoveEventToTrack(int32 FromTrack, int32 EventIndex, int32 ToTrack)
{
	UKzDialogueTimeline* T = Timeline.Get();
	if (!T || T->Tracks.Num() == 0) { return; }
	ToTrack = FMath::Clamp(ToTrack, 0, T->Tracks.Num() - 1);
	if (FromTrack == ToTrack || !T->Tracks.IsValidIndex(FromTrack) || !T->Tracks[FromTrack].Events.IsValidIndex(EventIndex)) { return; }

	const FScopedTransaction Transaction(LOCTEXT("MoveEventToTrackTransaction", "Move Notify To Track"));
	T->Modify();
	const FKzDialogueNotifyEvent Moved = T->Tracks[FromTrack].Events[EventIndex];
	T->Tracks[FromTrack].Events.RemoveAt(EventIndex);
	const int32 NewIndex = T->Tracks[ToTrack].Events.Add(Moved);
	SelTrack = ToTrack;
	SelEvent = NewIndex;
	Modified();
	Rebuild();
}

void SKzDialogueTimeline::OnEventDetailsChanged(const FPropertyChangedEvent& PropertyChangedEvent)
{
	UKzDialogueTimeline* T = Timeline.Get();
	FKzDialogueNotifyEvent* LiveEvent = SelectedEvent();
	if (!T || !LiveEvent || !EventScope.IsValid()) { return; }

	// Write the edited copy back onto the live event (its time source and bFireIfSkipped; the
	// notify object is shared, so its params were already edited in place).
	T->Modify();
	FKzDialogueNotifyEvent::StaticStruct()->CopyScriptStruct(LiveEvent, EventScope->GetStructMemory());

	// If the notify class was changed via the picker, the struct-on-scope creates the new object
	// without our outer; re-home it under the timeline so it persists.
	if (LiveEvent->Notify && LiveEvent->Notify->GetOuter() != T)
	{
		LiveEvent->Notify = DuplicateObject<UKzDialogueNotifyBase>(LiveEvent->Notify, T);
	}

	Modified();
	Rebuild();
}

#undef LOCTEXT_NAMESPACE