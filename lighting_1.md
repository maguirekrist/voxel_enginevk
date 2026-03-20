# Skylight System Implementation Plan

## Goal

Implement a proper voxel skylight system for the current chunked world so that:

- trees and other structures cast visible shade
- skylight is authoritative data, not an incidental generation artifact
- chunk meshes sample stable per-voxel light values
- world edits can invalidate and recompute skylight correctly
- chunk-boundary lighting is consistent
- the lighting pipeline fits the scheduler/versioned chunk architecture already in place

This plan is intentionally limited to skylight first. It does not include emissive block light, colored lighting, or true GI.

## Current Findings

### What exists now

- `Block` already contains `_sunlight` in [`src/game/block.h`](/C:/Users/magui/source/repos/voxel_enginevk/src/game/block.h)
- terrain generation initializes `_sunlight` heuristically in [`src/game/chunk.cpp`](/C:/Users/magui/source/repos/voxel_enginevk/src/game/chunk.cpp)
- `ChunkMesher::add_face_to_opaque_mesh()` multiplies vertex color by face-neighbor sunlight in [`src/world/chunk_mesher.cpp`](/C:/Users/magui/source/repos/voxel_enginevk/src/world/chunk_mesher.cpp)
- `ChunkMesher::propagate_sunlight()` exists but is not part of the active pipeline

### What is missing

- no authoritative skylight solve after terrain + structure edits
- no cross-chunk skylight propagation
- no relight after world edits
- no scheduler state for lighting jobs
- no versioning for stale async lighting results
- no explicit light invalidation model

### Consequence

The world currently has:

- AO
- static terrain-time `_sunlight` initialization
- no true tree shadows
- no reliable shadowing from post-generation structures

## Desired Outcome

After this implementation:

- every resident chunk owns authoritative skylight data in its voxels
- terrain and structures both affect skylight before meshing
- chunk meshes shade from stable voxel skylight
- skylight is recomputed safely when edits occur
- chunk boundaries do not disagree on lighting
- async lighting jobs are version-safe and discard stale results

## Design Principles

### 1. Lighting is authoritative chunk data

Skylight should not be treated as a render-only effect.

It should live in chunk voxel data and be derived from:

- terrain occupancy
- structure occupancy
- world edits

### 2. Meshing consumes light, it does not invent light

The mesher should only read final skylight values.

It should not be responsible for solving light propagation.

### 3. Lighting correctness should be scheduler-driven

Just like chunk generation/meshing, lighting should be derived from current truth:

- does chunk data exist?
- does chunk lighting need initialization?
- did neighbor lighting become stale?
- did a world edit invalidate skylight?

### 4. Boundary correctness comes before optimization

Do not optimize lighting propagation until seam correctness is stable.

First objective:

- no visible seams
- no missing tree shadows at chunk edges
- no persistent stale lighting after edits

## Scope

### In scope

- skylight only
- sunlight blocked by solid blocks
- top-down sunlight initialization
- downward and lateral propagation through transparent voxels
- cross-chunk lighting consistency
- structure-aware skylight
- edit invalidation and relight

### Out of scope

- emissive block light
- colored light
- bounced light / GI
- soft shadow maps
- volumetric fog lighting
- persistence format changes

## Proposed Architecture

## 1. Data Model

### Block data

Keep `_sunlight` on `Block`, but make it authoritative rather than loosely initialized.

### Chunk runtime state

Extend chunk runtime state with explicit lighting progress.

Suggested shape:

```cpp
enum class LightState : uint8_t
{
    Missing = 0,
    LightQueued = 1,
    Lighting = 2,
    Ready = 3,
    Stale = 4
};
```

Add to chunk runtime record:

- `lightVersion`
- `litAgainstSignature`
- `lightState`
- `lightJobInFlight`

### Why separate `lightVersion`

Skylight is derived from voxel occupancy, but not every future derived system should share one version counter.

Recommended:

- `dataVersion`: voxel occupancy/material changed
- `lightVersion`: skylight solve changed
- `mesh signature`: depends on both occupancy and lighting neighborhood state

## 2. Lighting Layer

Introduce a dedicated lighting layer between data and meshing.

Responsibilities:

- initialize skylight from top of world
- propagate skylight through transparent voxels
- solve across chunk boundaries
- write final `_sunlight` values into chunk data

Suggested module direction:

- `world/chunk_lighting.h`
- `world/chunk_lighting.cpp`
- `world/light_jobs.h`
- `world/light_jobs.cpp`

## 3. Lighting Scheduler

The scheduler should determine:

- should this chunk be lit?
- should this chunk be relit?
- are required lighting neighbors available?
- is a lighting result stale?

Meshing should depend on lighting readiness, not just voxel data readiness.

### Example derived rules

