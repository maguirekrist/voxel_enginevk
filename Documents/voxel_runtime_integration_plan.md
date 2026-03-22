# Voxel Runtime Integration Plan

## Purpose

Define the next implementation phase for taking authored voxel assets and using them as runtime-rendered items, props, and entity body parts.

This document is intentionally focused on runtime integration, not voxel editing. Editing and persistence are already covered in [voxel_format.md](/C:/Users/magui/source/repos/voxel_enginevk/Documents/voxel_format.md).

## Goals

- Load voxel assets at runtime without editor-only dependencies.
- Render voxel assets through a dedicated runtime instance path.
- Support items, static props, and entity body-part assemblies.
- Establish a first-class pivot and attachment/socket system for future skeleton work.
- Keep runtime ownership separate from authoring and repository concerns.
- Make the system scalable to many instances of the same voxel asset.

## Non-Goals

- Full skeletal animation in this phase.
- Per-voxel runtime deformation.
- Network replication design.
- Physics/collision generation beyond a first placeholder bounds model.
- LOD or mesh streaming optimizations beyond basic caching.

## Design Principles

- Asset data is immutable at runtime by default.
- Runtime instances reference shared asset and mesh state.
- Attachments are explicit metadata, not inferred from naming or hardcoded coordinates.
- Rendering concerns stay decoupled from gameplay/entity concerns.
- Editor scene and runtime scene must not share mutable state directly.

## Runtime Architecture

### 1. Asset Layer

- `VoxelModel`
  - Existing authored voxel asset.
  - Remains the source of truth for voxel content, pivot, and future metadata.
- `VoxelModelRepository`
  - Existing persistence boundary.
  - Runtime may use it at boot/load boundaries, but game code should not constantly hit disk.

### 2. Runtime Asset Cache

- `VoxelAssetManager`
  - Loads `VoxelModel` by asset id.
  - Builds or retrieves a cached shared mesh.
  - Owns asset lifetime, cache invalidation, and future hot-reload hooks.
- `VoxelRuntimeAsset`
  - Immutable loaded asset package.
  - Contains:
    - `assetId`
    - `VoxelModel`
    - shared meshed `Mesh`
    - local bounds
    - attachment/socket table

### 3. Attachment Metadata

Add lightweight attachment support directly to voxel assets.

Proposed first data shape:

```json
{
  "attachments": [
    {
      "name": "right_hand",
      "position": { "x": 4.0, "y": 10.0, "z": 2.0 },
      "forward": { "x": 1.0, "y": 0.0, "z": 0.0 },
      "up": { "x": 0.0, "y": 1.0, "z": 0.0 }
    }
  ]
}
```

Rules:

- Attachment coordinates are stored in voxel-local space before world transform.
- They are independent from mesh generation.
- They must remain stable identifiers for gameplay code.
- A missing attachment must fail gracefully at runtime.

### 4. Runtime Instance Layer

- `VoxelRenderInstance`
  - References a shared `VoxelRuntimeAsset`.
  - Stores transform state:
    - world position
    - world rotation
    - uniform scale if needed
  - Computes final model transform from:
    - instance transform
    - asset pivot
- `VoxelAttachmentInstance`
  - Optional child binding for one voxel asset attached to another asset’s socket.
  - First phase can support parent-child transform composition without full skeletons.

### 5. Entity Integration

Introduce a thin gameplay-facing component layer.

- `VoxelModelComponent`
  - For simple entities with a single voxel asset.
  - Example: potion, hammer, crate, dropped item.
- `VoxelAssemblyComponent`
  - For composed entities made of multiple voxel parts.
  - Example: ogre torso + head + hands + weapon.
  - Owns part instances and attachment relationships.

This is the bridge toward future skeleton/rig systems.

### 6. Rendering Integration

- Runtime voxel instances should be submitted through the normal scene renderer.
- They should not depend on editor render code, editor materials, or editor state.
- Shared meshes must be uploaded once and reused by many instances.
- Rendering path should support:
  - opaque voxel meshes first
  - future optional transparent/accent passes later

## Proposed Data Types

### Attachment Definition

```cpp
struct VoxelAttachment
{
    std::string name;
    glm::vec3 position;
    glm::vec3 forward;
    glm::vec3 up;
};
```

### Runtime Asset

```cpp
struct VoxelRuntimeAsset
{
    std::string assetId;
    VoxelModel model;
    std::shared_ptr<Mesh> mesh;
    VoxelBounds bounds;
    std::unordered_map<std::string, VoxelAttachment> attachments;
};
```

### Runtime Instance

```cpp
struct VoxelRenderInstance
{
    std::shared_ptr<VoxelRuntimeAsset> asset;
    glm::vec3 position;
    glm::quat rotation;
    float scale;
};
```

## Transform Rules

### Asset Space

- Local coordinates are authored in asset-local voxel space scaled by `voxelSize`.
- Asset-local coordinates may be signed.
- `0,0,0` is the asset-space origin, not a guarantee that geometry exists there.
- It is valid for `0,0,0` to be empty space.
- Pivot is applied here.

### Origin vs Pivot

- `origin`
  - The asset-local coordinate system origin.
  - This is the basis for voxel coordinates, sockets, and transforms.
  - It is not required to contain a voxel.
- `pivot`
  - The explicit local-space point used as the transform center for the asset.
  - Rotations and scaling should revolve around this point.
  - In the first editor pass, users may choose a voxel and the pivot can be set to that voxel center.
  - The persisted representation should remain a `vec3`, not just a voxel index, so the format can later support sub-voxel pivots without redesign.
- `socket/anchor`
  - A named local-space attachment point.
  - Independent from the asset pivot.

