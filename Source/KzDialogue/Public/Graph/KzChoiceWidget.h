// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameplayTagContainer.h"
#include "Graph/KzDialogueGraphInstance.h"
#include "KzChoiceWidget.generated.h"

class UPanelWidget;
class UTextBlock;
class UKzChoiceWidget;
class UKzDialogueChoiceListener;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FKzOnChoiceWidgetOptions, UKzDialogueGraphInstance*, Instance);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FKzOnChoiceWidgetHighlight, int32, Index);

/**
 * One option of a UKzChoiceWidget, made for each option shown. Name its texts OptionText and NumberText,
 * both optional. The mouse over it marks it and a click chooses it; Highlight and Choose let another
 * navigation (a KzUI box through its selectable interface) drive it the same way.
 */
UCLASS(Abstract)
class KZDIALOGUE_API UKzChoiceOption : public UUserWidget
{
	GENERATED_BODY()

public:
	/** The option it shows. */
	UFUNCTION(BlueprintPure, Category = "Dialogue|Choices")
	const FKzDialogueGraphOption& GetOption() const { return Option; }

	UFUNCTION(BlueprintPure, Category = "Dialogue|Choices")
	bool IsHighlighted() const { return bHighlighted; }

	/** Marks this option in its choice widget, as the navigation and the mouse do. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Choices")
	void Highlight();

	/** Chooses this option. False when it is not available. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Choices")
	bool Choose();

	/** Called by the choice widget that made it; Number is its place on screen, from 1. */
	void Setup(UKzChoiceWidget* InChoiceWidget, const FKzDialogueGraphOption& InOption, int32 Number);
	void SetHighlighted(bool bInHighlighted);

protected:
	UPROPERTY(BlueprintReadOnly, Category = "Dialogue|Choices", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> OptionText;

	UPROPERTY(BlueprintReadOnly, Category = "Dialogue|Choices", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> NumberText;

	/** The option arrived, its texts already set: the place to style an unavailable one. */
	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "On Option Set"))
	void ReceiveOptionSet(const FKzDialogueGraphOption& InOption);

	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "On Highlighted"))
	void ReceiveHighlighted();

	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "On Unhighlighted"))
	void ReceiveUnhighlighted();

	virtual void NativeConstruct() override;
	virtual void NativeOnMouseEnter(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

private:
	TWeakObjectPtr<UKzChoiceWidget> ChoiceWidget;
	FKzDialogueGraphOption Option;
	bool bHighlighted = false;

	/** Only a release over the option the press started on chooses it. */
	bool bPressed = false;
};

/**
 * Shows the options of the conversations of its channels, through a choice listener, and navigates them
 * itself: whoever has the input (KzUI, Enhanced Input, CommonUI) calls Previous Option, Next Option and
 * Accept. With OptionsPanel and OptionClass it makes a row per option; On Show Options is there to lay
 * them out by hand instead.
 */
