cmake_minimum_required(VERSION 3.31)
project(WGSLToSpirv LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------
set(WGSL_SOURCES
  "${CMAKE_CURRENT_LIST_DIR}/ColorShader.wgsl"
  "${CMAKE_CURRENT_LIST_DIR}/CompositorShader.wgsl"
  "${CMAKE_CURRENT_LIST_DIR}/TransformShader.wgsl"
)

if(NOT EMSCRIPTEN)
  if(TARGET tint_cmd_tint_cmd)
    set(TINT_TARGET tint_cmd_tint_cmd)
  elseif(TARGET tint)
    set(TINT_TARGET tint)
  else()
    message(FATAL_ERROR "Expected Dawn Tint target 'tint_cmd_tint_cmd' or 'tint' to exist")
  endif()

  set(TINT_EXE "$<TARGET_FILE:${TINT_TARGET}>")
endif()

set(_out_dir "${CMAKE_BINARY_DIR}/$<CONFIG>/shaders")

# ---------------------------------------------------------------------------
# Build rules
# ---------------------------------------------------------------------------
set(_shader_outputs)

foreach(_wgsl IN LISTS WGSL_SOURCES)
  get_filename_component(_name "${_wgsl}" NAME)
  set(_wgsl_copy_out "${_out_dir}/${_name}")
  set(_wgsl_validate_out "${_out_dir}/${_name}.validated.wgsl")

  if(EMSCRIPTEN)
    add_custom_command(
      OUTPUT "${_wgsl_copy_out}"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${_out_dir}"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${_wgsl}" "${_wgsl_copy_out}"
      DEPENDS "${_wgsl}"
      VERBATIM
    )
  else()
    add_custom_command(
      OUTPUT "${_wgsl_copy_out}"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${_out_dir}"
      COMMAND "${TINT_EXE}"
              --validate true
              --format wgsl
              --output-name "${_wgsl_validate_out}"
              "${_wgsl}"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${_wgsl}" "${_wgsl_copy_out}"
      DEPENDS ${TINT_TARGET} "${_wgsl}"
      VERBATIM
    )
  endif()

  list(APPEND _shader_outputs "${_wgsl_copy_out}")
endforeach()

add_custom_target(shaders ALL DEPENDS ${_shader_outputs})
if(NOT EMSCRIPTEN)
  add_dependencies(shaders ${TINT_TARGET})
endif()
set_target_properties(shaders PROPERTIES FOLDER "Shaders")
