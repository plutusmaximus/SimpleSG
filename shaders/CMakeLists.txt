cmake_minimum_required(VERSION 3.31)
project(WGSLToSpirv LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------
set(WGSL_SOURCES
  "${CMAKE_CURRENT_LIST_DIR}/ColorFragmentShader.fs.wgsl"
  "${CMAKE_CURRENT_LIST_DIR}/ColorVertexShader.vs.wgsl"
  "${CMAKE_CURRENT_LIST_DIR}/FragmentShader.fs.wgsl"
  "${CMAKE_CURRENT_LIST_DIR}/VertexShader.vs.wgsl"
  "${CMAKE_CURRENT_LIST_DIR}/FullScreenTriangle.vs.wgsl"
  "${CMAKE_CURRENT_LIST_DIR}/FullScreenTriangle.fs.wgsl"
)

set(HLSL_SOURCES
  "${CMAKE_CURRENT_LIST_DIR}/FragmentShader.ps.hlsl"
  "${CMAKE_CURRENT_LIST_DIR}/FullScreenTriangle.ps.hlsl"
)

set(TINT_EXE "${CMAKE_BINARY_DIR}/DEBUG/tint.exe" CACHE FILEPATH "Tint compiler executable")
set(TINT_SPIRV_VERSION "1.3" CACHE STRING "SPIR-V version to target (e.g. 1.0, 1.3, 1.5)")
set(_out_dir "${CMAKE_BINARY_DIR}/shaders/Debug")

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
# Build rules
# ---------------------------------------------------------------------------
set(_spv_outputs)

foreach(_wgsl IN LISTS WGSL_SOURCES)
  get_filename_component(_name "${_wgsl}" NAME)
  string(REGEX REPLACE "\\.wgsl$" "" _base_name "${_name}")
  set(_spv_out "${_out_dir}/${_base_name}.spv")
  set(_wgsl_copy_out "${_out_dir}/${_name}")

  add_custom_command(
    OUTPUT "${_spv_out}" "${_wgsl_copy_out}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_out_dir}"
    COMMAND "${TINT_EXE}" "${_wgsl}" -o "${_spv_out}" --spirv-version "${TINT_SPIRV_VERSION}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_wgsl}" "${_wgsl_copy_out}"
    DEPENDS "${_wgsl}"
    COMMENT "Tint: ${_base_name}.wgsl -> ${_base_name}.spv"
    VERBATIM
  )

  list(APPEND _spv_outputs "${_spv_out}")
  list(APPEND _spv_outputs "${_wgsl_copy_out}")
endforeach()

foreach(_hlsl IN LISTS HLSL_SOURCES)
  get_filename_component(_name "${_hlsl}" NAME)
  string(REGEX REPLACE "\\.hlsl$" "" _base_name "${_name}")
  set(_spv_out "${_out_dir}/${_base_name}.spv")

  add_custom_command(
    OUTPUT "${_spv_out}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_out_dir}"
    COMMAND "${DXC_EXE}" -T ps_6_7 -E main -enable-16bit-types -spirv -fvk-use-dx-layout -Fo "${_spv_out}" "${_hlsl}"
    DEPENDS "${_hlsl}"
    COMMENT "Tint: ${_base_name}.hlsl -> ${_base_name}.spv"
    VERBATIM
  )

  list(APPEND _spv_outputs "${_spv_out}")
endforeach()

add_custom_target(shaders ALL DEPENDS ${_spv_outputs})
set_target_properties(shaders PROPERTIES FOLDER "Shaders")
