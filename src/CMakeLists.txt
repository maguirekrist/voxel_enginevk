
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
    world.h
    world.cpp
    random.h
    chunk.h
    chunk.cpp
    block.h
    block.cpp
    chunk_mesher.h
    chunk_mesher.cpp
    chunk_manager.h
    chunk_manager.cpp
    cube_engine.h
    cube_engine.cpp
    player.h
    player.cpp
    terrain_gen.h
    terrain_gen.cpp
    constants.h
    utils/concurrentqueue.h
    utils/blockingconcurrentqueue.h
    utils/lightweightsemaphore.h)


set_property(TARGET vulkan_guide PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:vulkan_guide>")

target_include_directories(vulkan_guide PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(vulkan_guide vkbootstrap vma glm tinyobjloader imgui stb_image fmt)

target_link_libraries(vulkan_guide Vulkan::Vulkan sdl2 FastNoise TracyClient)

add_dependencies(vulkan_guide Shaders)
