# zsl_codec is intentionally app-agnostic. The parent supplies generated shader headers.
add_library(zsl_codec STATIC src/ZslCodec.cpp src/ZslDecoder.cpp)
add_library(rawr::zsl_codec ALIAS zsl_codec)
target_include_directories(zsl_codec PUBLIC include PRIVATE ${ZSL_CODEC_GENERATED_INCLUDE_DIR})
target_compile_features(zsl_codec PUBLIC cxx_std_17)
target_compile_options(zsl_codec PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_link_libraries(zsl_codec PUBLIC ${VULKAN_LIB})
