// Copyright 2026 kirzo

#include "Graph/KzChoiceWidget.h"
#include "Graph/KzDialogueChoiceListener.h"
#include "KzSubtitleWidget.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Animation/WidgetAnimation.h"
#include "InputCoreTypes.h"

// ---------------------------------------------------------------------------------------
// Option row
// ---------------------------------------------------------------------------------------

void UKzChoiceOption::Setup(UKzChoiceWidget* InChoiceWidget, const FKzDialogueGraphOption& InOption, int32 Number)
{
	ChoiceWidget = InChoiceWidget;
	Option = InOption;
	if (OptionText) { OptionText->SetText(Option.Line.GetDisplayText()); }
	if (NumberText) { NumberText->SetText(FText::AsNumber(Number)); }
	ReceiveOptionSet(Option);
}

void UKzChoiceOption::SetHighlighted(bool bInHighlighted)
{
	if (bHighlighted == bInHighlighted) { return; }

	bHighlighted = bInHighlighted;
	if (bHighlighted) { ReceiveHighlighted(); }
	else { ReceiveUnhighlighted(); }
}

void UKzChoiceOption::Highlight()
{
	if (UKzChoiceWidget* Widget = ChoiceWidget.Get())
	{
		Widget->HighlightOption(Option.Index);
	}
}

bool UKzChoiceOption::Choose()
{
	UKzChoiceWidget* Widget = ChoiceWidget.Get();
	return Widget && Widget->ChooseOption(Option.Index);
}

void UKzChoiceOption::NativeConstruct()
{
	Super::NativeConstruct();

	// A user widget does not take the mouse by default.
	SetVisibility(ESlateVisibility::Visible);
}

void UKzChoiceOption::NativeOnMouseEnter(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	Super::NativeOnMouseEnter(InGeometry, InMouseEvent);
	Highlight();
}

FReply UKzChoiceOption::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
	}
	bPressed = true;
	return FReply::Handled();
}

FReply UKzChoiceOption::NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() != EKeys::LeftMouseButton || !bPressed)
	{
		return Super::NativeOnMouseButtonUp(InGeometry, InMouseEvent);
	}
	bPressed = false;
	Choose();
	return FReply::Handled();
}

// ---------------------------------------------------------------------------------------
// Choice widget
// ---------------------------------------------------------------------------------------

void UKzChoiceWidget::NativeConstruct()
{
	Super::NativeConstruct();

	TArray<FGameplayTag> Channels = ListenedChannels;
	if (Channels.IsEmpty() && bUseSubtitleChannels)
	{
		if (const UKzSubtitleWidget* Subtitles = GetTypedOuter<UKzSubtitleWidget>())
		{
			Channels = Subtitles->GetListenedChannels();
		}
	}
	if (Channels.IsEmpty())
	{
		return;
	}

	Listener = UKzDialogueChoiceListener::CreateDialogueChoiceListener(this, Channels, GetOwningPlayer());
	if (!Listener)
	{
		return;
	}
	Listener->OnShowOptions.AddDynamic(this, &UKzChoiceWidget::HandleShowOptions);
	Listener->OnHideOptions.AddDynamic(this, &UKzChoiceWidget::HandleHideOptions);

	// A conversation already choosing when the widget appears.
	if (Listener->IsChoosing())
	{
		HandleShowOptions(Listener->GetInstance(), Listener->GetOptions());
	}
}

void UKzChoiceWidget::NativeDestruct()
{
	if (Listener)
	{
		Listener->OnShowOptions.RemoveAll(this);
		Listener->OnHideOptions.RemoveAll(this);
		Listener->StopListening();
		Listener = nullptr;
	}
	ClearRows();
	ShownOptions.Reset();
	ShownInstance.Reset();
	HighlightedPosition = INDEX_NONE;
	Super::NativeDestruct();
}

bool UKzChoiceWidget::ChooseOption(int32 Index)
{
	const FKzDialogueGraphOption* Shown = ShownOptions.FindByPredicate([Index](const FKzDialogueGraphOption& Option) { return Option.Index == Index; });
	if (!Listener || PendingChoice != INDEX_NONE || !Shown || !Shown->bAvailable)
	{
		return false;
	}

	UKzChoiceOption* Row = FindRow(Index);
	if (!Row || !Row->ChosenAnimation)
	{
		if (Row) { Row->ReceiveChosen(); }
		return Listener->ChooseOption(Index);
	}

	// The conversation goes on once the row has shown it was chosen.
	PendingChoice = Index;
	Row->ReceiveChosen();
	FWidgetAnimationDynamicEvent Finished;
	Finished.BindDynamic(this, &UKzChoiceWidget::HandleChosenAnimationFinished);
	Row->BindToAnimationFinished(Row->ChosenAnimation, Finished);
	Row->PlayAnimation(Row->ChosenAnimation);
	return true;
}

