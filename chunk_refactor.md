# Chunk System Refactor Plan

## Goal

Refactor the chunk system into a production-grade voxel world pipeline that is:

- deterministic
- easier to reason about
- robust at chunk boundaries
- safe under async jobs
- resilient against lost chunks and stale mesh results
- capable of handling local block edits quickly
- ready for future multiplayer block edits and simulation-driven world updates

This plan is intentionally broader than fixing one meshing bug. The objective is to make the chunk system itself much less bug-prone.

## Why This Refactor Is Needed

The current chunk pipeline works, but several parts are too implicit and fragile for a Minecraft-style game.

### Current Weaknesses

1. `ChunkState` is overloaded.

Current chunk state in [`chunk.h`](/C:/Users/magui/source/repos/voxel_enginevk/src/game/chunk.h) only tells part of the story:

- `Uninitialized`
- `Generated`
- `Rendered`

This is not enough to model:

- data generation in progress
- data available but waiting for neighbor readiness
- mesh queued
- mesh building
- mesh ready for upload
- mesh uploaded
- mesh stale after a block edit
- stale job results from previous generations

2. Neighbor readiness is event-driven instead of derived.

The current `NeighborBarrier` plus cross-signal behavior in [`chunk_manager.cpp`](/C:/Users/magui/source/repos/voxel_enginevk/src/world/chunk_manager.cpp) is clever, but it makes correctness depend on signals being fired and consumed in the right order.

That is the root risk behind "lost chunks":

- chunk data exists
- chunk is in the world
- mesh was never scheduled or retried
- chunk gets stuck in a state that no longer reflects what should happen

3. Meshing depends on transient neighbor timing.

Today a chunk is meshed when neighbors happen to be available and signaling lines up correctly.

For production, chunk meshing should be driven by explicit rules:

- does this chunk have data?
- do required neighbors have data?
- is mesh missing or stale?
- if yes, queue mesh

That should be true no matter how the chunk arrived in the current state.

4. Boundary sampling logic is too easy to get wrong.

Face visibility, AO, water visibility, and other future meshing rules all depend on reliable cross-chunk block access.

If boundary access is spread across multiple helper paths, bugs multiply quickly:

- black chunk faces
- false visibility seams
- AO lines at chunk borders
- neighbor disagreement between shading and geometry

5. The system is not yet edit-first.

A Minecraft-like game needs chunk updates driven by world edits:

- local player breaks a block
- local player places a block
- remote player changes a block
- structures stamp into the world
- explosions or simulation mutate blocks

The current system is still primarily generation-first rather than edit-first.

6. Mesh/upload invalidation is not versioned enough.

Async chunk jobs need strong stale-result rejection:

- chunk changes
- mesh job finishes late
- result is now obsolete

Without explicit versions, bugs show up as:

- lost chunk updates
- stale meshes
- mismatched geometry after edits

## Refactor Outcome

The refactor should produce a chunk system with these properties:

- chunk data is authoritative
- chunk mesh is derived from chunk data
- world edits mutate chunk data and mark chunks dirty
- neighbor dependencies are derived, not signal-dependent
- mesh builds are async and discard stale results safely
- chunk boundaries are sampled through one canonical path
- local and remote edits go through the same pipeline
- chunk recovery is self-healing if a mesh is missing or stale

## High-Level Architecture

The chunk system should be split into these layers.

### 1. Residency Layer

Responsibility:

- decide which chunks are resident around the player
- manage the chunk ring/cache
- create/reset chunk records as the active window moves

This is close to what `ChunkCache` already does and should mostly be preserved.

### 2. Data Layer

Responsibility:

- own authoritative voxel data for each chunk
- handle generation results
- handle block edits
- track data versions and dirty state

This must become the source of truth.

### 3. Meshing Layer

Responsibility:

- build chunk mesh output from chunk data plus stable neighbor data
- use one canonical neighbor/block sampling path
- produce CPU mesh output only

This should not mutate world state.

