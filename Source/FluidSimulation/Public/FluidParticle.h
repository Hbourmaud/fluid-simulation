#pragma once

struct FluidParticle
{
	FVector2D Position;
	FVector2D Velocity;
	FVector2D Force;

	float Mass;
	float Density;
	float Pressure;

	int32 TypeIndex = 0;

	static constexpr float SmoothingRadius = 8.0f;

	FluidParticle()
		: Position(FVector2D::ZeroVector)
		, Velocity(FVector2D::ZeroVector)
		, Force(FVector2D::ZeroVector)
		, Mass(1.0f)
		, Density(0.0f)
		, Pressure(0.0f)
		, TypeIndex(0)
	{
	}

	FluidParticle(const FVector2D& InPosition, float InMass = 1.0f, int32 InTypeIndex = 0)
		: Position(InPosition)
		, Velocity(FVector2D::ZeroVector)
		, Force(FVector2D::ZeroVector)
		, Mass(InMass)
		, Density(1.0f)
		, Pressure(0.0f)
		, TypeIndex(InTypeIndex)
	{
	}

	void ResetForce()
	{
		Force = FVector2D::ZeroVector;
	}

	FORCEINLINE FVector ToFVector(float PlaneZ) const
	{
		return FVector(Position.X, Position.Y, PlaneZ);
	}
};