# Voxel Assembly Plan

## Purpose

Define the first dedicated plan for authored voxel assemblies: multi-part voxel entities composed from saved voxel objects, editable in a new assembly scene, persistable as named assets, and ready for future animation without requiring animation support in this first pass.

This document expands the assembly direction briefly introduced in [docs/voxel_runtime_integration_plan.md](/C:/Users/magui/source/repos/voxel_enginevk/docs/voxel_runtime_integration_plan.md) and turns it into a concrete architecture and implementation plan.

## Goals

- Add a `VoxelAssemblyScene` for composing multi-part voxel entities.
- Let designers build assemblies from previously saved voxel object assets.
- Persist assemblies as named authored assets.
- Reuse the voxel editor orbit camera interaction model:
  - mouse yaw
  - mouse pitch
  - mouse zoom
- Provide the editing tools required to place, orient, bind, and organize parts.
- Support dynamic runtime replacement of parts by role or slot, not by hard-coded player-specific logic.
- Support multiple attachment states for the same logical item or part.
- Keep the runtime design ready for future animation/pose layers.

## Non-Goals

- Full animation authoring or playback in this phase.
- Bone/skinning support.
- Per-voxel deformation.
- Nested assemblies in v1.
- Gameplay equipment rules beyond the data/runtime hooks needed to support them.
- Network replication design.

## Design Principles

- Separate authored assembly data from runtime posed state.
- Use stable identifiers, never implicit array order, for any part or slot that gameplay will target.
- Keep voxel object assets immutable and reusable; assemblies only reference them.
- Avoid player-specific or item-specific branching in the assembly core.
- Prefer explicit binding/state data over naming conventions or hard-coded attachment coordinates.
- Keep editor concerns, authored asset concerns, and runtime instance concerns as separate layers.
- Design the transform graph so animation can later override local transforms without replacing the asset model.

## Conceptual Model

The assembly system should distinguish four concepts:

- `VoxelModel`
  - Existing authored voxel object asset.
  - Owns voxel content, pivot, and local attachments/sockets.
- `VoxelAssemblyAsset`
  - New authored asset describing how multiple voxel models are composed.
  - Contains parts, slots, states, and default transforms/bindings.
- `VoxelAssemblyComponent`
  - Runtime component referencing a `VoxelAssemblyAsset`.
  - Owns mutable state such as current slot selections, visibility, and future pose overrides.
- `VoxelAssemblyRenderInstance`
  - Runtime evaluated graph of renderable part instances after all bindings and replacements are resolved.

## Why Assembly Assets Need Their Own Format

A voxel object answers "what geometry is this?" An assembly answers "how do these objects relate, and what can replace them at runtime?" Those are different responsibilities and should not be mixed into `VoxelModel`.

If assembly data were pushed into raw voxel object files, reuse would degrade quickly:

- torso assets would become polluted with player-specific hand/sword logic
- equipment swaps would require ad hoc conventions
- future animation state would be harder to layer cleanly

The correct boundary is:

- `VoxelModel`: geometry + local sockets
- `VoxelAssemblyAsset`: composition + slot/state rules
- `VoxelAssemblyComponent`: runtime state

Future versions may also allow a `VoxelAssemblyAsset` to reference another `VoxelAssemblyAsset` as a nested child assembly, but that should remain out of scope for v1.

## Authoring Architecture

### New Scene

Add `VoxelAssemblyScene`.

Responsibilities:

- load saved voxel model ids into a browsable list/menu
- create a new assembly or load an existing one
- assign assembly `assetId` and `displayName`
- add/remove parts that reference saved voxel model assets
- edit part transforms relative to a parent binding
- preview the composed result in 3D
- save/load assembly assets through a dedicated repository

The scene should mirror the orbit interaction feel of [src/scenes/voxel_editor_scene.h](/C:/Users/magui/source/repos/voxel_enginevk/src/scenes/voxel_editor_scene.h):

- drag to orbit yaw/pitch
- wheel to zoom
- orbit around a stable preview target

### Scene UI Regions

Recommended first layout:

- left panel: saved voxel model browser and add-part actions
- center viewport: 3D assembly preview
- right panel: selected part inspector
- bottom or tabbed panel: assembly hierarchy, slots, and state definitions

### Authoring Tools

First-pass editing tools should include:

- create new assembly
- load existing assembly
- save assembly
- add part from saved voxel model
- rename part
- assign stable part id
- set parent part
- choose parent attachment/socket
- edit local position/rotation/scale offset
- toggle part visibility in the preview
- duplicate/remove part
- reorder purely for editor presentation
- define a slot on a part or logical mount point
- define state variants for slot bindings
- preview slot/state combinations
- validate missing assets/sockets

