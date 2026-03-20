# Vulkan / Architecture Refactor Todo

- [ ] Replace positional material resources with explicit descriptor bindings keyed by `set` and `binding`.
- [ ] Refactor `MaterialManager` to bind reflected shader layouts using explicit bindings instead of one-resource-per-set assumptions.
- [ ] Remove obsolete shader loading / reflection duplication from `vk_util` and keep shader module reflection in one place.
- [ ] Extract concrete responsibilities out of `VulkanEngine` so it is no longer the catch-all for app loop, platform, and render orchestration.
- [ ] Rebuild and validate the refactor, then update this file to reflect what was completed.
