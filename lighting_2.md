# Point Light And Glow System Implementation Plan

## Goal

Implement a real emissive lighting system on top of the existing skylight pipeline so that:

- blocks can emit configurable point light
- emitted light affects chunk meshes and propagates through the voxel world
- emissive sources can carry color and intensity
- future lamps, torches, fires, crystals, and similar props can reuse the same light propagation path
- emissive sources can optionally render a stylized glow / halo effect in screen space
- the glow system can be attached to either a cube/block or a future particle/object system

This plan is intentionally broader than `lighting_1.md`:

- `lighting_1.md` established authoritative skylight
- this plan adds emissive block light and glow rendering

## Desired Outcome

After this implementation:

- blocks may emit colored light based on block type or explicit block parameters
- chunk lighting data contains both skylight and local/emissive light
- chunk meshing shades terrain from combined lighting, not skylight alone
- lamps/fires can light nearby terrain and structures
- emissive sources can spawn an optional billboard/sprite/post-process glow
- glow can be reused for future non-block effects without rewriting lighting

## Design Principles

### 1. Separate lighting data from visual bloom/glow

These are two different systems:

- `point light propagation` changes voxel lighting values used for terrain shading
- `glow/halo rendering` is a stylized visual effect layered on top

Do not tie gameplay/world lighting correctness to a post-process bloom pass.

### 2. Emissive light should be authoritative chunk data

Just like skylight became authoritative voxel data, emissive light should also be authoritative.

Chunk meshes should consume final light data.

They should not guess light from nearby emitters at mesh time.

### 3. Keep block light and skylight as distinct channels

Do not immediately collapse everything into one scalar.

Recommended model:

- skylight channel
- block/emissive light channel

Why:

- different propagation rules
- different visual tuning
- easier debugging
- easier future support for colored block light

### 4. Colored light should be designed in from the start

Even if the first pass only needs warm lamp/fire colors, the data model should not lock the engine into grayscale local light.

### 5. Glow should be reusable by non-voxel entities

Future lamps/fires may not remain “just blocks”.

The glow system should be driven by generic emissive source descriptors, not only by block meshing.

## Current Findings

### What exists now

- authoritative skylight solve in [`src/world/chunk_lighting.cpp`](/C:/Users/magui/source/repos/voxel_enginevk/src/world/chunk_lighting.cpp)
- chunk runtime tracks lighting state/versioning in [`src/world/chunk_manager.h`](/C:/Users/magui/source/repos/voxel_enginevk/src/world/chunk_manager.h)
- chunk meshes already carry per-vertex lighting payloads through [`src/vk_vertex.h`](/C:/Users/magui/source/repos/voxel_enginevk/src/vk_vertex.h)
- terrain and water shaders already use a lighting UBO in:
  - [`shaders/tri_mesh.frag`](/C:/Users/magui/source/repos/voxel_enginevk/shaders/tri_mesh.frag)
  - [`shaders/water_mesh.frag`](/C:/Users/magui/source/repos/voxel_enginevk/shaders/water_mesh.frag)
- day/night stylization and runtime tuning already exist in [`src/scenes/game_scene.cpp`](/C:/Users/magui/source/repos/voxel_enginevk/src/scenes/game_scene.cpp)

### What is missing

- no emissive light field in voxel data
- no block-level concept of emission color/intensity
- no block light propagation job
- no combined skylight + local-light shading path in the mesher/shaders
- no reusable emissive source registry for glow sprites/particles
- no screen-space halo or post-process glow pass

## Scope

### In scope

- emissive block light
- configurable emission intensity and color
- block-type defaults plus override-capable parameters
- async propagation integrated with chunk lighting jobs
- chunk shading from skylight + local light
- stylized halo/glow for emissive sources
- support for future lamp/fire particle emitters

### Out of scope

- physically based lighting
- full HDR bloom pipeline for the whole renderer
- shadow maps
- GI / bounce lighting
- volumetric light shafts
- save/load persistence unless needed for custom block emission authoring

## Proposed Architecture

## 1. Data Model

### Block-level emission definition

