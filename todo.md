# Vulkan / Architecture Refactor Todo

- [x] Replace positional material resources with explicit descriptor bindings keyed by `set` and `binding`.
- [x] Refactor `MaterialManager` to bind reflected shader layouts using explicit bindings instead of one-resource-per-set assumptions.
- [x] Remove obsolete shader loading / reflection duplication from `vk_util` and keep shader module reflection in one place.
- [x] Extract concrete responsibilities out of `VulkanEngine` so it is no longer the catch-all for app loop, platform, and render orchestration.
- [x] Rebuild and validate the refactor, then update this file to reflect what was completed.
- [x] Replace chunk-local tree placement with deterministic world-coordinate placement cells.
- [x] Allow structure generators to emit world-space edits that overlap chunk borders and get applied per chunk.
