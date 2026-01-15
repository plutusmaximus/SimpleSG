cmake_minimum_required(VERSION 3.23)
project(SimpleSG LANGUAGES C CXX)

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
    #add_compile_definitions(_MSVC_STL_HARDENING=0 _ITERATOR_DEBUG_LEVEL=0)
endif()

# ============================================================
# Dependencies (SDL3, spdlog, glm, stb)
# ============================================================

# --- SDL3
set(SDL_SHARED OFF CACHE BOOL "Build shared SDL library" FORCE)
set(SDL_STATIC ON CACHE BOOL "Build static SDL library" FORCE)
set(SDL_TEST OFF CACHE BOOL "Build SDL test programs" FORCE)
set(SDL_TESTS OFF CACHE BOOL "Build SDL tests" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "Build SDL test library" FORCE)

add_subdirectory(thirdparty/SDL)

# --- spdlog
set(SPDLOG_BUILD_SHARED OFF CACHE BOOL "Build as shared library" FORCE)
#set(SPDLOG_FMT_EXTERNAL OFF CACHE BOOL "Use external fmt library" FORCE)
set(SPDLOG_USE_STD_FORMAT ON CACHE BOOL "Use C++20 std::format" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "Disable tests" FORCE)
set(SPDLOG_BUILD_EXAMPLES OFF CACHE BOOL "Disable examples" FORCE)
set(SPDLOG_INSTALL OFF CACHE BOOL "Disable install target" FORCE)
set(SPDLOG_HEADER_ONLY OFF CACHE BOOL "Disable header-only mode" FORCE)

add_subdirectory(thirdparty/spdlog)

# --- glm
set(GLM_BUILD_TESTS OFF CACHE BOOL "Build GLM test programs" FORCE)
set(GLM_BUILD_INSTALL OFF CACHE BOOL "Generate install target" FORCE)

add_subdirectory(thirdparty/glm)

# --- assimp
set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "Build assimp tests" FORCE)
set(ASSIMP_INSTALL OFF CACHE BOOL "Install assimp" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared libraries" FORCE)
set(ASSIMP_BUILD_ALL_EXPORTERS_BY_DEFAULT OFF CACHE BOOL "Build all exporters by default" FORCE)
set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT OFF CACHE BOOL "Build all importers by default" FORCE)
set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "Build assimp tests" FORCE)
set(ASSIMP_BUILD_ASSIMP_VIEW OFF CACHE BOOL "Build assimp viewer" FORCE)
set(ASSIMP_BUILD_GLTF_IMPORTER ON CACHE BOOL "Build glTF importer" FORCE)
set(ASSIMP_BUILD_STL_IMPORTER ON CACHE BOOL "Build STL importer" FORCE)
#set(ASSIMP_IMPORTER_GLTF2 ON CACHE BOOL "Build glTF2 importer" FORCE)
#set(ASSIMP_IMPORTER_GLTF ON CACHE BOOL "Build glTF importer" FORCE)

add_subdirectory(thirdparty/assimp)

# --- imgui
add_library(imgui STATIC
    thirdparty/imgui/imgui.cpp
    thirdparty/imgui/imgui_draw.cpp
    thirdparty/imgui/imgui_tables.cpp
    thirdparty/imgui/imgui_widgets.cpp
    thirdparty/imgui/imgui_demo.cpp
    thirdparty/imgui/backends/imgui_impl_sdl3.cpp
    thirdparty/imgui/backends/imgui_impl_sdlgpu3.cpp
)

target_sources(imgui PRIVATE
    thirdparty/imgui/backends/imgui_impl_sdl3.cpp
    # tests/imgui/backends/imgui_impl_vulkan.cpp
)

target_include_directories(imgui PUBLIC
    thirdparty/imgui
    thirdparty/imgui/backends
)

target_link_libraries(imgui PUBLIC SDL3::SDL3)

# --- lua
file(GLOB LUA_SOURCES CONFIGURE_DEPENDS "thirdparty/lua/*.c")
#Exclude onlua.c
list(FILTER LUA_SOURCES EXCLUDE REGEX "[/\\]onelua\\.c$")

# Create the Lua static library
add_library(lua STATIC ${LUA_SOURCES})
target_include_directories(lua PUBLIC thirdparty/lua)

# --- google test
add_subdirectory(thirdparty/googletest)

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
set(SSG_SOURCES
  src/Camera.cpp
  src/EcsChildTransformPool.cpp
  src/Error.cpp
  src/Image.cpp
  src/Model.cpp
  src/ResourceCache.cpp
  src/SDLGPUDevice.cpp
  src/SDLRenderGraph.cpp
  src/Shapes.cpp
  src/SpaceMouse.cpp
  src/Stopwatch.cpp
)

