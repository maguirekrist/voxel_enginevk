include(GoogleTest)

add_executable(engine_tests
    engine_tests.cpp
    ../src/settings/game_settings.cpp
    ../src/config/config_paths.cpp
    ../src/config/json_document_store.cpp
    ../src/config/config_service.cpp
    ../src/config/game_settings_config_repository.cpp
    ../src/config/world_gen_config_repository.cpp
    ../src/game/world.cpp
    ../src/game/structure.cpp
    ../src/game/cloud_structure_generator.cpp
    ../src/game/tree_structure_generator.cpp
    ../src/voxel/voxel_asset_manager.cpp
    ../src/voxel/voxel_render_instance.cpp
    ../src/voxel/voxel_mesher.cpp
    ../src/voxel/voxel_picking.cpp
    ../src/voxel/voxel_model_repository.cpp
    ../src/world/chunk_neighborhood.cpp
    ../src/world/chunk_lighting.cpp
    ../src/world/terrain_gen.cpp
)

target_include_directories(engine_tests
    PRIVATE
        "${PROJECT_SOURCE_DIR}/src"
        "${PROJECT_SOURCE_DIR}"
)

target_link_libraries(engine_tests
    PRIVATE
        gtest_main
        vkbootstrap
        libcuckoo
        vma
        glm
        tinyobjloader
        imgui
        spirv_reflect
        stb_image
        Vulkan::Vulkan
        sdl2
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
