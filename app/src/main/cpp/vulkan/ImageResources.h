#pragma once
#include "rawr/vk/OwnedImage.h"

// App-side names for the shared native image helpers.
namespace rawrcam::vulkan {
using rawr::vk::OwnedImage;
using rawr::vk::createOwnedImage;
using rawr::vk::destroyOwnedImage;
using rawr::vk::recordImageCopy;
}  // namespace rawrcam::vulkan