Add an emission definition layer that can answer:

- does this block emit light?
- what is its intensity?
- what is its tint/color?
- should it also spawn a glow effect?

Recommended first abstraction:

```cpp
struct BlockEmissionDef
{
    bool emits{false};
    uint8_t intensity{0};
    glm::u8vec3 color{0, 0, 0};
    bool hasGlow{false};
    float glowRadius{0.0f};
    float glowIntensity{0.0f};
};
```

Recommended lookup path:

- block type has default emission metadata
- optional per-block override can exist later

This keeps standard content simple while leaving room for custom lamps, magic blocks, etc.

### Voxel lighting storage

Do not overload `_sunlight`.

Recommended expansion of `Block` or equivalent lighting payload:

```cpp
struct BlockLight
{
    uint8_t skylight;
    uint8_t localR;
    uint8_t localG;
    uint8_t localB;
};
```

If embedding directly in `Block` feels too large, split lighting out into chunk-side arrays:

```cpp
struct ChunkLightData
{
    uint8_t skylight[...];
    glm::u8vec3 localLight[...];
};
```

Recommendation:

- keep skylight and local light separate
- strongly consider moving light storage out of `Block` if block size matters

### Chunk runtime state

Current `lightVersion` likely represents “some lighting”.

That is fine if lighting becomes a single authoritative solve that includes:

- skylight
- local light

But runtime signatures must now depend on:

- voxel occupancy/material
- emissive source configuration
- lighting neighborhood versions

## 2. Lighting Solve

## Phase A: Source collection

For a chunk neighborhood:

- collect skylight inputs as before
- scan blocks for emissive definitions
- seed local light propagation frontier from emitting voxels

Each emitter contributes:

- position
- RGB color
- intensity/range

### Emissive source examples

- torch/fire: warm orange, medium strength
- lamp: soft yellow, stable radius
- crystal: colored fantasy tint
- glowing cube: generic test source

## Phase B: Local light propagation

Run a voxel flood-fill similar to classic block light.

Recommended first-pass rule:

- propagate in 6 directions minimum: `+x, -x, +y, -y, +z, -z`
- optionally include diagonal horizontal propagation later if needed
- each step attenuates by 1
- solid blocks stop propagation
- translucent materials like leaves/water may attenuate more instead of fully blocking, if desired

### Colored light rule

For each propagated step:

- propagate each RGB channel independently
- attenuation applies per channel

Example:

```cpp
next.r = max(next.r, current.r - attenuation);
next.g = max(next.g, current.g - attenuation);
next.b = max(next.b, current.b - attenuation);
```

This is simple, stable, and standard for voxel block light.

## Phase C: Combined lighting result

The final authoritative lighting for a voxel becomes:

- skylight scalar
- local light RGB

Do not permanently flatten this into one number during the solve.

Flatten only at shading time.

## 3. Mesher / Vertex Payload

The mesher should stop treating lighting as “just skylight + AO”.

Recommended vertex payload expansion:

```cpp
struct VertexLighting
{
    float skylight;
    float ao;
    glm::vec3 localLight;
};
```

Then terrain shading can combine:

- broad ambient/day-night skylight
- local colored emissive light
- subtle AO/contact shading
- hemisphere tint

### Sampling model

Use the same corner-based smooth-lighting scheme already established for skylight:

- sample 4 outward-adjacent corner cells
- average skylight
- average local RGB light
- keep AO separate and subtle

This keeps point lights from looking blocky.

## 4. Shader Model

### Terrain/water shading

Update shaders so final color is driven by both:

- ambient/skylight term
- additive or blended local emissive term

Suggested high-level formula:

```glsl
vec3 ambientLit = baseColor * ambient * hemiTint * contactShadow;
vec3 emissiveLit = baseColor * localLightColor * localLightStrength;
vec3 finalColor = ambientLit + emissiveLit;
```

This is stylized, readable, and closer to the desired Cube World direction than physically accurate falloff.

### Runtime tuning controls

Add ImGui controls for:

- local light strength
- local light saturation/tint response
- lamp/fire glow intensity
- glow radius
- bloom threshold if post-processing is used