set(SSG_HEADERS
  src/Camera.h
  src/ECS.h
  src/EcsChildTransformPool.h
  src/Error.h
  src/GPUDevice.h
  src/Image.h
  src/Material.h
  src/Mesh.h
  src/Model.h
  src/RenderGraph.h
  src/ResourceCache.h
  src/SDLGPUDevice.h
  src/SDLRenderGraph.h
  src/Shapes.h
  src/SpaceMouse.h
  src/Stopwatch.h
  src/Uri.h
  src/VecMath.h
  src/Vertex.h
  src/imstring.h
  src/imvector.h
  src/scope_exit.h
)

add_library(ssg)
add_library(ssg::ssg ALIAS ssg)
target_sources(ssg
  PRIVATE ${SSG_SOURCES}
  PUBLIC FILE_SET HEADERS BASE_DIRS src FILES ${SSG_HEADERS}
)

target_link_libraries(ssg PUBLIC glm::glm spdlog::spdlog SDL3::SDL3 assimp::assimp)
target_include_directories(ssg PUBLIC src PRIVATE thirdparty/stb)

if(MSVC)
  target_compile_definitions(ssg PRIVATE _HAS_EXCEPTIONS=0)
  target_compile_options(ssg PRIVATE /GR- /W4 /WX)
else()
  target_compile_options(ssg PRIVATE -fno-exceptions -Wall -Wextra -Wpedantic -Werror)
endif()

if(SIMPLESG_STRICT)
  set_property(TARGET ssg PROPERTY COMPILE_WARNING_AS_ERROR ON)
endif()

# ============================================================
# Executable: Triangle
# ============================================================
add_executable(Triangle tests/triangle.cpp tests/MouseNav.cpp tests/AppDriver.cpp tests/LuaRepl.cpp)
target_link_libraries(Triangle PRIVATE ssg::ssg spdlog::spdlog imgui lua assimp::assimp)
add_dependencies(Triangle shaders copy_images copy_models)

if(MSVC)
  target_compile_definitions(Triangle PRIVATE _HAS_EXCEPTIONS=0)
  target_compile_options(Triangle PRIVATE /GR-)
else()
  target_compile_options(Triangle PRIVATE -fno-exceptions)
endif()

if(SIMPLESG_STRICT)
  set_property(TARGET Triangle PROPERTY COMPILE_WARNING_AS_ERROR ON)
endif()

# ============================================================
# Executable: Cube
# ============================================================
add_executable(Cube tests/cube.cpp tests/MouseNav.cpp tests/AppDriver.cpp tests/LuaRepl.cpp)
target_link_libraries(Cube PRIVATE ssg::ssg spdlog::spdlog imgui lua assimp::assimp)
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

# ============================================================
# Executable: Sponza
# ============================================================
add_executable(Sponza tests/sponza.cpp tests/MouseNav.cpp tests/AppDriver.cpp)
target_link_libraries(Sponza PRIVATE ssg::ssg spdlog::spdlog imgui assimp::assimp)
add_dependencies(Sponza shaders copy_images copy_models)

if(MSVC)
  target_compile_definitions(Sponza PRIVATE _HAS_EXCEPTIONS=0)
  target_compile_options(Sponza PRIVATE /GR-)
else()
  target_compile_options(Sponza PRIVATE -fno-exceptions)
endif()

if(SIMPLESG_STRICT)
  set_property(TARGET Sponza PROPERTY COMPILE_WARNING_AS_ERROR ON)
endif()

# ============================================================
# Executable: Tests
# ============================================================
add_executable(Tests
  "tests/ECS.test.cpp"
  "tests/imstring.test.cpp"
  "tests/EcsChildTransformPool.test.cpp"
  "tests/Uri.test.cpp"
  "tests/scope_exit.test.cpp"
  "tests/imvector.test.cpp"
  "tests/Radians.test.cpp")
  
target_link_libraries(Tests PRIVATE gtest_main ssg::ssg spdlog::spdlog)
#add_dependencies(RunAllTests shaders copy_images copy_models)

if(MSVC)
  target_compile_definitions(Tests PRIVATE _HAS_EXCEPTIONS=0)
  target_compile_options(Tests PRIVATE /GR-)
else()
  target_compile_options(Tests PRIVATE -fno-exceptions)
endif()

if(SIMPLESG_STRICT)
  set_property(TARGET Tests PROPERTY COMPILE_WARNING_AS_ERROR ON)
endif()

# Enable testing and register tests
# In Visual Studio tests should show up in Test Explorer.
# In VS Code install the CMake Test Explorer extension and open the testing view.
include(GoogleTest)
enable_testing()
gtest_discover_tests(Tests)