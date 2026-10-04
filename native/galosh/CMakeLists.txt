cmake_minimum_required(VERSION 3.22)
project(galosh VERSION 0.1.0 LANGUAGES CXX)

# GALOSH native module (P0: skeleton + vendored shaders + offline parity).
# GPU-only by design: the parity oracles are the upstream CPU exes
# (tmp/GALOSH, Apache-2.0) driven by tools/verify_galosh_parity.py.
# record() implementations land in P1; P0 only guarantees the public
# API compiles and the shader set builds to SPIR-V.

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(ANDROID)
  find_library(VULKAN_LIB vulkan REQUIRED)
else()
  find_package(Vulkan REQUIRED)
  set(VULKAN_LIB Vulkan::Vulkan)
endif()

add_library(galosh STATIC src/GaloshRawPipeline.cpp src/GaloshYuvPipeline.cpp)
add_library(galosh::galosh ALIAS galosh)
target_include_directories(galosh PUBLIC include)
target_link_libraries(galosh PUBLIC ${VULKAN_LIB})
target_compile_options(galosh PRIVATE -Wall -Wextra -Wpedantic)

option(GALOSH_BUILD_TESTS "Build offline validation executable" ON)
if(GALOSH_BUILD_TESTS)
  # MoltenVK construction smoke: builds every compute pipeline, catches
  # SPIR-V/DSL/layout errors. SPIR-V loads from files at runtime.
  add_executable(galosh_smoke validation/galosh_smoke.cpp)
  target_include_directories(galosh_smoke PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/validation)
  target_link_libraries(galosh_smoke PRIVATE galosh::galosh ${VULKAN_LIB})
  target_compile_options(galosh_smoke PRIVATE -Wall -Wextra -Wpedantic)
  # MoltenVK data runner: Bayer in/out through the native port (parity vs
  # CPU oracle in tools/validate_native_raw.py).
  add_executable(galosh_validate validation/galosh_validate.cpp)
  target_include_directories(galosh_validate PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/validation)
  target_link_libraries(galosh_validate PRIVATE galosh::galosh ${VULKAN_LIB})
  target_compile_options(galosh_validate PRIVATE -Wall -Wextra -Wpedantic)
  # MoltenVK data runner: linear RGB in/out through the native YUV port
  # (gates in tools/validate_native_yuv.py; no upstream GPU Q reference).
  add_executable(galosh_validate_yuv validation/galosh_validate_yuv.cpp)
  target_include_directories(galosh_validate_yuv PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/validation)
  target_link_libraries(galosh_validate_yuv PRIVATE galosh::galosh ${VULKAN_LIB})
  target_compile_options(galosh_validate_yuv PRIVATE -Wall -Wextra -Wpedantic)
endif()