### 4. Upload / Render Layer

Responsibility:

- upload mesh buffers to GPU
- swap render meshes when upload succeeds
- discard stale mesh outputs if versions no longer match

### 5. Scheduler Layer

Responsibility:

- continuously derive what each chunk should do next
- queue generation, meshing, and upload based on current truth
- ensure chunks cannot be permanently “lost”

This is the layer that replaces correctness-by-barrier with correctness-by-state.

## Core Design Principle

Correctness must come from derived scheduling rules, not one-off signaling.

The scheduler should repeatedly ask:

- should this chunk generate?
- should this chunk mesh?
- should this chunk upload?

If the answer is yes, and there is no active job already doing that work, queue it.

That makes the system self-healing.

## Proposed Chunk Record Model

Instead of one overloaded state enum, each resident chunk should track independent state.

### Chunk Residency

- `Absent`
- `Resident`

### Data State

- `Empty`
- `GenerateQueued`
- `Generating`
- `Ready`
- `Dirty`

### Mesh State

- `Missing`
- `MeshQueued`
- `Meshing`
- `MeshReady`
- `Uploaded`
- `Stale`

### Suggested Chunk Record

```cpp
struct ChunkRecord {
    ChunkCoord coord;

    std::shared_ptr<ChunkData> data;
    std::shared_ptr<ChunkMeshData> mesh;

    uint32_t chunkGenerationId;
    uint32_t dataVersion;
    uint32_t meshedAgainstSignature;
    uint32_t uploadedVersion;

    DataState dataState;
    MeshState meshState;

    bool resident;
};
```

This is not final code, but it reflects the right shape.

## Versioning Model

Versioning is mandatory for correct async jobs.

### `chunkGenerationId`

Purpose:

- invalidates all jobs when a chunk slot is reused by the ring cache

If a chunk slides and is reused for a different world coordinate, old job results must be ignored.

### `dataVersion`

Purpose:

- increments whenever voxel data changes

Examples:

- initial terrain generation finished
- block destroyed
- block placed
- structure edits applied
- multiplayer world update applied

### Neighborhood Signature

Purpose:

- capture the versions the current mesh depends on

For example:

- center chunk `dataVersion`
- north, south, east, west versions
- diagonals too if AO needs them

This is the value the mesh result should be built against.

If the neighborhood signature changes, the mesh becomes stale.

## Replace `NeighborBarrier` As The Correctness Backbone

This is one of the most important changes.

### Current Problem

`NeighborBarrier` is currently part of correctness, not just optimization.

That means:

- if a signal is missed
- if a chunk is reset at the wrong moment
- if timing changes

the chunk may remain generated but never meshed.

### Proposed Change

Use `NeighborBarrier` only as an optional optimization later, or remove it entirely.

Correctness should come from scheduler rules:

```cpp
should_mesh(chunk) =
    chunk.dataState in { Ready, Dirty } &&
    required_neighbors_have_data(chunk) &&
    mesh_missing_or_stale(chunk) &&
    !mesh_job_already_in_flight(chunk);
```

That means no chunk can remain lost forever if the scheduler keeps running.

## Canonical Neighborhood Sampling

This is the other major correctness change.

All meshing-related boundary logic should go through one canonical block sampler.

### Proposed Type

```cpp
struct ChunkNeighborhood {
    const ChunkData* center;
    const ChunkData* north;
    const ChunkData* south;
    const ChunkData* east;
    const ChunkData* west;
    const ChunkData* northEast;
    const ChunkData* northWest;
    const ChunkData* southEast;
    const ChunkData* southWest;
};
```

### Proposed Accessor

```cpp
std::optional<BlockView> sample_block(const ChunkNeighborhood&, int localX, int y, int localZ);
```

This should be used by:

- face visibility
- AO
- water face visibility
- future light propagation

### Rule

Do not spread boundary math across multiple mesher helpers.

The sampler must be the only place that decides:

