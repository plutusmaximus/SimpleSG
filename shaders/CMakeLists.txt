cmake_minimum_required(VERSION 3.31)
project(HLSLToSpirvAndDxil LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------
set(HLSL_SRC_DIR "${CMAKE_CURRENT_LIST_DIR}" CACHE PATH "Where to search for *.hlsl")

# Use a single stable shader output directory for rule registration
set(HLSL_BIN_DIR "${CMAKE_BINARY_DIR}/shaders" CACHE STRING "Where to place compiled shaders")

set(HLSL_ENTRYPOINT "main" CACHE STRING "Default HLSL entry point name")

# ---------------------------------------------------------------------------
# Find DXC from Vulkan SDK
# ---------------------------------------------------------------------------
if(NOT DEFINED DXC_EXE)
  if(NOT DEFINED ENV{VULKAN_SDK})
    message(FATAL_ERROR
      "VULKAN_SDK not set. Please install the Vulkan SDK or pass -DDXC_EXE=/path/to/dxc.")
  endif()

  if(WIN32)
    set(_dxc_guess "$ENV{VULKAN_SDK}/Bin/dxc.exe")
  else()
    set(_dxc_guess "$ENV{VULKAN_SDK}/bin/dxc")
    if(NOT EXISTS "${_dxc_guess}")
      set(_dxc_guess "$ENV{VULKAN_SDK}/Bin/dxc")
    endif()
  endif()

  if(EXISTS "${_dxc_guess}")
    set(DXC_EXE "${_dxc_guess}" CACHE FILEPATH "DXC compiler executable")
  else()
    message(FATAL_ERROR "Could not locate DXC under VULKAN_SDK.")
  endif()
endif()

# ---------------------------------------------------------------------------
# Infer shader stage from filename
# ---------------------------------------------------------------------------
function(_infer_hlsl_profile OUT_PROFILE INPUT_PATH)
  get_filename_component(_name "${INPUT_PATH}" NAME_WE)
  string(TOLOWER "${_name}" _lower)
  set(_profile "")

  string(REGEX REPLACE ".*[._-]([^._-]+)$" "\\1" _token "${_lower}")
  if(_token STREQUAL "vs")
    set(_profile "vs_6_7")
  elseif(_token STREQUAL "ps" OR _token STREQUAL "fs")
    set(_profile "ps_6_7")
  elseif(_token STREQUAL "cs")
    set(_profile "cs_6_7")
  elseif(_token STREQUAL "gs")
    set(_profile "gs_6_7")
  elseif(_token STREQUAL "hs")
    set(_profile "hs_6_7")
  elseif(_token STREQUAL "ds")
    set(_profile "ds_6_7")
  elseif(_token STREQUAL "ms")
    set(_profile "ms_6_7")
  elseif(_token STREQUAL "as")
    set(_profile "as_6_7")
  else()
    if(_lower MATCHES "compute")
      set(_profile "cs_6_7")
    elseif(_lower MATCHES "vertex")
      set(_profile "vs_6_7")
    elseif(_lower MATCHES "pixel|fragment")
      set(_profile "ps_6_7")
    endif()
  endif()

  set(${OUT_PROFILE} "${_profile}" PARENT_SCOPE)
endfunction()

# ---------------------------------------------------------------------------
# Gather shaders and create rules
# ---------------------------------------------------------------------------
file(GLOB HLSL_SOURCES CONFIGURE_DEPENDS "${HLSL_SRC_DIR}/*.hlsl")

set(_all_outputs "")
foreach(_src IN LISTS HLSL_SOURCES)
  get_filename_component(_base "${_src}" NAME_WE)

  _infer_hlsl_profile(_profile "${_src}")
  if(_profile STREQUAL "")
    message(WARNING "Skipping HLSL file (stage not recognized): ${_src}")
    continue()
  endif()

  # Stable logical outputs (no $<CONFIG>)
  set(_spv  "${HLSL_BIN_DIR}/${_base}.spv")
  set(_dxil "${HLSL_BIN_DIR}/${_base}.dxil")

  # Actual runtime output directory includes configuration
  set(_outdir "${HLSL_BIN_DIR}/$<CONFIG>")

  set(_common_args -T "${_profile}" -E "${HLSL_ENTRYPOINT}" -enable-16bit-types)

  # --- SPIR-V ---
  add_custom_command(
    OUTPUT "${_spv}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<SHELL_PATH:${_outdir}>"
    COMMAND "${DXC_EXE}" ${_common_args} -spirv -fvk-use-dx-layout
            -Fo "$<SHELL_PATH:${_outdir}>/${_base}.spv" "${_src}"
    DEPENDS "${_src}"
    BYPRODUCTS "$<SHELL_PATH:${_outdir}>/${_base}.spv"
    COMMENT "DXC: ${_base}.hlsl -> ${_base}.spv"
    VERBATIM
  )

  # --- DXIL ---
  add_custom_command(
    OUTPUT "${_dxil}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<SHELL_PATH:${_outdir}>"
    COMMAND "${DXC_EXE}" ${_common_args}
            -Fo "$<SHELL_PATH:${_outdir}>/${_base}.dxil" "${_src}"
    DEPENDS "${_src}"
    BYPRODUCTS "$<SHELL_PATH:${_outdir}>/${_base}.dxil"
    COMMENT "DXC: ${_base}.hlsl -> ${_base}.dxil"
    VERBATIM
  )

  list(APPEND _all_outputs "${_spv}" "${_dxil}")
endforeach()

# Aggregate target
add_custom_target(shaders ALL DEPENDS ${_all_outputs})
set_target_properties(shaders PROPERTIES FOLDER "Shaders")
