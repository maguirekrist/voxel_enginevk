# World Generation Architecture Plan

## Purpose

Define the long-term, from-scratch architecture for a CubeWorld-like voxel world generation system that supports:

- strong 2D macro world layout
- true volumetric terrain features
- coherent mountain ranges and shelf overhangs
- biome-aware color gradients and palette variation
- color-aware structures and decorations
- high tunability without turning the system into an intractable mess

This plan is intended to be the authoritative target architecture for world generation going forward.

## Visual Target

This plan assumes the target look has the following properties:

- macro world structure is primarily driven by 2D regional fields
- mountains and overhang-capable terrain are driven by 3D volumetric features
- mountains feel swollen, cohesive, and mass-driven, not like random spheres pasted onto a heightmap
- shelf plateaus and overhangs are true volumetric terrain, not just steep heightfield cliffs
- coloring is not binary
- biome color is continuous, gradient-based, and feature-aware
- exposed surfaces, cliff faces, riverbanks, beaches, snow zones, trees, and structures all participate in the same appearance logic

This is an inferred target based on the desired look, not a claim about the literal internal implementation of CubeWorld.

## Non-Negotiable Design Principles

- Every stage must have a single clear responsibility.
- Macro terrain, volumetric features, and appearance must be separate layer families.
- Volumetric features must not directly mutate `ChunkBlocks` as their primary abstraction.
- The system must remain deterministic and chunk-seam-safe.
- Tuning must happen through grouped settings and reusable biome/style definitions, not by scattering magic numbers through generator code.
- Debug outputs must be first-class.
- Structures and decorations must consume world generation context, not duplicate it.
- Appearance must not be reduced to static `BlockType -> Color`.

## High-Level Architecture

The system should be organized around explicit generation products.

### Product 1: `WorldRegionScaffold2D`

Purpose:

- represent the slow-changing regional logic of the world

Contains concepts such as:

- continentalness
- ocean/coast distance
- humidity
- temperature
- fertility
- magicalness or fantasy intensity
- river potential
- mountain affinity
- biome weights
- palette region influences

This is the macro language of the world.

### Product 2: `TerrainColumnScaffold2D`

Purpose:

- represent the per-column terrain scaffold used by both terrain and content systems

Contains concepts such as:

- base terrain height
- water table / sea interaction
- slope
- ridge strength
- erosion response
- sediment depth
- topsoil depth hint
- river proximity
- beach affinity
- snow affinity
- forest affinity
- initial biome classification or biome weights

This is not the final terrain geometry. It is the 2D scaffold that the 3D terrain system builds on top of.

### Product 3: `TerrainFeatureInstanceSet`

Purpose:

- represent placed, deterministic, world-space volumetric terrain features

Examples:

- mountain mass chain
- shelf overhang band
- mesa cap
- canyon cut
- river undercut
- cave system
- arch cluster

Each feature instance is a concrete placed object with:

- type
- seed
- world-space bounds
- orientation data
- parameter payload
- biome/style affiliation

This is the bridge between abstract fields and concrete chunk-local terrain synthesis.

### Product 4: `TerrainVolumeBuffer`

Purpose:

- represent composed chunk-local volumetric terrain data before rasterization

This should be a padded chunk-local 3D buffer containing values such as:

- density
- base material family
- feature ownership/debug id
- surface affinity hints

This buffer is the authoritative terrain geometry source for a chunk build.

### Product 5: `SurfaceClassificationBuffer`

Purpose:

- classify exposed terrain surfaces after geometry composition

Examples of classification:

- grass top
- beach top
- cliff face
- underside overhang rock
- scree slope
- interior stone
- snowy top
- wet riverbank
- sediment layer

This stage is where geometry becomes semantically understandable for the appearance system.

### Product 6: `AppearanceBuffer`

Purpose:

- store resolved appearance information used by meshing/rendering/content tinting

Examples:

- biome palette id
- color variation seed
- hue shift
- saturation shift
- strata band weight
- river-greening amount
- coast-sand amount
- cliff-rock variation amount
- snow coverage amount

This is where the “world looks alive” part of the system lives.

## Layer Families