void UKzChoiceWidget::HandleChosenAnimationFinished()
{
	const int32 Index = PendingChoice;
	PendingChoice = INDEX_NONE;
	if (UKzChoiceOption* Row = FindRow(Index))
	{
		Row->UnbindAllFromAnimationFinished(Row->ChosenAnimation);
	}
	if (Listener && Index != INDEX_NONE)
	{
		Listener->ChooseOption(Index);
	}
}

UKzChoiceOption* UKzChoiceWidget::FindRow(int32 Index) const
{
	const TObjectPtr<UKzChoiceOption>* Found = OptionWidgets.FindByPredicate([Index](const UKzChoiceOption* Row) { return Row && Row->GetOption().Index == Index; });
	return Found ? Found->Get() : nullptr;
}

void UKzChoiceWidget::Next()
{
	if (Listener)
	{
		Listener->Next();
	}
}

bool UKzChoiceWidget::Skip()
{
	return Listener && Listener->Skip();
}

void UKzChoiceWidget::PreviousOption()
{
	MoveHighlight(-1);
}

void UKzChoiceWidget::NextOption()
{
	MoveHighlight(1);
}

void UKzChoiceWidget::NavigateOption(EUINavigation Direction)
{
	FVector2D Axis;
	switch (Direction)
	{
		case EUINavigation::Left:		Axis = FVector2D(-1.0, 0.0); break;
		case EUINavigation::Right:		Axis = FVector2D(1.0, 0.0); break;
		case EUINavigation::Up:			Axis = FVector2D(0.0, -1.0); break;
		case EUINavigation::Down:		Axis = FVector2D(0.0, 1.0); break;
		case EUINavigation::Next:		MoveHighlight(1); return;
		case EUINavigation::Previous:	MoveHighlight(-1); return;
		default:						return;
	}

	auto GetCenter = [](const UKzChoiceOption* Row, FVector2D& OutCenter)
	{
		// A row never laid out, or hidden, has no place on screen to navigate by.
		const FGeometry& Geometry = Row->GetCachedGeometry();
		if (!Row->IsVisible() || FVector2D(Geometry.GetAbsoluteSize()).IsNearlyZero())
		{
			return false;
		}
		OutCenter = Geometry.GetAbsolutePositionAtCoordinates(FVector2f(0.5f, 0.5f));
		return true;
	};

	const UKzChoiceOption* From = ShownOptions.IsValidIndex(HighlightedPosition) ? FindRow(ShownOptions[HighlightedPosition].Index) : nullptr;
	FVector2D Origin;
	if (!From || !GetCenter(From, Origin))
	{
		MoveHighlight(1);
		return;
	}

	// The straightest row wins, so across a cross the row opposite beats a nearer one off to the side; among
	// rows as straight, the nearest. Past the edge, the straightest the other way, the farthest.
	struct FPick { const UKzChoiceOption* Row = nullptr; double Slope = 0.0; double Along = 0.0; };
	auto Consider = [](FPick& Pick, const UKzChoiceOption* Row, double Slope, double Along)
	{
		if (!Pick.Row || Slope < Pick.Slope - 0.01 || (Slope <= Pick.Slope + 0.01 && Along < Pick.Along))
		{
			Pick = { Row, Slope, Along };
		}
	};

	FPick Ahead;
	FPick Wrapped;
	for (const UKzChoiceOption* Row : OptionWidgets)
	{
		FVector2D Center;
		if (!Row || Row == From || !GetCenter(Row, Center))
		{
			continue;
		}
		const FVector2D Offset = Center - Origin;
		const double Along = FVector2D::DotProduct(Offset, Axis);
		const double Slope = FMath::Abs(FVector2D::CrossProduct(Offset, Axis)) / FMath::Max(FMath::Abs(Along), 1.0);
		if (Along > 1.0) { Consider(Ahead, Row, Slope, Along); }
		else if (Along < -1.0) { Consider(Wrapped, Row, Slope, Along); }
	}

	if (const UKzChoiceOption* Target = Ahead.Row ? Ahead.Row : (bLoopNavigation ? Wrapped.Row : nullptr))
	{
		HighlightOption(Target->GetOption().Index);
	}
}

void UKzChoiceWidget::Accept()
{
	if (ShownOptions.IsEmpty())
	{
		Next();
		return;
	}
	if (ShownOptions.IsValidIndex(HighlightedPosition) && ShownOptions[HighlightedPosition].bAvailable)
	{
		ChooseOption(ShownOptions[HighlightedPosition].Index);
	}
}

