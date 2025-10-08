cmake_minimum_required(VERSION 3.23)
project(SimpleSG LANGUAGES C CXX)

# ============================================================
# Global: Prefer static libraries (no DLLs)
# ============================================================
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

# MSVC: Link the static runtime (/MT) so you don't depend on vcruntime DLLs.
if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

option(SIMPLESG_STRICT "Treat warnings as errors for project sources" ON)

# Allow our Find*.cmake shims (e.g., FindZLIB.cmake)
list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# ============================================================
# Project-wide options and UTF-8 enforcement
# ============================================================
#add_library(project_policies INTERFACE)
#target_compile_definitions(project_policies INTERFACE
#  $<$<PLATFORM_ID:Windows>:UNICODE;_UNICODE>
#)
#target_compile_options(project_policies INTERFACE
#  $<$<CXX_COMPILER_ID:MSVC>:/utf-8 /W4>
#  $<$<AND:$<NOT:$<CXX_COMPILER_ID:MSVC>>,$<COMPILE_LANGUAGE:CXX>>:-Wall -Wextra -Wpedantic>
#)

# ============================================================
# zlib (static-only)
# ============================================================
set(ZLIB_BUILD_SHARED  OFF CACHE BOOL "" FORCE)
set(ZLIB_BUILD_STATIC  ON  CACHE BOOL "" FORCE)
set(ZLIB_ENABLE_SHARED OFF CACHE BOOL "" FORCE)
set(ZLIB_ENABLE_STATIC ON  CACHE BOOL "" FORCE)

add_subdirectory(zlib zlib-build EXCLUDE_FROM_ALL)

# Provide canonical alias
if(TARGET zlibstatic AND NOT TARGET ZLIB::ZLIB)
  add_library(ZLIB::ZLIB ALIAS zlibstatic)
elseif(TARGET zlib)
  get_target_property(_type zlib TYPE)
  if(_type STREQUAL "STATIC_LIBRARY" AND NOT TARGET ZLIB::ZLIB)
    add_library(ZLIB::ZLIB ALIAS zlib)
  endif()
endif()

# ============================================================
# SDL3 (static)
# ============================================================
set(SDL_TEST   OFF CACHE BOOL "" FORCE)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON  CACHE BOOL "" FORCE)
add_subdirectory(SDL SDL-build EXCLUDE_FROM_ALL)
if(NOT TARGET SDL3::SDL3-static)
  message(FATAL_ERROR "Expected SDL3::SDL3-static target not found in SDL subdirectory")
endif()

# ============================================================
# spdlog (static)
# ============================================================
set(SPDLOG_HEADER_ONLY OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS  OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
add_subdirectory(spdlog spdlog-build EXCLUDE_FROM_ALL)
if(NOT TARGET spdlog::spdlog)
  message(FATAL_ERROR "spdlog target not found — check the spdlog subdirectory")
endif()

# ============================================================
# libpng (static, uses ZLIB::ZLIB)
# ============================================================
set(PNG_SHARED OFF CACHE BOOL "" FORCE)
set(PNG_STATIC ON  CACHE BOOL "" FORCE)
set(PNG_TESTS  OFF CACHE BOOL "" FORCE)
set(PNG_TOOLS  OFF CACHE BOOL "" FORCE)

add_subdirectory(libpng libpng-build EXCLUDE_FROM_ALL)

if(NOT TARGET PNG::PNG)
  if(TARGET png_static)
    add_library(PNG::PNG ALIAS png_static)
  elseif(TARGET png)
    add_library(PNG::PNG ALIAS png)
  else()
    message(FATAL_ERROR "libpng did not define png_static or png")
  endif()
endif()

# ============================================================
# Shaders
# ============================================================
add_subdirectory(shaders shaders-build)

# ============================================================
# Images
# ============================================================

file(GLOB ImageFiles ${CMAKE_SOURCE_DIR}/Images/*.png ${CMAKE_SOURCE_DIR}/Images/*.jpeg ${CMAKE_SOURCE_DIR}/Images/*.jpg)

add_custom_target(copy_images
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different ${CMAKE_SOURCE_DIR}/Images ${CMAKE_CURRENT_BINARY_DIR}/Images/
    COMMENT "Copying image files to build directory"
)

# ============================================================
# App target
# ============================================================
file(GLOB_RECURSE SIMPLESG_CPP CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cxx"
)
file(GLOB_RECURSE SIMPLESG_HEADERS CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp"
)

add_executable(SimpleSG)

target_sources(SimpleSG
  PRIVATE ${SIMPLESG_CPP}
  PUBLIC
    FILE_SET HEADERS
      BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/src"
      FILES ${SIMPLESG_HEADERS}
)

target_include_directories(SimpleSG PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

target_link_libraries(SimpleSG
  PRIVATE
#    project_policies
    PNG::PNG
    ZLIB::ZLIB
    SDL3::SDL3-static
    spdlog::spdlog
)

add_dependencies(SimpleSG shaders copy_images)

target_compile_features(SimpleSG PRIVATE cxx_std_23)
set_target_properties(SimpleSG PROPERTIES CXX_EXTENSIONS NO)

if(MSVC)
  target_compile_definitions(SimpleSG PRIVATE _HAS_EXCEPTIONS=0)
  target_compile_options(SimpleSG PRIVATE /GR-)  # disable RTTI
else()
  target_compile_options(SimpleSG PRIVATE -fno-exceptions)
endif()

if(SIMPLESG_STRICT)
  set_property(TARGET SimpleSG PROPERTY COMPILE_WARNING_AS_ERROR ON)
endif()

# ============================================================
# IDE organization
# ============================================================
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

function(group_by_namespace ns folder)
  get_property(_tgt_list GLOBAL PROPERTY TARGETS)
  foreach(t ${_tgt_list})
    if(t MATCHES "^${ns}::")
      get_target_property(_aliased "${t}" ALIASED_TARGET)
      if(_aliased)
        set(_real "${_aliased}")
      else()
        set(_real "${t}")
      endif()
      if(TARGET "${_real}")
        set_target_properties("${_real}" PROPERTIES FOLDER "${folder}")
      endif()
    endif()
  endforeach()
endfunction()

group_by_namespace("SDL3"   "ThirdParty")
group_by_namespace("ZLIB"   "ThirdParty")
group_by_namespace("PNG"    "ThirdParty")
group_by_namespace("spdlog" "ThirdParty")

set_target_properties(shaders PROPERTIES FOLDER "Shaders")
set_target_properties(SimpleSG PROPERTIES FOLDER "")

# ============================================================
# Summary
# ============================================================
message(STATUS "-------------------------------------------------------")
message(STATUS "Build Summary:")
message(STATUS "  Project:        SimpleSG")
message(STATUS "  C++ Standard:   C++23")
message(STATUS "  Unicode:        Enabled (UTF-8 source/exec)")
message(STATUS "  PNG target:     PNG::PNG")
message(STATUS "  ZLIB target:    ZLIB::ZLIB (static)")
message(STATUS "  SDL target:     SDL3::SDL3-static")
message(STATUS "  spdlog target:  spdlog::spdlog (static)")
message(STATUS "  Shaders target: shaders")
message(STATUS "  STRICT (Werror): ${SIMPLESG_STRICT}")
message(STATUS "  BUILD_SHARED_LIBS: ${BUILD_SHARED_LIBS}")
if(MSVC)
  message(STATUS "  MSVC Runtime:   ${CMAKE_MSVC_RUNTIME_LIBRARY}")
endif()
message(STATUS "-------------------------------------------------------")
