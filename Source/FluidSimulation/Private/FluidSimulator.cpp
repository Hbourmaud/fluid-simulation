#include "FluidSimulator.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Async/ParallelFor.h"

static FORCEINLINE float Poly6Kernel3D(float r, float h)
{
	if (r < 0.0f || r > h) {
		return 0.0f;
	}

	const float h2 = h * h;
	const float r2 = r * r;
	const float x = (h2 - r2);

	const float coeff = 315.0f / (64.0f * PI * FMath::Pow(h, 9));

	return coeff * x * x * x;
}

static FORCEINLINE FVector2D SpikyGrad3D(const FVector2D& rVec, float r, float h)
{
	if (r <= 0.0f || r > h) {
		return FVector2D::ZeroVector;
	}

	const float coeff = -45.0f / (PI * FMath::Pow(h, 6));
	const float magOverR = coeff * (h - r) * (h - r) / r;

	return rVec * magOverR;
}

static FORCEINLINE float ViscosityLaplacian3D(float r, float h)
{
	if (r < 0.0f || r > h) {
		return 0.0f;
	}

	return 45.0f / (PI * FMath::Pow(h, 6)) * (h - r);
}

AFluidSimulator::AFluidSimulator()
{
	PrimaryActorTick.bCanEverTick = true;
}

void AFluidSimulator::BeginPlay()
{
	Super::BeginPlay();

	UE_LOG(LogTemp, Log, TEXT("NumParticles=%d, SpawnAreaMin=%s, SpawnAreaMax=%s, Gravity=%s, Dampening=%f"),
		NumParticles, *SpawnAreaMin.ToString(), *SpawnAreaMax.ToString(), *Gravity.ToString(), DampeningFactor);

	InitializeParticles();
}

void AFluidSimulator::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	UpdateParticles(DeltaTime);

	if (IsVisualize) {
		VisualizeParticles();
	}
}

void AFluidSimulator::InitializeParticles()
{
	Particles.Empty();
	Particles.Reserve(NumParticles);

	const float width = SpawnAreaMax.X - SpawnAreaMin.X;
	const float height = SpawnAreaMax.Y - SpawnAreaMin.Y;

	const float area = FMath::Abs(width * height);

	const float particleArea = (NumParticles > 0) ? (area / float(NumParticles)) : 1.0f;

	const float baseSpacing = FMath::Sqrt(FMath::Max(1e-6f, particleArea));

	const float xSpacing = baseSpacing * 0.9f;
	const float ySpacing = xSpacing * 0.8f;

	const int32 cols = FMath::Max(1,FMath::FloorToInt(width / xSpacing));

	const int32 rows = FMath::Max(1, FMath::FloorToInt(height / ySpacing));

	const int32 splitRow = FMath::FloorToInt(rows * (1.0f - SecondFluidFraction));

	const float totalWidth = cols * xSpacing;
	const float totalHeight = rows * ySpacing;

	const float offsetX = (width - totalWidth) * 0.5f;
	const float offsetY = (height - totalHeight) * 0.5f;

	FRandomStream Rand(12345);

	int32 placed = 0;

	int32 typeACount = 0;
	int32 typeBCount = 0;

	for (int32 row = 0; row < rows && placed < NumParticles; ++row)	{
		const float rowOffset = (row % 2 == 0) ? 0.0f : xSpacing * 0.5f;

		for (int32 col = 0; col < cols && placed < NumParticles; ++col)	{
			float posX = SpawnAreaMin.X + offsetX + rowOffset + (col + 0.5f) * xSpacing;

			float posY = SpawnAreaMin.Y + offsetY + (row + 0.5f) * ySpacing;

			posX += Rand.FRandRange(-0.05f * xSpacing, 0.05f * xSpacing);
			posY += Rand.FRandRange(-0.05f * ySpacing, 0.05f * ySpacing);

			const FVector2D pos(posX, posY);

			const int32 typeIdx = (FluidTypes.Num() > 1 && row >= splitRow) ? 1 : 0;

			if (typeIdx == 0) {
				typeACount++;
			} else {
				typeBCount++;
			}

			const float restDensity = FluidTypes.IsValidIndex(typeIdx) ? FluidTypes[typeIdx].RestDensity : RestDensity;

			const float mass2D = restDensity * particleArea;

			Particles.Emplace(pos, mass2D, typeIdx);

			placed++;
		}
	}

	Densities.SetNumZeroed(Particles.Num());
	Pressures.SetNumZeroed(Particles.Num());

	SpatialHash = FSpatialHash(SmoothingRadius);
	SpatialHash.Rebuild(Particles);
}

