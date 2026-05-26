cmake_minimum_required(VERSION 3.31)
project(WGSLToSpirv LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------
set(WGSL_SOURCES
  "${CMAKE_CURRENT_LIST_DIR}/ColorShader.wgsl"
  "${CMAKE_CURRENT_LIST_DIR}/PresentShader.wgsl"
  "${CMAKE_CURRENT_LIST_DIR}/TransformShader.wgsl"
)

set(TINT_EXE "$<TARGET_FILE:tint>")

set(_out_dir "${CMAKE_BINARY_DIR}/$<CONFIG>/shaders")

# ---------------------------------------------------------------------------
# Build rules
# ---------------------------------------------------------------------------
set(_shader_outputs)

foreach(_wgsl IN LISTS WGSL_SOURCES)
  get_filename_component(_name "${_wgsl}" NAME)
  set(_wgsl_copy_out "${_out_dir}/${_name}")
  set(_wgsl_validate_out "${_out_dir}/${_name}.validated.wgsl")

  add_custom_command(
    OUTPUT "${_wgsl_copy_out}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_out_dir}"
    # Validate shaders.
    COMMAND "${TINT_EXE}" --validate true --format wgsl --output-name "${_wgsl_validate_out}" "${_wgsl}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_wgsl}" "${_wgsl_copy_out}"
    DEPENDS tint "${_wgsl}"
    VERBATIM
  )

  list(APPEND _shader_outputs "${_wgsl_copy_out}")
endforeach()

add_custom_target(shaders ALL DEPENDS ${_shader_outputs})
add_dependencies(shaders tint)
set_target_properties(shaders PROPERTIES FOLDER "Shaders")
