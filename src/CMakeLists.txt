
# Add source to this project's executable.
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
    block.cpp)


set_property(TARGET vulkan_guide PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:vulkan_guide>")

target_include_directories(vulkan_guide PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(vulkan_guide vkbootstrap vma glm tinyobjloader imgui stb_image fmt)

target_link_libraries(vulkan_guide Vulkan::Vulkan sdl2 FastNoise)

add_dependencies(vulkan_guide Shaders)