## 5. Glow / Halo System

This must be a separate system from light propagation.

Recommended architecture:

### Generic emissive visual source

```cpp
struct GlowSource
{
    glm::vec3 worldPosition;
    glm::vec3 color;
    float radius;
    float intensity;
    GlowVisualType type;
};
```

Sources may come from:

- emissive blocks
- particles
- future entity components

### Two-stage implementation strategy

#### Stage 1: Billboard glow sprites

Fastest first implementation:

- render camera-facing quads/sprites for glow sources
- additive blending
- soft radial falloff in fragment shader
- depth-tested but optionally depth-softened

Benefits:

- cheap
- easy to tune
- works for fires/lamps immediately

#### Stage 2: Optional post-process bloom/halo extraction

Later enhancement:

- render emissive/glow mask to a secondary buffer
- blur it
- composite back in screen space

This is the path if you want stronger stylized halo around bright sources.

Recommendation:

- do billboard glow first
- only add full post-process bloom if the simpler path is insufficient

## 6. Block vs Particle Use Cases

### Emissive block

Example: glowing cube or lamp block

- contributes block light to voxel propagation
- may also register a glow source at block center

### Fire / lamp particle cluster

Example: future campfire

- one or more particle billboards render flame visuals
- one logical light source drives voxel point light
- one glow source renders stylized halo

Important:

- particle count should not control world lighting cost
- gameplay/world light should come from a single logical emitter, not every particle

## Recommended Module Plan

### `world/block_emission.*`

Owns:

- block emission metadata lookup
- default intensity/color/glow per block type

### `world/chunk_lighting.*`

Extend to own:

- emissive source seeding
- local colored light propagation
- combined authoritative light writeback

### `world/glow_sources.*`

Owns:

- collecting visible glow sources from chunks/entities
- culling / preparing renderable glow instances

### `render/glow_renderer.*`

Owns:

- billboard glow mesh or instance rendering
- optional glow extraction / blur / composite later

### Shader additions

- terrain shader: consume local light
- water shader: optionally react to local light
- glow billboard shader: radial falloff + additive blending
- optional post-process blur/composite shader later

## Migration Plan

## Phase 1: Emissive Metadata

### TODO

- [ ] define `BlockEmissionDef`
- [ ] add default emission metadata lookup by block type
- [ ] support intensity + RGB tint + glow flags
- [ ] add at least one test emissive block type or debug-configured block

### Exit criteria

- the engine can identify whether any block emits light and what that light should look like

## Phase 2: Local Light Data Channel

### TODO

- [ ] add local light storage alongside skylight
- [ ] decide whether local light lives in `Block` or chunk-side light arrays
- [ ] ensure chunk copies/job results carry local light state
- [ ] update signatures/versions as needed

### Exit criteria

- chunks can store authoritative local/emissive lighting data

## Phase 3: Point Light Propagation

### TODO

- [ ] seed local light frontier from emissive blocks
- [ ] propagate RGB light through transparent voxels
- [ ] respect solid occlusion
- [ ] write final local-light channel back into authoritative chunk data
- [ ] keep stale result rejection using generation/version/signature checks

### Exit criteria

- emissive blocks visibly light nearby terrain and structures

## Phase 4: Mesh And Shader Integration

### TODO

- [ ] extend vertex payload to carry local light
- [ ] smooth-sample local light per vertex
- [ ] update terrain shader to combine ambient + local light
- [ ] update water shader to optionally respond to local light
- [ ] add runtime tuning controls in ImGui

### Exit criteria

- point lights affect rendered chunk meshes with smooth gradients

## Phase 5: Glow Visual Sources

### TODO

- [ ] create generic `GlowSource` representation
- [ ] register glow sources from emissive blocks
- [ ] add glow instance collection/culling
- [ ] render billboard-based halo sprites with additive blending

### Exit criteria

- glowing blocks render a visible halo even beyond their terrain lighting contribution

## Phase 6: Particle / Lamp Ready Path

### TODO

