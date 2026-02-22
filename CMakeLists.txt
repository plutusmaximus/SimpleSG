cmake_minimum_required(VERSION 3.23)
cmake_policy(SET CMP0091 NEW) # Use the new MSVC runtime library flag behavior
project(SimpleSG LANGUAGES C CXX)

# ============================================================
# Global settings
# ============================================================
set(BUILD_SHARED_LIBS OFF)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(SSG_ENABLE_ASAN "Enable AddressSanitizer (MSVC x64 only)" OFF)

if(MSVC AND SSG_ENABLE_ASAN)
  # Apply ASan to all targets (including third-party libs) to avoid runtime mismatches.
  # /bigobj is required because ASan inflates object files and MSVC can hit the section limit.
  add_compile_options(/fsanitize=address /Zi /bigobj)
endif()

# Place build outputs in per-config folders (e.g., build/Debug, build/Release)
foreach(OUTPUTCONFIG DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${OUTPUTCONFIG} ${CMAKE_BINARY_DIR}/${OUTPUTCONFIG})
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${OUTPUTCONFIG} ${CMAKE_BINARY_DIR}/${OUTPUTCONFIG})
  set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${OUTPUTCONFIG} ${CMAKE_BINARY_DIR}/${OUTPUTCONFIG})
endforeach()

if(MSVC)
  if(SSG_ENABLE_ASAN)
    # ASan on MSVC requires the DLL runtime (/MD or /MDd).
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL" CACHE STRING "" FORCE)
  else()
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "" FORCE)
  endif()

    # Enable MSVC STL hardening
    add_compile_definitions($<$<CONFIG:Debug>:_MSVC_STL_HARDENING=2> $<$<CONFIG:Debug>:_ITERATOR_DEBUG_LEVEL=2>)
    #add_compile_definitions($<$<CONFIG:Debug>:_MSVC_STL_HARDENING=0> $<$<CONFIG:Debug>:_ITERATOR_DEBUG_LEVEL=0>)
endif()

# Helper function to set compiler flags for a target
function(compiler_flags target_name)
  if(MSVC)
    target_compile_definitions(${target_name} PRIVATE _HAS_EXCEPTIONS=0)
    target_compile_options(${target_name} PRIVATE /GR- /W4 /WX)
    if(SSG_ENABLE_ASAN)
      target_link_options(${target_name} PRIVATE /INCREMENTAL:NO)
    endif()
  else()
    target_compile_options(${target_name} PRIVATE -fno-exceptions -Wall -Wextra -Wpedantic -Werror)
  endif()

  set_property(TARGET ${target_name} PROPERTY COMPILE_WARNING_AS_ERROR ON)
endfunction()

