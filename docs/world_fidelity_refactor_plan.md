# World Fidelity Refactor Plan

## Goal

Refactor world geometry so voxel fidelity can increase or decrease without implicitly rescaling the physical world.

The target outcome is:

- mountains keep roughly the same physical size
- chunks keep the same physical footprint unless configured otherwise
- blocks remain true cubes
- denser settings produce more voxel samples, not smaller worlds
- chunk/block logic remains authoritative in voxel space
- rendering, collision, raycast, and lighting consume explicit voxel-to-world conversions

This is not a visual hack and not a one-constant tweak. It is a geometry model refactor.

## Why This Refactor Is Needed

The current codebase assumes:

- `CHUNK_SIZE` is both voxel resolution and world-space chunk width
- `CHUNK_HEIGHT` is both voxel resolution and world-space height
- `1 voxel == 1 world unit`

That assumption leaks through:

- chunk coordinate resolution
- chunk origins and bounds
- meshing
- render transforms
- collision sampling
- raycast stepping
- light sampling
- terrain generation sample coordinates
- settings derived from chunk world size

Because those meanings are conflated, the engine cannot currently express:

- same world size with higher voxel fidelity
- same physical mountain scale with denser chunk sampling
- future geometry descriptors that are not hard-coded to the default chunk resolution

## Core Design Principle

Separate **voxel space** from **physical world space**.

### Voxel Space

Voxel space is authoritative for:

- chunk ownership
- block edits
- neighbor sampling
- dirty propagation
- pathfinding/grid logic
- chunk-local storage

### Physical World Space

Physical world space is authoritative for:

- rendering
- camera/player transforms
- collision volumes
- world-space light sampling
- terrain/worldgen sample positions

All conversions between the two spaces must go through one explicit geometry layer.

## World Geometry Descriptor

Introduce a dedicated world descriptor:

```cpp
struct WorldGeometrySettings {
    int chunkVoxelWidth = 16;
    int chunkVoxelHeight = 256;
    float chunkWorldWidth = 16.0f;
    float chunkWorldHeight = 256.0f;
};
```

Derived rules:

- `chunkVoxelDepth == chunkVoxelWidth`
- `chunkWorldDepth == chunkWorldWidth`
- `blockWorldSize = chunkWorldWidth / chunkVoxelWidth`
- `chunkVoxelHeight` is normalized to preserve cubic blocks against `chunkWorldHeight`

Example:

- old: `16 x 256`, `16.0 x 256.0`, `blockWorldSize = 1.0`
- higher fidelity: `24 x 384`, `16.0 x 256.0`, `blockWorldSize = 2/3`

This keeps:

- physical chunk/world size stable
- blocks cubic
- voxel density higher

## Runtime Geometry Layer

Add a small runtime helper that owns geometry conversions:

```cpp
class WorldGeometry {
public:
    float block_world_size() const noexcept;
    glm::vec3 voxel_to_world(const glm::vec3& voxel) const noexcept;
    glm::vec3 world_to_voxel(const glm::vec3& world) const noexcept;
    glm::ivec3 world_to_voxel_cell(const glm::vec3& world) const noexcept;
    ChunkCoord world_to_chunk(const glm::vec3& world) const noexcept;
    glm::ivec3 world_to_local_voxel(const glm::vec3& world) const noexcept;
    glm::ivec3 chunk_voxel_origin(const ChunkCoord&) const noexcept;
    glm::vec3 chunk_world_origin(const ChunkCoord&) const noexcept;
};
```

This layer becomes the only source of truth for voxel/world conversion math.

## Storage Refactor Direction

Chunk and generation storage cannot remain fixed-size compile-time arrays if fidelity is configurable.

Replace:

- fixed `Block[CHUNK_SIZE][CHUNK_HEIGHT][CHUNK_SIZE]`
- fixed-size generation buffers indexed by `CHUNK_SIZE` / `CHUNK_HEIGHT`

With:

- runtime-sized contiguous storage
- explicit indexing helpers using the active geometry descriptor

This applies to:

- chunk block storage
- terrain generation buffers
- appearance buffers
- lighting scratch domains
- any helper that currently bakes in default chunk dimensions

## Terrain Generation Rule

Terrain generation must sample in physical world coordinates, not old voxel coordinates.

That means:

- the same physical position should produce the same macro terrain shape across fidelities
- changing fidelity should only change voxel sampling density
- local voxel positions are converted to physical sample positions through `blockWorldSize`

Do not let fidelity changes rescale mountain widths/heights by accident.

## Runtime Application Model

World fidelity is a **world descriptor**, not a live toggle.

V1 behavior:

- geometry settings load from config
- geometry settings apply on world creation or explicit regeneration
- geometry changes rebuild world state
- no live hot-swap of resident chunk dimensions in-place

This keeps the first pass robust and avoids partial transitions between incompatible chunk layouts.

## Config / Settings Integration

Persist geometry in its own config domain.

Do not overload:

- `GameSettingsPersistence`
- `TerrainGeneratorSettings`

Rationale:

- geometry affects world runtime, rendering, collision, and generation
- it is broader than scene settings
- it is broader than worldgen noise tuning

Recommended integration:

- `WorldGeometryConfigRepository`
- `ConfigService::world_geometry()`
- load alongside game settings and world-gen settings
- expose in the world-generation/settings workflow

## Migration Phases

### Phase 1: Descriptor + Docs

- add `WorldGeometrySettings`
- add `WorldGeometry`
- add config persistence
- document the architecture and invariants

### Phase 2: Dynamic Storage

- replace fixed-size chunk/generation containers with runtime-sized storage

### Phase 3: Conversion Layer Adoption

- route world/chunk/local conversion through `WorldGeometry`
- remove direct world-distance math based on `CHUNK_SIZE`

### Phase 4: Runtime System Conversion

- meshing
- render transforms
- collision
- raycast
- light sampling
- debug geometry

### Phase 5: Worldgen Conversion

- terrain generation samples physical world coordinates
- structures/decorations use physical chunk extents where appropriate

### Phase 6: Regeneration / UI Integration

- config save/load
- geometry editing workflow
- explicit regeneration path

## Success Criteria

This refactor is successful when:

- changing fidelity increases voxel density without shrinking the world
- blocks remain cubic
- chunk/block systems remain correct in voxel space
- render/query/physics systems use explicit geometry conversions
- terrain generation preserves physical macro scale across fidelity settings
- geometry settings round-trip through config cleanly
- regeneration cleanly rebuilds the world under a new geometry descriptor

## Non-Goals For The First Pass

Do not include in the first pass:

- live hot-swapping active chunk geometry during play
- non-cubic block support
- per-region/per-biome fidelity variation
- save-game compatibility migration for persistent world storage

The first pass should produce a clean, testable foundation for the full runtime refactor.