### Socket Space

- Socket transforms are defined relative to asset local space.
- Child assets inherit parent socket transform.

### World Space

- Final transform is:
  - `world * socket * childPivot`
  - or `world * assetPivot` for root instances

## Initial Use Cases

### Item Render

- A dropped item entity references one `VoxelModelComponent`.
- Runtime requests asset from `VoxelAssetManager`.
- Scene submits one render instance.

### Prop Render

- A placed prop in the world reuses the same asset/mesh cache.
- Only per-instance transform differs.

### Character Body Assembly

- Entity owns `VoxelAssemblyComponent`.
- Root part is body/torso.
- Child parts attach via named sockets.
- No animation yet, but the data model is compatible with later animated socket transforms.

## Implementation Phases

## Phase 1: Runtime Asset Pipeline

- Add attachment metadata to `VoxelModel`.
- Extend repository load/save for attachments.
- Introduce `VoxelRuntimeAsset`.
- Introduce `VoxelAssetManager`.
- Cache loaded asset + shared mesh by asset id.

Exit criteria:

- Runtime can load an asset by id and produce a shared mesh package.

## Phase 2: Runtime Render Instances

- Introduce `VoxelRenderInstance`.
- Add scene/render submission path for voxel instances.
- Render a simple world item/prop using a voxel asset.

Exit criteria:

- One asset can be instantiated many times with shared mesh state.

## Phase 3: Entity Components

- Add `VoxelModelComponent` for single-asset entities.
- Add `VoxelAssemblyComponent` for multi-part entities.
- Add initial transform hierarchy support.

Exit criteria:

- A composed multi-part entity can render from named part definitions.

## Phase 4: Attachment Authoring Support

- Add editor support for setting the model pivot from a selected voxel.
- Add editor visualization for the current pivot with a dedicated indicator render.
- Add editor support for creating/editing attachment sockets.
- Visualize sockets and local axes in the voxel editor.
- Persist sockets in asset files.

Exit criteria:

- Designers can author pivots and runtime attachment points without editing JSON manually.

## Phase 5: Skeleton-Ready Refinement

- Define a runtime interface for animated attachment transforms.
- Separate static attachment definitions from dynamic pose state.
- Establish compatibility surface for future skeleton/animation systems.

Exit criteria:

- Assembly runtime is ready to accept animated transforms later without redesign.

## Risks

### Asset / Runtime Coupling

- Risk:
  - Editor-only assumptions bleed into runtime classes.
- Mitigation:
  - Introduce explicit runtime asset/cache types and keep editor scene isolated.

### Mesh Upload Churn

- Risk:
  - Instances regenerate or reupload meshes too often.
- Mitigation:
  - Cache by asset id and upload once per runtime asset.

### Attachment Drift

- Risk:
  - Socket transforms become ambiguous or inconsistent across assets.
- Mitigation:
  - Store explicit basis vectors and validate orthogonality where possible.

### Future Skeleton Rewrite

- Risk:
  - Runtime assembly model is too rigid for later animation.
- Mitigation:
  - Keep attachment definitions static and treat pose transforms as a later layer.

## Validation Plan

- Unit tests for repository round-trip of attachment metadata.
- Unit tests for runtime asset cache hit/miss behavior.
- Unit tests for socket transform composition.
- Unit tests for pivot application and local-to-world transform correctness.
- One runtime scene smoke test rendering:
  - a single item
  - multiple reused props
  - one multi-part entity assembly

## Recommended First Implementation Slice

Implement in this order:

1. Extend `VoxelModel` and repository with attachment metadata.
2. Add `VoxelRuntimeAsset` and `VoxelAssetManager`.
3. Add a minimal runtime render instance path.
4. Spawn one voxel asset in `GameScene` as a world item/prop.
5. Add `VoxelModelComponent`.

This gives immediate runtime value without overcommitting to the full assembly system on the first pass.

## Worklist

- [ ] Explicitly support signed voxel-local coordinates in editor/runtime workflows.
- [ ] Add pivot metadata authoring to `VoxelModel`.
- [ ] Add editor action to set pivot from selected voxel.
- [ ] Add pivot indicator rendering in voxel editor.
- [x] Add attachment/socket metadata to voxel asset domain.
- [x] Persist attachments in repository load/save.
- [x] Add runtime asset package type.
- [x] Add runtime asset cache/manager.
- [ ] Add shared mesh upload lifecycle for runtime voxel assets.
- [x] Add shared mesh upload lifecycle for runtime voxel assets.
- [x] Add runtime voxel render instance submission.
- [ ] Add single-asset gameplay component.
- [ ] Add first multi-part assembly component.
- [x] Add transform composition tests.
- [x] Add one `GameScene` runtime demo entity using voxel assets.

## Current Status

- Implemented asset-level attachment metadata on `VoxelModel`.
- Repository now persists attachment metadata alongside pivot and voxels.
- Added `VoxelRuntimeAsset` as the immutable runtime package boundary.
- Added `VoxelAssetManager` to load/cache shared runtime voxel assets by asset id.
- Generalized mesh render submission to use full per-object model transforms instead of chunk-only XZ translation.
- Added `VoxelRenderInstance` for shared-asset runtime transforms.
- Added `VoxelRenderRegistry` for reusable voxel instance submission and shared mesh upload handling.
- Added a `GameScene` runtime voxel prop demo path that loads a saved asset id and spawns multiple shared-mesh world props near the player.
- Added tests for:
  - attachment round-trip persistence
  - runtime asset cache reuse and mesh generation
  - instance pivot/local-to-world transform correctness
  - attachment transform composition
