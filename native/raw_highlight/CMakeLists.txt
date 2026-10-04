cmake_minimum_required(VERSION 3.22)
project(raw_highlight LANGUAGES CXX)

# Highlight reconstruction: shaders for both methods (colour propagation and
# Inpaint Opposed) under shaders/, plus their host code (GuideChain,
# ColoroppProcessor). Consumers compile the shaders with this directory on the
# GLSL include path and pass the SPIR-V in.
add_library(raw_highlight_shader_contract INTERFACE)
add_library(raw_highlight::shader_contract ALIAS raw_highlight_shader_contract)
set_property(TARGET raw_highlight_shader_contract PROPERTY
    INTERFACE_RAW_HIGHLIGHT_SHADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders")

foreach(DEP vk_common shader_common)
  if(NOT TARGET rawr::${DEP})
    add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../${DEP} ${CMAKE_CURRENT_BINARY_DIR}/${DEP})
  endif()
endforeach()

add_library(raw_highlight STATIC src/GuideChain.cpp src/ColoroppProcessor.cpp)
add_library(raw_highlight::raw_highlight ALIAS raw_highlight)
target_compile_features(raw_highlight PUBLIC cxx_std_17)
target_include_directories(raw_highlight PUBLIC include)
target_link_libraries(raw_highlight PUBLIC rawr::vk_common rawr::shader_common)
target_compile_options(raw_highlight PRIVATE $<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:-Wall;-Wextra;-Wpedantic>)
