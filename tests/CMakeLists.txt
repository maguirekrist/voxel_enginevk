include(GoogleTest)

add_executable(engine_tests
    engine_tests.cpp
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
    ../src/game/structure.cpp
    ../src/game/cloud_structure_generator.cpp
    ../src/game/tree_structure_generator.cpp
    ../src/world/dynamic_light_registry.cpp
    ../src/world/world_light_sampler.cpp
    ../src/voxel/voxel_asset_manager.cpp
    ../src/voxel/voxel_assembly_asset_manager.cpp
    ../src/voxel/voxel_assembly_component_adapter.cpp
    ../src/voxel/voxel_assembly_repository.cpp
    ../src/voxel/voxel_component_render_adapter.cpp
    ../src/voxel/voxel_model_component_adapter.cpp
    ../src/voxel/voxel_render_instance.cpp
    ../src/voxel/voxel_mesher.cpp
    ../src/voxel/voxel_picking.cpp
    ../src/voxel/voxel_model_repository.cpp
    ../src/render/mesh_release_queue.cpp
    ../src/world/chunk_neighborhood.cpp
    ../src/world/chunk_lighting.cpp
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
)

set_target_properties(engine_tests PROPERTIES
    FOLDER "Tests"
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:engine_tests>"
)

gtest_discover_tests(engine_tests
    DISCOVERY_MODE PRE_TEST
)