# On Windows with MSVC ASan enabled, instrumented executables require
# clang_rt.asan_dynamic-x86_64.dll at runtime. This helper adds a POST_BUILD
# copy step so the DLL is placed beside the target executable, preventing
# startup failures (including GoogleTest discovery like --gtest_list_tests).
function(add_asan_runtime_copy target_name)
  if(WIN32 AND MSVC AND SSG_ENABLE_ASAN)
    get_filename_component(MSVC_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
    set(ASAN_RUNTIME_DLL "${MSVC_BIN_DIR}/clang_rt.asan_dynamic-x86_64.dll")
    if(EXISTS "${ASAN_RUNTIME_DLL}")
      message(STATUS "ASan enabled: ${target_name} will copy runtime DLL from ${ASAN_RUNTIME_DLL}")
      add_custom_command(
        TARGET ${target_name} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E echo Copying ASan runtime DLL to $<TARGET_FILE_DIR:${target_name}>
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${ASAN_RUNTIME_DLL}" "$<TARGET_FILE_DIR:${target_name}>"
        COMMENT "Copying ASan runtime to the ${target_name} output directory"
      )
    else()
      message(WARNING "ASan runtime DLL not found for ${target_name}: ${ASAN_RUNTIME_DLL}")
    endif()
  endif()
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

add_subdirectory(thirdparty/SDL)

# --- Dawn
set(DAWN_FETCH_DEPENDENCIES ON)
set(DAWN_BUILD_SAMPLES OFF CACHE BOOL "Disable samples" FORCE)
set(DAWN_BUILD_TESTS OFF CACHE BOOL "Disable tests" FORCE)
set(DAWN_ENABLE_INSTALL OFF CACHE BOOL "Disable Dawn install" FORCE)
set(DAWN_BUILD_MONOLITHIC_LIBRARY STATIC CACHE STRING "Build Dawn monolithic library as static" FORCE)
if(SSG_ENABLE_ASAN)
  set(USE_MSVC_RUNTIME_LIBRARY_DLL ON CACHE BOOL "Use DLL MSVC runtime in GLFW" FORCE)
  set(protobuf_MSVC_STATIC_RUNTIME OFF CACHE BOOL "Use DLL MSVC runtime in protobuf" FORCE)
  set(ABSL_MSVC_STATIC_RUNTIME OFF CACHE BOOL "Use DLL MSVC runtime in abseil" FORCE)
else()
  set(USE_MSVC_RUNTIME_LIBRARY_DLL OFF CACHE BOOL "Use static MSVC runtime in GLFW" FORCE)
  set(protobuf_MSVC_STATIC_RUNTIME ON CACHE BOOL "Use static MSVC runtime in protobuf" FORCE)
  set(ABSL_MSVC_STATIC_RUNTIME ON CACHE BOOL "Use static MSVC runtime in abseil" FORCE)
endif()
set(TINT_ENABLE_INSTALL OFF CACHE BOOL "Disable Tint install" FORCE)
set(TINT_BUILD_TESTS OFF CACHE BOOL "Disable Tint tests" FORCE)

add_subdirectory(thirdparty/dawn EXCLUDE_FROM_ALL)

# --- spdlog
set(SPDLOG_BUILD_SHARED OFF CACHE BOOL "Build as shared library" FORCE)
#set(SPDLOG_FMT_EXTERNAL OFF CACHE BOOL "Use external fmt library" FORCE)
set(SPDLOG_USE_STD_FORMAT ON CACHE BOOL "Use C++20 std::format" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "Disable tests" FORCE)
set(SPDLOG_BUILD_EXAMPLES OFF CACHE BOOL "Disable examples" FORCE)
set(SPDLOG_INSTALL OFF CACHE BOOL "Disable install target" FORCE)
set(SPDLOG_HEADER_ONLY OFF CACHE BOOL "Disable header-only mode" FORCE)

add_subdirectory(thirdparty/spdlog)

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

# --- lua
file(GLOB LUA_SOURCES CONFIGURE_DEPENDS "thirdparty/lua/*.c")
#Exclude onlua.c
list(FILTER LUA_SOURCES EXCLUDE REGEX "[/\\]onelua\\.c$")

# Create the Lua static library
add_library(lua STATIC ${LUA_SOURCES})
target_include_directories(lua PUBLIC thirdparty/lua)

# ============================================================
# Shaders / Assets
# ============================================================
add_subdirectory(shaders shaders-build)

add_custom_target(copy_images
  COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
          ${CMAKE_SOURCE_DIR}/images ${CMAKE_BINARY_DIR}/images
  COMMENT "Copying image files"
)
add_custom_target(copy_models
  COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
          ${CMAKE_SOURCE_DIR}/models ${CMAKE_BINARY_DIR}/models
  COMMENT "Copying model files"
)

# ============================================================
# Library: ssg
# ============================================================
set(SSG_SOURCES
  src/Camera.cpp
  src/CoopScheduler.cpp
  src/DawnGpuDevice.cpp
  src/DawnRenderer.cpp
  src/EcsChildTransformPool.cpp
  src/Error.cpp
  src/FileIo.cpp
  src/Model.cpp
  src/PerfMetrics.cpp
  src/ResourceCache.cpp
  src/SdlGpuDevice.cpp
  src/SdlRenderer.cpp
  src/Shapes.cpp
  src/SpaceMouse.cpp
  src/Stopwatch.cpp
  src/ThreadPool.cpp
)

set(SSG_HEADERS
  src/Camera.h
  src/CoopScheduler.h
  src/DawnGpuDevice.h
  src/DawnRenderer.h
  src/ECS.h
  src/EcsChildTransformPool.h
  src/Error.h
  src/FileIo.h
  src/imstring.h
  src/imvector.h
  src/inlist.h
  src/instack.h
  src/GpuDevice.h
  src/Logging.h
  src/Material.h
  src/Mesh.h
  src/Model.h
  src/PerfMetrics.h
  src/PoolAllocator.h
  src/Renderer.h
  src/ResourceCache.h
  src/Result.h
  src/SdlGpuDevice.h
  src/SdlRenderer.h
  src/Shapes.h
  src/SpaceMouse.h
  src/Stopwatch.h
  src/ThreadPool.h
  src/Uri.h
  src/VecMath.h
  src/Vertex.h
  src/scope_exit.h
)

add_library(ssg)
add_library(ssg::ssg ALIAS ssg)
target_sources(ssg
  PRIVATE ${SSG_SOURCES}
  PUBLIC FILE_SET HEADERS BASE_DIRS src FILES ${SSG_HEADERS}
)

target_link_libraries(ssg PUBLIC spdlog::spdlog SDL3::SDL3 assimp::assimp webgpu_dawn imgui)
target_include_directories(ssg PUBLIC src PRIVATE thirdparty/stb)

add_dependencies(ssg tint_cmd_tint_cmd)

compiler_flags(ssg)

# ============================================================
# Helper function for sample executables
# ============================================================
function(add_sample_executable target_name source_file)
  add_executable(${target_name} ${source_file} tests/MouseNav.cpp tests/AppDriver.cpp tests/LuaRepl.cpp)
  target_link_libraries(${target_name} PRIVATE ssg::ssg spdlog::spdlog imgui lua assimp::assimp webgpu_dawn webgpu_glfw glfw)
  add_dependencies(${target_name} shaders copy_images copy_models)

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

  add_asan_runtime_copy(${target_name})
endfunction()

# ============================================================
# Executable: Triangle
# ============================================================
add_sample_executable(Triangle tests/triangle.cpp)

# ============================================================
# Executable: Cube
# ============================================================
add_sample_executable(Cube tests/cube.cpp)

# ============================================================
# Executable: Sponza
# ============================================================
add_sample_executable(Sponza tests/sponza.cpp)

# ============================================================
# Executable: Tests
# ============================================================
add_executable(Tests
  "tests/CoopScheduler.test.cpp"
  "tests/ECS.test.cpp"
  "tests/EcsChildTransformPool.test.cpp"
  "tests/imstring.test.cpp"
  "tests/imvector.test.cpp"
  "tests/inlist.test.cpp"
  "tests/instack.test.cpp"
  "tests/Mat44.test.cpp"
  "tests/PoolAllocator.test.cpp"
  "tests/Quat.test.cpp"
  "tests/Radians.test.cpp"
  "tests/scope_exit.test.cpp"
  "tests/Vec2.test.cpp"
  "tests/Vec3.test.cpp"
  "tests/Vec4.test.cpp"
  "tests/Uri.test.cpp")

target_link_libraries(Tests PRIVATE gtest_main ssg::ssg spdlog::spdlog)
#add_dependencies(RunAllTests shaders copy_images copy_models)

compiler_flags(Tests)

add_asan_runtime_copy(Tests)

# Enable testing and register tests
# In Visual Studio tests should show up in Test Explorer.
# In VS Code install the CMake Test Explorer extension and open the testing view.
include(GoogleTest)
enable_testing()
gtest_discover_tests(Tests)