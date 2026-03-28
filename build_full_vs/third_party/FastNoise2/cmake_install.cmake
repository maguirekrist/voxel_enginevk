# Install script for directory: C:/Users/magui/.codex/worktrees/f1b2/voxel_enginevk/third_party/FastNoise2

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "C:/Program Files/vulkan_guide")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "Release")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "FALSE")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  if(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "C:/Users/magui/.codex/worktrees/f1b2/voxel_enginevk/build_full_vs/third_party/FastNoise2/src/Debug/FastNoiseD.lib")
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "C:/Users/magui/.codex/worktrees/f1b2/voxel_enginevk/build_full_vs/third_party/FastNoise2/src/Release/FastNoise.lib")
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Mm][Ii][Nn][Ss][Ii][Zz][Ee][Rr][Ee][Ll])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "C:/Users/magui/.codex/worktrees/f1b2/voxel_enginevk/build_full_vs/third_party/FastNoise2/src/MinSizeRel/FastNoise.lib")
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ww][Ii][Tt][Hh][Dd][Ee][Bb][Ii][Nn][Ff][Oo])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "C:/Users/magui/.codex/worktrees/f1b2/voxel_enginevk/build_full_vs/third_party/FastNoise2/src/RelWithDebInfo/FastNoise.lib")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/FastSIMD" TYPE FILE FILES
    "C:/Users/magui/.codex/worktrees/f1b2/voxel_enginevk/third_party/FastNoise2/src/../include/FastSIMD/FastSIMD.h"
    "C:/Users/magui/.codex/worktrees/f1b2/voxel_enginevk/third_party/FastNoise2/src/../include/FastSIMD/FastSIMD_Config.h"
    "C:/Users/magui/.codex/worktrees/f1b2/voxel_enginevk/third_party/FastNoise2/src/../include/FastSIMD/FastSIMD_Export.h"
    "C:/Users/magui/.codex/worktrees/f1b2/voxel_enginevk/third_party/FastNoise2/src/../include/FastSIMD/FunctionList.h"
    "C:/Users/magui/.codex/worktrees/f1b2/voxel_enginevk/third_party/FastNoise2/src/../include/FastSIMD/InlInclude.h"
    "C:/Users/magui/.codex/worktrees/f1b2/voxel_enginevk/third_party/FastNoise2/src/../include/FastSIMD/SIMDTypeList.h"
    )
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/FastNoise" TYPE FILE FILES
    "C:/Users/magui/.codex/worktrees/f1b2/voxel_enginevk/third_party/FastNoise2/src/../include/FastNoise/FastNoise.h"
    "C:/Users/magui/.codex/worktrees/f1b2/voxel_enginevk/third_party/FastNoise2/src/../include/FastNoise/FastNoise_C.h"
    "C:/Users/magui/.codex/worktrees/f1b2/voxel_enginevk/third_party/FastNoise2/src/../include/FastNoise/FastNoise_Config.h"
    "C:/Users/magui/.codex/worktrees/f1b2/voxel_enginevk/third_party/FastNoise2/src/../include/FastNoise/FastNoise_Export.h"
    "C:/Users/magui/.codex/worktrees/f1b2/voxel_enginevk/third_party/FastNoise2/src/../include/FastNoise/Metadata.h"
    "C:/Users/magui/.codex/worktrees/f1b2/voxel_enginevk/third_party/FastNoise2/src/../include/FastNoise/SmartNode.h"
    )
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/FastNoise/Generators" TYPE FILE FILES
    "C:/Users/magui/.codex/worktrees/f1b2/voxel_enginevk/third_party/FastNoise2/src/../include/FastNoise/Generators/BasicGenerators.h"
    "C:/Users/magui/.codex/worktrees/f1b2/voxel_enginevk/third_party/FastNoise2/src/../include/FastNoise/Generators/Blends.h"
    "C:/Users/magui/.codex/worktrees/f1b2/voxel_enginevk/third_party/FastNoise2/src/../include/FastNoise/Generators/Cellular.h"
    "C:/Users/magui/.codex/worktrees/f1b2/voxel_enginevk/third_party/FastNoise2/src/../include/FastNoise/Generators/DomainWarp.h"
    "C:/Users/magui/.codex/worktrees/f1b2/voxel_enginevk/third_party/FastNoise2/src/../include/FastNoise/Generators/DomainWarpFractal.h"
    "C:/Users/magui/.codex/worktrees/f1b2/voxel_enginevk/third_party/FastNoise2/src/../include/FastNoise/Generators/Fractal.h"
    "C:/Users/magui/.codex/worktrees/f1b2/voxel_enginevk/third_party/FastNoise2/src/../include/FastNoise/Generators/Generator.h"
    "C:/Users/magui/.codex/worktrees/f1b2/voxel_enginevk/third_party/FastNoise2/src/../include/FastNoise/Generators/Modifiers.h"
    "C:/Users/magui/.codex/worktrees/f1b2/voxel_enginevk/third_party/FastNoise2/src/../include/FastNoise/Generators/Perlin.h"
    "C:/Users/magui/.codex/worktrees/f1b2/voxel_enginevk/third_party/FastNoise2/src/../include/FastNoise/Generators/Simplex.h"
    "C:/Users/magui/.codex/worktrees/f1b2/voxel_enginevk/third_party/FastNoise2/src/../include/FastNoise/Generators/Value.h"
    )
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/FastNoise2" TYPE FILE FILES
    "C:/Users/magui/.codex/worktrees/f1b2/voxel_enginevk/build_full_vs/third_party/FastNoise2/generated/FastNoise2Config.cmake"
    "C:/Users/magui/.codex/worktrees/f1b2/voxel_enginevk/build_full_vs/third_party/FastNoise2/generated/FastNoise2ConfigVersion.cmake"
    )
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/FastNoise2/FastNoise2Targets.cmake")
    file(DIFFERENT _cmake_export_file_changed FILES
         "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/FastNoise2/FastNoise2Targets.cmake"
         "C:/Users/magui/.codex/worktrees/f1b2/voxel_enginevk/build_full_vs/third_party/FastNoise2/CMakeFiles/Export/685b7b2fe36c8f3a2e6ec6f0a6279b14/FastNoise2Targets.cmake")
    if(_cmake_export_file_changed)
      file(GLOB _cmake_old_config_files "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/FastNoise2/FastNoise2Targets-*.cmake")
      if(_cmake_old_config_files)
        string(REPLACE ";" ", " _cmake_old_config_files_text "${_cmake_old_config_files}")
        message(STATUS "Old export file \"$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/FastNoise2/FastNoise2Targets.cmake\" will be replaced.  Removing files [${_cmake_old_config_files_text}].")
        unset(_cmake_old_config_files_text)
        file(REMOVE ${_cmake_old_config_files})
      endif()
      unset(_cmake_old_config_files)
    endif()
    unset(_cmake_export_file_changed)
  endif()
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/FastNoise2" TYPE FILE FILES "C:/Users/magui/.codex/worktrees/f1b2/voxel_enginevk/build_full_vs/third_party/FastNoise2/CMakeFiles/Export/685b7b2fe36c8f3a2e6ec6f0a6279b14/FastNoise2Targets.cmake")
  if(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/FastNoise2" TYPE FILE FILES "C:/Users/magui/.codex/worktrees/f1b2/voxel_enginevk/build_full_vs/third_party/FastNoise2/CMakeFiles/Export/685b7b2fe36c8f3a2e6ec6f0a6279b14/FastNoise2Targets-debug.cmake")
  endif()
  if(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Mm][Ii][Nn][Ss][Ii][Zz][Ee][Rr][Ee][Ll])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/FastNoise2" TYPE FILE FILES "C:/Users/magui/.codex/worktrees/f1b2/voxel_enginevk/build_full_vs/third_party/FastNoise2/CMakeFiles/Export/685b7b2fe36c8f3a2e6ec6f0a6279b14/FastNoise2Targets-minsizerel.cmake")
  endif()
  if(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ww][Ii][Tt][Hh][Dd][Ee][Bb][Ii][Nn][Ff][Oo])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/FastNoise2" TYPE FILE FILES "C:/Users/magui/.codex/worktrees/f1b2/voxel_enginevk/build_full_vs/third_party/FastNoise2/CMakeFiles/Export/685b7b2fe36c8f3a2e6ec6f0a6279b14/FastNoise2Targets-relwithdebinfo.cmake")
  endif()
  if(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/FastNoise2" TYPE FILE FILES "C:/Users/magui/.codex/worktrees/f1b2/voxel_enginevk/build_full_vs/third_party/FastNoise2/CMakeFiles/Export/685b7b2fe36c8f3a2e6ec6f0a6279b14/FastNoise2Targets-release.cmake")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  if(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE DIRECTORY FILES "C:/Users/magui/.codex/worktrees/f1b2/voxel_enginevk/build_full_vs/third_party/FastNoise2/pdb-files/Debug/")
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE DIRECTORY FILES "C:/Users/magui/.codex/worktrees/f1b2/voxel_enginevk/build_full_vs/third_party/FastNoise2/pdb-files/Release/")
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Mm][Ii][Nn][Ss][Ii][Zz][Ee][Rr][Ee][Ll])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE DIRECTORY FILES "C:/Users/magui/.codex/worktrees/f1b2/voxel_enginevk/build_full_vs/third_party/FastNoise2/pdb-files/MinSizeRel/")
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ww][Ii][Tt][Hh][Dd][Ee][Bb][Ii][Nn][Ff][Oo])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE DIRECTORY FILES "C:/Users/magui/.codex/worktrees/f1b2/voxel_enginevk/build_full_vs/third_party/FastNoise2/pdb-files/RelWithDebInfo/")
  endif()
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for each subdirectory.
  include("C:/Users/magui/.codex/worktrees/f1b2/voxel_enginevk/build_full_vs/third_party/FastNoise2/src/cmake_install.cmake")

endif()

string(REPLACE ";" "\n" CMAKE_INSTALL_MANIFEST_CONTENT
       "${CMAKE_INSTALL_MANIFEST_FILES}")
if(CMAKE_INSTALL_LOCAL_ONLY)
  file(WRITE "C:/Users/magui/.codex/worktrees/f1b2/voxel_enginevk/build_full_vs/third_party/FastNoise2/install_local_manifest.txt"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
endif()
