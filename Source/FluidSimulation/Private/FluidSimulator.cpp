#include "FluidSimulator.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "Engine/Engine.h"

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

	FRandomStream RandomStream(12345);

	const float area = FMath::Abs((SpawnAreaMax.X - SpawnAreaMin.X) * (SpawnAreaMax.Y - SpawnAreaMin.Y));
	const float particleArea = (NumParticles > 0) ? (area / float(NumParticles)) : 1.0f;
	const float mass2D = RestDensity * particleArea;

	FRandomStream Rand(12345);

	for (int32 i = 0; i < NumParticles; ++i) {
		const FVector2D pos(
			Rand.FRandRange(SpawnAreaMin.X, SpawnAreaMax.X),
			Rand.FRandRange(SpawnAreaMin.Y, SpawnAreaMax.Y)
		);

		Particles.Emplace(pos, mass2D);

		if (i < 5) {
			UE_LOG(LogTemp, Verbose, TEXT("Spawn particle %d pos=%s mass2D=%.6f"), i, *pos.ToString(), mass2D);
		}
	}

	Densities.SetNumZeroed(Particles.Num());
	Pressures.SetNumZeroed(Particles.Num());

	UE_LOG(LogTemp, Log, TEXT("Initialized %d particles (mass2D ~ %.6f)"), Particles.Num(), mass2D);
}

void AFluidSimulator::UpdateParticles(float DeltaTime)
{
	float remaining = DeltaTime;

	while (remaining > KINDA_SMALL_NUMBER) {
		const float dt = FMath::Min(SubstepDt, remaining);
		ComputeDensityPressure();
		ComputeForces();
		Integrate(dt);
		remaining -= dt;
	}
}

void AFluidSimulator::ComputeDensityPressure()
{
	const float h = SmoothingRadius;
	const int32 N = Particles.Num();

	if (N == 0) {
		return;
	}

	Densities.SetNumUninitialized(N);
	Pressures.SetNumUninitialized(N);

	// to refacto
	for (int32 i = 0; i < N; ++i) {
		float rho = 0.0f;
		const FVector2D pi = Particles[i].Position;

		for (int32 j = 0; j < N; ++j) {
			const FVector2D pj = Particles[j].Position;
			const FVector2D rij = pi - pj;
			const float r = rij.Size(); // SizeSquared ?

			if (r <= h) {
				const float m3D = Particles[j].Mass * ParticleThickness;
				const float w = Use3DKernels ? Poly6Kernel3D(r, h) : Poly6Kernel3D(r, h);
				rho += m3D * w;
			}
		}

		// useless ?
		if (rho <= KINDA_SMALL_NUMBER) {
			rho = KINDA_SMALL_NUMBER;
		}

		Densities[i] = rho;
		Particles[i].Density = rho;

		if (Use3DKernels) {
			Pressures[i] = TaitK * (FMath::Pow(rho / RestDensity, TaitGamma) - 1.0f);
		} else {
			Pressures[i] = TaitK * (rho - RestDensity);
		}

		Particles[i].Pressure = Pressures[i];
	}
}

void AFluidSimulator::ComputeForces()
{
	const float h = SmoothingRadius;
	const int32 N = Particles.Num();

	if (N == 0) {
		return;
	}

	for (int32 i = 0; i < N; ++i) {
		Particles[i].ResetForce();
	}

	// to refacto
	for (int32 i = 0; i < N; ++i) {
		FVector2D fPressure = FVector2D::ZeroVector;
		FVector2D fVisc = FVector2D::ZeroVector;

		const FVector2D pi = Particles[i].Position;
		const FVector2D vi = Particles[i].Velocity;
		const float rhoi = Densities[i];
		const float piPressure = Pressures[i];

		for (int32 j = 0; j < N; ++j) {
			if (i == j) {
				continue;
			}

			const FVector2D pj = Particles[j].Position;
			const FVector2D rij = pi - pj;
			const float r = rij.Size(); // SizeSquared ?

			if (r <= 0.0f || r > h) {
				continue;
			}

			const float m3D = Particles[j].Mass * ParticleThickness;
			const float rhj = Densities[j];

			if (rhj <= KINDA_SMALL_NUMBER) {
				continue;
			}

			const FVector2D gradW = SpikyGrad3D(rij, r, h);
			//fPressure += -m3D * (piPressure + Pressures[j]) / (2.0f * rhj) * gradW; old working formula ?
			fPressure += -m3D * ((piPressure / (rhoi * rhoi)) + (Pressures[j] / (rhj * rhj))) * gradW;

			const FVector2D velDiff = Particles[j].Velocity - vi;
			const float lap = ViscosityLaplacian3D(r, h);
			//fVisc += Viscosity * m3D * (velDiff / rhj) * lap; old working formula ?
			fVisc += Viscosity * m3D * (velDiff / (0.5f * (rhoi + rhj))) * lap;
		}

		const float mass3D_i = Particles[i].Mass * ParticleThickness;
		const FVector2D fGravity = Gravity * mass3D_i;

		Particles[i].Force = fPressure + fVisc + fGravity;
	}
}

void AFluidSimulator::Integrate(float Dt)
{
	const int32 N = Particles.Num();

	if (N == 0) {
		return;
	}

	for (int32 i = 0; i < N; ++i) {
		const float mass3D = Particles[i].Mass * ParticleThickness;
		const FVector2D accel = Particles[i].Force / mass3D;

		Particles[i].Velocity += accel * Dt;
		Particles[i].Velocity *= DampeningFactor;
		Particles[i].Position += Particles[i].Velocity * Dt;

		if (bClampToGround) {
			if (Particles[i].Position.X < SpawnAreaMin.X) {
				Particles[i].Position.X = SpawnAreaMin.X; Particles[i].Velocity.X *= -0.25f;
			} else if (Particles[i].Position.X > SpawnAreaMax.X) {
				Particles[i].Position.X = SpawnAreaMax.X; Particles[i].Velocity.X *= -0.25f;
			}

			if (Particles[i].Position.Y < SpawnAreaMin.Y) {
				Particles[i].Position.Y = SpawnAreaMin.Y; Particles[i].Velocity.Y *= -0.25f;
			}else if (Particles[i].Position.Y > SpawnAreaMax.Y) {
				Particles[i].Position.Y = SpawnAreaMax.Y; Particles[i].Velocity.Y *= -0.25f;
			}
		}
	}
}

// debug / temp ?
void AFluidSimulator::VisualizeParticles() const
{
	DrawDebugBox(GetWorld(), GetActorLocation(), FVector(16.0f), FColor::White, false, -1.0f, 0, 1.0f);

	for (const FluidParticle& P : Particles) {
		const FVector worldPos(P.Position.X, P.Position.Y, PlaneZ);
		DrawDebugSphere(GetWorld(), worldPos, DebugSphereRadius, 8, FColor::Cyan, false, -1.0f, 0, 1.0f);
	}
}