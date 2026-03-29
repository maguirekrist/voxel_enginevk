include(GoogleTest)

add_executable(engine_tests
    test_support.cpp
    world_tests.cpp
    gameplay_tests.cpp
    settings_tests.cpp
    voxel_asset_tests.cpp
    voxel_geometry_tests.cpp
    ui_runtime_tests.cpp
    ../src/settings/game_settings.cpp
    ../src/config/config_paths.cpp
    ../src/config/json_document_store.cpp
    ../src/config/config_service.cpp
    ../src/config/game_settings_config_repository.cpp
    ../src/config/world_gen_config_repository.cpp
    ../src/game/chunk.cpp
    ../src/game/world.cpp
    ../src/game/player_entity.cpp
    ../src/game/world_collision.cpp
    ../src/game/decoration.cpp
    ../src/world/structures/structure.cpp
    ../src/world/structures/cloud_structure_generator.cpp
    ../src/world/structures/tree_structure_generator.cpp
    ../src/world/dynamic_light_registry.cpp
    ../src/world/world_light_sampler.cpp
    ../src/voxel/voxel_asset_manager.cpp
    ../src/voxel/voxel_assembly_asset_manager.cpp
    ../src/voxel/voxel_assembly_component_adapter.cpp
    ../src/voxel/voxel_assembly_repository.cpp
    ../src/voxel/voxel_component_render_adapter.cpp
    ../src/voxel/voxel_model_component_adapter.cpp
    ../src/voxel/voxel_placement.cpp
    ../src/voxel/voxel_spatial_collider.cpp
    ../src/voxel/voxel_spatial_bounds.cpp
    ../src/voxel/voxel_render_instance.cpp
    ../src/voxel/voxel_mesher.cpp
    ../src/voxel/voxel_picking.cpp
    ../src/voxel/voxel_model_repository.cpp
    ../src/render/mesh_release_queue.cpp
    ../src/ui/ui_text.cpp
    ../src/ui/ui_text_system.cpp
    ../src/ui/ui_font_backend_freetype.cpp
    ../src/ui/ui_font_backend_msdf.cpp
    ../src/ui/ui_world.cpp
    ../src/ui/ui_runtime.cpp
    ../src/world/chunk_neighborhood.cpp
    ../src/world/chunk_lighting.cpp
    ../src/world/generation/terrain_generation_buffers.cpp
    ../src/world/generation/terrain_generation_helpers.cpp
    ../src/world/terrain_gen.cpp
)

target_include_directories(engine_tests
    PRIVATE
        "${PROJECT_SOURCE_DIR}/src"
        "${PROJECT_SOURCE_DIR}"
        "${Vulkan_INCLUDE_DIR}"
)

target_link_libraries(engine_tests
    PRIVATE
        gtest_main
        libcuckoo
        vma
        glm
        FastNoise
        TracyClient
        freetype
        ${VOXEL_ENGINE_HARFBUZZ_TARGET}
        msdf-atlas-gen::msdf-atlas-gen
)


if (MSVC)
    target_compile_options(engine_tests PRIVATE /MP)
endif()


set_target_properties(engine_tests PROPERTIES
    FOLDER "Tests"
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:engine_tests>"
)

gtest_discover_tests(engine_tests
    DISCOVERY_MODE PRE_TEST
)
