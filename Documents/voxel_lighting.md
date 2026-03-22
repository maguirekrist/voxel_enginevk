# Voxel Lighting Plan

## Purpose

Define a lighting architecture for runtime voxel objects, chunk terrain, and future entity-held light sources that preserves the performance benefits of pre-baked chunk lighting while supporting dynamic runtime light emitters.

This plan is intentionally broader than "make voxel props respond to lamps." The real goal is to establish one shared world-lighting model that can scale to:

- forest flowers and world props
- dropped voxel items
- future voxel entities and equipment
- moving lights such as torches, lanterns, spells, and glow effects

## Core Decision

Keep pre-baked chunk lighting.

Do not replace it with fully dynamic lighting.

Instead, split lighting into two layers:

- `Static / baked lighting`
  - solved into chunk data
  - remains the main lighting source for terrain
  - cheap and stable
- `Dynamic runtime lighting`
  - registered at runtime by moving or temporary emitters
  - sampled on demand
  - layered on top where needed

This gives the engine both:

- chunk-scale performance from baking
- correct visual response to moving light sources

## Problem Statement

Today:

- chunks use the `tri_mesh` shader path and receive meaningful baked per-vertex light inputs
- runtime voxel objects also use `tri_mesh`, but their meshes do not carry chunk-baked local light data
- only block-defined light emitters participate in the chunk lighting solve
- moving future light sources, such as a torch in the player's hand, have no clean path to affect either chunks or voxel objects

This creates a mismatch:

- terrain reacts to baked block light
- voxel props/entities do not
- future dynamic lights would have no unified way to affect the world

## Goals

- Make runtime voxel objects respond to world lighting.
- Preserve baked chunk lighting as the primary terrain lighting path.
- Support dynamic point lights from runtime systems.
- Allow both chunks and voxel objects to consume the same world-light model.
- Avoid rebaking voxel meshes when instances move.
- Avoid double-lighting chunk geometry.
- Keep the design compatible with future entities, equipment, and assemblies.

## Non-Goals

- Full physically based lighting.
- Shadow casting from dynamic lights.
- Full deferred lighting.
- Replacing chunk light propagation with fully dynamic runtime lighting.
- Rebuilding chunk meshes every frame for moving lights.

## Current State

### Shared Shader

Opaque chunks and runtime voxel objects both currently use:

- [tri_mesh.vert](/C:/Users/magui/source/repos/voxel_enginevk/shaders/tri_mesh.vert)
- [tri_mesh.frag](/C:/Users/magui/source/repos/voxel_enginevk/shaders/tri_mesh.frag)

through the `defaultmesh` material.

### Important Difference

Chunks and voxel objects do not differ primarily by shader. They differ by input data:

- chunk meshes are generated with baked vertex lighting inputs
- voxel runtime meshes currently use neutral/default lighting inputs

So the missing system is not "invent a completely separate voxel shader" by default. The missing system is lighting data and lighting mode selection.

## Design Principles

- Static lighting remains dominant for terrain.
- Dynamic lighting is additive and selective, not a wholesale replacement.
- Runtime voxel objects should sample light, not rebake meshes.
- One shared world-lighting service should feed both chunks and voxel objects.
- Lighting inputs must distinguish between:
  - baked chunk lighting
  - sampled runtime lighting
- Render code should not silently double-apply both.

## Architecture

## 1. Baked Lighting Layer

### Source

Chunk lighting remains solved from chunk/world block data:

- sunlight
- static local block emitters

### Ownership

This continues to live in chunk data and chunk lighting solve code.

### Usage

- chunk meshes continue using baked light as their base lighting
- runtime voxel objects will be able to sample baked light from chunk data at world positions

This is important:

- baked lighting is not only for chunk meshes
- it should also be queryable as world lighting data

## 2. Dynamic Light Layer

Introduce a runtime light source registry.

### Examples

- player-held torch
- lantern item
- magic projectile
- glowing entity
- temporary area light
- future emissive voxel entity part

### Expected Data Shape

```cpp
struct DynamicPointLight
{
    uint64_t id;
    glm::vec3 position;
    glm::vec3 color;
    float intensity;
    float radius;
    uint32_t affectMask;
    bool active;
};
```

### Notes

