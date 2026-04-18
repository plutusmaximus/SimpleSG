cmake_minimum_required(VERSION 3.31)
project(WGSLToSpirv LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------
set(WGSL_SOURCES
  "${CMAKE_CURRENT_LIST_DIR}/ColorShader.wgsl"
  "${CMAKE_CURRENT_LIST_DIR}/ResolveShader.wgsl"
  "${CMAKE_CURRENT_LIST_DIR}/TransformShader.wgsl"
)

set(TINT_EXE "${CMAKE_BINARY_DIR}/thirdparty/dawn/tint.exe" CACHE FILEPATH "Tint compiler executable")
set(_out_dir "${CMAKE_BINARY_DIR}/shaders")

# ---------------------------------------------------------------------------
# Build rules
# ---------------------------------------------------------------------------
set(_shader_outputs)

foreach(_wgsl IN LISTS WGSL_SOURCES)
  get_filename_component(_name "${_wgsl}" NAME)
  set(_wgsl_copy_out "${_out_dir}/${_name}")

  add_custom_command(
    OUTPUT "${_wgsl_copy_out}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_out_dir}"
    COMMAND "${TINT_EXE}" --parse-only true "${_wgsl}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_wgsl}" "${_wgsl_copy_out}"
    DEPENDS "${_wgsl}"
    VERBATIM
  )

  list(APPEND _shader_outputs "${_wgsl_copy_out}")
endforeach()

add_custom_target(shaders ALL DEPENDS ${_shader_outputs})
set_target_properties(shaders PROPERTIES FOLDER "Shaders")
