cmake_minimum_required(VERSION 3.20)
project(TonemapEngine VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Production builds are warning-clean by contract. Keep these flags PRIVATE so
# TonemapEngine does not impose warning policy on applications that consume it.
option(TONEMAP_WARNINGS_AS_ERRORS "Treat TonemapEngine project warnings as errors" ON)
option(TONEMAP_BUILD_TESTS "Build TonemapEngine validation executables" ON)
function(tonemap_enable_project_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
        if(TONEMAP_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
        if(TONEMAP_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

add_library(tonemap_math INTERFACE)
target_include_directories(tonemap_math INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
add_library(TonemapEngine::Math ALIAS tonemap_math)
set_target_properties(tonemap_math PROPERTIES EXPORT_NAME Math)

if(TONEMAP_BUILD_TESTS)
    add_executable(tonemap_math_test tests/test_math.cpp)
    add_executable(tonemap_lut_core_test tests/test_lut_core.cpp)
    target_sources(tonemap_lut_core_test PRIVATE src/color/ColorSpace.cpp src/lut/Lut3D.cpp src/lut/LutChain.cpp src/lut/LutGpuPayload.cpp)
    target_include_directories(tonemap_lut_core_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    tonemap_enable_project_warnings(tonemap_lut_core_test)
    target_sources(tonemap_math_test PRIVATE src/color/ColorSpace.cpp src/lut/Lut3D.cpp src/lut/LutChain.cpp src/lut/LutGpuPayload.cpp)
    target_include_directories(tonemap_math_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(tonemap_math_test PRIVATE tonemap_math)
    tonemap_enable_project_warnings(tonemap_math_test)
endif()

option(TONEMAP_BUILD_VULKAN "Build Vulkan backend and GPU validation runner" ON)
if(TONEMAP_BUILD_VULKAN)
    if(ANDROID)
        add_library(tonemap_engine STATIC
            src/TonemapEngine.cpp
            src/color/ColorSpace.cpp
            src/lut/Lut3D.cpp
            src/lut/LutChain.cpp
            src/lut/LutGpuPayload.cpp)
        target_include_directories(tonemap_engine PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
        target_link_libraries(tonemap_engine PUBLIC vulkan)
        tonemap_enable_project_warnings(tonemap_engine)
    else()
        find_package(Vulkan QUIET)
        if(Vulkan_FOUND)
            add_library(tonemap_engine STATIC
            src/TonemapEngine.cpp
            src/color/ColorSpace.cpp
            src/lut/Lut3D.cpp
            src/lut/LutChain.cpp
            src/lut/LutGpuPayload.cpp)
            target_include_directories(tonemap_engine PUBLIC
                $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
                $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
            target_link_libraries(tonemap_engine PUBLIC Vulkan::Vulkan)
            tonemap_enable_project_warnings(tonemap_engine)
        else()
            message(STATUS "Vulkan SDK not found: Vulkan backend will not be built")
        endif()
    endif()

    if(TARGET tonemap_engine)
        add_library(TonemapEngine::Vulkan ALIAS tonemap_engine)
        set_target_properties(tonemap_engine PROPERTIES OUTPUT_NAME tonemap_engine EXPORT_NAME Vulkan)
        if(TONEMAP_BUILD_TESTS)
            add_executable(tonemap_vulkan_test tests/vulkan_runner.cpp)
            target_link_libraries(tonemap_vulkan_test PRIVATE tonemap_engine)
            tonemap_enable_project_warnings(tonemap_vulkan_test)
        endif()
    endif()
endif()

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(TARGETS tonemap_math
    EXPORT TonemapEngineTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
if(TARGET tonemap_engine)
    install(TARGETS tonemap_engine
        EXPORT TonemapEngineTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
endif()
install(EXPORT TonemapEngineTargets
    FILE TonemapEngineTargets.cmake
    NAMESPACE TonemapEngine::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/TonemapEngine)

configure_package_config_file(
    cmake/TonemapEngineConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/TonemapEngineConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/TonemapEngine)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/TonemapEngineConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/TonemapEngineConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/TonemapEngineConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/TonemapEngine)
