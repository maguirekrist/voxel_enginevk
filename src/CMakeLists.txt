# Add source to this project's executable.
set(CMAKE_CXX_FLAGS_DEBUG, "/Zi")

set(VOXEL_ENGINE_RUNTIME_TARGET game)

add_executable(${VOXEL_ENGINE_RUNTIME_TARGET}
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
        world/structures/structure.h
        world/structures/structure.cpp
        game/entity.h
        game/player_input_state.h
        game/player_entity.h
        game/player_entity.cpp
        game/world_collision.h
        game/world_collision.cpp
        game/decoration.h
        game/decoration.cpp
        world/structures/cloud_structure_generator.h
        world/structures/cloud_structure_generator.cpp
        world/structures/tree_structure_generator.h
        world/structures/tree_structure_generator.cpp
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
        world/dynamic_light_registry.h
        world/dynamic_light_registry.cpp
        world/world_light_sampler.h
        world/world_light_sampler.cpp
        world/chunk_dirty_tracker.h
        world/chunk_dirty_tracker.cpp
        world/world_edit_queue.h
        world/world_edit_queue.cpp
        world/generation/terrain_generation_buffers.cpp
        world/generation/terrain_generation_helpers.h
        world/generation/terrain_generation_helpers.cpp
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
    scenes/orbit_orientation_gizmo.h
    scenes/orbit_orientation_gizmo.cpp
    scenes/voxel_assembly_scene.h
    scenes/voxel_assembly_scene.cpp
    scenes/voxel_editor_scene.h
    scenes/voxel_editor_scene.cpp
    settings/game_settings.h
    settings/game_settings.cpp
    voxel/voxel_model.h
    voxel/voxel_assembly_asset.h
    voxel/voxel_assembly_repository.h
    voxel/voxel_assembly_repository.cpp
    voxel/voxel_runtime_asset.h
    voxel/voxel_spatial_bounds.h
    voxel/voxel_spatial_bounds.cpp
    voxel/voxel_orientation.h
    voxel/voxel_placement.h
    voxel/voxel_placement.cpp
    voxel/voxel_asset_manager.h
    voxel/voxel_asset_manager.cpp
    voxel/voxel_model_component_adapter.h
    voxel/voxel_model_component_adapter.cpp
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
    config/world_geometry_config_repository.h
    config/world_geometry_config_repository.cpp
    config/world_gen_config_repository.h
    config/world_gen_config_repository.cpp
    world/world_geometry.h
    world/world_geometry.cpp
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
        components/character_body_component.h
        components/character_motor_component.h
        components/spatial_collider_component.h
        components/voxel_assembly_component.h
        components/voxel_model_component.h
        components/game_object.h
        components/component.h
        voxel/voxel_assembly_asset_manager.h
        voxel/voxel_assembly_asset_manager.cpp
        voxel/voxel_assembly_component_adapter.h
        voxel/voxel_assembly_component_adapter.cpp
        voxel/voxel_component_render_adapter.h
        voxel/voxel_component_render_adapter.cpp
        voxel/voxel_spatial_collider.h
        voxel/voxel_spatial_collider.cpp
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
        render/chunk_decoration_render_registry.cpp
        render/chunk_decoration_render_registry.h
        render/scene_render_state.h
        render/mesh_release_queue.cpp
        render/mesh_release_queue.h
)


set_target_properties(${VOXEL_ENGINE_RUNTIME_TARGET} PROPERTIES OUTPUT_NAME "${VOXEL_ENGINE_OUTPUT_NAME}")
set_property(TARGET ${VOXEL_ENGINE_RUNTIME_TARGET} PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:${VOXEL_ENGINE_RUNTIME_TARGET}>")

target_include_directories(${VOXEL_ENGINE_RUNTIME_TARGET} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${PROJECT_SOURCE_DIR}")
target_link_libraries(${VOXEL_ENGINE_RUNTIME_TARGET} vkbootstrap libcuckoo vma glm tinyobjloader imgui spirv_reflect stb_image)
target_link_libraries(${VOXEL_ENGINE_RUNTIME_TARGET} Vulkan::Vulkan sdl2 FastNoise TracyClient)

target_compile_options(${VOXEL_ENGINE_RUNTIME_TARGET} PRIVATE)
target_link_options(${VOXEL_ENGINE_RUNTIME_TARGET} PRIVATE)

if (MSVC)
    target_compile_options(${VOXEL_ENGINE_RUNTIME_TARGET} PRIVATE /MP)
endif()

add_dependencies(${VOXEL_ENGINE_RUNTIME_TARGET} Shaders)

add_custom_command(TARGET ${VOXEL_ENGINE_RUNTIME_TARGET} POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy_directory
	"${CMAKE_SOURCE_DIR}/shaders"
	"$<TARGET_FILE_DIR:${VOXEL_ENGINE_RUNTIME_TARGET}>/shaders"
)
