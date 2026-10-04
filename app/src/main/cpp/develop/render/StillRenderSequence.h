#pragma once
#include "develop/render/RenderCommandSession.h"
#include "develop/render/RenderResources.h"
namespace rawrcam::develop::rendered {
RenderedStillCompletion executeStillRender(RenderResources& resources, RenderCommandSession& commands,
                                           const RenderDeviceContext& device, const RenderedStillContext& rendered,
                                           VkImage sourceImage, VkImageView sourceView, VkImage clipStateImage,
                                           VkImageView clipStateView, const std::string& filesDir,
                                           const RenderResources::Diagnostic& emit);
}  // namespace rawrcam::develop::rendered