UCLASS(Abstract)
class KZDIALOGUE_API UKzChoiceWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Chooses one of the options shown, by its Index. False when none are shown or that option is not available. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Choices")
	bool ChooseOption(int32 Index);

	/** Advances the line of the conversation, for a "press A to go on" in Manual mode. Nothing while choosing. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Choices")
	void Next();

	/** Moves the mark to the option before; with Loop Navigation it wraps around. Unavailable options are marked too, as in KzUI. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Choices")
	void PreviousOption();

	/** Moves the mark to the option after; with Loop Navigation it wraps around. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Choices")
	void NextOption();

	/** With options shown, chooses the marked one (nothing when it is unavailable); without, advances the line. One button for both. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Choices")
	void Accept();

	/** Marks an option shown, by its Index. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Choices")
	void HighlightOption(int32 Index);

	/** The Index of the marked option; INDEX_NONE when none are shown. */
	UFUNCTION(BlueprintPure, Category = "Dialogue|Choices")
	int32 GetHighlightedOption() const;

	/** The play whose options are shown; null when none are. */
	UFUNCTION(BlueprintPure, Category = "Dialogue|Choices")
	UKzDialogueGraphInstance* GetShownInstance() const { return ShownInstance.Get(); }

	/** The listener it shows options through, for its conversation events and its Next. */
	UFUNCTION(BlueprintPure, Category = "Dialogue|Choices")
	UKzDialogueChoiceListener* GetListener() const { return Listener; }

	/** The rows made for the options shown, in order. For a navigation from outside, such as a KzUI box's Get Options. */
	UFUNCTION(BlueprintPure, Category = "Dialogue|Choices")
	TArray<UKzChoiceOption*> GetOptionWidgets() const;

	/** The row of the option Get Default Option picks. */
	UFUNCTION(BlueprintPure, Category = "Dialogue|Choices")
	UKzChoiceOption* GetDefaultOptionWidget() const;

	/** Options are shown, their rows already made: a navigation from outside refreshes here. */
	UPROPERTY(BlueprintAssignable, Category = "Dialogue|Choices")
	FKzOnChoiceWidgetOptions OnOptionsShown;

	/** The options shown are gone, their rows already removed. */
	UPROPERTY(BlueprintAssignable, Category = "Dialogue|Choices")
	FKzOnChoiceWidgetOptions OnOptionsHidden;

	/** The mark moved to the option with this Index. */
	UPROPERTY(BlueprintAssignable, Category = "Dialogue|Choices")
	FKzOnChoiceWidgetHighlight OnOptionHighlighted;

protected:
	/** Channels whose conversations it shows, matched hierarchically. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue|Choices", meta = (Categories = "Dialogue.Channel"))
	TArray<FGameplayTag> ListenedChannels;

	/** Without Listened Channels, take those of the subtitle widget it sits in. Without either, it listens to nothing. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue|Choices")
	bool bUseSubtitleChannels = true;

	/** Previous Option and Next Option wrap around the ends. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue|Choices")
	bool bLoopNavigation = false;

	/** Panel the option rows go in, in order (normally a Vertical Box). Optional. */
	UPROPERTY(BlueprintReadOnly, Category = "Dialogue|Choices", meta = (BindWidgetOptional))
	TObjectPtr<UPanelWidget> OptionsPanel;

	/** The row made for each option shown in OptionsPanel. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue|Choices")
	TSubclassOf<UKzChoiceOption> OptionClass;

	/** The Index of the option marked when they show: the one the Choice marks Default when it is available, else the first available. */
	UFUNCTION(BlueprintNativeEvent, BlueprintPure, Category = "Dialogue|Choices")
	int32 GetDefaultOption(const TArray<FKzDialogueGraphOption>& Options) const;
	virtual int32 GetDefaultOption_Implementation(const TArray<FKzDialogueGraphOption>& Options) const;

	/** Options to show, rows already made. Each one's Index is what Choose Option takes; unavailable ones are there only to be shown disabled. */
	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "On Show Options"))
	void ReceiveShowOptions(UKzDialogueGraphInstance* Instance, const TArray<FKzDialogueGraphOption>& Options);

	/** The options shown are gone: one was chosen, the time ran out, or the conversation ended. */
	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "On Hide Options"))
	void ReceiveHideOptions(UKzDialogueGraphInstance* Instance);

	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

private:
	UFUNCTION()
	void HandleShowOptions(UKzDialogueGraphInstance* Instance, const TArray<FKzDialogueGraphOption>& Options);

	UFUNCTION()
	void HandleHideOptions(UKzDialogueGraphInstance* Instance);

	/** Moves the mark by Step places, wrapping with Loop Navigation. */
	void MoveHighlight(int32 Step);

	/** Marks the option at Position in ShownOptions. */
	void SetHighlightedPosition(int32 Position);

	void ClearRows();

	UPROPERTY(Transient)
	TObjectPtr<UKzDialogueChoiceListener> Listener;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UKzChoiceOption>> OptionWidgets;

	TArray<FKzDialogueGraphOption> ShownOptions;
	TWeakObjectPtr<UKzDialogueGraphInstance> ShownInstance;

	/** Place in ShownOptions of the marked option. */
	int32 HighlightedPosition = INDEX_NONE;
};