void AFluidSimulator::UpdateParticles(float DeltaTime)
{
	float remaining = DeltaTime;

	while (remaining > KINDA_SMALL_NUMBER) {
		const float dt = FMath::Min(SubstepDt, remaining);

		SpatialHash.Rebuild(Particles);

		ComputeDensityPressure();
		ComputeForces();
		Integrate(dt);
		remaining -= dt;
	}
}

void AFluidSimulator::ComputeDensityPressure()
{
	const int32 N = Particles.Num();

	if (N == 0) {
		return;
	}

	const float h = SmoothingRadius;
	const float h2 = h * h;
	const float h3 = h * h * h;
	const float h6 = h3 * h3;
	const float h9 = h6 * h3;
	const float poly6Coeff = 315.0f / (64.0f * PI * h9);

	Densities.SetNumUninitialized(N);
	Pressures.SetNumUninitialized(N);

	// to refacto
	ParallelFor(N, [this, h, h2, poly6Coeff](int32 i) {
		// avoid allocations per iteration
		static thread_local TArray<int32> Neighbors;

		if (Neighbors.GetData() == nullptr) {
			Neighbors.Reserve(32);
		} else {
			Neighbors.Reset();
		}

		float rho = 0.0f;
		const FVector2D pi = Particles[i].Position;

		SpatialHash.QueryNeighbors(pi, h, Neighbors);

		for (int32 j : Neighbors) {
			const FVector2D pj = Particles[j].Position;
			const FVector2D rij = pi - pj;
			const float r2 = rij.SizeSquared();

			if (r2 > h2) {
				continue;
			}

			const float m3D = Particles[j].Mass * ParticleThickness;
			const float x = (h2 - r2);
			const float w = poly6Coeff * x * x * x;
			rho += m3D * w;
		}

		if (rho <= KINDA_SMALL_NUMBER) {
			rho = KINDA_SMALL_NUMBER;
		}

		Densities[i] = rho;
		Particles[i].Density = rho;

		const int32 tIdx = Particles[i].TypeIndex;
		const float restRho = FluidTypes.IsValidIndex(tIdx) ? FluidTypes[tIdx].RestDensity : RestDensity;
		const float k = FluidTypes.IsValidIndex(tIdx) ? FluidTypes[tIdx].TaitK : TaitK;

		Pressures[i] = k * (FMath::Pow(rho / restRho, TaitGamma) - 1.0f);
		Particles[i].Pressure = Pressures[i];
	});
}

