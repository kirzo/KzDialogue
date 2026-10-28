// Copyright 2026 kirzo

#include "Graph/KzChoiceWidget.h"
#include "Graph/KzDialogueChoiceListener.h"
#include "KzSubtitleWidget.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
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
	return Listener && Listener->ChooseOption(Index);
}

void UKzChoiceWidget::Next()
{
	if (Listener)
	{
		Listener->Next();
	}
}

void UKzChoiceWidget::PreviousOption()
{
	MoveHighlight(-1);
}

void UKzChoiceWidget::NextOption()
{
	MoveHighlight(1);
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
	const int32 Index = GetDefaultOption(ShownOptions);
	const TObjectPtr<UKzChoiceOption>* Found = OptionWidgets.FindByPredicate([Index](const UKzChoiceOption* Row) { return Row && Row->GetOption().Index == Index; });
	return Found ? Found->Get() : nullptr;
}

int32 UKzChoiceWidget::GetDefaultOption_Implementation(const TArray<FKzDialogueGraphOption>& Options) const
{
	const FKzDialogueGraphOption* Marked = Options.FindByPredicate([](const FKzDialogueGraphOption& Option) { return Option.bDefault && Option.bAvailable; });
	const FKzDialogueGraphOption* Available = Options.FindByPredicate([](const FKzDialogueGraphOption& Option) { return Option.bAvailable; });
	const FKzDialogueGraphOption* Picked = Marked ? Marked : (Available ? Available : (Options.IsEmpty() ? nullptr : &Options[0]));
	return Picked ? Picked->Index : INDEX_NONE;
}

void UKzChoiceWidget::HandleShowOptions(UKzDialogueGraphInstance* Instance, const TArray<FKzDialogueGraphOption>& Options)
{
	ClearRows();
	ShownInstance = Instance;
	ShownOptions = Options;
	HighlightedPosition = INDEX_NONE;

	if (OptionsPanel && OptionClass)
	{
		for (int32 Position = 0; Position < Options.Num(); ++Position)
		{
			UKzChoiceOption* Row = CreateWidget<UKzChoiceOption>(this, OptionClass);
			if (!Row) { continue; }

			OptionsPanel->AddChild(Row);
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
	if (Position == HighlightedPosition || !ShownOptions.IsValidIndex(Position))
	{
		return;
	}

	if (OptionWidgets.IsValidIndex(HighlightedPosition)) { OptionWidgets[HighlightedPosition]->SetHighlighted(false); }
	HighlightedPosition = Position;
	if (OptionWidgets.IsValidIndex(HighlightedPosition)) { OptionWidgets[HighlightedPosition]->SetHighlighted(true); }
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