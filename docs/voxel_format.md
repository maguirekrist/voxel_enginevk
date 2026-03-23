# Voxel Format / Cube Assembly System

## Goals

- Introduce a persisted voxel-assembly asset format for characters, items, props, and monsters.
- Use a voxel unit size of `1 / 16` block by default so authored models scale naturally against world blocks.
- Convert sparse voxel data into a single render mesh, hiding interior faces.
- Keep the format dynamic and sparse so tiny items and very large assemblies both work.
- Build a dedicated voxel editor scene with IMGui tooling for authoring, previewing, and saving assets.
- Keep the architecture extensible toward future skeleton, attachment, and animation systems.

## Requirements

- Sparse voxel storage, not fixed chunk-sized storage.
- Stable persisted format with versioning.
- Mesh generation independent from chunk dimensions.
- Clear separation between:
  - Authoring data
  - Meshing/build pipeline
  - Runtime render instance
  - Persistence/repository access
- Editor must support:
  - Clicking a grid to place voxels
  - Rotating the editing grid
  - Changing grid dimensions / slice
  - Choosing the next voxel color
  - Saving to disk

## Proposed Architecture

### Core domain

- `VoxelColor`
  - Packed RGBA byte color for persistence and deterministic meshing.
- `VoxelCoord`
  - Integer voxel-space coordinate.
- `VoxelModel`
  - Sparse map of `VoxelCoord -> VoxelColor`.
  - Stores metadata like asset id, display name, voxel size, pivot, and bounds helpers.
- `VoxelModelRepository`
  - Loads/saves versioned JSON assets from `models/voxels/`.

### Build/runtime

- `VoxelMesher`
  - Reuses the chunk-mesher face emission pattern, but operates on sparse voxel coordinates.
  - Emits only visible faces.
  - Scales vertices by `voxelSize`.
  - Applies local pivot offset so future attachments/skeleton integration have a sane origin.
- `VoxelModelRenderState`
  - Owns the uploaded mesh and dirty state for editor/runtime preview.

### Scene/editor

- `VoxelEditorScene`
  - Uses IMGui for authoring controls and a 2D slice/grid editor.
  - Maintains the currently edited `VoxelModel`.
  - Regenerates the preview mesh whenever the model changes.
  - Saves through `VoxelModelRepository`.

## Persistence Format

Planned JSON shape:

```json
{
  "version": 1,
  "assetId": "ogre_body",
  "displayName": "Ogre Body",
  "voxelSize": 0.0625,
  "pivot": { "x": 0.0, "y": 0.0, "z": 0.0 },
  "voxels": [
    { "x": 0, "y": 0, "z": 0, "color": { "r": 120, "g": 160, "b": 80, "a": 255 } }
  ]
}
```

## Extension Points For Skeleton Work

- Pivot/origin is part of the asset now.
- Asset local origin and pivot are not the same thing:
  - `0,0,0` is the asset-space origin and may be empty space.
  - `pivot` is the explicit local-space transform center.
- Future work should add named attachment sockets and eventually bone/part references instead of baking them into raw voxel data.
- The voxel asset should stay independent from animation state so one model can feed many runtime rig instances.

## Live Worklist

- [x] Review current chunk meshing, rendering, scene, and config architecture.
- [x] Create this architecture/worklist document.
- [x] Implement sparse voxel domain types and bounds helpers.
- [x] Implement voxel JSON repository and asset path conventions.
- [x] Implement voxel mesher for single-mesh output.
- [x] Add voxel editor render state and mesh upload lifecycle.
- [x] Add `VoxelEditorScene` with IMGui slice editing, grid rotation, color selection, and save.
- [x] Wire scene registration / switching so the editor can be launched in-engine.
- [x] Add focused tests for voxel repository round-trip and mesh visibility rules.
- [x] Run build/tests and update this worklist with outcomes and follow-up items.
- [ ] Add editor affordances for multi-voxel box fills, eyedropper/color history, and pivot editing.
- [ ] Add editor affordances for setting the model pivot from a selected voxel and rendering a pivot indicator.
- [x] Add runtime-facing voxel model instance path separate from the editor preview path.
- [x] Add sockets / attachment metadata groundwork for future skeleton integration.
- [x] Add a post-structure world-decoration pass for chunk-owned voxel props.
- [x] Add visible-chunk runtime submission for world decorations (forest flowers).

## Notes During Implementation

- Initial editor interaction can use a 2D orthographic slice editor in IMGui while the 3D scene provides a live preview mesh.
- Chunk AO and lighting do not need to be copied into the first voxel mesher pass; keep the asset pipeline deterministic first.
- Keep the mesh output and repository interfaces decoupled so future import/export tooling can reuse them outside the live editor.
- Current verification status:
  - `cmake --build build --config Debug --target vulkan_guide` succeeded on March 22, 2026.
  - `cmake --build build --config Debug --target engine_tests` succeeded on March 22, 2026.
  - `engine_tests.exe --gtest_list_tests` includes runtime attachment/cache and decoration coverage on March 22, 2026.
  - `ctest --output-on-failure -C Debug` passed 31/31 on March 22, 2026.
