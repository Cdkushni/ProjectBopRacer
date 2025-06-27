#include "EngineComponent.h"

#include "Net/UnrealNetwork.h"

UEngineComponent::UEngineComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    SetRelativeScale3D(FVector(0.5f)); // Larger for podracer engines
}

void UEngineComponent::Initialize(const FEngineStats& Stats, const FVector& Offset)
{
    EngineStats = Stats;
    CurrentHealth = Stats.MaxHealth;
    SetRelativeLocation(Offset);
    State = EEngineState::Normal;
}

void UEngineComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (BoostTimer > 0.0f)
    {
        BoostTimer -= DeltaTime;
        if (BoostTimer <= 0.0f)
        {
            State = CurrentHealth <= 0.0f ? EEngineState::Destroyed : EEngineState::Normal;
            OnEngineStateChanged.Broadcast(this);
        }
    }
}

void UEngineComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME_CONDITION(UEngineComponent, EngineStats, COND_InitialOnly); // Only replicate once
    DOREPLIFETIME_CONDITION(UEngineComponent, State, COND_SkipOwner);
    DOREPLIFETIME_CONDITION(UEngineComponent, CurrentHealth, COND_SkipOwner);
    DOREPLIFETIME_CONDITION(UEngineComponent, BoostTimer, COND_SkipOwner);
    DOREPLIFETIME_CONDITION(UEngineComponent, bIsEnabled, COND_SkipOwner);
}

float UEngineComponent::GetThrustForce(float Input, bool bIsBoosting, bool bIsDrifting, float DriftMultiplier, float BoostMultiplier) const
{
    if (!bIsEnabled || State == EEngineState::Destroyed)
        return 0.0f;

    float Multiplier = (State == EEngineState::Boosted || bIsBoosting) ? (State == EEngineState::Boosted ? EngineStats.BoostMultiplier : BoostMultiplier) : 1.0f;
    float HealthScale = (State == EEngineState::Damaged) ? CurrentHealth / EngineStats.MaxHealth : 1.0f;
    float DriftValue = bIsDrifting ? 1.0f / DriftMultiplier : 1.0f;
    return EngineStats.ThrustForce * Input * Multiplier * HealthScale * DriftValue;
}

float UEngineComponent::GetHoverForce(float ForcePercent) const
{
    if (!bIsEnabled || State == EEngineState::Destroyed)
        return 0.0f;

    float Multiplier = (State == EEngineState::Boosted) ? EngineStats.BoostMultiplier : 1.0f;
    float HealthScale = (State == EEngineState::Damaged) ? CurrentHealth / EngineStats.MaxHealth : 1.0f;
    return EngineStats.HoverForce * ForcePercent * Multiplier * HealthScale;
}

void UEngineComponent::DamageEngine(float DamageAmount)
{
    if (!bIsEnabled || State == EEngineState::Destroyed)
        return;

    CurrentHealth = FMath::Max(0.0f, CurrentHealth - DamageAmount);
    if (CurrentHealth <= 0.0f)
    {
        State = EEngineState::Destroyed;
    }
    else if (CurrentHealth < EngineStats.MaxHealth * 0.5f)
    {
        State = EEngineState::Damaged;
    }
    OnEngineStateChanged.Broadcast(this);
}

void UEngineComponent::RepairEngine(float DeltaTime)
{
    if (!bIsEnabled || State == EEngineState::Destroyed || CurrentHealth >= EngineStats.MaxHealth)
        return;

    State = EEngineState::Repairing;
    CurrentHealth = FMath::Min(EngineStats.MaxHealth, CurrentHealth + EngineStats.RepairRate * DeltaTime);
    if (CurrentHealth >= EngineStats.MaxHealth)
    {
        State = EEngineState::Normal;
    }
    OnEngineStateChanged.Broadcast(this);
}

void UEngineComponent::BoostEngine(float Duration)
{
    if (!bIsEnabled || State == EEngineState::Destroyed)
        return;

    State = EEngineState::Boosted;
    BoostTimer = Duration;
    OnEngineStateChanged.Broadcast(this);
}

void UEngineComponent::DisableEngine()
{
    bIsEnabled = false;
    OnEngineStateChanged.Broadcast(this);
}

void UEngineComponent::EnableEngine()
{
    bIsEnabled = true;
    State = CurrentHealth <= 0.0f ? EEngineState::Destroyed : EEngineState::Normal;
    OnEngineStateChanged.Broadcast(this);
}