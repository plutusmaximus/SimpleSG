cmake_minimum_required(VERSION 3.23)
cmake_policy(SET CMP0091 NEW) # Use the new MSVC runtime library flag behavior
set(CMAKE_CROSS_CONFIGS "all")
project(SimpleSG LANGUAGES C CXX)

# ============================================================
# Global settings
# ============================================================
set(BUILD_SHARED_LIBS OFF)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "" FORCE)

    # Enable MSVC STL hardening
    add_compile_definitions($<$<CONFIG:Debug>:_MSVC_STL_HARDENING=2> $<$<CONFIG:Debug>:_ITERATOR_DEBUG_LEVEL=2>)
    #add_compile_definitions($<$<CONFIG:Debug>:_MSVC_STL_HARDENING=0> $<$<CONFIG:Debug>:_ITERATOR_DEBUG_LEVEL=0>)
endif()

add_library(project_warnings INTERFACE)

target_compile_options(project_warnings INTERFACE
    $<$<CXX_COMPILER_ID:GNU>:
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wsign-conversion
        -Wshadow
        -Wformat=2
        -Wundef
        -Wcast-align
        -Wcast-qual
        -Wold-style-cast
        -Woverloaded-virtual
        -Wnon-virtual-dtor
        -Wnull-dereference
    >

    $<$<CXX_COMPILER_ID:Clang,AppleClang>:
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wsign-conversion
        -Wshadow-all
        -Wformat=2
        -Wundef
        -Wcast-align
        -Wcast-qual
        -Wold-style-cast
        -Woverloaded-virtual
        -Wnon-virtual-dtor
        -Wnull-dereference
        -Wextra-semi
        -Wimplicit-fallthrough
        -Wunreachable-code
        -Wno-c2y-extensions
    >

    $<$<CXX_COMPILER_ID:MSVC>:
        /W4
        /permissive-
    >
)

add_library(project_options INTERFACE)

target_compile_options(project_options INTERFACE
    # Disable RTTI and exceptions.
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:
        -fno-rtti
        -fno-exceptions
    >

    # Disable RTTI and exceptions.
    $<$<CXX_COMPILER_ID:MSVC>:
        /GR-
        /EHs-c-
        /Zc:preprocessor
    >
)

add_library(project_definitions INTERFACE)

target_compile_definitions(project_definitions INTERFACE
    # Disable exceptions in the standard library to avoid pulling in unwanted code and dependencies.
    $<$<CXX_COMPILER_ID:MSVC>:_HAS_EXCEPTIONS=0>
)

# Helper function to set compiler flags for a target
function(compiler_flags target_name)
  target_link_libraries(${target_name} PRIVATE project_warnings project_options project_definitions)
  set_property(TARGET ${target_name} PROPERTY COMPILE_WARNING_AS_ERROR ON)
endfunction()

# ============================================================
# Dependencies (SDL3, spdlog, stb)
# ============================================================

# --- Google Test
# --- add google test before anything that might use it (like Dawn)
add_subdirectory(thirdparty/googletest)

# --- SDL3
set(SDL_SHARED OFF CACHE BOOL "Build shared SDL library" FORCE)
set(SDL_STATIC ON CACHE BOOL "Build static SDL library" FORCE)
set(SDL_TEST OFF CACHE BOOL "Build SDL test programs" FORCE)
set(SDL_TESTS OFF CACHE BOOL "Build SDL tests" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "Build SDL test library" FORCE)

add_subdirectory(thirdparty/SDL EXCLUDE_FROM_ALL SYSTEM)

