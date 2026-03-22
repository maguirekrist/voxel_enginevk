# Game Systems Notes

## Purpose

Document the major gameplay/engine architecture patterns currently present in the project.

This file is meant to answer:

- what architectural style the project is using today
- what important systems already exist
- what boundaries are solid
- what is transitional
- what is underused or only partially realized

## Current Architectural Style

The project is **not** a full ECS.

It is currently a mix of:

- scene-driven engine orchestration
- subsystem-oriented world simulation
- object/component-style gameplay entities
- render registries for submitting runtime content

That means:

- world/chunk systems are already fairly system-oriented
- gameplay entity architecture is still maturing
- the `GameObject` + `Component` model exists, but is not the dominant architecture of the whole engine

## Component System

## What Exists

The current component model is centered on:

- [game_object.h](C:/Users/magui/source/repos/voxel_enginevk/src/components/game_object.h)

It provides:

- `GameObject`
- embedded `Component` base type
- `Add<T>()`
- `Get<T>()`
- `Has<T>()`
- a fixed-size component slot array

There is also:

- [component.h](C:/Users/magui/source/repos/voxel_enginevk/src/components/component.h)

but it is currently just a stub and is not the real source of the component model.

## What This Is

This is best described as:

- a **component-based object model**
- not a data-oriented ECS

Why it is not ECS:

- entities are still full objects, not lightweight IDs
- components are stored inside each object, not in central dense stores
- there is no main `System` layer iterating across central component tables
- behavior still lives on gameplay objects and related classes, not primarily in data-driven systems

## Current Components

The meaningful components currently in use are:

- [voxel_model_component.h](C:/Users/magui/source/repos/voxel_enginevk/src/components/voxel_model_component.h)
- [player_input_component.h](C:/Users/magui/source/repos/voxel_enginevk/src/components/player_input_component.h)

### `VoxelModelComponent`

This is the most effective use of the component pattern in the current codebase.

It works well because it is:

- mostly data-oriented
- reusable
- render-facing
- easy to adapt into runtime voxel render instances

It already serves as a clean conceptual bridge between:

- gameplay/runtime ownership
- voxel asset/render systems

### `PlayerInputComponent`

This is now mostly a legacy first-person controller artifact.

It used to drive the old player movement model directly on `GameObject`.
After the new `PlayerEntity` pass, the real gameplay path no longer depends on it.

So:

- it was useful earlier
- it is not the right long-term player architecture
- it is now a candidate for cleanup or replacement

## Are We Using The Component System Effectively?

Short answer:

- **partially**

More specifically:

- `VoxelModelComponent`: yes, effectively
- `PlayerInputComponent`: no, not anymore
- `GameObject` component hosting overall: only lightly and inconsistently

### Where It Works Well

The component model works well when:

- the component is mostly data
- the component represents an orthogonal concern
- another system adapts that data into runtime behavior

`VoxelModelComponent` fits this well.

### Where It Does Not Work Well

It is not being used especially effectively as a general gameplay framework because:

- most gameplay logic is not expressed as reusable systems over components
- world systems are built independently of `GameObject`
- many engine subsystems bypass the component model entirely
- the component inventory is still tiny
- there is no coherent component-system execution model around it

So the honest answer is:

- the project has a usable component mechanism
- but the engine is **not currently organized around it**

## Recommendation

Do **not** force a full ECS immediately.

Instead:

- keep the current `GameObject`/`Entity` model for near-term gameplay work
- continue using data-like components where they are genuinely useful
- avoid adding behavior-heavy micro-components just to imitate ECS

Best near-term direction:

- use `Entity` subclasses for major gameplay actors
- use components for orthogonal data such as render identity, inventory, stats, etc.
- keep world/chunk systems as standalone systems

That is a cleaner fit for the current codebase than a sudden full ECS migration.

## Other Important Patterns

## 1. Scene-Oriented Application Layer

Main runtime modes are orchestrated by scenes:

- [game_scene.h](C:/Users/magui/source/repos/voxel_enginevk/src/scenes/game_scene.h)
- [voxel_editor_scene.h](C:/Users/magui/source/repos/voxel_enginevk/src/scenes/voxel_editor_scene.h)
- [scene_renderer.cpp](C:/Users/magui/source/repos/voxel_enginevk/src/render/scene_renderer.cpp)

Pattern:

