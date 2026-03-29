find_package(Vulkan REQUIRED)

add_library(vkbootstrap STATIC)
add_library(glm INTERFACE)
add_library(vma INTERFACE)
add_library(libcuckoo INTERFACE)
add_library(imguizmo STATIC)

add_library(stb_image INTERFACE)

add_library(tinyobjloader STATIC)

target_sources(vkbootstrap PRIVATE 
    vkbootstrap/VkBootstrap.h
    vkbootstrap/VkBootstrap.cpp
    )

target_include_directories(vkbootstrap PUBLIC vkbootstrap)
target_link_libraries(vkbootstrap PUBLIC Vulkan::Vulkan $<$<BOOL:UNIX>:${CMAKE_DL_LIBS}>)

#both vma and glm and libcuckoo and header only libs so we only need the include path
target_include_directories(vma INTERFACE vma)
target_include_directories(glm INTERFACE glm)
target_include_directories(libcuckoo INTERFACE libcuckoo)

target_sources(tinyobjloader PRIVATE 
    tinyobjloader/tiny_obj_loader.h
    tinyobjloader/tiny_obj_loader.cc
    )

target_include_directories(tinyobjloader PUBLIC tinyobjloader)

set(FASTNOISE2_NOISETOOL OFF CACHE BOOL "Build Noise Tool" FORCE) #example if don't need the graph tool
add_subdirectory(FastNoise2)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
add_subdirectory(SPIRV-Reflect/third_party/googletest EXCLUDE_FROM_ALL)

add_subdirectory(tracy)

include(FetchContent)
set(SDL2_LOCAL_SOURCE_DIR "${PROJECT_SOURCE_DIR}/build/_deps/sdl2-src")

if (EXISTS "${SDL2_LOCAL_SOURCE_DIR}/CMakeLists.txt")
    set(FETCHCONTENT_SOURCE_DIR_SDL2 "${SDL2_LOCAL_SOURCE_DIR}")
endif()

FetchContent_Declare(
    sdl2
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        release-2.28.4
)
FetchContent_MakeAvailable(sdl2)

add_library(sdl2 INTERFACE)
target_link_libraries(sdl2 INTERFACE SDL2::SDL2)
if (WIN32 AND TARGET SDL2::SDL2main)
    target_link_libraries(sdl2 INTERFACE SDL2::SDL2main)
endif()

add_library(imgui STATIC)

target_include_directories(imgui PUBLIC imgui)

target_sources(imgui PRIVATE
        imgui/imgui.h
        imgui/imconfig.h
        imgui/imgui_internal.h
        imgui/imstb_rectpack.h
        imgui/imstb_textedit.h
        imgui/imstb_truetype.h

        imgui/imgui.cpp
        imgui/imgui_demo.cpp
        imgui/imgui_draw.cpp
        imgui/imgui_tables.cpp
        imgui/imgui_widgets.cpp

        imgui/backends/imgui_impl_vulkan.cpp
        imgui/backends/imgui_impl_sdl2.cpp
    )

target_link_libraries(imgui PUBLIC Vulkan::Vulkan sdl2)
target_include_directories(stb_image INTERFACE stb_image)

target_include_directories(imguizmo PUBLIC ImGuizmo imgui)
target_sources(imguizmo PRIVATE
    ImGuizmo/ImGuizmo.h
    ImGuizmo/ImGuizmo.cpp
    ImGuizmo/ImSequencer.h
    ImGuizmo/ImSequencer.cpp
)
target_link_libraries(imguizmo PUBLIC imgui)

add_library(spirv_reflect STATIC)
target_include_directories(spirv_reflect PUBLIC SPIRV-Reflect)
target_sources(spirv_reflect PRIVATE
    SPIRV-Reflect/spirv_reflect.h
    SPIRV-Reflect/spirv_reflect.cpp
)
