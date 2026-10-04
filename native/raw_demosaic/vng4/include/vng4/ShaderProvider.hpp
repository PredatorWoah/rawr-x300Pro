#pragma once
#include <cstdint>
#include <functional>
#include <string_view>
#include <vector>
namespace vng4 { using ShaderProvider=std::function<std::vector<uint32_t>(std::string_view)>; }
