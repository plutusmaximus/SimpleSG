cmake_minimum_required(VERSION 3.25)
cmake_policy(SET CMP0091 NEW) # Use the new MSVC runtime library flag behavior
set(CMAKE_CROSS_CONFIGS "all")
project(SimpleSG LANGUAGES C CXX)

# ============================================================
# Global settings
# ============================================================

set(CMAKE_CONFIGURATION_TYPES
    Debug
    Release
    Sanitize
    CACHE STRING "Available build configurations" FORCE
)

add_compile_options(
    # Clang/GCC
    $<$<AND:$<CONFIG:Sanitize>,$<COMPILE_LANG_AND_ID:C,Clang,GNU>>:-O1>
    $<$<AND:$<CONFIG:Sanitize>,$<COMPILE_LANG_AND_ID:CXX,Clang,GNU>>:-O1>
    $<$<AND:$<CONFIG:Sanitize>,$<COMPILE_LANG_AND_ID:C,Clang,GNU>>:-g>
    $<$<AND:$<CONFIG:Sanitize>,$<COMPILE_LANG_AND_ID:CXX,Clang,GNU>>:-g>
    $<$<AND:$<CONFIG:Sanitize>,$<COMPILE_LANG_AND_ID:C,Clang,GNU>>:-fno-omit-frame-pointer>
    $<$<AND:$<CONFIG:Sanitize>,$<COMPILE_LANG_AND_ID:C,Clang,GNU>>:-fno-sanitize-merge>
    $<$<AND:$<CONFIG:Sanitize>,$<COMPILE_LANG_AND_ID:CXX,Clang,GNU>>:-fno-omit-frame-pointer>
    $<$<AND:$<CONFIG:Sanitize>,$<COMPILE_LANG_AND_ID:CXX,Clang,GNU>>:-fno-sanitize-merge>
    $<$<AND:$<CONFIG:Sanitize>,$<COMPILE_LANG_AND_ID:C,Clang,GNU>>:-fsanitize=address,undefined>
    $<$<AND:$<CONFIG:Sanitize>,$<COMPILE_LANG_AND_ID:CXX,Clang,GNU>>:-fsanitize=address,undefined>

    # MSVC
    $<$<AND:$<CONFIG:Sanitize>,$<COMPILE_LANG_AND_ID:C,MSVC>>:/fsanitize=address>
    $<$<AND:$<CONFIG:Sanitize>,$<COMPILE_LANG_AND_ID:CXX,MSVC>>:/fsanitize=address>
)

add_link_options(
    # Clang/GCC
    $<$<AND:$<CONFIG:Sanitize>,$<C_COMPILER_ID:Clang,GNU>>:-fsanitize=address,undefined>
    $<$<AND:$<CONFIG:Sanitize>,$<CXX_COMPILER_ID:Clang,GNU>>:-fsanitize=address,undefined>

    # MSVC
    $<$<AND:$<CONFIG:Sanitize>,$<C_COMPILER_ID:MSVC>>:/fsanitize=address>
    $<$<AND:$<CONFIG:Sanitize>,$<CXX_COMPILER_ID:MSVC>>:/fsanitize=address>
)

set(BUILD_SHARED_LIBS OFF)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "" FORCE)

  # Disable exceptions in the MSVC STL
  add_compile_definitions(_HAS_EXCEPTIONS=0)

  # Enable MSVC STL hardening
  add_compile_definitions($<$<OR:$<CONFIG:Debug>,$<CONFIG:Sanitize>>:_MSVC_STL_HARDENING=2> $<$<OR:$<CONFIG:Debug>,$<CONFIG:Sanitize>>:_ITERATOR_DEBUG_LEVEL=2>)
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
        -Wno-missing-designated-field-initializers
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

# Helper function to set compiler flags for a target
function(compiler_flags target_name)
  target_link_libraries(${target_name} PRIVATE project_warnings project_options)
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
set(DAWN_FETCH_DEPENDENCIES ON CACHE BOOL "Fetch Dawn dependencies" FORCE)
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
if(WIN32)
  set(DAWN_USE_WAYLAND OFF CACHE BOOL "Enable Wayland support" FORCE)
  set(DAWN_USE_X11 OFF CACHE BOOL "Enable X11 support" FORCE)
  set(DAWN_USE_WINDOWS_UI ON CACHE BOOL "Enable Windows UI support" FORCE)
  set(DAWN_ENABLE_METAL OFF CACHE BOOL "Enable Metal backend" FORCE)
elseif(APPLE)
  set(DAWN_USE_WAYLAND OFF CACHE BOOL "Enable Wayland support" FORCE)
  set(DAWN_USE_X11 OFF CACHE BOOL "Enable X11 support" FORCE)
  set(DAWN_USE_WINDOWS_UI OFF CACHE BOOL "Enable Windows UI support" FORCE)
  set(DAWN_ENABLE_METAL ON CACHE BOOL "Enable Metal backend" FORCE)
elseif(EMSCRIPTEN)
  set(DAWN_USE_WAYLAND OFF CACHE BOOL "Enable Wayland support" FORCE)
  set(DAWN_USE_X11 OFF CACHE BOOL "Enable X11 support" FORCE)
  set(DAWN_USE_WINDOWS_UI OFF CACHE BOOL "Enable Windows UI support" FORCE)
  set(DAWN_ENABLE_METAL OFF CACHE BOOL "Enable Metal backend" FORCE)
  set(DAWN_ENABLE_WEBGPU_ON_WEBGPU ON CACHE BOOL "Enable WebGPU backend" FORCE)
