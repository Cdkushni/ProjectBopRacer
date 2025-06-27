// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "EngineComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnEngineStateChanged, UEngineComponent*, Engine);

UENUM(BlueprintType)
enum class EEngineState : uint8
{
	Normal,
	Damaged,
	Destroyed,
	Repairing,
	Boosted
};

USTRUCT(BlueprintType)
struct FEngineStats : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Engine")
	FName EngineName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Engine")
	float ThrustForce = 250000.0f; // 2 engines = 500000.0f

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Engine")
	float HoverForce = 200000.0f; // 2 engines = 400000.0f

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Engine")
	float MaxHealth = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Engine")
	float RepairRate = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Engine")
	float BoostMultiplier = 3.0f; // Matches AHoverRacer
};

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class PROJECTPODRACER_API UEngineComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	// Sets default values for this component's properties
    UEngineComponent();

    UFUNCTION(BlueprintCallable, Category = "Engine")
    void Initialize(const FEngineStats& Stats, const FVector& Offset);

    UFUNCTION(BlueprintCallable, Category = "Engine")
    float GetThrustForce(float Input, bool bIsBoosting, bool bIsDrifting, float DriftMultiplier, float BoostMultiplier) const;

    UFUNCTION(BlueprintCallable, Category = "Engine")
    float GetHoverForce(float ForcePercent) const;

    UFUNCTION(BlueprintCallable, Category = "Engine")
    FVector GetForceApplicationPoint() const { return GetComponentLocation(); }

    UFUNCTION(BlueprintCallable, Category = "Engine")
    void DamageEngine(float DamageAmount);

    UFUNCTION(BlueprintCallable, Category = "Engine")
    void RepairEngine(float DeltaTime);

    UFUNCTION(BlueprintCallable, Category = "Engine")
    void BoostEngine(float Duration);

    UFUNCTION(BlueprintCallable, Category = "Engine")
    void DisableEngine();

    UFUNCTION(BlueprintCallable, Category = "Engine")
    void EnableEngine();

    UFUNCTION(BlueprintCallable, Category = "Engine")
    EEngineState GetState() const { return State; }

    UFUNCTION(BlueprintCallable, Category = "Engine")
    float GetHealth() const { return CurrentHealth; }

	UPROPERTY(BlueprintAssignable, Category = "Engine")
	FOnEngineStateChanged OnEngineStateChanged;

protected:
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
    UPROPERTY(EditAnywhere, Transient, Replicated, Category = "Engine")
    FEngineStats EngineStats;

    UPROPERTY(VisibleAnywhere, Transient, Replicated, Category = "Engine")
    EEngineState State = EEngineState::Normal;

    UPROPERTY(VisibleAnywhere, Transient, Replicated, Category = "Engine")
    float CurrentHealth;

    UPROPERTY(VisibleAnywhere, Transient, Replicated, Category = "Engine")
    float BoostTimer;

    UPROPERTY(VisibleAnywhere, Transient, Replicated, Category = "Engine")
    bool bIsEnabled = true;
};