- [ ] decouple glow/light source from block-only logic
- [ ] add support for logical non-block emitters
- [ ] ensure one future fire/lamp object can own:
  - one point light source
  - one glow source
  - many decorative particles

### Exit criteria

- future fires/lamps can be added without redesigning the lighting system

## Phase 7: Optional Post-Process Glow Upgrade

### TODO

- [ ] render emissive mask or glow buffer
- [ ] blur it in screen space
- [ ] composite back into final image
- [ ] expose threshold/intensity tuning

### Exit criteria

- billboard glow can be replaced or enhanced by a true post-process halo when desired

## Implementation Considerations

## 1. Don’t mix glow brightness with world light strength

A lamp may:

- cast moderate world light
- have a strong visible halo

Those should be independently tunable.

## 2. Avoid recomputing more than necessary

Point light changes can invalidate lighting significantly.

First implementation should prioritize correctness:

- relight full affected chunk neighborhood

Later optimization can narrow relight volumes around changed emitters.

## 3. Leaves and water attenuation

Decide early how emissive light interacts with semitransparent materials:

- leaves may attenuate but not fully block
- water may absorb colored light more strongly

Recommendation:

- first pass: treat non-solid transparent cells as passable
- then add material-specific attenuation tuning later

## 4. Colored light precision

For voxel games, `u8` RGB channels are usually enough.

You do not need HDR voxel storage for the first pass.

## 5. Chunk seam correctness

Just like skylight, point light must cross chunk seams.

This means:

- lighting solve neighborhood must include required adjacent chunks
- mesh signatures must depend on neighboring light versions

## 6. Debugging tools

Strongly recommended:

- local light visualization mode
- RGB false-color debug mode
- emissive source markers
- glow source debug overlay
- per-chunk light version/state display

## Risks

### Risk: block light and skylight become visually muddy

Mitigation:

- keep channels separate until shader combine
- add tuning controls for ambient vs local light balance

### Risk: glow looks detached from world light

Mitigation:

- use one shared source definition for both
- tune separately but derive from same emitter metadata

### Risk: point-light relight is expensive

Mitigation:

- start with correctness-first chunk relight
- optimize later with bounded dirty-light volumes

### Risk: too many glow sources become expensive

Mitigation:

- frustum/distance cull glow instances
- aggregate tiny clustered sources if needed
- start with billboard glows before full bloom

## Testing Checklist

### Emissive block tests

- [ ] single emissive cube lights nearby terrain
- [ ] two colored emitters blend predictably
- [ ] emitter near chunk edge lights both sides consistently
- [ ] removing emitter removes light after relight

### Visual tests

- [ ] lamp under canopy still reads clearly
- [ ] fire at night feels warm and readable
- [ ] water near emitter reacts plausibly
- [ ] glow halo remains visible without overwhelming terrain

### Runtime tests

- [ ] toggling emissive block type causes deterministic relight
- [ ] stale async light jobs are rejected correctly
- [ ] mesh updates follow lighting updates without bad popping

### Glow tests

- [ ] emissive cube can spawn halo effect
- [ ] future non-block source API can spawn halo without chunk edits
- [ ] glow respects depth and distance well enough for gameplay

## Recommended First Implementation Order

1. Add emissive metadata lookup by block type
2. Add local light storage to authoritative chunk lighting data
3. Extend `chunk_lighting` to propagate RGB point light
4. Mesh/shader integration for smooth local light on terrain
5. Add a debug emissive block and tuning controls
6. Add billboard glow sources tied to emissive blocks
7. Generalize glow/light source API for future lamps/fires
8. Add optional post-process halo pass only if billboard glow is insufficient

## Final Recommendation

Treat this as two coordinated systems:

- authoritative emissive voxel lighting
- reusable stylized glow rendering

Do not skip straight to bloom.

The highest-value path is:

1. make blocks emit real colored point light into chunk lighting
2. make chunk meshes shade from that light
3. add a simple reusable glow billboard system
4. only then decide whether a heavier post-process bloom pass is worth it

That sequence fits the current engine architecture, keeps correctness high, and directly enables the future lamp/fire fantasy lighting you want.
