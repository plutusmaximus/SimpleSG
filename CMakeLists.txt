cmake_minimum_required(VERSION 3.23)
project(SimpleSG LANGUAGES C CXX)

include(FetchContent)

# ============================================================
# Global: Prefer static libraries (no DLLs)
# ============================================================
set(BUILD_SHARED_LIBS OFF CACHE INTERNAL "")

# MSVC: Link the static runtime (/MT) so you don't depend on vcruntime DLLs.
if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

option(SIMPLESG_STRICT "Treat warnings as errors for project sources" ON)

# ============================================================
# SDL3
# ============================================================
set(SDL_TEST   OFF CACHE INTERNAL "")
set(SDL_TEST_LIBRARY   OFF CACHE INTERNAL "")
set(SDL_SHARED OFF CACHE INTERNAL "")
set(SDL_STATIC ON  CACHE INTERNAL "")

FetchContent_Declare(
	SDL3
	GIT_REPOSITORY	https://github.com/libsdl-org/SDL.git
	GIT_TAG 	release-3.2.24
)

FetchContent_MakeAvailable(SDL3)

# ============================================================
# spdlog
# ============================================================
set(SPDLOG_HEADER_ONLY OFF CACHE INTERNAL "")
set(SPDLOG_BUILD_SHARED OFF CACHE INTERNAL "")
set(SPDLOG_BUILD_STATIC ON CACHE INTERNAL "")
set(SPDLOG_BUILD_TESTS  OFF CACHE INTERNAL "")
set(SPDLOG_BUILD_EXAMPLE OFF CACHE INTERNAL "")
set(SPDLOG_WCHAR_SUPPORT ON CACHE INTERNAL "")

FetchContent_Declare(
	spdlog
	GIT_REPOSITORY	https://github.com/gabime/spdlog.git
	GIT_TAG 	v1.16.0
)

FetchContent_MakeAvailable(spdlog)

# ============================================================
# glm
# ============================================================
FetchContent_Declare(
	glm
	GIT_REPOSITORY	https://github.com/g-truc/glm.git
	GIT_TAG 	1.0.2
)

FetchContent_MakeAvailable(glm)

# ============================================================
# stb
# ============================================================
FetchContent_Declare(
	stb
	GIT_REPOSITORY	https://github.com/nothings/stb.git
	GIT_TAG 	f1c79c02822848a9bed4315b12c8c8f3761e1296
)

FetchContent_MakeAvailable(stb)

# ============================================================
# Shaders
# ============================================================
add_subdirectory(shaders shaders-build)

# ============================================================
# Images
# ============================================================
add_custom_target(copy_images
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different ${CMAKE_SOURCE_DIR}/Images ${CMAKE_CURRENT_BINARY_DIR}/Images/
    COMMENT "Copying image files to build directory"
)

# ============================================================
# Models
# ============================================================
add_custom_target(copy_models
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different ${CMAKE_SOURCE_DIR}/Models ${CMAKE_CURRENT_BINARY_DIR}/Models/
    COMMENT "Copying model files to build directory"
)

# ============================================================
# App target
# ============================================================
file(GLOB_RECURSE SIMPLESG_CPP CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)
file(GLOB_RECURSE SIMPLESG_HEADERS CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
)

add_executable(SimpleSG)

target_sources(SimpleSG
  PRIVATE ${SIMPLESG_CPP}
  PUBLIC
    FILE_SET HEADERS
      BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/src"
      FILES ${SIMPLESG_HEADERS}
)

target_include_directories(SimpleSG PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src" ${stb_SOURCE_DIR})

target_link_libraries(SimpleSG
  PRIVATE
    SDL3::SDL3
    spdlog::spdlog
    glm::glm
)

add_dependencies(SimpleSG shaders copy_images copy_models)

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
group_by_namespace("spdlog" "ThirdParty")

set_target_properties(shaders PROPERTIES FOLDER "Shaders")
set_target_properties(SimpleSG PROPERTIES FOLDER "")