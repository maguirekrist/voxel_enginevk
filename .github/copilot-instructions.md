# Voxel Engine VK - AI Agent Instructions

## Project Overview
Multi-threaded Vulkan 1.1 voxel rendering engine with infinite terrain generation, similar to Minecraft. Features include chunk-based world management, compute shader post-processing, FastNoise2 procedural generation, and Tracy profiler integration.

## Architecture

### Core Components
- **VulkanEngine** (`src/vk_engine.{h,cpp}`): Singleton managing Vulkan state, swapchain, render passes, and frame synchronization. Uses `FunctionQueue` for deferred cleanup.
- **CubeEngine** (`src/game/cube_engine.{h,cpp}`): Game logic layer coordinating ChunkManager and World state.
- **ChunkManager** (`src/world/chunk_manager.{h,cpp}`): Orchestrates chunk generation/meshing via ThreadPool. Uses `MapRange` to track loaded chunks around player.
- **SceneRenderer** (`src/render/scene_renderer.{h,cpp}`): Handles rendering pipeline, manages scene switching, batches draw calls by material.

### Multi-threading Model
- **ThreadPool** (`src/world/thread_pool.h`): Fixed-size pool (4 threads) using `moodycamel::BlockingConcurrentQueue` for work distribution.
- **NeighborBarrier** (`src/world/neighbor_barrier.{h,cpp}`): Synchronizes chunk neighbor dependencies before meshing.
- Workflow: Generate chunk data → wait for 8 neighbors → mesh generation → GPU upload.
- Thread-safe chunk access via `ChunkCache` with concurrent hash map (`libcuckoo`).

### Data Structures
- **Chunk** (`src/game/chunk.h`): 16×256×16 blocks, uses `ChunkCoord` (x,z) for identification. Stores `ChunkData` and optional `ChunkMeshData`.
- **Block** (`src/game/block.h`): Voxel definition with face visibility, lighting, color. Uses `FaceDirection` enum for cube faces.
- **sparse_set** (`src/collections/spare_set.h`): Custom ECS-style container with generational handles, used for `RenderObject` management in opaque/transparent sets.
- **RenderObject** (`src/render/render_primitives.h`): Links Mesh + Material + chunk position + render layer.

### Rendering Pipeline
1. **Offscreen Pass**: Renders geometry to fullscreen image
2. **Compute Shader**: Post-processing effects (fog shader: `shaders/fog.comp`)
3. **Present Pass**: Full-screen quad with final image

Materials contain pipeline, layout, descriptor sets, and push constant callbacks. Push constants include chunk translation (`ivec2 translate`) for instanced chunk rendering without per-chunk matrices.

## Key Conventions

### File Organization
- Headers/implementation split (`.h`/`.cpp`)
- Component categories: `game/`, `world/`, `render/`, `scenes/`, `collections/`, `utils/`
- Shaders in `shaders/` compiled to `.spv` via CMake custom commands

### Naming Patterns
- Private members: `_underscorePrefix` (e.g., `_device`, `_swapchain`)
- Public members: lowercase snake_case (e.g., `m_chunkCache`)
- Constants: `SCREAMING_SNAKE_CASE` in `src/constants.h`
- Managers follow singleton or owned-by-VulkanEngine pattern

### Constants (`src/constants.h`)
- `CHUNK_SIZE = 16`, `CHUNK_HEIGHT = 256`
- `GameConfig::DEFAULT_VIEW_DISTANCE = 12` (chunks from player)
- `FRAME_OVERLAP = 1` (for frame pipelining)
- `USE_VALIDATION_LAYERS`, `USE_IMGUI` feature flags

### Tracy Profiler Integration
- Enabled via `TRACY_MEM_ENABLE` in `main.cpp`
- Use `ZoneScopedN("Description")` for profiling (see `chunk.cpp:37`, `vk_engine.cpp:192`)
- Custom allocator tracking with `TRACY_ALLOC`/`TRACY_FREE`

## Build & Development

### CMake Configuration
```powershell
# Generate build files (from repo root)
cmake -B build -S . -G "Visual Studio 17 2022"

# Or use CMakeSettings.json with Visual Studio
```

### Shader Compilation
Shaders auto-compile via CMake target `Shaders`. Use `glslangValidator`:
```powershell
glslangValidator -V shaders/shader.vert -o shaders/shader.vert.spv
```
Shader hot-reload requires pipeline rebuild (see `GameScene::rebuild_pipelines()`).

### Running
Output binary: `bin/vulkan_guide.exe`
Requires Vulkan SDK and SDL2. Assets in `models/`, shaders in `bin/shaders/`.

### Third-Party Dependencies
Managed in `third_party/CMakeLists.txt`:
- **vkbootstrap**: Vulkan initialization helper
- **VMA**: Vulkan Memory Allocator
- **FastNoise2**: Procedural terrain generation
- **Tracy**: Profiler (optional)
- **ImGui**: Debug UI (if `USE_IMGUI = true`)
- **libcuckoo**: Lock-free concurrent hash map

## Common Patterns

### Vulkan Resource Management
- Use `AllocatedBuffer`/`AllocatedImage` wrappers with VMA allocations
- Defer cleanup via `_mainDeletionQueue.push_function([]{...})`
- Frame resources in `FrameData` array (`_frames[FRAME_OVERLAP]`)

### Chunk Lifecycle
1. Create `Chunk` with `ChunkCoord`
2. `ThreadPool::post()` terrain generation task
3. Wait for neighbors via `NeighborBarrier`
4. `ChunkMesher::generate_mesh()` creates mesh data
5. Upload to GPU in `MeshManager`
6. Insert `RenderObject` into scene's sparse_set

### Scene Pattern
Inherit from `Scene` (see `GameScene`). Override:
- `update(deltaTime)`: Game logic
- `build_pipelines()`: Material/pipeline setup
- `draw_imgui()`: Debug UI
- `handle_input(event)`: SDL event handling

### Material Creation
See `MaterialManager`. Requires:
- Shader stages (vert/frag compiled `.spv`)
- Descriptor set layouts (e.g., UBO bindings)
- Push constant definitions
- Pipeline state (depth test, blending)

## Debugging Tips
- Enable validation layers: `USE_VALIDATION_LAYERS = true`
- ImGui overlay shows FPS, chunk stats (`GameScene::draw_imgui()`)
- Tracy profiler for CPU/GPU timings
- Chunk meshing issues: Check neighbor barrier sync and `ChunkCache` state
- Pipeline errors: Verify shader reflection with SPIRV-Reflect

## Code Modification Guidelines
- When adding voxel types: Update `Block` enum and `ChunkMesher` visibility logic
- New rendering passes: Extend `SceneRenderer`, add framebuffer/renderpass in `VulkanEngine`
- Threading changes: Ensure chunk access uses `ChunkCache` locks or immutable shared_ptr
- Shader edits: Rerun CMake or manually invoke `glslangValidator`, then call `rebuild_pipelines()`
