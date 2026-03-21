# Notes

## Backlog
- [ ] Replace the fixed-size per-mesh slab allocator with a more resilient mesh upload strategy.
  Implementation outline:
  - Introduce multiple mesh size classes such as `small`, `medium`, and `large`, each with its own vertex/index slab sizes and free list.
  - Keep the fixed-slot arena for common chunk meshes, but route oversized uploads into a larger pool instead of crashing.
  - Add an overflow path for rare meshes that still exceed the largest pooled slot, likely by allocating dedicated GPU buffers for that mesh and tagging the allocation type.
  - Decouple allocator capacity from `GameConfig::DEFAULT_VIEW_DISTANCE`; changing runtime render distance currently does not resize slot capacity, which can trigger `no free slots` even when the rest of the engine accepts the new view distance.
  - Make slot-pool sizing depend on the actual configured/current view distance, or rebuild/recreate the allocator cleanly when render distance changes.
  - Move staging/upload validation to check the selected pool capacity explicitly, and emit metrics so we can see how often meshes spill into larger classes.
  - Long term, consider replacing slot-based slabs entirely with a suballocation/range allocator over large GPU buffers so mesh complexity can vary without constant slab retuning.
