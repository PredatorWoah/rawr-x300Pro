cmake_minimum_required(VERSION 3.22)
project(rawr_multiframe LANGUAGES CXX)

# Multiframe burst stack. alignment/ and merge_wronski/ are shader + header
# packages compiled by pipeline/; output/ projects the merge back to CFA and
# prepares the JPEG path; sharpness/ scores burst frames for reference
# selection. Package options (RAW_GPU_PIPELINE_BUILD_TESTS,
# RAW_SHARPNESS_BUILD_VULKAN, RAW_SHARPNESS_BUILD_TESTS) pass through.
enable_testing()
add_subdirectory(pipeline)
add_subdirectory(output)
add_subdirectory(sharpness)
