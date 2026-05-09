#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FluidParticle.h"
#include "SpatialHash.h"
#include "FluidSimulator.generated.h"

UCLASS()
class FLUIDSIMULATION_API AFluidSimulator : public AActor
{
	GENERATED_BODY()

public:
	AFluidSimulator();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid")
	int32 NumParticles = 2000;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid")
	FVector2D SpawnAreaMin = FVector2D(-500.0f, -500.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid")
	FVector2D SpawnAreaMax = FVector2D(500.0f, 500.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid")
	FVector2D Gravity = FVector2D(0.0f, -980.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fluid")
	float DampeningFactor = 0.98f;

	// SPH
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SPH")
	float SmoothingRadius = 24.0f;

	// temp
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SPH")
	float ParticleThickness = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SPH")
	float RestDensity = 0.001f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SPH")
	float Viscosity = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SPH")
	float TaitK = 2000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SPH")
	float TaitGamma = 7.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SPH")
	bool Use3DKernels = true;

	// temp ? spatial hash ?
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation")
	float SubstepDt = 0.008333f;

	// debug
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
	bool IsVisualize = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
	float DebugSphereRadius = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
	float PlaneZ = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
	bool bClampToGround = false;

private:
	TArray<FluidParticle> Particles;

	void InitializeParticles();
	void UpdateParticles(float DeltaTime);
	void ComputeDensityPressure();
	void ComputeForces();
	void Integrate(float Dt);

	void VisualizeParticles() const;

	TArray<float> Densities;
	TArray<float> Pressures;

	FSpatialHash SpatialHash;
};