cmake_minimum_required(VERSION 3.22)
project(rawr_shader_common LANGUAGES NONE)

# Shared RAW conventions: GLSL includes under shaders/ and the host-side
# layout structs that feed them under include/.
add_library(rawr_shader_common INTERFACE)
add_library(rawr::shader_common ALIAS rawr_shader_common)
target_include_directories(rawr_shader_common INTERFACE include)
set(RAWR_SHADER_COMMON_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders" CACHE INTERNAL "")
