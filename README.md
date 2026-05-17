# Fluid Simulation (2D)

A 2D SPH system with spatial hashing, multithreaded, handling two fluid types

## Overview

- Simulate fluids in the XY plane and render at a fixed world Z
- Particles are simulated in 2D but use 3D SPH kernel constants
- Coexistence of multiple fluid types (rest density, viscosity, TaitK, debug color)
- Spatial grid to reduce neighbor search
- Multithreaded density/force/integrate passes
- Debug drawing to show particle positions and colors

## Configuration Parameters

Spawner:
- `NumParticles` : number of particles to simulate
- `SpawnAreaMin` / `SpawnAreaMax` : XY spawn rectangle
- `SecondFluidFraction` : fraction of particles assigned to the second fluid

Simulation:
- `SubstepDt` : fixed substep timestep
- `DampeningFactor` : velocity damping per substep
- `Gravity` : 2D gravity

SPH / kernel / mass:
- `SmoothingRadius` : kernel support radius
- `ParticleThickness` : converts 2D mass → 3D mass
- `RestDensity` : fallback rest density
- `TaitK`, `TaitGamma` : Tait EOS constants
- `Viscosity` : fallback viscosity

Fluid types:
- `FluidTypes` (array of `FluidTypeProperties`) per-type:
  - `Name`, `RestDensity`, `Viscosity`, `TaitK`, `DebugColor`

Boundary:
- `UseBoundaryForces` : enable boundaries
- `BoundaryThickness`, `BoundaryStiffness`, `BoundaryDamping`

Rendering / debug:
- `IsVisualize` : toggle drawing
- `DebugSphereRadius` : debug sphere radius in world units
- `PlaneZ` : world Z where particles are drawn

## Next Steps / TODO

- Optimize performance to reach 2k particles and 60 FPS
- Better reaction between fluids