Out of scope for first pass:

- animation timeline UI
- keyframes
- blend trees

## Runtime Architecture

### Asset Layer

Add:

- `VoxelAssemblyAsset`
- `VoxelAssemblyRepository`
- `VoxelAssemblyAssetManager`

Responsibilities:

- repository handles persistence
- asset manager handles load/cache lifecycle
- assembly asset is immutable after load

### Runtime Component Layer

Add `VoxelAssemblyComponent`.

Core responsibility:

- reference one authored assembly asset
- store runtime slot selections and state flags
- evaluate final part bindings for rendering

This should become the replacement path for multi-part characters and composite items. Single-model props should continue using `VoxelModelComponent`.

### Evaluated Runtime Graph

At runtime, assembly evaluation should resolve:

- which parts are active
- which model each logical part currently uses
- which parent each part is bound to
- which attachment/socket on the parent is active
- what local override transform applies

This evaluated graph should then emit normal voxel render instances, one per resolved visible part, using the existing voxel runtime asset pipeline.

That keeps rendering simple:

- assembly system computes transforms and asset selection
- existing voxel render path draws the parts

## Core Data Model

### Part Definitions

A part definition represents one logical component of an assembly, not necessarily one permanently fixed model.

Proposed shape:

```cpp
struct VoxelAssemblyPartDefinition
{
    std::string partId;
    std::string displayName;
    std::string defaultModelAssetId;
    std::string parentPartId;
    std::string parentAttachmentName;
    glm::vec3 localPositionOffset{0.0f};
    glm::quat localRotationOffset{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 localScale{1.0f};
    bool visibleByDefault{true};
    std::string slotId;
};
```

Rules:

- `partId` is the stable authored identifier.
- `displayName` is editor-facing only.
- `defaultModelAssetId` is the fallback model for this logical part.
- `slotId` is optional; it links the part to a runtime-replaceable logical slot.
- v1 allows at most one `slotId` per part definition.

### Slots

A slot is the gameplay-facing replacement surface.

In v1, a part may optionally declare a single slot binding. Future versions may allow slots to be authored independently of part definitions where needed.

Examples:

- `head`
- `left_hand`
- `right_hand`
- `back_attachment`
- `main_weapon`
- `offhand_weapon`
- `gloves`

Proposed shape:

```cpp
struct VoxelAssemblySlotDefinition
{
    std::string slotId;
    std::string displayName;
    std::string fallbackPartId;
    bool required{false};
};
```

Important rule:

- gameplay should target `slotId`, not raw `partId`, unless it intentionally needs a specific authored part

This is what enables "replace the left and right gloves/feet/hands models" style logic without hard-coding player body layout into the engine core.

### Binding States

A single logical item or part may need multiple authored bind targets depending on state.

Examples:

- sword on back when unequipped
- sword in right hand when equipped
- dagger in each hand when dual wielding
- staff bound to both hands in a future pass

To support that, bindings should be stateful.

Proposed shape:

```cpp
struct VoxelAssemblyBindingState
{
    std::string stateId;
    std::string parentPartId;
    std::string parentAttachmentName;
    glm::vec3 localPositionOffset{0.0f};
    glm::quat localRotationOffset{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 localScale{1.0f};
    bool visible{true};
};
```

Then a stateful part can declare:

```cpp
struct VoxelAssemblyPartBindingSet
{
    std::string defaultStateId;
    std::unordered_map<std::string, VoxelAssemblyBindingState> states;
};
```

This keeps state changes data-driven. Runtime systems can switch a state id instead of rewriting transforms ad hoc.

### Runtime Overrides

Runtime should support lightweight overrides separate from authored asset data.

Examples:

- replace current `main_weapon` model asset id
- switch `main_weapon` state from `sheathed` to `equipped_right`
- temporarily hide `helmet`

Proposed runtime shape:

```cpp
struct VoxelAssemblySlotOverride
{
    std::string slotId;
    std::optional<std::string> modelAssetIdOverride;
    std::optional<std::string> activeStateIdOverride;
    std::optional<bool> visibleOverride;
};
```

This should live on the runtime component, not on the authored asset.

## Persistence Format

Add a dedicated persisted assembly asset under a new repository path such as:

- `models/voxel_assemblies/`

Recommended JSON shape:

