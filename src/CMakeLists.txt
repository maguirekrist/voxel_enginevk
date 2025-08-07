
# Add source to this project's executable.
set(CMAKE_CXX_FLAGS_DEBUG, "/Zi")

add_executable(vulkan_guide
    main.cpp
    vk_engine.cpp
    vk_engine.h
    vk_types.h
    vk_initializers.cpp
    vk_initializers.h
    sdl_utils.h
    vk_mesh.h
    vk_mesh.cpp
    camera.h
    camera.cpp
        game/world.h
        game/world.cpp
    random.h
        game/chunk.h
        game/chunk.cpp
        game/block.h
        game/block.cpp
        world/chunk_mesher.h
        world/chunk_mesher.cpp
        world/chunk_manager.h
        world/chunk_manager.cpp
        game/cube_engine.h
        game/cube_engine.cpp
        game/player.h
        game/player.cpp
        world/terrain_gen.h
        world/terrain_gen.cpp
    constants.h
    utils/concurrentqueue.h
    utils/blockingconcurrentqueue.h
    utils/lightweightsemaphore.h
    vk_pipeline_builder.h
    vk_pipeline_builder.cpp
    vk_util.h
    vk_util.cpp
    render/mesh_manager.h
    render/mesh_manager.cpp
    render/material_manager.h
    render/material_manager.cpp
    scenes/scene.h
    scenes/game_scene.h
    scenes/game_scene.cpp
    scenes/blueprint_builder_scene.h
    scenes/blueprint_builder_scene.cpp
    render/scene_renderer.h
    render/scene_renderer.cpp
        render/render_primitives.h
        render/resource.cpp
    render/resource.h
        world/chunk_cache.cpp
        world/chunk_cache.h
        collections/spare_set.h
)


set_property(TARGET vulkan_guide PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:vulkan_guide>")

target_include_directories(vulkan_guide PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(vulkan_guide vkbootstrap libcuckoo vma glm tinyobjloader imgui spirv_reflect stb_image)
target_link_libraries(vulkan_guide Vulkan::Vulkan sdl2 FastNoise TracyClient)

target_compile_options(vulkan_guide PRIVATE)
target_link_options(vulkan_guide PRIVATE)

add_dependencies(vulkan_guide Shaders)
