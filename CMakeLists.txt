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
FetchContent_Declare(SDL3
  GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
  GIT_TAG release-3.2.24
)
FetchContent_MakeAvailable(SDL3)

FetchContent_Declare(spdlog
  GIT_REPOSITORY https://github.com/gabime/spdlog.git
  GIT_TAG v1.16.0
)
FetchContent_MakeAvailable(spdlog)

FetchContent_Declare(glm
  GIT_REPOSITORY https://github.com/g-truc/glm.git
  GIT_TAG 1.0.2
)
FetchContent_MakeAvailable(glm)

FetchContent_Declare(stb
  GIT_REPOSITORY https://github.com/nothings/stb.git
  GIT_TAG f1c79c02822848a9bed4315b12c8c8f3761e1296
)
FetchContent_MakeAvailable(stb)
FetchContent_GetProperties(stb)
if(NOT stb_POPULATED)
  FetchContent_Populate(stb)
endif()
add_library(stb INTERFACE)
target_include_directories(stb INTERFACE "${stb_SOURCE_DIR}")
add_library(stb::stb ALIAS stb)

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

target_link_libraries(ssg PUBLIC glm::glm spdlog::spdlog SDL3::SDL3 stb::stb)
target_include_directories(ssg PUBLIC src)

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
target_link_libraries(Cube PRIVATE ssg::ssg)
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