```cpp
should_light(chunk) =
    chunk.dataState in { Ready, Dirty } &&
    chunk.lightState in { Missing, Stale } &&
    required_light_neighbors_available(chunk) &&
    !light_job_in_flight(chunk);
```

```cpp
should_mesh(chunk) =
    chunk.dataState in { Ready, Dirty } &&
    chunk.lightState == Ready &&
    chunk.meshState in { Missing, Stale } &&
    required_mesh_neighbors_available(chunk) &&
    !mesh_job_in_flight(chunk);
```

## 4. Lighting Neighborhood

Just like meshing, lighting needs a canonical neighborhood model.

Suggested type:

```cpp
struct LightNeighborhood
{
    ChunkData* center;
    ChunkData* north;
    ChunkData* south;
    ChunkData* east;
    ChunkData* west;
};
```

For skylight, start with N/S/E/W only unless lateral propagation requires more.

Diagonals may not be necessary for first-pass skylight solve.

## 5. Job Model

### Lighting job contract

Worker threads may:

- read stable chunk voxel/light snapshots
- solve skylight into CPU-side chunk copies

Worker threads must not:

- mutate live chunk data
- directly patch render state

Suggested result:

```cpp
struct LightBuildResult
{
    ChunkCoord coord;
    uint32_t chunkGenerationId;
    uint32_t dataVersion;
    uint64_t lightNeighborhoodSignature;
    std::shared_ptr<ChunkData> litData;
};
```

### Acceptance rule

Accept only if:

- chunk still matches generation id
- `dataVersion` still matches
- neighborhood signature still matches expected lighting inputs

Otherwise discard.

## Skylight Solve Strategy

## Phase A: Top-down initialization

For each `(x, z)` column:

1. start from `y = CHUNK_HEIGHT - 1`
2. set current sunlight to `MAX_LIGHT_LEVEL`
3. descend until hitting solid blocks
4. transparent voxels receive sunlight
5. solid voxels block downward sunlight

This alone gives direct “sun from above” behavior.

### Important

If a structure like leaves/trunk exists after terrain generation, it must participate in this step.

That means:

- solve lighting after terrain + structures are finalized
- not before structure stamping

## Phase B: Lateral propagation

Once top-down initialization is done:

- propagate skylight sideways and downward through transparent blocks
- reduce light by one step when moving away from a source path

This is the familiar voxel flood-fill model.

Suggested behavior:

- vertical downward continuation from full skylight remains strong
- lateral spread attenuates by 1 per step
- solid blocks stop propagation

### Benefits

This creates:

- shade under trees
- soft spill near openings
- cavern entrances with gradual transition

## Phase C: Cross-chunk seam propagation

Lighting must cross chunk edges.

Requirements:

- if a boundary voxel is transparent and lit, its neighbor chunk voxel must be considered
- chunk edges cannot be solved in isolation

Recommended first implementation:

- only run lighting when N/S/E/W neighbor chunk data exists
- include seam voxels in propagation frontier
- defer meshing until lighting is ready for center + required neighbors

This is simpler than speculative partial lighting.

## World Edit / Relight Strategy

## Principle

Any edit affecting opacity invalidates skylight.

Examples:

- remove a solid block
- place a solid block
- place/remove leaves
- structure stamping

## Edit invalidation rules

### Interior edits

If edit is fully interior:

- mark owner chunk lighting stale

### Boundary edits

If edit touches chunk edge:

- mark owner chunk lighting stale
- mark touching neighbor chunk lighting stale

### Vertical implication

If edit changes opacity:

- column above/below may change dramatically
- lateral propagation may also change

So relight must not be a tiny local mesh-only patch.

For first implementation:

- relight the whole affected chunk
- relight touching neighbor chunks when edge is involved

This is not the most optimized approach, but it is much safer.

## Mesh Dependency Update

Today meshing depends on chunk occupancy plus neighbor occupancy.

After skylight:

mesh output depends on:

- center data version
- center light version
- neighbor data versions
- neighbor light versions

Suggested mesh signature contents:

- center `dataVersion`
- center `lightVersion`
- N/S/E/W neighbor `dataVersion`
- N/S/E/W neighbor `lightVersion`
- diagonals too only if AO/visibility still require them

## Recommended Module Plan

### `world/chunk_lighting.*`

Owns:

- skylight initialization
- flood propagation
- seam propagation helpers

### `world/light_queue.*`

Owns:

- lighting job queue/result types

### `world/chunk_scheduler.*`

Extend to include:

- `should_light()`
- light prioritization

### `world/chunk_record.*`

Extend with:

- `LightState`
- `lightVersion`
- `litAgainstSignature`

## Migration Plan

## Phase 1: State and Signatures

### TODO

- [ ] add `LightState` to chunk runtime
- [ ] add `lightVersion` to chunk runtime
- [ ] add `litAgainstSignature` to runtime
- [ ] extend debug UI to show light state/version
- [ ] update mesh signature model to include lighting versions