- these lights are runtime-owned, not chunk-owned
- they are sampled at render/runtime, not baked into chunk data
- first pass can support only point lights

## 3. World Light Sampling Service

Introduce a shared query layer that combines:

- baked chunk light
- dynamic runtime lights

Suggested boundary:

```cpp
struct SampledWorldLight
{
    glm::vec3 bakedLocalLight;
    float bakedSunlight;
    glm::vec3 dynamicLight;
};

class WorldLightSampler
{
public:
    [[nodiscard]] SampledWorldLight sample(glm::vec3 worldPosition, uint32_t affectMask) const;
};
```

### Responsibilities

- read baked light from lit chunk data at a world position
- accumulate nearby dynamic point lights
- return a stable combined lighting payload for runtime consumers

### Why this matters

This prevents lighting logic from being duplicated across:

- chunk rendering
- voxel prop rendering
- future entity rendering

## 4. Lighting Modes For Renderables

To prevent double-lighting, renderables must declare how they consume lighting.

Suggested modes:

```cpp
enum class LightingMode : uint8_t
{
    BakedChunk = 0,
    SampledRuntime = 1,
    BakedPlusDynamic = 2,
    Unlit = 3
};
```

### Expected Usage

- `BakedChunk`
  - chunk opaque mesh base path
  - uses baked vertex lighting
- `SampledRuntime`
  - runtime voxel props/items/entities
  - uses sampled lighting payload
- `BakedPlusDynamic`
  - chunk terrain when dynamic-light overlay is enabled
- `Unlit`
  - debug visuals, indicators, gizmos

This is the key to avoiding accidental double-application of light.

## 5. Shader Strategy

Do not fork a totally separate voxel lighting shader as the first move.

### Recommended First Approach

Keep the existing `tri_mesh` shader family and extend it to support per-object lighting mode and per-object sampled light inputs.

### Why

- chunks and voxel objects are both opaque lit triangle meshes
- the real difference is input lighting source, not shading model
- forking shaders too early increases drift and maintenance cost

### Important Constraint

The shader must not blindly add:

- baked chunk vertex lighting
- sampled runtime lighting

for every object.

Instead, it must select behavior based on object lighting mode.

## 6. Runtime Voxel Object Lighting

Runtime voxel objects should not rebake lighting into their meshes.

### Why

- many voxel instances move
- nearby lights may change
- rebaking meshes on transform/light changes is the wrong architecture

### First Sampling Rule

Sample one lighting payload per runtime voxel instance.

Suggested initial sample point:

- instance origin / pivot / placement position

This is good enough for:

- flowers
- chests
- dropped items
- small props

### Later Refinements

- sample at feet/base for grounded props
- sample per-part for assemblies
- sample multiple points for large props/entities if needed

## 7. Chunk Dynamic Light Response

Chunks should keep baked lighting as their base.

If dynamic lights are added, chunks should optionally receive:

- a runtime dynamic-light overlay

not a rebaked full lighting solve.

### Meaning

- terrain remains cheap
- moving torchlights can still brighten nearby terrain

### Important Distinction

This does not mean the chunk lighting solver must rerun every frame.
It means the terrain shader can add a dynamic-light contribution at draw time.

## 8. Affect Masks / Filtering

Long term, dynamic lights should support routing masks.

Example:

```cpp
enum LightAffects : uint32_t
{
    AffectWorld   = 1 << 0,
    AffectProps   = 1 << 1,
    AffectEntity  = 1 << 2,
    AffectAll     = 0xFFFFFFFFu
};
```

### First Pass Recommendation

Keep it simple:

- all dynamic lights affect both terrain and voxel objects

### Later Use Cases

- UI/debug-only lights
- entity-only glow
- world-only environmental light
- authored structure-local light groups

This should be explicit configuration, not accidental divergence between chunk and voxel systems.

## Implementation Phases

## Phase 1: World Light Sampling API

- Expose baked local-light query from lit chunk data.
- Define a `SampledWorldLight` payload.
- Add a shared `WorldLightSampler` service boundary.

Exit criteria:

- runtime code can sample baked world light at an arbitrary world-space position

## Phase 2: Runtime Voxel Lighting Inputs

- Extend runtime voxel instance/render submission with:
  - lighting mode
  - sampled light payload
