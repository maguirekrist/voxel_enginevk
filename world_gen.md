# World Generation Refactor

## Todo
- [x] Audit the current terrain and structure generation flow
- [x] Track the architecture and implementation plan in this file
- [x] Decouple structure registration from tree-specific generation code
- [x] Introduce tree generation strategies so tree variants scale independently
- [x] Replace raw height-map-only terrain generation with biome-aware chunk column output
- [x] Add first-class world generation layers for base terrain, biome classification, and surface shaping
- [ ] Add more biome-specific surface blocks and decoration sets
- [ ] Add region-level caching / precomputation for world generation data
- [ ] Add cave, island, and erosion detail layers beyond the current surface pipeline
- [ ] Add rivers that carve continuously across chunk boundaries
- [ ] Add tests for biome classification and structure placement/generation invariants
- [ ] Expose world seed/configuration cleanly instead of relying on the default generator singleton

## Notes

### Problems In The Old Design
- `src/game/structure.cpp` mixed three responsibilities in one file: structure registration, tree placement, and tree mesh/block generation.
- Tree variants were handled with hardcoded branching inside a single lambda, which does not scale once more tree types or non-tree structures appear.
- `src/world/terrain_gen.cpp` only produced a height map, so biome, shoreline, and river logic had nowhere first-class to live.
- The old terrain generator randomized its seed at startup, which is a poor fit for deterministic procedural generation.

### Structure Architecture
- `StructureRegistry` now owns registered structures instead of raw function callbacks.
- Each structure type is modeled as:
  - a `IStructurePlacementStrategy` for anchor collection
  - a `IStructureGenerator` for converting anchors into block edits
- Trees are further decomposed into `ITreeVariantGenerator` implementations, so `Oak` and `Giant` are sibling strategies rather than branches inside one monolith.

### World Generation Architecture
- The terrain system now produces `ChunkTerrainData`, which is a per-column output model rather than only a float height map.
- Each column carries:
  - terrain height
  - stone depth
  - biome classification
  - top/filler block choice
  - river/beach flags
  - source noise values
- World generation is applied as a sequence of `IWorldGenLayer` passes:
  - `BaseTerrain`
  - `BiomeClassification`
  - `SurfaceShaping`

### Current Biome Layering
- Base terrain combines `continentalness`, `peaks/valleys`, and `erosion` through spline-shaped height responses.
- Climate noise (`temperature`, `humidity`) influences biome assignment.
- River noise is used as a shaping signal and biome classifier.
- Surface shaping currently outputs:
  - ocean floors
  - shore/beach columns
  - riverbeds
  - plains / forest ground
  - mountain stone caps

### Next Logical Expansions
- Add more surface materials so biome output is visually richer.
- Promote generator configuration into data objects or JSON/tables instead of hardcoded constants.
- Introduce region caches so chunk generation can reuse precomputed climate/biome fields.
- Add 3D density/cave layers after the surface pipeline is stable.