- scene owns UI and per-mode orchestration
- scene builds materials/pipelines
- scene produces `SceneRenderState`

Important note:

- scenes are orchestration boundaries, not the right place for deep gameplay rules

## 2. World Simulation Is Subsystem-Oriented

The chunk/world pipeline is already strongly system-driven:

- [chunk_manager.cpp](C:/Users/magui/source/repos/voxel_enginevk/src/world/chunk_manager.cpp)
- [chunk_lighting.cpp](C:/Users/magui/source/repos/voxel_enginevk/src/world/chunk_lighting.cpp)
- [chunk_mesher.cpp](C:/Users/magui/source/repos/voxel_enginevk/src/world/chunk_mesher.cpp)
- [terrain_gen.cpp](C:/Users/magui/source/repos/voxel_enginevk/src/world/terrain_gen.cpp)

Pattern:

- generation
- lighting
- meshing
- upload

are separate world systems with clear staging.

This is one of the strongest architecture areas in the project.

## 3. Runtime Voxel Rendering Uses Registry/Adapter Patterns

Voxel runtime rendering is not hard-coded per use case.

Important files:

- [voxel_model_component_adapter.cpp](C:/Users/magui/source/repos/voxel_enginevk/src/voxel/voxel_model_component_adapter.cpp)
- [voxel_render_registry.cpp](C:/Users/magui/source/repos/voxel_enginevk/src/voxel/voxel_render_registry.cpp)
- [chunk_decoration_render_registry.cpp](C:/Users/magui/source/repos/voxel_enginevk/src/render/chunk_decoration_render_registry.cpp)

Pattern:

- gameplay/runtime data becomes `VoxelRenderInstance`
- registry owns render submission + mesh upload lifecycle

This is a good reusable pattern and should continue.

## 4. Materials/Shaders Are Shared By Render Role, Not By Gameplay Type

Current rendering is organized around material/pipeline roles:

- `defaultmesh`
- `watermesh`
- `glowmesh`
- `editorpreview`
- `chunkboundary`

This is practical now, but the project is approaching the point where:

- chunk terrain rendering
- voxel object rendering

may need cleaner separation as gameplay-driven effects increase.

## 5. Settings Use Binding/Subscriber Style

Settings are not polled everywhere manually.

Relevant files:

- [game_settings.h](C:/Users/magui/source/repos/voxel_enginevk/src/settings/game_settings.h)
- [game_scene.cpp](C:/Users/magui/source/repos/voxel_enginevk/src/scenes/game_scene.cpp)

Pattern:

- scene binds handlers
- settings changes trigger targeted runtime updates

This is a solid pattern worth preserving.

## 6. Lighting Is Becoming A Shared Service

Recent lighting work introduced:

- [world_light_sampler.h](C:/Users/magui/source/repos/voxel_enginevk/src/world/world_light_sampler.h)
- [dynamic_light_registry.h](C:/Users/magui/source/repos/voxel_enginevk/src/world/dynamic_light_registry.h)

Pattern:

- baked chunk lighting stays foundational
- dynamic lights are runtime overlays
- both chunks and voxel objects can consume the same world-light model

This is an important architectural direction for future entities and effects.

## 7. Gameplay Entity Layer Is New And Transitional

The new gameplay direction is:

- [entity.h](C:/Users/magui/source/repos/voxel_enginevk/src/game/entity.h)
- [player_entity.h](C:/Users/magui/source/repos/voxel_enginevk/src/game/player_entity.h)

Pattern:

- `Entity` subclasses own gameplay simulation
- components can still be attached where useful
- `CubeEngine` owns gameplay actors and world interaction

This is currently the best model for the codebase.

## Recommended Rules Going Forward

1. Use `Entity` subclasses for major gameplay actors.
2. Use components for mostly-data concerns, not as a forced pattern.
3. Keep chunk/world pipelines as standalone systems.
4. Keep render registries as the bridge from gameplay/runtime data into rendering.
5. Avoid calling the current architecture “ECS” in docs or design discussions, because that will create confusion.

## Summary

The engine currently has:

- a real component mechanism
- but not a real ECS

And the component system is being used:

- **well** for voxel render identity
- **poorly or only historically** for player control
- **lightly overall** as a general engine architecture

That is not a problem by itself.

The better move right now is:

- strengthen entity/system boundaries
- keep useful components
- avoid premature ECS migration