# --- Dawn
set(DAWN_FETCH_DEPENDENCIES ON)
set(DAWN_BUILD_SAMPLES OFF CACHE BOOL "Disable samples" FORCE)
set(DAWN_USE_GLFW OFF CACHE BOOL "Disable GLFW" FORCE)
set(DAWN_BUILD_TESTS OFF CACHE BOOL "Disable tests" FORCE)
set(DAWN_ENABLE_INSTALL OFF CACHE BOOL "Disable Dawn install" FORCE)
set(DAWN_BUILD_MONOLITHIC_LIBRARY STATIC CACHE STRING "Build Dawn monolithic library as static" FORCE)
set(USE_MSVC_RUNTIME_LIBRARY_DLL OFF CACHE BOOL "Use static MSVC runtime in GLFW" FORCE)
set(protobuf_MSVC_STATIC_RUNTIME ON CACHE BOOL "Use static MSVC runtime in protobuf" FORCE)
set(ABSL_MSVC_STATIC_RUNTIME ON CACHE BOOL "Use static MSVC runtime in abseil" FORCE)
set(TINT_ENABLE_INSTALL OFF CACHE BOOL "Disable Tint install" FORCE)
set(TINT_BUILD_TESTS OFF CACHE BOOL "Disable Tint tests" FORCE)
set(DAWN_USE_WAYLAND ON CACHE BOOL "Enable Wayland support" FORCE)
set(DAWN_USE_X11 ON CACHE BOOL "Enable X11 support" FORCE)

add_subdirectory(thirdparty/dawn EXCLUDE_FROM_ALL SYSTEM)

# --- spdlog
set(SPDLOG_BUILD_SHARED OFF CACHE BOOL "Build as shared library" FORCE)
#set(SPDLOG_FMT_EXTERNAL OFF CACHE BOOL "Use external fmt library" FORCE)
set(SPDLOG_USE_STD_FORMAT ON CACHE BOOL "Use C++20 std::format" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "Disable tests" FORCE)
set(SPDLOG_BUILD_EXAMPLES OFF CACHE BOOL "Disable examples" FORCE)
set(SPDLOG_INSTALL OFF CACHE BOOL "Disable install target" FORCE)
set(SPDLOG_HEADER_ONLY OFF CACHE BOOL "Disable header-only mode" FORCE)

add_subdirectory(thirdparty/spdlog EXCLUDE_FROM_ALL SYSTEM)

# --- imgui
add_library(imgui STATIC
    thirdparty/imgui/imgui.cpp
    thirdparty/imgui/imgui_draw.cpp
    thirdparty/imgui/imgui_tables.cpp
    thirdparty/imgui/imgui_widgets.cpp
    thirdparty/imgui/backends/imgui_impl_sdl3.cpp
    thirdparty/imgui/backends/imgui_impl_sdlgpu3.cpp
    thirdparty/imgui/backends/imgui_impl_wgpu.cpp
)

target_include_directories(imgui PUBLIC
    thirdparty/imgui
    thirdparty/imgui/backends
    thirdparty/dawn/include
    ${CMAKE_BINARY_DIR}/thirdparty/dawn/gen/include
)

target_link_libraries(imgui PUBLIC SDL3::SDL3)
target_compile_definitions(imgui PRIVATE IMGUI_IMPL_WEBGPU_BACKEND_DAWN)

# ============================================================
# Shaders / Assets
# ============================================================
add_subdirectory(src/shaders)

add_custom_target(copy_images
  COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
          ${CMAKE_SOURCE_DIR}/images ${CMAKE_BINARY_DIR}/$<CONFIG>/images
  COMMENT "Copying image files"
)

# ============================================================
# Library: ssg
# ============================================================
set(SSG_SOURCES
  src/AssertHelper.cpp
  src/Bounds.cpp
  src/Compositor.cpp
  src/CoopScheduler.cpp
  src/EcsChildTransformPool.cpp
  src/FileFetcher.cpp
  src/GltfLoader.cpp
  src/ImGuiRenderer.cpp
  src/Level.cpp
  src/Log.cpp
  src/PerfMetrics.cpp
  src/PhysicsLevel.cpp
  src/Projection.cpp
  src/PropKit.cpp
  src/Renderer.cpp
  src/Scene.cpp
  src/Shapes.cpp
  src/Stopwatch.cpp
  src/TextureCache.cpp
  src/ThreadPool.cpp
  src/WebgpuHelper.cpp
)

