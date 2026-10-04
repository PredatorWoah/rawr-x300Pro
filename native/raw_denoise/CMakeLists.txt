cmake_minimum_required(VERSION 3.22)
project(raw_denoise VERSION 1.0.0 LANGUAGES CXX)

# GPU-only module: the parity oracle is Python
# (native/raw_denoise/tools/denoiseprofile_oracle.py, float32-pinned).
# No C++ CPU oracle by design (see module README).

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(RAW_DENOISE_BUILD_VULKAN "Build Vulkan production backend" ON)
option(RAW_DENOISE_BUILD_TESTS "Build offline validation executable" ON)

if(ANDROID)
  find_library(VULKAN_LIB vulkan REQUIRED)
  set(VULKAN_FOUND TRUE)
else()
  find_package(Vulkan REQUIRED)
  set(VULKAN_LIB Vulkan::Vulkan)
endif()

if(RAW_DENOISE_BUILD_VULKAN)
  # Production tiled image pipeline (links into the app still path).
  add_library(raw_denoise STATIC src/DenoisePipeline.cpp)
  add_library(raw_denoise::raw_denoise ALIAS raw_denoise)
  target_include_directories(raw_denoise PUBLIC include)
  target_link_libraries(raw_denoise PUBLIC ${VULKAN_LIB})
  target_compile_options(raw_denoise PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(RAW_DENOISE_BUILD_TESTS)
  # Production pipeline runner: timing on device, parity dumps on host
  # (tools/verify_denoise_parity.py). SPIR-V loads from files at runtime.
  add_executable(denoise_bench validation/denoise_bench.cpp)
  target_include_directories(denoise_bench PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${Vulkan_INCLUDE_DIRS})
  target_link_libraries(denoise_bench PRIVATE raw_denoise ${VULKAN_LIB})
  target_compile_options(denoise_bench PRIVATE -Wall -Wextra -Wpedantic)
endif()
