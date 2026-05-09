#pragma once

#include "CoreMinimal.h"
#include "Containers/Map.h"
#include <FluidParticle.h>

class FSpatialHash
{
public:
	FSpatialHash(float InGridChunkSize = 24.0f);

	void Rebuild(const TArray<FluidParticle>& Particles);

	void QueryNeighbors(const FVector2D& Position, float Radius, TArray<int32>& OutNeighborIndices) const;

	void Clear() { Grid.Empty(); }

private:
	float GridChunkSize;

	TMap<int64, TArray<int32>> Grid;

	FORCEINLINE int64 GetChunkHash(const FVector2D& Position) const;

	void GetChunksInRadius(const FVector2D& Position, float Radius, TArray<int64>& OutChunks) const;
};

