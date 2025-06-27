// Uses a PID (Proportional Integral Derivative) Controller to maintain hover without bouncing from push off force
#pragma once

#include "CoreMinimal.h"
#include "PIDController.generated.h"

USTRUCT(BlueprintType)
struct FPIDController
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PID")
	float PCoeff = 0.05f;//0.3f;//0.8f; // Lowered for stability

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PID")
	float ICoeff = 0.000005f;//0.0005f;//0.0002f; // Adjusted for steady-state

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PID")
	float DCoeff = 6.0f;//1.0f;//0.2f; // Increased for damping

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PID")
	float Minimum = -0.15f;//-1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PID")
	float Maximum = 0.15f;//1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PID")
	float IntegralClamp = 10.0f;//100.0f; // Prevent windup

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PID")
	float LerpAlpha = 0.98f;//0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PID|Debug")
	bool bEnableDebugLogging = true;
	
	float Integral = 0.0f;
	float LastProportional = 0.0f;
	float LastOutput = 0.0f;

public:
	float Seek(float SeekValue, float CurrentValue, float DeltaTime)
	{
		float Error = SeekValue - CurrentValue;
		float Derivative = 0.0f;
		if (DeltaTime > KINDA_SMALL_NUMBER)
		{
			Derivative = (Error - LastProportional) / DeltaTime;
		}
		Integral = FMath::Clamp(Integral + Error * DeltaTime, -IntegralClamp, IntegralClamp);
		LastProportional = Error;

		float Value = PCoeff * Error + ICoeff * Integral + DCoeff * Derivative;
		Value = FMath::Clamp(Value, Minimum, Maximum);
		Value = FMath::Lerp(LastOutput, Value, LerpAlpha);
		LastOutput = Value;

		if (bEnableDebugLogging)
		{
			UE_LOG(LogTemp, Log, TEXT("PID Seek: Error=%.1f, Integral=%.1f, Derivative=%.1f, Output=%.3f"),
				Error, Integral, Derivative, Value);
		}
		return Value;
		/*
		float Proportional = SeekValue - CurrentValue;
		float Derivative = (Proportional - LastProportional) / DeltaTime;
		Integral = FMath::Clamp(Integral + Proportional * DeltaTime, -IntegralClamp, IntegralClamp);
		LastProportional = Proportional;

		float Value = PCoeff * Proportional + ICoeff * Integral + DCoeff * Derivative;
		Value = FMath::Clamp(Value, Minimum, Maximum);

		// Smooth output
		Value = FMath::Lerp(LastOutput, Value, LerpAlpha);
		LastOutput = Value;

		return Value;
		*/
	}

	void Reset()
	{
		Integral = 0.0f;
		LastProportional = 0.0f;
		LastOutput = 0.0f;
	}
};