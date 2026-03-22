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
        vk_vertex.h
        vk_vertex.cpp
    camera.h
    camera.cpp
    window_system.h
    window_system.cpp
    frame_clock.h
    frame_clock.cpp
        game/world.h
        game/world.cpp
    random.h
        game/chunk.h
        game/chunk.cpp
        game/block.h
        game/block.cpp
        game/structure.h
        game/structure.cpp
        game/cloud_structure_generator.h
        game/cloud_structure_generator.cpp
        game/tree_structure_generator.h
        game/tree_structure_generator.cpp
        world/chunk_mesher.h
        world/chunk_mesher.cpp
        world/chunk_manager.h
        world/chunk_manager.cpp
        world/chunk_record.h
        world/chunk_scheduler.h
        world/chunk_scheduler.cpp
        world/chunk_neighborhood.h
        world/chunk_neighborhood.cpp
        world/chunk_lighting.h
        world/chunk_lighting.cpp
        world/chunk_dirty_tracker.h
        world/chunk_dirty_tracker.cpp
        world/world_edit_queue.h
        world/world_edit_queue.cpp
        game/cube_engine.h
        game/cube_engine.cpp
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
    render/shader_program.h
    render/shader_program.cpp
    scenes/scene.h
    scenes/scene_services.h
    scenes/game_scene.h
    scenes/game_scene.cpp
    scenes/voxel_editor_scene.h
    scenes/voxel_editor_scene.cpp
    settings/game_settings.h
    settings/game_settings.cpp
    voxel/voxel_model.h
    voxel/voxel_runtime_asset.h
    voxel/voxel_asset_manager.h
    voxel/voxel_asset_manager.cpp
    voxel/voxel_render_instance.h
    voxel/voxel_render_instance.cpp
    voxel/voxel_render_registry.h
    voxel/voxel_render_registry.cpp
    voxel/voxel_mesher.h
    voxel/voxel_mesher.cpp
    voxel/voxel_picking.h
    voxel/voxel_picking.cpp
    voxel/voxel_model_repository.h
    voxel/voxel_model_repository.cpp
    config/config_paths.h
    config/config_paths.cpp
    config/json_document_store.h
    config/json_document_store.cpp
    config/config_service.h
    config/config_service.cpp
    config/game_settings_config_repository.h
    config/game_settings_config_repository.cpp
    config/world_gen_config_repository.h
    config/world_gen_config_repository.cpp
    render/scene_renderer.h
    render/scene_renderer.cpp
        render/render_primitives.h
        render/frame_render_context.h
        render/resource.cpp
    render/resource.h
        world/chunk_cache.cpp
        world/chunk_cache.h
        collections/spare_set.h
        physics/aabb.cpp
        physics/aabb.h
        components/player_input_component.cpp
        components/player_input_component.h
        components/game_object.h
        components/component.h
        world/thread_pool.cpp
        world/thread_pool.h
        world/neighbor_barrier.cpp
        world/neighbor_barrier.h
        render/staging_buffer.cpp
        render/staging_buffer.h
        render/mesh_allocator.cpp
        render/mesh_allocator.h
        render/material.cpp
        render/material.h
        render/mesh.cpp
        render/mesh.h
        render/chunk_render_registry.cpp
        render/chunk_render_registry.h
        render/scene_render_state.h
        render/mesh_release_queue.cpp
        render/mesh_release_queue.h
)


set_property(TARGET vulkan_guide PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:vulkan_guide>")

target_include_directories(vulkan_guide PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${PROJECT_SOURCE_DIR}")
target_link_libraries(vulkan_guide vkbootstrap libcuckoo vma glm tinyobjloader imgui spirv_reflect stb_image)
target_link_libraries(vulkan_guide Vulkan::Vulkan sdl2 FastNoise TracyClient)

target_compile_options(vulkan_guide PRIVATE)
target_link_options(vulkan_guide PRIVATE)

add_dependencies(vulkan_guide Shaders)


add_custom_command(TARGET vulkan_guide POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy_directory
	"${CMAKE_SOURCE_DIR}/shaders"
	"$<TARGET_FILE_DIR:vulkan_guide>/shaders"
)