### Exit criteria

- chunk runtime can explicitly represent missing/stale lighting

## Phase 2: Authoritative Skylight Solve

### TODO

- [ ] add `chunk_lighting.*`
- [ ] implement top-down skylight initialization
- [ ] implement lateral flood propagation
- [ ] write final `_sunlight` values into solved chunk data
- [ ] remove terrain-time fake sunlight assumptions from generation path

### Exit criteria

- structures like trees darken the ground beneath them within a chunk

## Phase 3: Scheduler Integration

### TODO

- [ ] add `should_light()` to scheduler
- [ ] queue lighting jobs after data generation or data edits
- [ ] require lighting readiness before meshing
- [ ] discard stale light job results by generation/version/signature

### Exit criteria

- no chunk meshes with missing/stale lighting data

## Phase 4: Boundary-Correct Lighting

### TODO

- [ ] define required lighting neighbors
- [ ] propagate skylight across N/S/E/W chunk seams
- [ ] ensure seam voxels agree on `_sunlight`
- [ ] add seam-focused debug testing

### Exit criteria

- no visible lighting seams at chunk borders
- tree shadows continue correctly across chunk boundaries

## Phase 5: World Edit Relighting

### TODO

- [ ] mark lighting stale on opacity-changing block edits
- [ ] relight owner chunk after edit
- [ ] relight touching neighbors for edge edits
- [ ] ensure edit-triggered relight does not pop meshes out

### Exit criteria

- placing/removing blocks updates skylight deterministically

## Phase 6: Visual Tuning

### TODO

- [ ] adjust sunlight attenuation model
- [ ] tune interaction between AO and skylight
- [ ] reduce over-darkening when AO and low sunlight stack together
- [ ] validate under trees, cliffs, cave mouths, shorelines

### Exit criteria

- lighting looks intentional and readable in the target cube-world style

## Implementation Considerations

## 1. AO and skylight stacking

Once real skylight exists, AO may become too dark if multiplied directly with low sunlight.

Possible solution:

- keep AO subtle
- clamp minimum combined shading
- or blend AO against ambient term instead of pure multiply

This should be tuned after skylight is working.

## 2. Full-chunk relight first, local relight later

Incremental local relight is possible, but not the best first move.

Safer first implementation:

- relight whole affected chunk(s)
- keep correctness first

Optimize later if needed.

## 3. Structures must be included before lighting

Do not solve skylight on terrain-only data and then stamp trees afterward.

Correct order:

1. generate terrain
2. apply structures
3. solve skylight
4. mesh

## 4. Missing neighbors policy

Do not light a chunk against missing required neighbors in the first pass.

Recommended policy:

- wait for required N/S/E/W chunk data
- then solve lighting

This avoids seam disagreement.

## 5. Debugging tools

Strongly recommended debug additions:

- chunk debug view showing `LightState`
- light version counters
- option to visualize `_sunlight` as grayscale or false color
- logging for stale light result rejection

These will make the system much easier to validate.

## Risks

### Risk: stale lighting jobs overwrite newer data

Mitigation:

- generation id
- data version
- neighborhood signature checks

### Risk: seam mismatches between chunks

Mitigation:

- canonical boundary rules
- required-neighbor policy
- seam-focused tests

### Risk: edit-time hitching

Mitigation:

- keep lighting async
- prioritize nearby edited chunks
- full-chunk relight first, optimize later

## Testing Checklist

### Generation tests

- [ ] isolated flat terrain lights correctly
- [ ] trees cast shadows under leaves
- [ ] structure-generated chunks relight correctly before meshing

### Seam tests

- [ ] cliff crossing chunk border has no seam
- [ ] tree spanning chunk boundary shades both chunks consistently
- [ ] shoreline across seam has matching light values

### Edit tests

- [ ] break roof block and sunlight opens correctly
- [ ] place block above lit area and sunlight updates correctly
- [ ] edit at chunk edge relights both sides
- [ ] no chunk pop-out during relight/remesh transition

### Regression tests

- [ ] no lost chunks after lighting integration
- [ ] no stale lighting accepted after rapid edits
- [ ] meshing waits for valid lighting

## Recommended First Implementation Order

1. Add `LightState`, `lightVersion`, and debug visibility
2. Implement authoritative per-chunk skylight solve
3. Integrate lighting jobs into scheduler
4. Require lighting readiness before meshing
5. Add N/S/E/W seam correctness
6. Add edit-triggered relight
7. Tune AO + skylight visual blend

## Final Recommendation

Do skylight before any more advanced lighting work.

It directly addresses the current visual gap:

- no tree shadows
- no structure-aware sky blocking

It also fits the architecture you already refactored:

- scheduler-driven correctness
- versioned async jobs
- canonical boundary handling
- edit-first world mutation pipeline

This is the highest-value next lighting system for the engine.