- Add per-instance lighting sync before render submission.

Exit criteria:

- runtime voxel props can respond to baked world light without rebaking meshes

## Phase 3: Shader / Material Integration

- Extend `tri_mesh` shader inputs to support sampled runtime light
- Add lighting mode selection in shader path
- Ensure chunks do not get double-lit

Exit criteria:

- one shader family supports chunk and runtime voxel lighting modes correctly

## Phase 4: Dynamic Light Source System

- Add runtime point light registration/update/removal
- Add dynamic light sampling in `WorldLightSampler`
- Feed dynamic light results to voxel props/entities first

Exit criteria:

- moving runtime lights affect voxel objects

## Phase 5: Dynamic Light Overlay For Chunks

- Add dynamic-light overlay path for chunk terrain shading
- Keep baked lighting as base

Exit criteria:

- moving lights can affect terrain visually without rebaking chunk lighting

## Phase 6: Entity / Assembly Integration

- Route sampled lighting through `VoxelModelComponent`
- Support per-part sampling for assemblies when needed
- Allow equipped torches / emissive items to both emit and receive light

Exit criteria:

- future voxel entities/equipment can participate in the same lighting model

## Risks

### Double Lighting

Risk:

- chunks receive both baked and sampled light unintentionally

Mitigation:

- explicit `LightingMode`
- separate baked and sampled light paths

### Over-Forking Shader Paths

Risk:

- chunk and voxel shaders diverge unnecessarily

Mitigation:

- keep one shader family first
- fork only if real behavioral divergence appears later

### Runtime Cost Explosion

Risk:

- every object samples too many dynamic lights every frame

Mitigation:

- first pass uses one sample per small runtime instance
- later add radius culling and spatial partitioning for dynamic lights

### Wrong Ownership

Risk:

- `GameScene` or ad hoc systems own lighting decisions directly

Mitigation:

- introduce a shared `WorldLightSampler`
- keep light source ownership in runtime systems, not scenes

## Validation Plan

- Unit tests for baked light sampling from chunk data
- Unit tests for runtime sampled-light payload generation
- Unit tests for lighting mode routing logic
- Shader/path smoke tests for:
  - chunk only baked
  - voxel prop sampled baked
  - voxel prop sampled dynamic
  - chunk baked plus dynamic overlay
- Manual runtime checks:
  - flower near lamp block
  - chest near glowing object
  - moving torch in player hand

## Recommended First Slice

Implement in this order:

1. Add baked light sampling from chunk data
2. Add `WorldLightSampler`
3. Add `LightingMode` to runtime render objects
4. Add sampled-light input for runtime voxel props
5. Make flowers/chests react to baked local light
6. Add dynamic point light registry after that

This preserves chunk baking while immediately solving the current voxel-object lighting gap.

## Summary

The correct plan is not:

- "replace baking with dynamic lights"

The correct plan is:

- keep baked chunk lighting as the foundation
- add a shared world-light sampling layer
- let runtime voxel objects sample light
- later overlay dynamic runtime lights onto both chunks and voxel objects

That gives the engine a scalable path for:

- forest flowers
- props
- voxel entities
- equipped torches
- future moving emissive gameplay objects

## Implementation Status

Completed in the current engine slice:

- [x] Added `DynamicLightRegistry`
- [x] Added `WorldLightSampler`
- [x] Added baked world-light sampling from lit chunk data
- [x] Added runtime dynamic point-light sampling with affect masks
- [x] Added `LightingMode` to render objects
- [x] Added sampled-light payload plumbing for runtime voxel render instances
- [x] Extended `tri_mesh` to distinguish baked chunk, sampled runtime, baked-plus-dynamic, and unlit paths
- [x] Wired chunk terrain to `BakedPlusDynamic`
- [x] Wired runtime voxel props/decorations to sampled runtime lighting
- [x] Added a `GameScene` player torch dynamic-light debug path for validation
- [x] Added unit coverage for baked and dynamic light sampling

Still future work:

- [ ] Route lighting through higher-level entity/component systems beyond the current voxel model component path
- [ ] Add richer authored/runtime light ownership outside `GameScene` debug controls
- [ ] Add optional multi-sample lighting strategies for large voxel assemblies