void AFluidSimulator::ComputeForces()
{
	const int32 N = Particles.Num();

	if (N == 0) {
		return;
	}

	const float h = SmoothingRadius;
	const float h2 = h * h;
	const float h3 = h * h * h;
	const float h6 = h3 * h3;
	const float spikyCoeff = -45.0f / (PI * h6);
	const float viscLapCoeff = 45.0f / (PI * h6);

	ParallelFor(N, [this](int32 i) {
		Particles[i].ResetForce();
	});


	ParallelFor(N, [this, h, h2, spikyCoeff, viscLapCoeff](int32 i) {
		// avoid allocations per iteration
		static thread_local TArray<int32> Neighbors;

		if (Neighbors.GetData() == nullptr) {
			Neighbors.Reserve(32);
		} else {
			Neighbors.Reset();
		}

		FVector2D fPressure = FVector2D::ZeroVector;
		FVector2D fVisc = FVector2D::ZeroVector;

		const FVector2D pi = Particles[i].Position;
		const FVector2D vi = Particles[i].Velocity;
		const float rhoi = Densities[i];
		const float piPressure = Pressures[i];
		const int32 typeI = Particles[i].TypeIndex;
		const float mui = FluidTypes.IsValidIndex(typeI) ? FluidTypes[typeI].Viscosity : Viscosity;

		// a way too avoid two queryneighbors call etc ?

		SpatialHash.QueryNeighbors(pi, h, Neighbors);

		for (int32 j : Neighbors) {
			if (i == j) {
				continue;
			}

			const FVector2D pj = Particles[j].Position;
			const FVector2D rij = pi - pj;
			const float r2 = rij.SizeSquared();

			if (r2 <= KINDA_SMALL_NUMBER || r2 > h2) {
				continue;
			}

			const float r = FMath::Sqrt(r2);
			const float m3D = Particles[j].Mass * ParticleThickness;
			const float rhj = Densities[j];

			if (rhj <= KINDA_SMALL_NUMBER) {
				continue;
			}

			const float invR = 1.0f / r;
			const float gradMag = spikyCoeff * (h - r) * (h - r);

			const FVector2D gradW = SpikyGrad3D(rij, r, h);

			fPressure += -m3D * ((piPressure / (rhoi * rhoi)) + (Pressures[j] / (rhj * rhj))) *	gradW;

			const int32 typeJ = Particles[j].TypeIndex;

			const float muj = FluidTypes.IsValidIndex(typeJ) ? FluidTypes[typeJ].Viscosity : Viscosity;

			const float muPair = 0.5f * (mui + muj);

			const FVector2D velDiff = Particles[j].Velocity - vi;

			const float lap = viscLapCoeff * (h - r);

			fVisc += muPair * m3D * (velDiff / rhj) * lap;

			if (typeI != typeJ)	{
				const FVector2D rHat = rij / r;

				const float repelStrength = 700.0f;
				const float repel = repelStrength * (1.0f - r / h);

				fPressure += rHat * repel;
			}
		}

		const float mass3D_i = Particles[i].Mass * ParticleThickness;
		fPressure *= mass3D_i;
		fVisc *= mass3D_i;

		const FVector2D fGravity = Gravity * mass3D_i;

		// Boundary handling

		FVector2D fBoundaryForce = FVector2D::ZeroVector;

		if (UseBoundaryForces) {
			const float thresh = BoundaryThickness;

			{
				const float dist = pi.X - SpawnAreaMin.X;
				if (dist < thresh) {
					const float depth = thresh - dist;
					const FVector2D normal(1.0f, 0.0f);
					const float velN = FVector2D::DotProduct(vi, normal);
					const FVector2D acc = BoundaryStiffness * depth * normal - BoundaryDamping * velN * normal;
					fBoundaryForce += acc * mass3D_i;
				}
			}

			{
				const float dist = SpawnAreaMax.X - pi.X;
				if (dist < thresh) {
					const float depth = thresh - dist;
					const FVector2D normal(-1.0f, 0.0f);
					const float velN = FVector2D::DotProduct(vi, normal);
					const FVector2D acc = BoundaryStiffness * depth * normal - BoundaryDamping * velN * normal;
					fBoundaryForce += acc * mass3D_i;
				}
			}

			{
				const float dist = pi.Y - SpawnAreaMin.Y;
				if (dist < thresh) {
					const float depth = thresh - dist;
					const FVector2D normal(0.0f, 1.0f);
					const float velN = FVector2D::DotProduct(vi, normal);
					const FVector2D acc = BoundaryStiffness * depth * normal - BoundaryDamping * velN * normal;
					fBoundaryForce += acc * mass3D_i;
				}
			}

			{
				const float dist = SpawnAreaMax.Y - pi.Y;
				if (dist < thresh) {
					const float depth = thresh - dist;
					const FVector2D normal(0.0f, -1.0f);
					const float velN = FVector2D::DotProduct(vi, normal);
					const FVector2D acc = BoundaryStiffness * depth * normal - BoundaryDamping * velN * normal;
					fBoundaryForce += acc * mass3D_i;
				}
			}
		}

		Particles[i].Force = fPressure + fVisc + fGravity + fBoundaryForce;
	});
}

void AFluidSimulator::Integrate(float Dt)
{
	const int32 N = Particles.Num();

	if (N == 0) {
		return;
	}

	ParallelFor(N, [this, Dt](int32 i) {
		const float mass3D = Particles[i].Mass * ParticleThickness;
		const FVector2D accel = Particles[i].Force / mass3D;

		Particles[i].Velocity += accel * Dt;
		Particles[i].Velocity *= DampeningFactor;
		Particles[i].Position += Particles[i].Velocity * Dt;

		if (!UseBoundaryForces && bClampToGround) {
			if (Particles[i].Position.X < SpawnAreaMin.X) {
				Particles[i].Position.X = SpawnAreaMin.X; Particles[i].Velocity.X *= -0.25f;
			}
			else if (Particles[i].Position.X > SpawnAreaMax.X) {
				Particles[i].Position.X = SpawnAreaMax.X; Particles[i].Velocity.X *= -0.25f;
			}

			if (Particles[i].Position.Y < SpawnAreaMin.Y) {
				Particles[i].Position.Y = SpawnAreaMin.Y; Particles[i].Velocity.Y *= -0.25f;
			}
			else if (Particles[i].Position.Y > SpawnAreaMax.Y) {
				Particles[i].Position.Y = SpawnAreaMax.Y; Particles[i].Velocity.Y *= -0.25f;
			}
		}
	});
}

void AFluidSimulator::VisualizeParticles() const
{
	for (const FluidParticle& P : Particles) {
		const FVector worldPos(P.Position.X, P.Position.Y, PlaneZ);
		FColor color = FColor::Cyan;

		if (FluidTypes.IsValidIndex(P.TypeIndex)) {
			color = FluidTypes[P.TypeIndex].DebugColor;
		}

		DrawDebugSphere(GetWorld(), worldPos, DebugSphereRadius, 8, color, false, -1.0f, 0, 1.0f);
	}
}