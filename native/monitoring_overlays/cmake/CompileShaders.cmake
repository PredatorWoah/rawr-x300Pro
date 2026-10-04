find_program(MONITORING_GLSLANG_VALIDATOR glslangValidator)
if(NOT MONITORING_GLSLANG_VALIDATOR)
  message(FATAL_ERROR "glslangValidator is required when MONITORING_OVERLAYS_COMPILE_SHADERS=ON")
endif()
set(MONITORING_SHADER_OUTPUTS)
foreach(name raw_state_overlay focus_peaking false_color tonemap_shadow combined_overlay)
  set(src "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${name}.comp")
  set(dst "${CMAKE_CURRENT_BINARY_DIR}/shaders/${name}.spv")
  add_custom_command(OUTPUT "${dst}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders"
    COMMAND "${MONITORING_GLSLANG_VALIDATOR}" -V --target-env vulkan1.1 -o "${dst}" "${src}"
    DEPENDS "${src}"
    VERBATIM)
  list(APPEND MONITORING_SHADER_OUTPUTS "${dst}")
endforeach()
add_custom_target(monitoring_overlays_shaders ALL DEPENDS ${MONITORING_SHADER_OUTPUTS})
