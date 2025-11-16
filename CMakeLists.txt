cmake_minimum_required(VERSION 3.23)
project(SimpleSG LANGUAGES C CXX)
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
set(SDL_SHARED OFF CACHE BOOL "Build shared SDL library" FORCE)
set(SDL_STATIC ON CACHE BOOL "Build static SDL library" FORCE)
set(SDL_TEST OFF CACHE BOOL "Build SDL test programs" FORCE)
set(SDL_TESTS OFF CACHE BOOL "Build SDL tests" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "Build SDL test library" FORCE)

add_subdirectory(thirdparty/SDL)

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

# ---- Example app ----
add_executable(example_sdl3_sdlgpu3
    thirdparty/imgui/examples/example_sdl3_sdlgpu3/main.cpp
)

target_link_libraries(example_sdl3_sdlgpu3 PRIVATE imgui SDL3::SDL3)

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
add_executable(Cube tests/cube.cpp tests/MouseNav.cpp tests/LuaRepl.cpp)
target_link_libraries(Cube PRIVATE ssg::ssg spdlog::spdlog imgui lua)
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
# Executable: EcsTests
# ============================================================
add_executable(EcsTests "tests/ECS.test.cpp")
target_link_libraries(EcsTests PRIVATE gtest_main ssg::ssg spdlog::spdlog imgui lua)
#add_dependencies(RunAllTests shaders copy_images copy_models)

if(MSVC)
  target_compile_definitions(EcsTests PRIVATE _HAS_EXCEPTIONS=0)
  target_compile_options(EcsTests PRIVATE /GR-)
else()
  target_compile_options(EcsTests PRIVATE -fno-exceptions)
endif()

if(SIMPLESG_STRICT)
  set_property(TARGET EcsTests PROPERTY COMPILE_WARNING_AS_ERROR ON)
endif()

# Enable testing and register tests
# In Visual Studio tests should show up in Test Explorer.
# In VS Code install the CMake Test Explorer extension and open the testing view.
include(GoogleTest)
enable_testing()
gtest_discover_tests(EcsTests)