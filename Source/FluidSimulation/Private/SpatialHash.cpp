#include "SpatialHash.h"
#include "FluidParticle.h"

FSpatialHash::FSpatialHash(float InGridChunkSize)
	: GridChunkSize(InGridChunkSize)
{
}

int64 FSpatialHash::GetChunkHash(const FVector2D& Position) const
{
	const int32 ChunkX = FMath::FloorToInt(Position.X / GridChunkSize);
	const int32 ChunkY = FMath::FloorToInt(Position.Y / GridChunkSize);

	const int64 Sum = static_cast<int64>(ChunkX) + static_cast<int64>(ChunkY);
	const int64 CantorHash = (Sum * (Sum + 1)) / 2 + ChunkY;

	return CantorHash;
}

void FSpatialHash::Rebuild(const TArray<FluidParticle>& Particles)
{
	Grid.Empty();

	for (int32 i = 0; i < Particles.Num(); ++i)	{
		const int64 Hash = GetChunkHash(Particles[i].Position);
		Grid.FindOrAdd(Hash).Add(i);
	}
}

// replace side effect
void FSpatialHash::GetChunksInRadius(const FVector2D& Position, float Radius, TArray<int64>& OutChunks) const
{
	OutChunks.Empty();

	const int32 MinChunkX = FMath::FloorToInt((Position.X - Radius) / GridChunkSize);
	const int32 MaxChunkX = FMath::FloorToInt((Position.X + Radius) / GridChunkSize);
	const int32 MinChunkY = FMath::FloorToInt((Position.Y - Radius) / GridChunkSize);
	const int32 MaxChunkY = FMath::FloorToInt((Position.Y + Radius) / GridChunkSize);

	// to refacto (two loops + reused 32-64 bits trick)

	for (int32 ChunkX = MinChunkX; ChunkX <= MaxChunkX; ++ChunkX) {
		for (int32 ChunkY = MinChunkY; ChunkY <= MaxChunkY; ++ChunkY) {
			const int64 Sum = static_cast<int64>(ChunkX) + static_cast<int64>(ChunkY);
			const int64 CantorHash = (Sum * (Sum + 1)) / 2 + ChunkY;

			OutChunks.Add(CantorHash);
		}
	}
}

// replace side effect
void FSpatialHash::QueryNeighbors(const FVector2D& Position, float Radius, TArray<int32>& OutNeighborIndices) const
{
	OutNeighborIndices.Empty();

	TArray<int64> Chunks;
	GetChunksInRadius(Position, Radius, Chunks);

	// to refactor

	for (const int64 ChunkHash : Chunks) {
		if (const TArray<int32>* ParticleIndices = Grid.Find(ChunkHash)) {
			for (int32 ParticleIndex : *ParticleIndices) {
				OutNeighborIndices.Add(ParticleIndex);
			}
		}
	}
}