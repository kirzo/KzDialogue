// Copyright 2026 kirzo

#pragma once

#include "CoreMinimal.h"
#include "KzWordAsset.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "KzNamedTokenSubsystem.generated.h"

/**
 * Runtime override of one named-asset token: values chosen during play (the player's name
 * and gender, a renamed pet...) that win over the asset's authored fields. Unset pieces
 * fall through to the asset, so the authored values double as the "not chosen yet" state.
 */
USTRUCT(BlueprintType)
struct KZDIALOGUE_API FKzNamedTokenOverride
{
	GENERATED_BODY()

	/** Explicit flag: overriding to Unspecified is itself a valid choice. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Override")
	bool bOverrideGender = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Override")
	EKzGender Gender = EKzGender::Unspecified;

	/** Part texts by part name ("given" -> the typed name; "Text" for words). Pinning an atom flows into every composition; pinning a composition part replaces it outright. Player-typed values are culture invariant; picks from preset lists keep their localization. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Override")
	TMap<FName, FText> Parts;
};

/** Serializable snapshot of every token override: store it in the game's save and re-apply on load. */
USTRUCT(BlueprintType)
struct KZDIALOGUE_API FKzNamedTokenOverrides
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Override")
	TMap<FName, FKzNamedTokenOverride> Tokens;
};

/**
 * Game-instance home of named tokens: the token-to-asset registry cache and the runtime
 * overrides, both surviving level travel. A token must already be claimed by a
 * UKzNamedAsset (the asset is the promise: it declares the parts schema and the fallback
 * values); the setters reject unknown tokens. Persistence is the game's business through
 * the snapshot: GetNamedTokenOverrides into the save, ApplyNamedTokenOverrides on load.
 */
UCLASS()
class KZDIALOGUE_API UKzNamedTokenSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** Overrides the gender used to resolve the token's name parts and its ":gender" argument. False when no named asset claims Token. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Named Tokens", meta = (KzTokenPin = "Token"))
	bool SetNamedTokenGender(FName Token, EKzGender Gender);

	/** Overrides one part text ("given", "nick"; "Text" for words). Atoms flow into compositions. False when no named asset claims Token. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Named Tokens", meta = (KzTokenPin = "Token", KzTokenPartPin = "Part"))
	bool SetNamedTokenPart(FName Token, FName Part, FText Text);

	/** Drops every override of Token; the asset's authored values apply again. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Named Tokens", meta = (KzTokenPin = "Token"))
	void ClearNamedTokenOverride(FName Token);

	UFUNCTION(BlueprintCallable, Category = "Dialogue|Named Tokens")
	void ClearAllNamedTokenOverrides();

	/** Snapshot for the game's save data. */
	UFUNCTION(BlueprintPure, Category = "Dialogue|Named Tokens")
	FKzNamedTokenOverrides GetNamedTokenOverrides() const { return Overrides; }

	/** Restores a saved snapshot, replacing the current overrides. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue|Named Tokens")
	void ApplyNamedTokenOverrides(const FKzNamedTokenOverrides& InOverrides) { Overrides = InOverrides; }

	/** Override for Token, or null. Consulted by the token resolution and the subtitle speaker label. */
	const FKzNamedTokenOverride* FindOverride(FName Token) const { return Overrides.Tokens.Find(Token); }

	/** Asset path claiming Token, building the registry-scan cache on first use. Null when unclaimed. */
	const FSoftObjectPath* FindNamedAssetPath(FName Token) const;

	/** The store reachable through WorldContextObject's game instance, or null. */
	static UKzNamedTokenSubsystem* Get(const UObject* WorldContextObject);

	/** Override for Token reachable through WorldContextObject's game instance, or null (no world, no subsystem, no override). */
	static const FKzNamedTokenOverride* FindOverrideFor(const UObject* WorldContextObject, FName Token);

private:
	UPROPERTY(Transient)
	FKzNamedTokenOverrides Overrides;

	/** Named-asset tokens gathered from the asset registry on first use (session-lifetime cache; assets load lazily). */
	mutable TMap<FName, FSoftObjectPath> NamedAssetTokens;
	mutable bool bNamedAssetTokensBuilt = false;
};