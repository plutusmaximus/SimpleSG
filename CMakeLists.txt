cmake_minimum_required(VERSION 3.23)
project(SimpleSG LANGUAGES CXX)
include(FetchContent)

# ============================================================
# Global settings
# ============================================================
set(BUILD_SHARED_LIBS OFF)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_EXTENSIONS OFF)
option(SIMPLESG_STRICT "Treat warnings as errors" ON)

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

    # Enable MSVC STL hardening
    #   TODO - remove in release builds
    add_compile_definitions(_MSVC_STL_HARDENING=2 _ITERATOR_DEBUG_LEVEL=2)
endif()

# ============================================================
# Dependencies (SDL3, spdlog, glm, stb)
# ============================================================

# --- SDL3
set(SDL3_SOURCE_DIR ${CMAKE_SOURCE_DIR}/thirdparty/SDL)

set(SDL_SHARED OFF CACHE BOOL "Build shared SDL library" FORCE)
set(SDL_STATIC ON CACHE BOOL "Build static SDL library" FORCE)
set(SDL_TEST OFF CACHE BOOL "Build SDL test programs" FORCE)
set(SDL_TESTS OFF CACHE BOOL "Build SDL tests" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "Build SDL test library" FORCE)

add_subdirectory(${SDL3_SOURCE_DIR} ${CMAKE_BINARY_DIR}/thirdparty/SDL-build)

# --- spdlog
set(SPDLOG_BUILD_SHARED OFF CACHE BOOL "Build as shared library" FORCE)
#set(SPDLOG_FMT_EXTERNAL OFF CACHE BOOL "Use external fmt library" FORCE)
#set(SPDLOG_USE_STD_FORMAT ON CACHE BOOL "Use C++20 std::format" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "Disable tests" FORCE)
set(SPDLOG_BUILD_EXAMPLES OFF CACHE BOOL "Disable examples" FORCE)
set(SPDLOG_INSTALL OFF CACHE BOOL "Disable install target" FORCE)
set(SPDLOG_HEADER_ONLY OFF CACHE BOOL "Disable header-only mode" FORCE)

add_subdirectory(thirdparty/spdlog)

# --- glm
set(GLM_BUILD_TESTS OFF CACHE BOOL "Build GLM test programs" FORCE)
set(GLM_BUILD_INSTALL OFF CACHE BOOL "Generate install target" FORCE)

add_subdirectory(thirdparty/glm)

# ============================================================
# Shaders / Assets
# ============================================================
add_subdirectory(shaders shaders-build)

add_custom_target(copy_images
  COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
          ${CMAKE_SOURCE_DIR}/Images ${CMAKE_BINARY_DIR}/Images
  COMMENT "Copying image files"
)
add_custom_target(copy_models
  COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
          ${CMAKE_SOURCE_DIR}/Models ${CMAKE_BINARY_DIR}/Models
  COMMENT "Copying model files"
)

# ============================================================
# Library: ssg
# ============================================================
file(GLOB_RECURSE SSG_CPP CONFIGURE_DEPENDS src/*.cpp)
file(GLOB_RECURSE SSG_HEADERS CONFIGURE_DEPENDS src/*.h)

add_library(ssg ${SSG_CPP})
add_library(ssg::ssg ALIAS ssg)
target_sources(ssg PUBLIC FILE_SET HEADERS BASE_DIRS src FILES ${SSG_HEADERS})

target_link_libraries(ssg PUBLIC glm::glm spdlog::spdlog SDL3::SDL3)
target_include_directories(ssg PUBLIC src PRIVATE thirdparty/stb)

if(MSVC)
  target_compile_definitions(ssg PRIVATE _HAS_EXCEPTIONS=0)
  target_compile_options(ssg PRIVATE /GR-)
else()
  target_compile_options(ssg PRIVATE -fno-exceptions)
endif()

if(SIMPLESG_STRICT)
  set_property(TARGET ssg PROPERTY COMPILE_WARNING_AS_ERROR ON)
endif()

# ============================================================
# Executable: Cube
# ============================================================
add_executable(Cube tests/cube.cpp)
target_link_libraries(Cube PRIVATE ssg::ssg spdlog::spdlog)
add_dependencies(Cube shaders copy_images copy_models)

if(MSVC)
  target_compile_definitions(Cube PRIVATE _HAS_EXCEPTIONS=0)
  target_compile_options(Cube PRIVATE /GR-)
else()
  target_compile_options(Cube PRIVATE -fno-exceptions)
endif()

if(SIMPLESG_STRICT)
  set_property(TARGET Cube PROPERTY COMPILE_WARNING_AS_ERROR ON)
endif()
