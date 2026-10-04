cmake_minimum_required(VERSION 3.22)
project(raw_sharpness VERSION 1.0.0 LANGUAGES CXX)

option(RAW_SHARPNESS_BUILD_VULKAN "Build Vulkan scoring backend" ON)
option(RAW_SHARPNESS_BUILD_TESTS "Build validation executables" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

function(raw_sharpness_enable_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /WX)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
endfunction()

# Host reference + selection policy. No Vulkan dependency; always built so
# offline tools can link it without the GPU backend.
add_library(raw_sharpness_reference STATIC
  reference/raw_sharpness_cpu.cpp
)
add_library(raw_sharpness::reference ALIAS raw_sharpness_reference)
target_include_directories(raw_sharpness_reference PUBLIC include)
raw_sharpness_enable_warnings(raw_sharpness_reference)

if(RAW_SHARPNESS_BUILD_VULKAN)
  if(ANDROID)
    find_library(RAW_SHARPNESS_VULKAN_LIB vulkan REQUIRED)
    set(RAW_SHARPNESS_VULKAN_FOUND TRUE)
  else()
    find_package(Vulkan QUIET)
    if(Vulkan_FOUND)
      set(RAW_SHARPNESS_VULKAN_LIB Vulkan::Vulkan)
      set(RAW_SHARPNESS_VULKAN_FOUND TRUE)
    endif()
  endif()
  if(RAW_SHARPNESS_VULKAN_FOUND)
    find_program(GLSLANG_VALIDATOR glslangValidator)
    find_program(GLSLC glslc)
    if(NOT GLSLANG_VALIDATOR AND NOT GLSLC)
      message(FATAL_ERROR "No GLSL-to-SPIR-V compiler found (glslangValidator or glslc)")
    endif()
    set(RAW_SHARPNESS_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
    file(MAKE_DIRECTORY ${RAW_SHARPNESS_GENERATED_DIR})
    if(GLSLANG_VALIDATOR)
      add_custom_command(
        OUTPUT ${RAW_SHARPNESS_GENERATED_DIR}/score_green_laplacian.spv
        COMMAND ${GLSLANG_VALIDATOR} -V --target-env vulkan1.1
                ${CMAKE_CURRENT_SOURCE_DIR}/shaders/score_green_laplacian.comp
                -o ${RAW_SHARPNESS_GENERATED_DIR}/score_green_laplacian.spv
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/score_green_laplacian.comp
        VERBATIM)
    else()
      add_custom_command(
        OUTPUT ${RAW_SHARPNESS_GENERATED_DIR}/score_green_laplacian.spv
        COMMAND ${GLSLC} --target-env=vulkan1.1 -fshader-stage=compute
                ${CMAKE_CURRENT_SOURCE_DIR}/shaders/score_green_laplacian.comp
                -o ${RAW_SHARPNESS_GENERATED_DIR}/score_green_laplacian.spv
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/score_green_laplacian.comp
        VERBATIM)
    endif()
    add_custom_command(
      OUTPUT ${RAW_SHARPNESS_GENERATED_DIR}/raw_sharpness_shaders_spv.hpp
      COMMAND ${CMAKE_COMMAND} -DINPUT=${RAW_SHARPNESS_GENERATED_DIR}/score_green_laplacian.spv
              -DOUTPUT=${RAW_SHARPNESS_GENERATED_DIR}/raw_sharpness_shaders_spv.hpp
              -DSYMBOL=raw_sharpness_score_spv -P ${CMAKE_CURRENT_SOURCE_DIR}/../../vk_common/cmake/EmbedBinary.cmake
      DEPENDS ${RAW_SHARPNESS_GENERATED_DIR}/score_green_laplacian.spv
              ${CMAKE_CURRENT_SOURCE_DIR}/../../vk_common/cmake/EmbedBinary.cmake
      VERBATIM)
    add_custom_target(raw_sharpness_shaders
      DEPENDS ${RAW_SHARPNESS_GENERATED_DIR}/raw_sharpness_shaders_spv.hpp)

    add_library(raw_sharpness STATIC src/raw_sharpness_vulkan.cpp)
    add_library(raw_sharpness::raw_sharpness ALIAS raw_sharpness)
    add_dependencies(raw_sharpness raw_sharpness_shaders)
    target_include_directories(raw_sharpness PUBLIC include PRIVATE src ${RAW_SHARPNESS_GENERATED_DIR})
    # The selection policy (selectSharpest) ships with the reference TU, and
    # consumers of the scoring backend use both; link it transitively.
    target_link_libraries(raw_sharpness PUBLIC raw_sharpness_reference ${RAW_SHARPNESS_VULKAN_LIB})
    raw_sharpness_enable_warnings(raw_sharpness)

    if(RAW_SHARPNESS_BUILD_TESTS)
      add_executable(raw_sharpness_vulkan_validation
        tests/raw_sharpness_vulkan_validation.cpp)
      target_include_directories(raw_sharpness_vulkan_validation PRIVATE include tests)
      target_link_libraries(raw_sharpness_vulkan_validation
        PRIVATE raw_sharpness raw_sharpness_reference ${RAW_SHARPNESS_VULKAN_LIB})
      raw_sharpness_enable_warnings(raw_sharpness_vulkan_validation)
      add_test(NAME raw_sharpness_vulkan_validation COMMAND raw_sharpness_vulkan_validation)
    endif()
  else()
    message(WARNING "Vulkan not found; skipping Vulkan backend")
  endif()
endif()

if(RAW_SHARPNESS_BUILD_TESTS)
  enable_testing()
  add_executable(raw_sharpness_cpu_validation
    tests/raw_sharpness_cpu_validation.cpp)
  target_include_directories(raw_sharpness_cpu_validation PRIVATE include)
  target_link_libraries(raw_sharpness_cpu_validation PRIVATE raw_sharpness_reference)
  raw_sharpness_enable_warnings(raw_sharpness_cpu_validation)
  add_test(NAME raw_sharpness_cpu_validation COMMAND raw_sharpness_cpu_validation)
endif()