set(SSG_HEADERS
  src/AssertHelper.h
  src/Bounds.h
  src/Compositor.h
  src/CoopScheduler.h
  src/ECS.h
  src/EcsChildTransformPool.h
  src/FileFetcher.h
  src/GltfLoader.h
  src/ImGuiRenderer.h
  src/inlist.h
  src/instack.h
  src/Level.h
  src/Log.h
  src/Mechanics.h
  src/PerfMetrics.h
  src/PhysicsLevel.h
  src/PoolAllocator.h
  src/Projection.h
  src/PropKit.h
  src/Renderer.h
  src/Result.h
  src/Scene.h
  src/scope_exit.h
  src/shaders/ShaderInterop.h
  src/Shapes.h
  src/Stopwatch.h
  src/TextureCache.h
  src/ThreadPool.h
  src/VecMath.h
  src/Vertex.h
  src/WebgpuHelper.h
)

add_library(ssg)
add_library(ssg::ssg ALIAS ssg)
target_sources(ssg
  PRIVATE ${SSG_SOURCES}
  PUBLIC FILE_SET HEADERS BASE_DIRS src FILES ${SSG_HEADERS}
)

target_link_libraries(ssg PUBLIC spdlog::spdlog SDL3::SDL3 webgpu_dawn imgui)
target_include_directories(ssg PUBLIC src src/shaders PRIVATE thirdparty/stb thirdparty/cgltf)

add_dependencies(ssg tint_cmd_tint_cmd)

compiler_flags(ssg)

# ============================================================
# Helper function for sample executables
# ============================================================
function(add_sample_executable target_name source_file)
  add_executable(${target_name} ${source_file} samples/MouseNav.cpp)
  target_link_libraries(${target_name} PRIVATE ssg::ssg spdlog::spdlog imgui webgpu_dawn webgpu_glfw glfw)
  add_dependencies(${target_name} shaders copy_images)

  compiler_flags(${target_name})

  # Keep required runtime DLLs beside the executable so runs work outside System32.
  if(WIN32)
    set(WIN_SYSTEM_DIR "$ENV{SystemRoot}/System32")
    add_custom_command(
      TARGET ${target_name} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              ${WIN_SYSTEM_DIR}/d3dcompiler_47.dll $<TARGET_FILE_DIR:${target_name}>/
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              ${WIN_SYSTEM_DIR}/vulkan-1.dll $<TARGET_FILE_DIR:${target_name}>/
      COMMENT "Copying d3dcompiler_47.dll and vulkan-1.dll to the ${target_name} output directory"
    )
  endif()
endfunction()

# ============================================================
# Executable: Triangle
# ============================================================
add_sample_executable(Triangle samples/Triangle.cpp)

# ============================================================
# Executable: Viewer
# ============================================================
add_sample_executable(Viewer samples/Viewer.cpp)

# ============================================================
# Executable: Orbit
# ============================================================
add_sample_executable(Orbit samples/Orbit.cpp)

# ============================================================
# Executable: Collision
# ============================================================
add_sample_executable(Collision samples/Collision.cpp)

# ============================================================
# Executable: Tests
# ============================================================
add_executable(Tests
  "tests/BoundingBox.test.cpp"
  "tests/CoopScheduler.test.cpp"
  "tests/ECS.test.cpp"
  "tests/EcsChildTransformPool.test.cpp"
  "tests/GridHash.test.cpp"
  "tests/inlist.test.cpp"
  "tests/instack.test.cpp"
  "tests/Mat44.test.cpp"
  "tests/PoolAllocator.test.cpp"
  "tests/Quat.test.cpp"
  "tests/Radians.test.cpp"
  "tests/scope_exit.test.cpp"
  "tests/SemanticInteger.test.cpp"
  "tests/TrsTransform.test.cpp"
  "tests/Vec2.test.cpp"
  "tests/Vec3.test.cpp"
  "tests/Vec4.test.cpp")

target_link_libraries(Tests PRIVATE gtest_main ssg::ssg spdlog::spdlog)
#add_dependencies(RunAllTests shaders copy_images)

compiler_flags(Tests)

# Enable testing and register tests
# In Visual Studio tests should show up in Test Explorer.
# In VS Code install the CMake Test Explorer extension and open the testing view.
include(GoogleTest)
enable_testing()
gtest_discover_tests(Tests)