- which chunk owns a boundary block
- how negative/local overflow coordinates are handled
- what happens if data is missing

## Data Edits / World Edit Pipeline

The refactor must explicitly support live block edits from day one.

### Principle

All changes to chunk voxel data should go through one shared world edit path.

That includes:

- local player mining
- local player placement
- remote player block updates
- server corrections
- structure stamping
- explosion/simulation edits

### Proposed Edit Type

```cpp
struct BlockEdit {
    glm::ivec3 worldPos;
    Block newBlock;
    EditSource source;
};
```

### Proposed Flow

1. world receives edit
2. resolve owning chunk
3. mutate authoritative chunk data
4. increment `dataVersion`
5. mark owning chunk dirty
6. if edit touches a boundary, mark neighbor chunks dirty
7. scheduler queues required remesh work

### Why This Matters

This is how the same chunk system supports:

- instant-feeling local edits
- remote multiplayer edits
- future dynamic structures or explosions

without separate special-case paths.

## Dirty Propagation Rules

Dirty propagation must be explicit.

### Interior Edit

If a block edit is not on a chunk edge:

- mark owning chunk dirty

### Edge Edit

If a block edit is on a chunk border:

- mark owning chunk dirty
- mark touching neighbor dirty

### Corner / AO Case

If AO sampling depends on diagonal occupancy:

- mark diagonals dirty when corner-adjacent data can affect AO

This should be encoded centrally, not in scattered caller logic.

## Async Meshing Model

Meshing must stay async, but it should become version-safe.

### Rule

Worker threads may:

- read chunk data snapshots or stable references
- build CPU mesh output

Worker threads must not:

- mutate live authoritative chunk data
- directly modify GPU/render state

### Proposed Mesh Job Result

```cpp
struct MeshBuildResult {
    ChunkCoord coord;
    uint32_t chunkGenerationId;
    uint32_t neighborhoodSignature;
    std::shared_ptr<ChunkMeshData> meshData;
};
```

### Main Thread Acceptance Rule

Accept a mesh result only if:

- chunk is still resident at that coord
- chunk generation id still matches
- chunk still needs that neighborhood signature

Otherwise discard it.

This is how stale jobs become harmless.

## Upload Model

GPU upload should be a separate state transition.

### Flow

1. mesh job builds CPU mesh
2. scheduler queues upload
3. render/upload system uploads buffers
4. render state swaps to the uploaded mesh

### Suggested Mesh States

- `Missing`
- `MeshQueued`
- `Meshing`
- `MeshReady`
- `Uploaded`
- `Stale`

This gives clear debugging semantics.

## Scheduler Design

The scheduler becomes the heart of correctness.

### Core Responsibilities

- scan resident chunks regularly
- queue missing data jobs
- queue missing/stale mesh jobs
- queue uploads for ready mesh data
- ensure no chunk remains stuck due to missed signals

### Suggested Priority Queues

Not all chunk work should have the same urgency.

#### Urgent

- chunks edited by the local player
- chunks near the camera
- seam fixes after boundary edits

#### Normal

- nearby stream-in chunks

#### Background

- farther chunks
- precomputation work

This is how local block edits can feel “Minecraft-fast” even if background streaming is busy.

## Lost Chunk Prevention

This refactor must make “lost chunks” structurally hard to create.

### Success Condition

A chunk with valid data must not rely on one past event to ever receive a mesh.

Instead, every scheduler pass should be able to conclude:

- this chunk has data
- its neighbors are ready
- it lacks a current mesh
- therefore it must be queued for meshing

If that logic exists, lost chunks become a scheduler bug instead of a silent lifecycle bug.

That is much easier to test and reason about.

## Chunk Boundary Behavior

The chunk boundary system should be explicitly defined.

### Requirements

- no face visibility disagreement across chunk seams
- AO must use the same neighbor data model as visibility
- missing neighbors should be treated intentionally, not accidentally

### Recommended Policy

Do not mesh a chunk until all required meshing neighbors are available.