The system should use multiple layer interfaces, each with narrow ownership.

## 1. Region Layers

`IWorldRegionLayer`

Responsibility:

- write or refine large-scale 2D regional fields

Examples:

- continent layout
- climate fields
- humidity flow
- macro mountain affinity
- fantasy region masks
- ocean/coast fields

These layers operate on large-scale 2D buffers and should be cheap to cache.

## 2. Column Scaffold Layers

`IColumnScaffoldLayer`

Responsibility:

- translate regional fields into chunk-local column scaffolds

Examples:

- base height derivation
- slope derivation
- ridge derivation
- river corridor shaping
- sediment/topsoil depth hints
- column biome weights

These layers produce chunk-local 2D terrain intelligence, not final 3D terrain.

## 3. Feature Placement Layers

`ITerrainFeaturePlacementLayer`

Responsibility:

- place concrete volumetric terrain feature instances in world space

Examples:

- mountain range skeleton placement
- overhang shelf placement
- mesa cap placement
- canyon placement
- cave network seed placement

These layers do not write geometry directly.
They emit deterministic `TerrainFeatureInstance`s.

This separation is critical for debugging and extensibility.

## 4. Volume Composition Layers

`ITerrainVolumeLayer`

Responsibility:

- write density/material influence into `TerrainVolumeBuffer`

Inputs:

- `TerrainColumnScaffold2D`
- overlapping `TerrainFeatureInstance`s
- world-space coordinates

Examples:

- base terrain mass
- mountain mass volume
- shelf cap volume
- underside erosion shaping
- cave subtraction
- arch shaping

This is the core 3D terrain geometry stage.

## 5. Surface Classification Layers

`ISurfaceClassificationLayer`

Responsibility:

- inspect final solid/empty volume and classify exposed surfaces

Examples:

- classify top vs side vs underside
- classify wet riverbank vs dry plains top
- classify beach sand vs inland dirt
- classify exposed cliff rock vs soil-covered slope
- classify snowcap and frost coverage

This stage prevents geometry code from also being appearance code.

## 6. Appearance Layers

`ITerrainAppearanceLayer`

Responsibility:

- resolve final color/material appearance fields from geometry and classification context

Examples:

- river greening
- coast sand blending
- mesa strata bands
- snow hue variation
- grass hue fields
- cliff face rock gradients
- underside darkening/cooling

These layers should be able to sample:

- world-space position
- biome weights
- slope/exposure
- surface classification
- proximity to rivers/oceans
- feature ownership/type

This is where biome-aware, feature-aware 3D color logic belongs.

## 7. Content Appearance Consumers

Structures and decorations should not invent their own color logic from scratch.

Trees, flowers, ruins, and other content should be able to consume the same appearance context:

- biome palette
- humidity
- fantasy intensity
- local color fields
- nearby terrain classification

That is how trees, grass, and structures all remain visually coherent with the terrain instead of looking bolted on.

## Template-Method Pipeline

Chunk generation should follow a strict orchestration pipeline.

1. Sample or fetch cached `WorldRegionScaffold2D`
2. Build `TerrainColumnScaffold2D` for the padded chunk region
3. Place overlapping `TerrainFeatureInstance`s
4. Compose `TerrainVolumeBuffer`
5. Rasterize density into solid/empty terrain voxels
6. Build `SurfaceClassificationBuffer`
7. Build `AppearanceBuffer`
8. Rasterize final terrain block/material data
9. Generate structures using the same scaffold/appearance context
10. Generate decorations using the same scaffold/appearance context
11. Mesh with geometry + appearance data

This is the correct template-method shape.

## The Geometry Model

The terrain geometry should not be:

- “heightmap first, then random bubbles mutate blocks”

Instead it should be:

- a composed density system with layered responsibilities

### Base Density

The base terrain mass should come from the column scaffold:

- terrain is solid below a scaffold-driven base height
- but this should already be expressed as a density field, not only as a block fill loop

That makes later composition much cleaner.

### Mountain Ranges

Mountain ranges should not be random isolated metaballs.

They should come from placed range features with internal structure, such as:

