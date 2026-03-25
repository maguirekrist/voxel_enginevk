# Voxel Spatial, Orientation, and Bounds Plan

## Purpose

Formalize the engine-wide rules for voxel orientation, pivots, bounds, placement, and collision so authored voxel models and voxel assemblies behave consistently in editors, runtime rendering, and future gameplay/animation systems.

This plan is intentionally broader than a rendering tweak. It defines the long-term architecture needed for:

- players
- monsters
- chests
- decorations
- future animated assemblies
- eventual ECS-oriented runtime systems

## Why This Plan Exists

The current engine already supports:

- authored voxel model pivots
- authored model attachments/sockets
- authored voxel assemblies
- runtime assembly rendering through a reusable component path

However, several important semantics are still implicit or incomplete:

- local orientation is only conventionally standardized
- bounds exist on voxel models but are not yet a first-class gameplay/runtime concept
- assembly aggregate bounds are not yet derived as a reusable runtime product
- world placement currently uses pivot/root origin directly, which can cause assets to sink into terrain
- editor visualization for bounds and pivots is incomplete

Those gaps are manageable now, but they will become architectural debt once animation, more assemblies, and more gameplay systems are layered on top.

## Goals

- Standardize voxel orientation across models, assemblies, and future animation data.
- Make bounds first-class runtime data for both voxel models and voxel assemblies.
- Separate render pivot/origin from placement semantics and gameplay collision semantics.
- Add editor-quality visualization for pivots, bounds, and orientation.
- Keep the solution generic and reusable across entities and world objects.
- Keep the runtime architecture compatible with future ECS migration.

## Non-Goals

- Full animation authoring or animation playback in this pass.
- Replacing the existing player physics model in this pass.
- Final rigid body or physics-engine integration.
- Per-voxel collision.
- Automatic convex collision generation.

## Design Principles

- Treat orientation as an engine rule, not a loosely shared convention.
- Treat bounds as derived spatial data, not editor-only debug information.
- Keep authored render-space metadata separate from gameplay collision data.
- Avoid player-specific, chest-specific, or decoration-specific logic in the core voxel runtime.
- Ensure the same spatial abstractions work for single-model and multi-part entities.
- Keep authoring, runtime evaluation, and gameplay systems as separate layers.

## Standardized Orientation

### Engine-Wide Convention

Formalize the following as the authoritative voxel convention:

- `+X` = forward
- `+Y` = up
- `+Z` = right or lateral axis completing the right-handed basis

This convention must apply consistently to:

- `VoxelModel` local authoring
- `VoxelAttachment` forward/up bases
- `VoxelAssemblyAsset` local transforms
- future animation pose tracks
- runtime transform evaluation

### Practical Authoring Rule

When authoring a voxel object intended to face “forward” in gameplay:

- the front of the object should face local `+X`
- the top of the object should face local `+Y`

This should be explicitly stated in editor UI and documentation.

### Validation and Sanitization

The engine should continue normalizing attachment basis data and should tighten validation to catch:

- nearly parallel `forward` and `up`
- zero-length basis vectors
- invalid imported or hand-edited basis data

Validation should happen in:

- voxel editor authoring
- repository load paths where safe
- assembly validation UI where attachment references are consumed

## Spatial Concepts

The engine should explicitly distinguish the following concepts.

### 1. Origin

The local coordinate system origin for an authored asset.

- This is just coordinate space.
- It does not imply where geometry begins.
- It does not imply bottom contact with the world.

### 2. Pivot

The authored local-space transform reference point.

- Rotation and scaling are evaluated around this point.
- Pivot may be inside or outside the occupied voxel volume.
- Pivot is a render/local transform concept, not a collision concept.

### 3. Render Bounds

The visual extents of an asset after meshing.

- For `VoxelModel`, this is the AABB enclosing occupied voxels.
- For `VoxelAssembly`, this is the union of resolved visible part bounds after transform evaluation.
- Render bounds are used for debug display, culling in the future, spatial queries, and placement helpers.

### 4. Placement Policy

The rule that determines how an authored asset is aligned to a world position.

This must become explicit instead of being implicitly “place pivot at world position”.