elseif(UNIX)
  set(DAWN_USE_WAYLAND ON CACHE BOOL "Enable Wayland support" FORCE)
  set(DAWN_USE_X11 ON CACHE BOOL "Enable X11 support" FORCE)
  set(DAWN_USE_WINDOWS_UI OFF CACHE BOOL "Enable Windows UI support" FORCE)
  set(DAWN_ENABLE_METAL OFF CACHE BOOL "Enable Metal backend" FORCE)
endif()

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
    thirdparty/imgui/imgui_demo.cpp
)

target_include_directories(imgui SYSTEM PUBLIC
    thirdparty/imgui
    thirdparty/imgui/backends
)

target_link_libraries(imgui PUBLIC SDL3::SDL3 dawn_headers)
target_compile_definitions(imgui PRIVATE IMGUI_IMPL_WEBGPU_BACKEND_DAWN)

# ============================================================
# Enable clang-tidy only after all thirdparty targets exist
# ============================================================

option(ENABLE_CLANG_TIDY "Enable clang-tidy analysis" OFF)

if(ENABLE_CLANG_TIDY)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang")
    find_program(CLANG_TIDY_EXE NAMES clang-tidy-22 clang-tidy)
    if(CLANG_TIDY_EXE)
      set(CMAKE_CXX_CLANG_TIDY
        ${CLANG_TIDY_EXE}
        --use-color
        "--exclude-header-filter=(thirdparty|build)[/\\]*")
    else()
      message(FATAL_ERROR "ENABLE_CLANG_TIDY is ON and the compiler is Clang, but clang-tidy was not found")
    endif()
  else()
    message(STATUS "ENABLE_CLANG_TIDY is ON, but the compiler is not Clang; skipping clang-tidy")
  endif()
endif()

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
  src/CliUi.cpp
  src/cgltf.cpp
  src/Compositor.cpp
  src/FileFetcher.cpp
  src/GridHash.cpp
  src/GltfLoader.cpp
  src/ImGuiRenderer.cpp
  src/Level.cpp
  src/Log.cpp
  src/PerfMetrics.cpp
  src/PhysicsLevel.cpp
  src/Camera.cpp
  src/PropKit.cpp
  src/Renderer.cpp
  src/Scene.cpp
  src/Shapes.cpp
  src/stb_image.cpp
  src/TextureCache.cpp
  src/ThreadPool.cpp
  src/Timer.cpp
  src/WebgpuHelper.cpp
)

set(SSG_HEADERS
  src/AssertHelper.h
  src/Bounds.h
  src/Camera.h
  src/CliUi.h
  src/Compositor.h
  src/FileFetcher.h
  src/GltfLoader.h
  src/GridHash.h
  src/ImGuiRenderer.h
  src/inlist.h
  src/Level.h
  src/LevelDefs.h
  src/Log.h
  src/PhysicsTypes.h
  src/PerfMetrics.h
  src/PhysicsLevel.h
  src/PropKit.h
  src/Renderer.h
  src/Result.h
  src/Scene.h
  src/scope_exit.h
  src/shaders/ShaderInterop.h
  src/Shapes.h
  src/CliState.h
  src/TextureCache.h
  src/ThreadPool.h
  src/Timer.h
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

# Keep third-party implementations out of clang-tidy noise.
set_source_files_properties(src/cgltf.cpp PROPERTIES SKIP_LINTING ON)
set_source_files_properties(src/stb_image.cpp PROPERTIES SKIP_LINTING ON)

target_link_libraries(ssg PUBLIC spdlog::spdlog SDL3::SDL3 webgpu_dawn imgui)
target_include_directories(ssg PUBLIC src src/shaders)
target_include_directories(ssg SYSTEM PRIVATE thirdparty/stb thirdparty/cgltf)

compiler_flags(ssg)

# ============================================================
# Helper function for sample executables
# ============================================================
function(add_sample_executable target_name source_file)
  add_executable(${target_name} ${source_file} samples/MouseNav.cpp)
  target_link_libraries(${target_name} PRIVATE ssg::ssg)
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
  elseif(UNIX AND NOT APPLE)
    find_package(Vulkan REQUIRED)

    target_link_libraries(${target_name} PRIVATE Vulkan::Vulkan)
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
# Executable: Tests
# ============================================================
add_executable(Tests
  "tests/BoundingBox.test.cpp"
  "tests/GridHash.test.cpp"
  "tests/inlist.test.cpp"
  "tests/Mat44.test.cpp"
  "tests/Quat.test.cpp"
  "tests/Radians.test.cpp"
  "tests/scope_exit.test.cpp"
  "tests/TrsTransform.test.cpp"
  "tests/Vec2.test.cpp"
  "tests/Vec3.test.cpp"
  "tests/Vec4.test.cpp")

target_link_libraries(Tests PRIVATE gtest_main ssg::ssg)
#add_dependencies(RunAllTests shaders copy_images)

compiler_flags(Tests)

# Enable testing and register tests
# In Visual Studio tests should show up in Test Explorer.
# In VS Code install the CMake Test Explorer extension and open the testing view.
include(GoogleTest)
enable_testing()
gtest_discover_tests(Tests)