# Settings Architecture

## Goal

The settings system is designed around these rules:

1. Settings are persisted as data, not scattered as ad hoc booleans and ints across scene code.
2. Subsystems do not poll a global singleton every frame for live values.
3. Settings changes are pushed downstream through narrow subsystem-specific contracts.
4. Runtime consumers only receive the data they actually need.

This first pass moves `GameScene` away from directly owning renderer/world settings state and introduces a central settings manager that propagates changes to the world streaming pipeline and renderer mesh budget.

## Core Pieces

### `settings::GameSettingsPersistence`

File: `src/settings/game_settings.h`

This is the durable settings shape for the scene. It currently contains:

- `world`
  - `viewDistance`
  - `ambientOcclusionEnabled`
- `debug`
  - `showChunkBoundaries`
- `dayNight`
  - `paused`
  - `timeOfDay`
  - `tuning`

This is the single source of truth for settings values inside `GameScene`.

### `settings::SettingsManager`

Files:

- `src/settings/game_settings.h`
- `src/settings/game_settings.cpp`

Responsibilities:

- owns `GameSettingsPersistence`
- normalizes values after mutation
- computes derived runtime settings
- pushes setting changes to interested handlers

Current push channels:

- `ViewDistanceRuntimeSettings`
- `AmbientOcclusionRuntimeSettings`

The manager uses `mutate(...)` to update persisted state. If a mutation changes relevant values, downstream handlers are invoked immediately.

## Push-Based Flow

### View distance

The flow is:

1. UI changes `world.viewDistance` through `SettingsManager::mutate(...)`.
2. `SettingsManager` derives `ViewDistanceRuntimeSettings`.
3. `GameScene` receives the push in `apply_view_distance_settings(...)`.
4. `GameScene` forwards the change to:
   - `ChunkManager::apply_streaming_settings(...)`
   - `MeshManager::apply_view_distance_settings(...)`

This keeps scene code as the composition root while preserving narrow subsystem contracts.

### Ambient occlusion

The flow is:

1. UI changes `world.ambientOcclusionEnabled`.
2. `SettingsManager` pushes `AmbientOcclusionRuntimeSettings`.
3. `GameScene` forwards the change to `ChunkManager::apply_mesh_settings(...)`.

This is intentionally scoped to the chunk meshing domain rather than exposing the full settings object to the world system.

## Why This Is Better Than A Singleton

The system does centralize settings ownership, but it avoids a raw pull-based global settings object.

Benefits:

- subsystems do not depend on broad global state
- runtime contracts stay specific
- changes are applied at mutation time
- derived values can be computed once and passed down cleanly
- the scene remains the orchestration layer that wires settings to engine subsystems

## Renderer Integration

Before this refactor, renderer mesh capacity was effectively tied to `GameConfig::DEFAULT_VIEW_DISTANCE`.

Now:

- `MeshManager` accepts `ViewDistanceRuntimeSettings`
- renderer capacity is derived from view distance at runtime
- `MeshAllocator` and `StagingBuffer` support safe reconfiguration
- reconfiguration is deferred until there are no live mesh allocations in the upload/release path

This means `DEFAULT_VIEW_DISTANCE` is now just an initial seed, not a live runtime dependency.

## World Integration

`ChunkManager` now exposes specific setting application interfaces:

- `apply_streaming_settings(const ChunkStreamingSettings&)`
- `apply_mesh_settings(const ChunkMeshSettings&)`

These are domain-oriented contracts:

- streaming settings affect chunk residency / cache shape
- mesh settings affect chunk meshing invalidation behavior

The world system does not know about the full scene settings model.

## `GameScene` Role

`GameScene` now does three things:

1. owns the `SettingsManager`
2. edits persisted settings from ImGui
3. binds push handlers that route changes to subsystems

`GameScene` is intentionally still the composition point. The scene knows which subsystems should react to which settings, while the subsystems stay decoupled from the broader settings schema.

## How To Add A New Setting

Use this pattern:

1. Add the new field to `GameSettingsPersistence`.
2. Decide whether downstream consumers need:
   - raw persisted value
   - derived runtime value
3. If the setting affects a subsystem, add or extend a narrow runtime settings struct.
4. Add a handler channel in `SettingsManager` if push propagation is needed.
5. Bind that handler in `GameScene`.
6. Forward the change into a subsystem-specific apply method.

Preferred pattern:

- `SettingsManager` owns values and emits change events.
- `GameScene` translates those events into subsystem calls.
- subsystems expose focused `apply_*_settings(...)` methods.

Avoid:

- passing the full settings object into many systems
- making subsystems read from a global singleton
- adding renderer/world logic directly into ImGui callbacks

## Example Extension Pattern

If a future setting controls shadow quality:

1. Add `shadowQuality` to persistence.
2. Add `RendererQualitySettings` or similar runtime struct if needed.
3. Add a handler in `SettingsManager`.
4. Bind it in `GameScene`.
5. Forward it to a renderer-specific method such as `Renderer::apply_quality_settings(...)`.

If a future setting controls chunk generation concurrency:

1. Add it to persistence.
2. Create a world-specific runtime/settings contract.
3. Push it to `ChunkManager` or scheduler code through a dedicated apply method.

## Current Limitation

This first pass is scene-local. The settings architecture is clean and scalable, but persistence to disk and cross-scene/global engine ownership are not implemented yet.

That can be added later without changing the push-based structure:

- load persisted settings into `GameSettingsPersistence`
- construct `SettingsManager` from saved data
- serialize the manager's persistence state on shutdown or change

## Summary

The important architectural boundary is:

- persisted settings live in one place
- settings changes are pushed, not polled
- `GameScene` wires changes to subsystem-specific contracts
- renderer/world systems receive only the settings relevant to them

That is the pattern to preserve as more settings are added.