- range spline or ridge backbone
- clustered lobe masses along the backbone
- domain-warped cohesive mass fields
- ridge emphasis and valley suppression

The important point is:

- mountain range identity should exist before per-voxel noise is applied

The noise deforms the range.
It should not be the only thing defining the range.

### Shelf Overhangs

Shelf overhangs should come from explicit shelf or cap features that:

- attach to an existing slope/ridge context
- extend laterally in a preferred direction
- maintain a top walking surface
- maintain underside empty space where appropriate
- are shaped by curved/noisy underside erosion logic

These are not generic “cliff noise.”

### Caves Later

Caves should be a later subtractive volume family:

- cave tube networks
- pocket systems
- arch systems

They belong in the same volume composition architecture, but they are not the first design driver.

## Appearance And Coloring Model

This system needs to support much more than block enums and fixed colors.

### The Wrong Model

- `BlockType::GROUND -> one green`
- `BlockType::STONE -> one gray`

That model cannot express the target look.

### The Right Model

Coarse material class and final appearance must be separated.

Recommended distinction:

- `MaterialClass`
  - ground
  - stone
  - sand
  - snow
  - wood
  - leaves
- `SurfaceClass`
  - grass top
  - cliff face
  - underside rock
  - beach top
  - riverbank
  - interior stone
  - etc.
- `AppearanceContext`
  - biome weights
  - world-space position
  - proximity to river
  - proximity to ocean
  - humidity/temperature
  - slope/exposure
  - feature type
- `AppearanceResult`
  - palette id
  - base color
  - hue shift
  - gradient weights
  - strata weight
  - variation seed

This makes the system expressive without exploding `BlockType`.

## Color Fields

The appearance system should support multiple kinds of color fields.

### 1. Biome Color Fields

Used for:

- grass hue variation
- desert/sand warmth
- snow tint
- forest saturation

Usually driven by:

- 2D regional fields
- 2D local noise
- climate and biome weights

### 2. Feature-Aware Color Fields

Used for:

- cliff rock coloration
- shelf underside darkening
- mesa-like rock banding
- scree and sediment blending

Usually driven by:

- 3D world position
- surface classification
- feature ownership/type
- slope/exposure

### 3. Proximity-Driven Color Fields

Used for:

- greener river corridors
- sandier coasts
- wet transition zones
- snow melt transitions

Usually driven by:

- distance to rivers
- distance to ocean
- altitude
- humidity

### 4. Palette Regions

Used for:

- world-scale aesthetic variation
- mystical biome variants
- continent-level palette shifts
- different world seeds feeling distinct

This allows the same biome family to have different regional moods.

## Structures And Decorations

Structures and decorations should consume the same generation context.

That means:

- tree placement uses biome and terrain scaffold
- tree coloration uses appearance fields
- ruin/structure coloration can use biome palettes
- flower color can vary by biome region and humidity

This gives a unified world instead of separate systems with unrelated colors.

## Tuning And Configuration

This system needs a layered tuning model, not a giant flat settings blob.

Recommended grouping:

- `WorldStyleProfile`
- `BiomeDefinitions`
- `RegionSettings`
- `ColumnScaffoldSettings`
- `FeaturePlacementSettings`
- `VolumeCompositionSettings`
- `SurfaceClassificationSettings`
- `AppearanceSettings`
- `StructurePlacementSettings`
- `DecorationPlacementSettings`

### `WorldStyleProfile`

This is the top-level creative control unit.

It should define:

- available biome families
- palette families
- major terrain tendencies
- enabled feature families
- aesthetic targets

Examples:

- archipelago
- alpine highlands
- mesa wasteland
- mystical forest realm
- giant mountain continent

This is how the system becomes broadly expressive without code smells.

## Debugging And Tooling

Debuggability must be designed in, not added later.

The system should support debug visualization for:

- regional fields
- column scaffold fields
- feature placements and bounds
- density slices
- final solid volume
- surface classification
- color field outputs
- palette region influence

The developer should be able to ask:

- why is this mountain here?
- why is this shelf here?
- why is this surface green instead of sandy?
- why is this cliff underside using this rock palette?

