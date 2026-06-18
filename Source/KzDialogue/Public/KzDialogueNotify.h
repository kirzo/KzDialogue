// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "KzDialogueNotify.generated.h"

class AActor;
class UKzDialoguePlayer;
class UKzDialogueSpeakerComponent;

/**
 * Runtime context handed to every notify call. The player resolves the effective
 * target before invoking, so notifies never deal with speaker overrides themselves.
 * The current line is reachable through Player->GetCurrentLine() when a notify needs it.
 */
USTRUCT(BlueprintType)
struct KZDIALOGUE_API FKzDialogueNotifyContext
{
	GENERATED_BODY()

	/** Player driving the line. */
	UPROPERTY(BlueprintReadOnly, Category = "Dialogue|Notify")
	TObjectPtr<UKzDialoguePlayer> Player = nullptr;

	/** Speaker of the line being played. Null for narration. */
	UPROPERTY(BlueprintReadOnly, Category = "Dialogue|Notify")
	TObjectPtr<UKzDialogueSpeakerComponent> LineSpeaker = nullptr;

	/** Effective target speaker (the notify's override if set, else the line speaker). */
	UPROPERTY(BlueprintReadOnly, Category = "Dialogue|Notify")
	TObjectPtr<UKzDialogueSpeakerComponent> TargetSpeaker = nullptr;

	/** Actor owning TargetSpeaker. The thing a notify usually acts on. */
	UPROPERTY(BlueprintReadOnly, Category = "Dialogue|Notify")
	TObjectPtr<AActor> TargetActor = nullptr;

	/** Resolved line duration, in seconds. */
	UPROPERTY(BlueprintReadOnly, Category = "Dialogue|Notify")
	float LineDuration = 0.0f;

	/** Current line time at this call, in seconds. */
	UPROPERTY(BlueprintReadOnly, Category = "Dialogue|Notify")
	float CurrentTime = 0.0f;

	/** Event start, in seconds (baked). */
	UPROPERTY(BlueprintReadOnly, Category = "Dialogue|Notify")
	float EventStart = 0.0f;

	/** Event end, in seconds (baked). Equals EventStart for point notifies. */
	UPROPERTY(BlueprintReadOnly, Category = "Dialogue|Notify")
	float EventEnd = 0.0f;
};

/**
 * Shared base for dialogue notifies. Holds only what point and state notifies have in
 * common; the firing contract lives in the two subclasses.
 */
UCLASS(Abstract, EditInlineNew, Blueprintable)
class KZDIALOGUE_API UKzDialogueNotifyBase : public UObject
{
	GENERATED_BODY()

public:
	/** Optional target override. When unset, the notify acts on the line's speaker. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Notify", meta = (Categories = "Dialogue.Speaker"))
	FGameplayTag TargetSpeakerOverride;

	/** Label shown on the timeline marker, the add menu and debug overlays. */
	virtual FText GetNotifyName() const;

	/** Cached by the player driving this notify so GetWorld() resolves at runtime (for blueprint
	 * world-context nodes, spawning, timers). The context still carries the player per call. */
	void SetOwningPlayer(UKzDialoguePlayer* InPlayer);

	//~ UObject
	virtual UWorld* GetWorld() const override;

#if WITH_EDITORONLY_DATA
	/** Tint of this notify's marker in the timeline editor. */
	UPROPERTY(EditAnywhere, Category = "Notify")
	FLinearColor NotifyColor = FLinearColor(0.46f, 0.62f, 0.85f);
#endif

#if WITH_EDITOR
	/** Marker tint in the editor timeline. */
	virtual FLinearColor GetEditorColor() const { return NotifyColor; }

	/** Editor-only: append config errors (e.g. an unset asset) so the asset validator can report them. */
	virtual void ValidateNotify(TArray<FText>& OutErrors) const {}
#endif

private:
	/** Runtime-only back-reference to the driving player; weak so it never keeps it alive. */
	TWeakObjectPtr<UKzDialoguePlayer> OwningPlayer;
};

/**
 * Point notify: fires once when the line's playhead crosses its time. Use for
 * fire-and-forget effects (a one-shot sfx, a self-terminating camera shake).
 */
UCLASS(Abstract, EditInlineNew, Blueprintable)
class KZDIALOGUE_API UKzDialogueNotify : public UKzDialogueNotifyBase
{
	GENERATED_BODY()

public:
	/** Fired once at the event's time. */
	UFUNCTION(BlueprintNativeEvent, Category = "Notify")
	void Notify(const FKzDialogueNotifyContext& Context);
	virtual void Notify_Implementation(const FKzDialogueNotifyContext& Context) {}
};

/**
 * State notify: bounded to a [start, end] window. Begin and End are guaranteed to pair
 * even if the line is cut short, so End must undo whatever Begin did. Use for effects
 * the line owns: a held montage, a mood, a look-at.
 */
UCLASS(Abstract, EditInlineNew, Blueprintable)
class KZDIALOGUE_API UKzDialogueNotifyState : public UKzDialogueNotifyBase
{
	GENERATED_BODY()

public:
	/** Entered the window. */
	UFUNCTION(BlueprintNativeEvent, Category = "Notify")
	void NotifyBegin(const FKzDialogueNotifyContext& Context);
	virtual void NotifyBegin_Implementation(const FKzDialogueNotifyContext& Context) {}

	/** Called each frame while inside the window. */
	UFUNCTION(BlueprintNativeEvent, Category = "Notify")
	void NotifyTick(const FKzDialogueNotifyContext& Context, float DeltaTime);
	virtual void NotifyTick_Implementation(const FKzDialogueNotifyContext& Context, float DeltaTime) {}

	/** Left the window, or the line ended. Must undo whatever Begin did. */
	UFUNCTION(BlueprintNativeEvent, Category = "Notify")
	void NotifyEnd(const FKzDialogueNotifyContext& Context);
	virtual void NotifyEnd_Implementation(const FKzDialogueNotifyContext& Context) {}
};