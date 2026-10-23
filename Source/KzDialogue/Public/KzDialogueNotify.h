// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "KzDialogueNotify.generated.h"

struct FInstancedStruct;
class AActor;
class UKzDialoguePlayer;
class UKzDialogueSpeakerComponent;
class UKzSpeakerAsset;

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

	/** NotifyEnd, or a skipped point firing late: the line was cut short (skipped, stopped, interrupted) instead of running its course. */
	UPROPERTY(BlueprintReadOnly, Category = "Dialogue|Notify")
	bool bCutShort = false;

	/** A state notify's per-playback data, kept from NotifyBegin to NotifyEnd: what Begin started, so End undoes exactly that. C++ only, valid during the call. */
	FInstancedStruct* InstanceData = nullptr;
};

/**
 * Shared base for dialogue notifies. Holds only what point and state notifies have in
 * common; the firing contract lives in the two subclasses, so Blueprints derive from those.
 * One instance lives on the asset's timeline and serves every playback of the line, in
 * every world: keep per-playback state out of member variables.
 */
UCLASS(Abstract, EditInlineNew, NotBlueprintable)
class KZDIALOGUE_API UKzDialogueNotifyBase : public UObject
{
	GENERATED_BODY()

public:
	/** Optional target override. When unset, the notify acts on the line's speaker. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Notify")
	TObjectPtr<UKzSpeakerAsset> TargetSpeakerOverride;

	/** Label shown on the timeline marker, the add menu and debug overlays. Defaults to the class name. */
	UFUNCTION(BlueprintNativeEvent, Category = "Dialogue|Notify")
	FText GetNotifyName() const;
	virtual FText GetNotifyName_Implementation() const;

	/** Set by the player right before each call, so GetWorld() resolves to the world of the playback being served (blueprint world-context nodes, spawning, timers). */
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
	UFUNCTION(BlueprintNativeEvent, Category = "Dialogue|Notify")
	void Notify(const FKzDialogueNotifyContext& Context);
	virtual void Notify_Implementation(const FKzDialogueNotifyContext& Context) {}
};

/**
 * State notify: bounded to a [start, end] window. Begin and End are guaranteed to pair
 * even if the line is cut short, so End must undo whatever Begin did; what Begin needs to remember for that
 * goes in Context.InstanceData, since the instance is shared. Use for effects the line owns: a held montage,
 * a mood, a look-at.
 */
UCLASS(Abstract, EditInlineNew, Blueprintable)
class KZDIALOGUE_API UKzDialogueNotifyState : public UKzDialogueNotifyBase
{
	GENERATED_BODY()

public:
	/** Entered the window. */
	UFUNCTION(BlueprintNativeEvent, Category = "Dialogue|Notify")
	void NotifyBegin(const FKzDialogueNotifyContext& Context);
	virtual void NotifyBegin_Implementation(const FKzDialogueNotifyContext& Context) {}

	/** Called each frame while inside the window. */
	UFUNCTION(BlueprintNativeEvent, Category = "Dialogue|Notify")
	void NotifyTick(const FKzDialogueNotifyContext& Context, float DeltaTime);
	virtual void NotifyTick_Implementation(const FKzDialogueNotifyContext& Context, float DeltaTime) {}

	/** Left the window, or the line ended. Must undo whatever Begin did. */
	UFUNCTION(BlueprintNativeEvent, Category = "Dialogue|Notify")
	void NotifyEnd(const FKzDialogueNotifyContext& Context);
	virtual void NotifyEnd_Implementation(const FKzDialogueNotifyContext& Context) {}
};