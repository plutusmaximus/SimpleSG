# Minimal, target-aware FindZLIB that prefers a static library.
# It never searches the system; it only reflects targets already added
# by your superbuild (e.g., add_subdirectory(zlib ...)).

# If the canonical alias already exists, honor it.
if(TARGET ZLIB::ZLIB)
  get_target_property(_incs ZLIB::ZLIB INTERFACE_INCLUDE_DIRECTORIES)
  set(ZLIB_FOUND TRUE)
  set(ZLIB_INCLUDE_DIRS "${_incs}")
  set(ZLIB_INCLUDE_DIR "${_incs}")      # legacy singular var
  set(ZLIB_LIBRARIES ZLIB::ZLIB)        # allow targets in legacy vars
  set(ZLIB_LIBRARY   ZLIB::ZLIB)
  return()
endif()

# Helper to populate result vars from a target
function(_zlib_set_result_from_target _tgt)
  get_target_property(_incs "${_tgt}" INTERFACE_INCLUDE_DIRECTORIES)
  # INTERFACE_INCLUDE_DIRECTORIES might be empty on some trees; that's OK.
  set(ZLIB_FOUND TRUE PARENT_SCOPE)
  set(ZLIB_INCLUDE_DIRS "${_incs}" PARENT_SCOPE)
  set(ZLIB_INCLUDE_DIR  "${_incs}" PARENT_SCOPE)
  set(ZLIB_LIBRARIES ZLIB::ZLIB PARENT_SCOPE)
  set(ZLIB_LIBRARY   ZLIB::ZLIB PARENT_SCOPE)
endfunction()

# Prefer an explicitly static target first
if(TARGET zlibstatic)
  if(NOT TARGET ZLIB::ZLIB)
    add_library(ZLIB::ZLIB ALIAS zlibstatic)
  endif()
  _zlib_set_result_from_target(zlibstatic)
  return()
endif()

# Some trees only produce "zlib"; accept it only if it's STATIC
if(TARGET zlib)
  get_target_property(_zlib_type zlib TYPE)
  if(_zlib_type STREQUAL "STATIC_LIBRARY")
    if(NOT TARGET ZLIB::ZLIB)
      add_library(ZLIB::ZLIB ALIAS zlib)
    endif()
    _zlib_set_result_from_target(zlib)
    return()
  else()
    # We found only a SHARED zlib. Fail fast so we don't pull a DLL.
    set(ZLIB_FOUND FALSE)
    message(FATAL_ERROR
      "FindZLIB.cmake: Found only SHARED target 'zlib' in the build graph. "
      "A static zlib is required. Ensure static options are set before add_subdirectory "
      "and clear the zlib sub-build cache.")
  endif()
endif()

# If we got here, the superbuild hasn't added any zlib target yet.
set(ZLIB_FOUND FALSE)