Required set:

- N/S/E/W for face visibility
- diagonals too if current AO implementation depends on them

This is simpler and more robust than trying to partial-mesh against unknown neighbors.

## Structure / Worldgen Compatibility

The chunk refactor must be compatible with the newer world-space structure system.

### Implication

Structures and terrain edits may change chunk data after initial chunk generation.

Therefore:

- data versioning must support post-generation mutation
- dirty remesh rules must work for structure edits too

This is another reason the chunk system must become edit-first rather than generation-first.

## Multiplayer Readiness

This refactor should be designed so multiplayer does not require changing chunk fundamentals later.

### Principle

Remote edits should flow through the exact same world edit pipeline as local edits.

Only the source changes:

- local input
- network message
- server correction

Chunk mutation/remeshing logic stays identical.

### Benefits

- fewer codepaths
- easier debugging
- deterministic replay/testing

## Recommended File / Module Direction

This is a suggested structural direction, not a required exact file list.

### `world/chunk_record.*`

Owns runtime state per resident chunk:

- coord
- versions
- data state
- mesh state
- current data/mesh handles

### `world/chunk_scheduler.*`

Owns:

- `should_generate`
- `should_mesh`
- `should_upload`
- queue prioritization

### `world/chunk_neighborhood.*`

Owns canonical neighborhood sampling and boundary math.

### `world/world_edit_queue.*`

Owns queued world edits from:

- player
- network
- systems

### `world/chunk_dirty_tracker.*`

Owns dirty propagation across chunk edges and diagonals.

### `world/chunk_jobs.*`

Owns job contracts and result types for:

- generation
- meshing
- upload handoff

## Migration Strategy

This should not be implemented as a giant rewrite in one step.

### Phase 1: State Model

Replace `ChunkState` with clearer state/versions while keeping existing code paths working.

Target:

- chunk generation id
- data version
- mesh state

### Phase 2: Scheduler Truth

Introduce scheduler-driven `should_generate()` and `should_mesh()`.

Target:

- chunks no longer rely on barrier signals for correctness

### Phase 3: Canonical Sampler

Introduce chunk neighborhood sampling as the only source of boundary truth.

Target:

- visibility and AO use the same access model

### Phase 4: World Edit Pipeline

Add `apply_block_edit()` path and dirty propagation.

Target:

- local block edits work through authoritative chunk mutation

### Phase 5: Stale Result Rejection

Make mesh results version-checked and discardable.

Target:

- async jobs become safe under rapid edits and chunk sliding

### Phase 6: Priority Scheduling

Add urgent remesh queue for player-facing changes.

Target:

- fast local edit responsiveness

### Phase 7: Multiplayer Integration

Feed remote edits into the same world edit queue.

Target:

- no separate multiplayer-specific remesh pipeline

## Production-Grade Success Criteria

This refactor should be considered successful when:

- chunk data, mesh state, and residency are separated clearly
- a generated chunk cannot remain permanently unmeshed due to a missed event
- chunk boundary visibility and AO use one trusted neighborhood model
- local block edits update nearby chunks quickly and deterministically
- remote edits can use the same pipeline later
- stale async mesh jobs are harmless
- chunk state is inspectable and debuggable without guessing

## What Should Not Be Done

To keep scope disciplined, this refactor should not try to solve everything at once.

Do not include in the initial pass:

- persistence/save format redesign
- chunk compression
- chunk LOD system
- server replication protocol
- large lighting rewrite

The goal is a correct and robust chunk lifecycle first.

## Final Recommendation

This does not need a total engine rewrite.

The current chunk cache and async job direction are viable. The problem is that correctness currently depends on fragile transitions and implicit neighbor timing.

The right refactor is:

- keep the broad chunk streaming model
- replace the chunk lifecycle/state model
- replace barrier-driven correctness with scheduler-driven truth
- unify boundary sampling
- make world edits first-class

That is the path that gets this system close to production-grade for a Minecraft-style voxel RPG.
