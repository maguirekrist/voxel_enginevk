find_package(Vulkan REQUIRED)

add_library(vkbootstrap STATIC)
add_library(glm INTERFACE)
add_library(vma INTERFACE)
add_library(libcuckoo INTERFACE)

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

set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
FetchContent_Declare(
    freetype
    GIT_REPOSITORY https://github.com/freetype/freetype.git
    GIT_TAG        VER-2-13-3
)
FetchContent_MakeAvailable(freetype)

if (TARGET freetype AND NOT TARGET Freetype::Freetype)
    add_library(Freetype::Freetype ALIAS freetype)
endif()

set(HB_BUILD_SUBSET OFF CACHE BOOL "" FORCE)
set(HB_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(HB_BUILD_UTILS OFF CACHE BOOL "" FORCE)
set(HB_HAVE_FREETYPE ON CACHE BOOL "" FORCE)
set(HB_HAVE_GLIB OFF CACHE BOOL "" FORCE)
set(HB_HAVE_ICU OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    harfbuzz
    GIT_REPOSITORY https://github.com/harfbuzz/harfbuzz.git
    GIT_TAG        10.2.0
)
FetchContent_MakeAvailable(harfbuzz)

if (TARGET harfbuzz::harfbuzz)
    set(VOXEL_ENGINE_HARFBUZZ_TARGET harfbuzz::harfbuzz CACHE INTERNAL "")
elseif(TARGET harfbuzz)
    set(VOXEL_ENGINE_HARFBUZZ_TARGET harfbuzz CACHE INTERNAL "")
else()
    message(FATAL_ERROR "Unable to find a HarfBuzz CMake target after FetchContent_MakeAvailable(harfbuzz).")
endif()

set(MSDF_ATLAS_BUILD_STANDALONE OFF CACHE BOOL "" FORCE)
set(MSDF_ATLAS_USE_VCPKG OFF CACHE BOOL "" FORCE)
set(MSDF_ATLAS_USE_SKIA OFF CACHE BOOL "" FORCE)
set(MSDF_ATLAS_NO_ARTERY_FONT ON CACHE BOOL "" FORCE)
set(MSDFGEN_DISABLE_PNG ON CACHE BOOL "" FORCE)
FetchContent_Declare(
    msdf_atlas_gen
    GIT_REPOSITORY https://github.com/Chlumsky/msdf-atlas-gen.git
    GIT_TAG        v1.4
    GIT_SHALLOW    TRUE
    GIT_SUBMODULES "msdfgen"
)
FetchContent_MakeAvailable(msdf_atlas_gen)

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

add_library(spirv_reflect STATIC)
target_include_directories(spirv_reflect PUBLIC SPIRV-Reflect)
target_sources(spirv_reflect PRIVATE
    SPIRV-Reflect/spirv_reflect.h
    SPIRV-Reflect/spirv_reflect.cpp
)
