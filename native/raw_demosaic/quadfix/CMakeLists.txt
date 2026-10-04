cmake_minimum_required(VERSION 3.22)
project(quadfix_filter VERSION 0.1.0 LANGUAGES CXX)
include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(ANDROID)
  set(VULKAN_LIB vulkan)
else()
  find_package(Vulkan REQUIRED)
  set(VULKAN_LIB Vulkan::Vulkan)
endif()

add_library(quadfix STATIC src/Pipeline.cpp)
add_library(quadfix::quadfix ALIAS quadfix)
target_include_directories(quadfix PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_features(quadfix PUBLIC cxx_std_17)
target_compile_options(quadfix PRIVATE $<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:-O3>)
target_link_libraries(quadfix PUBLIC ${VULKAN_LIB})

option(QUADFIX_ENABLE_WARNINGS "Enable strict compiler warnings" ON)
if(QUADFIX_ENABLE_WARNINGS)
  target_compile_options(quadfix PRIVATE $<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:-Wall;-Wextra;-Wpedantic;-Werror>)
endif()

install(TARGETS quadfix EXPORT quadfixTargets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(DIRECTORY shaders/ DESTINATION ${CMAKE_INSTALL_DATADIR}/quadfix/shaders FILES_MATCHING PATTERN "*.comp" PATTERN "*.glsl")
install(EXPORT quadfixTargets NAMESPACE quadfix:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/quadfix)