```json
{
  "version": 1,
  "assetId": "player_base",
  "displayName": "Player Base",
  "rootPartId": "torso",
  "parts": [
    {
      "partId": "torso",
      "displayName": "Torso",
      "defaultModelAssetId": "player_torso",
      "visibleByDefault": true
    },
    {
      "partId": "left_hand",
      "displayName": "Left Hand",
      "defaultModelAssetId": "player_left_hand",
      "slotId": "left_hand",
      "binding": {
        "defaultStateId": "default",
        "states": [
          {
            "stateId": "default",
            "parentPartId": "torso",
            "parentAttachmentName": "left_hand",
            "localPositionOffset": { "x": 0.0, "y": 0.0, "z": 0.0 },
            "localRotationOffset": { "x": 0.0, "y": 0.0, "z": 0.0, "w": 1.0 },
            "localScale": { "x": 1.0, "y": 1.0, "z": 1.0 },
            "visible": true
          }
        ]
      }
    }
  ],
  "slots": [
    {
      "slotId": "left_hand",
      "displayName": "Left Hand",
      "fallbackPartId": "left_hand",
      "required": true
    }
  ]
}
```

Notes:

- Store quaternions or another non-gimbal-prone rotation representation in persistence.
- Use `version` from day one.
- Keep ids stable and string-based for save compatibility.
- Do not store transient runtime selection state in the asset.

## Transform and Binding Rules

### Root Part

- Every assembly has exactly one root part.
- Root part resolves from the entity/world transform.
- Root part may still have a local authored offset.

### Child Parts

- Child part transform is composed from:
  - parent world transform
  - parent socket transform
  - selected binding-state local offset
  - child model pivot

### Slots vs Parts

- `partId` identifies authored structure.
- `slotId` identifies gameplay replacement targets.
- in v1, slots are authored through part definitions rather than as independent authored nodes
- Multiple parts may share a conceptual category but should not share the same `partId`.
- A slot should resolve to at most one active logical replacement target per assembly evaluation path unless a future explicit multi-bind type is introduced.

### Missing Data Behavior

- missing model asset: part is skipped and validation warning is emitted
- missing parent part: part is skipped and validation warning is emitted
- missing attachment/socket: part falls back to parent origin only if explicitly allowed; otherwise skipped
- missing override state id: fall back to authored default state

Fail visibly in tools, fail gracefully at runtime.

## Dynamic Replacement Strategy

The engine should avoid APIs like:

- `equip_player_glove_left(modelId)`
- `attach_sword_to_back()`

Those APIs lock the assembly system to one actor type.

Prefer generic operations on `VoxelAssemblyComponent`:

- `set_slot_model(slotId, modelAssetId)`
- `clear_slot_model(slotId)`
- `set_slot_state(slotId, stateId)`
- `set_part_visibility(partId, visible)`

Example:

- equipping gloves could set `left_hand` and `right_hand` slot model overrides
- equipping a two-handed staff could set:
  - `main_weapon` model override to `staff_oak`
  - `main_weapon` state override to `equipped_two_hand`
- unequipping could switch the same slot state to `sheathed_back`

This keeps gameplay systems declarative and reusable across players, monsters, NPCs, and props.

## Animation Readiness

Animation is not part of this first pass, but the design must leave a clean insertion point.

Future animation support should override evaluated local pose inputs, not replace assembly definitions.

Recommended future layering:

- `VoxelAssemblyAsset`
  - static authored graph
- `VoxelAssemblyPose`
  - current per-part local transform overrides produced by animation
- `VoxelAssemblyComponent`
  - current slot selections, state selections, and pose reference

That means this plan should already avoid baking "current transform" directly into immutable asset definitions at runtime.

Prepare now for:

- per-part local pose overrides
- additive item/slot state changes
- animation-driven socket transforms
- future partial-body animation masks

## Editor Validation Requirements

`VoxelAssemblyScene` should validate:

- duplicate `partId`
- duplicate `slotId`
- missing referenced voxel model asset
- missing referenced parent part
- missing referenced parent socket
- root cycles / graph cycles
- unreferenced states
- invalid root part

The scene should surface validation inline before save and on load.

## Recommended Class Set

Authoring/persistence:

- `VoxelAssemblyAsset`
- `VoxelAssemblyRepository`
- `VoxelAssemblyScene`

Runtime:

- `VoxelAssemblyAssetManager`
- `VoxelAssemblyComponent`
- `VoxelAssemblyEvaluator`
- `VoxelAssemblyResolvedPart`
- `VoxelAssemblyComponentAdapter`

Separation of responsibilities:

- repository: disk IO and version upgrades
- asset manager: loaded immutable cache
- evaluator: resolve slots/states/part graph into renderable parts
- component adapter: translate resolved parts into `VoxelRenderInstance`s

## Phased Implementation Plan

## Phase 1: Assembly Asset Domain

- add assembly domain structs
- add repository and JSON schema version 1
- add round-trip tests

Exit criteria:

- named assembly assets can be loaded and saved independent of runtime/editor

## Phase 2: Assembly Scene Skeleton

- add `VoxelAssemblyScene`
- load saved voxel model list from `VoxelModelRepository`
- create/load/save assembly assets
- add orbit camera controls matching `VoxelEditorScene`
- render a composed preview of parts

Exit criteria:

- designer can create a named assembly using saved voxel objects and save it

## Phase 3: Part Editing Tools

- add part hierarchy editor
- add parent socket selection
- add transform editing
- add slot definitions
- add binding state definitions
- add validation UI

Exit criteria:

- designer can author a player-like multi-part assembly with slot/state metadata

## Phase 4: Runtime Assembly Evaluation

- add `VoxelAssemblyComponent`
- add `VoxelAssemblyAssetManager`
- add evaluator that resolves parts into render instances
- integrate with runtime render submission

Exit criteria:

- one entity can render a composed assembly at runtime

## Phase 5: Dynamic Slot Replacement

- add runtime override API for slot model/state changes
- support fallback/default resolution
- add smoke-test gameplay integration for equipment swapping

Exit criteria:

- gameplay can replace a slot model and/or binding state without modifying authored assembly data

## Phase 6: Animation-Ready Refinement

- introduce explicit pose layer placeholder types
- ensure evaluator can accept future pose overrides
- tighten validation and transform tests around future animated paths

Exit criteria:

- assembly system can accept animation work later without structural redesign

## Risks

### Overloading Parts and Slots

- Risk:
  - part ids and slot ids get used interchangeably, causing unstable gameplay bindings.
- Mitigation:
  - document the distinction clearly and expose separate APIs for each.

### Hard-Coded Actor Logic

- Risk:
  - first player implementation leaks player-specific equipment rules into core assembly code.
- Mitigation:
  - require gameplay systems to use generic slot/state override APIs only.

### Transform Ambiguity

- Risk:
  - parent pivot, socket basis, and child offsets become difficult to reason about.
- Mitigation:
  - keep transform composition explicit and cover it with focused tests.

### Future Animation Rewrite

- Risk:
  - first-pass runtime mutates authored definitions directly and blocks clean pose layering.
- Mitigation:
  - keep immutable asset definitions separate from runtime override/pose state from the start.

## Validation Plan

- repository round-trip tests for assembly assets
- evaluator tests for root/child transform composition
- tests for slot override precedence
- tests for default-state fallback behavior
- tests for missing asset/socket graceful handling
- editor validation tests for cycles and duplicate ids
- runtime smoke test for:
  - base player assembly
  - weapon sheathed on back
  - weapon equipped in hand
  - left/right slot replacement

## Recommended First Vertical Slice

Build this order:

1. `VoxelAssemblyAsset` + repository + tests
2. `VoxelAssemblyScene` with model browser, save/load, orbit preview
3. simple part hierarchy authoring with parent socket selection
4. runtime evaluator producing multiple `VoxelRenderInstance`s
5. one `player_base` authored assembly rendered in `GameScene`
6. one generic runtime slot replacement demo

This keeps the first delivery practical:

- designers can author assemblies immediately
- runtime can consume them without waiting for animation
- gameplay gets a generic replacement surface early

## Initial Worklist

- [ ] Add `VoxelAssemblyAsset` domain structs.
- [ ] Add `VoxelAssemblyRepository`.
- [ ] Define versioned JSON schema for assembly assets.
- [ ] Add assembly repository round-trip tests.
- [ ] Add `VoxelAssemblyScene`.
- [ ] Mirror `VoxelEditorScene` orbit camera yaw/pitch/zoom controls.
- [ ] Load saved voxel model ids into the assembly scene.
- [ ] Add assembly part hierarchy editing UI.
- [ ] Add slot and binding-state editing UI.
- [ ] Add assembly validation UI.
- [ ] Add `VoxelAssemblyAssetManager`.
- [ ] Add `VoxelAssemblyComponent`.
- [ ] Add `VoxelAssemblyEvaluator`.
- [ ] Add `VoxelAssemblyComponentAdapter`.
- [ ] Add runtime slot override APIs.
- [ ] Add one runtime player assembly demo.
