cmake_minimum_required(VERSION 3.20)
project(monitoring_overlays VERSION 1.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
include(CTest)

option(MONITORING_OVERLAYS_STRICT_WARNINGS "Treat project compiler warnings as errors" ON)
option(MONITORING_OVERLAYS_BUILD_TESTS "Build monitoring validation/sample executables" ON)
option(MONITORING_OVERLAYS_BUILD_ANDROID_MILESTONE "Build Android benchmark/stability milestone executable" ON)
function(monitoring_overlays_strict target)
  if(NOT MONITORING_OVERLAYS_STRICT_WARNINGS)
    return()
  endif()
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /WX)
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(${target} PRIVATE
      -Wall
      -Wextra
      -Wpedantic
      -Werror
      -Wno-missing-field-initializers
    )
  endif()
endfunction()

add_library(monitoring_overlays_cpu STATIC src/cpu_reference.cpp)
target_include_directories(monitoring_overlays_cpu PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_library(monitoring_overlays::cpu ALIAS monitoring_overlays_cpu)

if(MONITORING_OVERLAYS_BUILD_TESTS)
  add_executable(monitoring_overlays_validation tests/cpu_validation.cpp)
  target_link_libraries(monitoring_overlays_validation PRIVATE monitoring_overlays_cpu)
  add_test(NAME monitoring_overlays_cpu_validation COMMAND monitoring_overlays_validation)
endif()

option(MONITORING_OVERLAYS_BUILD_VULKAN "Build Vulkan production backend" ON)
if(MONITORING_OVERLAYS_BUILD_VULKAN)
  if(ANDROID)
    add_library(monitoring_overlays STATIC src/monitoring_overlays.cpp)
    target_link_libraries(monitoring_overlays PUBLIC vulkan)
    set(MONITORING_OVERLAYS_VULKAN_LINK vulkan)
  else()
    find_package(Vulkan QUIET)
    if(Vulkan_FOUND)
      add_library(monitoring_overlays STATIC src/monitoring_overlays.cpp)
      target_link_libraries(monitoring_overlays PUBLIC Vulkan::Vulkan)
      set(MONITORING_OVERLAYS_VULKAN_LINK Vulkan::Vulkan)
    else()
      message(STATUS "Vulkan SDK not found: production Vulkan backend skipped")
    endif()
  endif()
  if(TARGET monitoring_overlays)
    target_include_directories(monitoring_overlays PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
    # FalseColorParams::defaultPreset() is defined in cpu_reference.cpp and is
    # part of the production API defaults, so both Android and host Vulkan
    # targets require this private implementation dependency.
    target_link_libraries(monitoring_overlays PRIVATE monitoring_overlays_cpu)
    add_library(monitoring_overlays::vulkan ALIAS monitoring_overlays)
  endif()
endif()

option(MONITORING_OVERLAYS_COMPILE_SHADERS "Compile GLSL shaders reproducibly" OFF)
if(MONITORING_OVERLAYS_COMPILE_SHADERS)
  include(cmake/CompileShaders.cmake)
endif()

if(TARGET monitoring_overlays AND MONITORING_OVERLAYS_BUILD_TESTS)
  add_library(monitoring_overlays_integration_example OBJECT examples/record_combined_example.cpp)
  target_link_libraries(monitoring_overlays_integration_example PRIVATE monitoring_overlays)
  target_compile_features(monitoring_overlays_integration_example PRIVATE cxx_std_17)

  add_executable(monitoring_overlays_abi_validation tests/abi_validation.cpp)
  target_link_libraries(monitoring_overlays_abi_validation PRIVATE monitoring_overlays ${MONITORING_OVERLAYS_VULKAN_LINK})
  target_compile_features(monitoring_overlays_abi_validation PRIVATE cxx_std_17)

  add_executable(monitoring_overlays_visual_samples_vulkan tests/visual_samples_vulkan.cpp)
  target_link_libraries(monitoring_overlays_visual_samples_vulkan PRIVATE monitoring_overlays ${MONITORING_OVERLAYS_VULKAN_LINK})
  target_compile_features(monitoring_overlays_visual_samples_vulkan PRIVATE cxx_std_17)

  add_executable(monitoring_overlays_raw_state_vulkan_validation tests/raw_state_vulkan_validation.cpp)
  target_link_libraries(monitoring_overlays_raw_state_vulkan_validation PRIVATE monitoring_overlays monitoring_overlays_cpu ${MONITORING_OVERLAYS_VULKAN_LINK})
  target_compile_features(monitoring_overlays_raw_state_vulkan_validation PRIVATE cxx_std_17)

  add_executable(monitoring_overlays_remaining_modes_vulkan_validation tests/remaining_modes_vulkan_validation.cpp)
  target_link_libraries(monitoring_overlays_remaining_modes_vulkan_validation PRIVATE monitoring_overlays monitoring_overlays_cpu ${MONITORING_OVERLAYS_VULKAN_LINK})
  target_compile_features(monitoring_overlays_remaining_modes_vulkan_validation PRIVATE cxx_std_17)

  add_executable(monitoring_overlays_tonemap_shadow_vulkan_validation tests/tonemap_shadow_vulkan_validation.cpp)
  target_link_libraries(monitoring_overlays_tonemap_shadow_vulkan_validation PRIVATE monitoring_overlays monitoring_overlays_cpu ${MONITORING_OVERLAYS_VULKAN_LINK})
  target_compile_features(monitoring_overlays_tonemap_shadow_vulkan_validation PRIVATE cxx_std_17)

  if(MONITORING_OVERLAYS_COMPILE_SHADERS)
    add_dependencies(monitoring_overlays_visual_samples_vulkan monitoring_overlays_shaders)
    add_dependencies(monitoring_overlays_raw_state_vulkan_validation monitoring_overlays_shaders)
    add_dependencies(monitoring_overlays_remaining_modes_vulkan_validation monitoring_overlays_shaders)
  endif()
endif()

if(ANDROID AND TARGET monitoring_overlays AND MONITORING_OVERLAYS_BUILD_ANDROID_MILESTONE)
  add_executable(monitoring_overlays_android_benchmark_stability benchmark/android_benchmark_stability.cpp)
  target_link_libraries(monitoring_overlays_android_benchmark_stability PRIVATE monitoring_overlays monitoring_overlays_cpu vulkan)
  target_compile_features(monitoring_overlays_android_benchmark_stability PRIVATE cxx_std_17)
  if(MONITORING_OVERLAYS_COMPILE_SHADERS)
    add_dependencies(monitoring_overlays_android_benchmark_stability monitoring_overlays_shaders)
  endif()
endif()

# Project-owned targets compile warning-clean. External Vulkan/NDK headers and CMake
# toolchain diagnostics are not converted into project-source warnings.
foreach(_target IN ITEMS
  monitoring_overlays_cpu
  monitoring_overlays_validation
  monitoring_overlays
  monitoring_overlays_integration_example
  monitoring_overlays_abi_validation
  monitoring_overlays_visual_samples_vulkan
  monitoring_overlays_raw_state_vulkan_validation
  monitoring_overlays_remaining_modes_vulkan_validation
  monitoring_overlays_tonemap_shadow_vulkan_validation
  monitoring_overlays_android_benchmark_stability
)
  if(TARGET ${_target})
    monitoring_overlays_strict(${_target})
  endif()
endforeach()