and get a direct answer from the pipeline.

## Caching Strategy

Different stages should cache at different granularities.

### Region Cache

- large 2D tiles
- reused heavily

### Column Scaffold Cache

- chunk-local or region-subtile local

### Feature Instance Cache

- world-space feature placement cells
- reused by overlapping chunks

### Volume Buffer

- transient per chunk build

### Appearance Buffer

- transient per chunk build unless needed later for runtime editing/debug

This keeps the system scalable without conflating long-lived and short-lived data.

## Rendering Integration

The chunk mesher should not infer final terrain beauty from raw `BlockType`.

Instead it should consume:

- geometry from final terrain blocks
- appearance data from the appearance stage

The mesher can then:

- sample appearance at each exposed face vertex
- bake per-vertex colors directly
- or pack compact per-vertex appearance parameters for shaders

The important architectural rule is:

- appearance is produced by the world generation system
- rendering consumes it
- rendering does not invent terrain color semantics on its own

### Explicit Color Ownership Rule

Final terrain color should be resolved as a mesh/vertex concern, not as a block-owned gradient object.

That means:

- `BlockType` remains a coarse material identity, not the final visual answer
- blocks should not store heavyweight gradient definitions
- world generation and appearance layers produce appearance context and resolved appearance fields
- the chunk mesher samples those appearance fields per exposed face vertex
- smooth gradients come from vertex-color interpolation across the terrain mesh

This rule matters because the target look requires:

- riverbank greening gradients
- coast-to-inland sand transitions
- mesa-like rock banding
- cliff-face and underside color variation
- broad grass hue blending

Those effects should not be approximated as flat per-block colors.
They should be resolved as per-vertex appearance on the final mesh.

## Concrete Interface Set

If naming from scratch, the system should look something like:

- `IWorldRegionLayer`
- `IColumnScaffoldLayer`
- `ITerrainFeaturePlacementLayer`
- `ITerrainVolumeLayer`
- `ISurfaceClassificationLayer`
- `ITerrainAppearanceLayer`
- `IStructurePlacementLayer`
- `IDecorationPlacementLayer`

And the main orchestrator should be something like:

- `WorldGenerationPipeline`

with explicit stage products instead of hidden side effects.

## What This Plan Explicitly Rejects

- monolithic terrain generation classes that own every concern
- random sphere placement as a substitute for mountain range design
- directly mutating final chunk blocks as the primary geometry abstraction
- mixing geometry, biome logic, and color logic in one layer
- encoding all appearance variation as extra block enums
- renderer-owned terrain color semantics

## Implementation Phases

## Phase 1: Architecture Split

- Introduce explicit scaffold, feature, volume, classification, and appearance products
- Introduce the layered interfaces above
- Move the generator to a real orchestrated pipeline

## Phase 2: Replace Terrain Geometry Core

- Replace “heightfield then patch blocks” with density composition
- Implement base terrain mass as density

## Phase 3: Mountain Range Feature Family

- range backbone placement
- cohesive mountain mass composition
- high-frequency deformation that preserves range identity

## Phase 4: Shelf And Overhang Feature Family

- explicit shelf placement
- underside shaping
- top walkability

## Phase 5: Surface Classification

- exposed surface semantic classification

## Phase 6: Appearance System

- biome color fields
- feature-aware rock fields
- proximity-driven gradients
- structure/deco color consumers

## Phase 7: Advanced Volume Features

- caves
- arches
- river undercuts
- canyon cuts

## Migration Strategy From Current Code

The clean migration path is:

1. keep the current system only as a temporary baseline
2. introduce the new staged products and interfaces
3. move current 2D terrain logic into scaffold layers
4. replace current ad hoc volumetric feature code with formal feature placement + volume composition
5. replace static block coloring with appearance layers and mesher integration
6. then retire the transitional terrain code

## Final Standard

The goal state is a system where, if asked “would you refactor this architecture,” the answer is effectively no because:

- responsibilities are properly separated
- volumetric terrain is first-class
- coloring is first-class
- tuning is structured
- debugability is built in
- the pipeline is extensible without code smell

That is the standard this plan is targeting.