void UKzChoiceWidget::HighlightOption(int32 Index)
{
	SetHighlightedPosition(ShownOptions.IndexOfByPredicate([Index](const FKzDialogueGraphOption& Option) { return Option.Index == Index; }));
}

int32 UKzChoiceWidget::GetHighlightedOption() const
{
	return ShownOptions.IsValidIndex(HighlightedPosition) ? ShownOptions[HighlightedPosition].Index : INDEX_NONE;
}

TArray<UKzChoiceOption*> UKzChoiceWidget::GetOptionWidgets() const
{
	return ObjectPtrDecay(OptionWidgets);
}

UKzChoiceOption* UKzChoiceWidget::GetDefaultOptionWidget() const
{
	return FindRow(GetDefaultOption(ShownOptions));
}

int32 UKzChoiceWidget::GetDefaultOption_Implementation(const TArray<FKzDialogueGraphOption>& Options) const
{
	const FKzDialogueGraphOption* Marked = Options.FindByPredicate([](const FKzDialogueGraphOption& Option) { return Option.bDefault && Option.bAvailable; });
	const FKzDialogueGraphOption* Available = Options.FindByPredicate([](const FKzDialogueGraphOption& Option) { return Option.bAvailable; });
	const FKzDialogueGraphOption* Picked = Marked ? Marked : (Available ? Available : (Options.IsEmpty() ? nullptr : &Options[0]));
	return Picked ? Picked->Index : INDEX_NONE;
}

UPanelWidget* UKzChoiceWidget::GetPanelForOption_Implementation(int32 Position, const FKzDialogueGraphOption& Option) const
{
	return OptionsPanel;
}

void UKzChoiceWidget::HandleShowOptions(UKzDialogueGraphInstance* Instance, const TArray<FKzDialogueGraphOption>& Options)
{
	ClearRows();
	ShownInstance = Instance;
	ShownOptions = Options;
	HighlightedPosition = INDEX_NONE;
	PendingChoice = INDEX_NONE;

	if (OptionClass)
	{
		for (int32 Position = 0; Position < Options.Num(); ++Position)
		{
			UPanelWidget* Panel = GetPanelForOption(Position, Options[Position]);
			UKzChoiceOption* Row = Panel ? CreateWidget<UKzChoiceOption>(this, OptionClass) : nullptr;
			if (!Row) { continue; }

			Panel->AddChild(Row);
			Row->Setup(this, Options[Position], Position + 1);
			OptionWidgets.Add(Row);
		}
	}

	HighlightOption(GetDefaultOption(Options));
	ReceiveShowOptions(Instance, Options);
	OnOptionsShown.Broadcast(Instance);
}

void UKzChoiceWidget::HandleHideOptions(UKzDialogueGraphInstance* Instance)
{
	if (ShownInstance != Instance)
	{
		return;
	}

	ClearRows();
	ShownOptions.Reset();
	ShownInstance.Reset();
	HighlightedPosition = INDEX_NONE;
	PendingChoice = INDEX_NONE;
	ReceiveHideOptions(Instance);
	OnOptionsHidden.Broadcast(Instance);
}

void UKzChoiceWidget::MoveHighlight(int32 Step)
{
	const int32 Count = ShownOptions.Num();
	if (Count == 0)
	{
		return;
	}

	int32 Position = HighlightedPosition == INDEX_NONE ? 0 : HighlightedPosition + Step;
	if (Position < 0 || Position >= Count)
	{
		if (!bLoopNavigation) { return; }
		Position = (Position + Count) % Count;
	}
	SetHighlightedPosition(Position);
}

void UKzChoiceWidget::SetHighlightedPosition(int32 Position)
{
	// The mark stays on the option being chosen.
	if (Position == HighlightedPosition || !ShownOptions.IsValidIndex(Position) || PendingChoice != INDEX_NONE)
	{
		return;
	}

	// By Index: an option the layout gave no panel has no row, so rows and positions may not line up.
	if (UKzChoiceOption* Row = ShownOptions.IsValidIndex(HighlightedPosition) ? FindRow(ShownOptions[HighlightedPosition].Index) : nullptr) { Row->SetHighlighted(false); }
	HighlightedPosition = Position;
	if (UKzChoiceOption* Row = FindRow(ShownOptions[HighlightedPosition].Index)) { Row->SetHighlighted(true); }
	OnOptionHighlighted.Broadcast(ShownOptions[HighlightedPosition].Index);
}

void UKzChoiceWidget::ClearRows()
{
	for (UKzChoiceOption* Row : OptionWidgets)
	{
		if (Row) { Row->RemoveFromParent(); }
	}
	OptionWidgets.Reset();
}