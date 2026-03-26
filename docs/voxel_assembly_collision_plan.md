# Voxel Assembly Collision Plan

## Goal

Move voxel-object and voxel-assembly world collision toward a generic runtime system driven by authored asset data, not per-entity hard-coded dimensions.

## Principles

- `VoxelAssemblyAsset` owns collision authoring data.
- Runtime systems evaluate collision bounds from active voxel assets and component state.
- Entities carry generic components; they do not hand-author collision sizes for each gameplay type.
- Render bounds and gameplay collision are related but distinct.
- Collision evaluation must live in runtime/game systems, not scene-only code.

## Runtime Direction

### 1. Runtime Asset Ownership

`CubeEngine` should own runtime voxel repositories and asset managers so gameplay systems can evaluate assemblies without depending on `GameScene`.

### 2. Assembly Collision Authoring

`VoxelAssemblyAsset` should include collision metadata:

- `collision.mode`
  - `none`
  - `tagged_parts`
  - `custom_bounds`
- `collision.customBoundsMin`
- `collision.customBoundsMax`

Each `VoxelAssemblyPartDefinition` should include:

- `contributesToCollision`

This allows equipment or floating decorative parts to be excluded from gameplay collision while still rendering.

### 3. Generic Collider Component

Introduce a reusable local-space collider component:

- stores evaluated local AABB
- stores validity/diagnostics
- is not player-specific

Runtime systems convert that local AABB into world bounds using the owning object transform.

### 4. Collision Evaluation

Add shared voxel collision evaluators for:

- `VoxelModelComponent`
- `VoxelAssemblyComponent`
- generic object-level fallback

Assembly collision evaluation should:

- resolve active parts and states
- honor placement policy
- include only active parts that contribute to collision
- support authored custom bounds override

### 5. Player Migration

The player should consume the generic collider component.

- remove player-authored collision width/depth/height controls
- keep camera target offset configurable
- evaluate collision bounds from the active player assembly each tick

## Editor Work

`VoxelAssemblyScene` should expose:

- assembly collision mode
- custom collision bounds when needed
- part-level `contributesToCollision`
- collision validation and debug visualization later

## Phasing

### Phase 1

- move runtime voxel asset ownership into `CubeEngine`
- add assembly collision metadata and serialization
- add generic collider component and evaluators
- migrate player collision to evaluated assembly bounds
- keep camera target offset in player settings

### Phase 2

- add assembly editor collision UI
- add validation messages
- visualize authored collision bounds in the assembly editor

### Phase 3

- support state-specific collision profiles where needed
- support animation-aware collision refresh
- expand generic collider usage beyond player to monsters and interactables