Recommended placement policies:

- `Origin`
- `Pivot`
- `BottomCenter`
- `BoundsCenter`
- `NamedAttachment`
- future `FootContact`

### 5. Gameplay Collision Bounds

A gameplay-defined spatial volume used for interactions with the world.

- This should be allowed to differ from render bounds.
- The player already follows this pattern with a dedicated physics AABB.
- Other entities may eventually derive, override, or author their gameplay collision separately.

## Bounds Architecture

## Voxel Model Bounds

`VoxelModel` already computes occupied-voxel bounds. That should remain the raw source for model visual bounds.

Required next steps:

- make the model bounds an explicit runtime spatial product, not just an internal helper
- expose local-space min/max/center and derived world-space bounds through runtime APIs
- add clear editor visualization for these bounds

## Voxel Assembly Bounds

Add explicit aggregate assembly bounds evaluation.

Recommended output type:

```cpp
struct VoxelSpatialBounds
{
    bool valid{false};
    glm::vec3 localMin{0.0f};
    glm::vec3 localMax{0.0f};
    glm::vec3 worldMin{0.0f};
    glm::vec3 worldMax{0.0f};
};
```

For assemblies, aggregate bounds should be derived by:

1. evaluating visible resolved parts
2. computing each part’s transformed world-space bounds
3. unioning them into one assembly-level bounds result

This result should be reusable by:

- editor preview
- debug overlays
- runtime placement helpers
- future collision/culling systems

## Runtime Spatial Evaluation Layer

The runtime should not leave spatial reasoning buried in scenes.

Add or formalize a reusable voxel spatial evaluation layer in `src/voxel`.

Recommended responsibilities:

- evaluate single-model local/world bounds
- evaluate assembly aggregate bounds
- provide placement helper transforms
- expose attachment transforms and root transforms consistently
- later provide pose-aware bounds once animation exists

Recommended shape:

```cpp
struct VoxelSpatialEvaluation
{
    std::vector<VoxelRenderInstance> renderParts;
    VoxelSpatialBounds aggregateBounds;
};
```

This should be generic enough to support:

- `VoxelModelComponent`
- `VoxelAssemblyComponent`
- future animation/pose layers

## Placement and Grounding

The current implicit behavior of placing voxel objects/assemblies at their pivot/root origin is acceptable as a low-level render rule, but it is not sufficient as a long-term gameplay placement rule.

The engine should support explicit placement alignment.

### Recommended Runtime Placement Data

```cpp
enum class VoxelPlacementPolicy : uint8_t
{
    Pivot = 0,
    BottomCenter = 1,
    BoundsCenter = 2,
    NamedAttachment = 3
};
```

This should be usable by:

- entity spawn systems
- world decoration placement
- future item drop systems
- gameplay transformations such as polymorph/hex effects

Examples:

- flower decoration: `BottomCenter`
- chest: `BottomCenter`
- floating magical orb: `Pivot`
- weapon pickup aligned to grip/socket: future `NamedAttachment`

## Collision Strategy

The engine should not force one collision strategy for everything.

Recommended layered approach:

### Phase 1

Support collision derived from evaluated bounds where appropriate.

Use cases:

- simple props
- simple chests
- simple decorative assemblies that should block

### Phase 2

Allow authored or gameplay-specific collision override.

Use cases:

- player
- monsters
- interactables that need looser or tighter collision than visual bounds

### Architectural Rule

Render bounds and gameplay collision bounds should be related, but not identical by mandate.

That gives flexibility without losing consistency.

## Editor QoL and Debugging

## Voxel Editor

Add or formalize the following visualization toggles:

- `Show Pivot Marker`
- `Show Pivot Voxel`
- `Show Model Bounds`
- `Show Attachment Markers`
- `Show Attachment Axes`
- `Ghost Mesh`

Recommended behavior:

- bounds are always shown regardless of whether the pivot is inside the mesh
- ghost mesh makes the solid model partially transparent so pivot and bounds remain readable
- selected attachment shows both point marker and orientation basis

## Assembly Editor

Add or formalize:

- `Show Selected Part Bounds`
- `Show Assembly Bounds`
- `Show Selected Part Pivot`
- `Show Assembly Root Pivot`
- `Show Selected Part Attachments`
- `Show Parent Attachment Marker`
- `Show Part/Root Axes`
- `Ghost Selected Part`
- `Ghost Whole Assembly`

Recommended behavior:

- selected part bounds remain available
- aggregate assembly bounds shows the union across visible resolved parts
- root pivot marker shows the assembly-space origin/reference
- ghost rendering helps when the pivot or bounds lie inside dense geometry

## GameScene and Runtime Debugging

Add runtime debug overlays eventually for:

- assembly aggregate bounds
- resolved part bounds
- root pivot
- attachment markers where useful

This should not be editor-only. Runtime debug overlays help validate placement, collision, and animation later.

## ECS and Scalability Considerations

The runtime design should stay generic and system-oriented.

Recommended long-term separation:

- data components
  - `VoxelModelComponent`
  - `VoxelAssemblyComponent`
  - future `VoxelAnimationComponent`
  - future `VoxelCollisionComponent`
- runtime systems
  - asset managers
  - assembly evaluator
  - spatial bounds evaluator
  - render submission system
  - future animation pose system
  - future collision system

Scenes should remain clients of these systems, not owners of the logic.

That keeps the code adaptable as the project becomes more ECS-like.

## What Was Missing From the Earlier Summary

The previous runtime assembly summary should be extended with the following explicit work items:

- formalize `+X forward / +Y up` as the engine-wide voxel standard
- expose that convention in authoring and validation tools
- add explicit model and assembly bounds visualization in editor scenes
- add aggregate assembly bounds evaluation as reusable runtime data
- introduce explicit placement/alignment policy instead of relying on pivot/root origin for terrain alignment
- define how render bounds and gameplay collision bounds relate without forcing them to be identical

## Proposed Implementation Phases

## Phase 1: Orientation Formalization

- document `+X forward / +Y up` in engine docs and editor UI
- tighten attachment basis validation/sanitization
- add simple axis indicators in editors

Exit criteria:

- authors have one clear orientation rule
- attachment bases are validated consistently

## Phase 2: Bounds Visualization

- add `Show Model Bounds` to voxel editor
- add `Show Assembly Bounds` and `Show Assembly Root Pivot` to assembly editor
- add optional ghost mesh display in both editors

Exit criteria:

- pivots and bounds are visually inspectable even when inside geometry

## Phase 3: Runtime Aggregate Bounds

- add reusable runtime spatial bounds evaluation for models and assemblies
- expose aggregate assembly bounds
- add tests for transformed and chained assembly bounds

Exit criteria:

- runtime can compute world-space bounds for any voxel renderable object

## Phase 4: Placement Policy

- add explicit placement/alignment policy to runtime-spawned voxel content
- migrate world props/decorations away from implicit pivot-root grounding where appropriate

Exit criteria:

- flowers, chests, props, and assemblies can be placed consistently without manual pivot hacks

## Phase 5: Collision Integration

- add optional collision usage of evaluated voxel bounds
- keep gameplay override path for entities like player and monsters

Exit criteria:

- blocking voxel content can use bounds-based collision where appropriate
- custom gameplay collision remains possible where needed

## Phase 6: Animation Compatibility Pass

- ensure pose-aware bounds can later override or expand aggregate bounds
- ensure animation uses the same orientation standard
- keep animation as a separate layer after assembly structure evaluation

Exit criteria:

- the spatial system is ready for future animation work without redesign

## Open Questions

- Should simple chests and props default to bounds-based collision, or require explicit collision opt-in?
- Should `BottomCenter` become the default placement policy for world decorations?
- Do we want a future authored collision override asset, or keep collision purely gameplay-authored?
- Should assembly root pivot be persisted explicitly as a separate asset-level field, or remain assembly origin by definition?

## Recommendation

Yes, the current next-pass scope should include all of the following:

- orientation standardization
- editor pivot/bounds QoL
- runtime model and assembly bounds evaluation
- explicit placement policy
- collision architecture using those bounds where appropriate

That is the minimum coherent spatial foundation needed before animation and more complex voxel gameplay systems scale cleanly.
