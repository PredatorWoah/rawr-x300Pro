#pragma once
#include "develop/render/RenderCommandSession.h"
#include "develop/render/RenderResources.h"
namespace rawrcam::develop::rendered {
struct FilmRenderOutcome {
    bool rendered = false;
    bool usedGlow = false;
    bool memoryRejected = false;
};
FilmRenderOutcome renderFilmOrTone(RenderResources& resources, RenderCommandSession& commands,
                                   const RenderedStillContext& rendered, VkImageView sourceView,
                                   const std::string& filesDir, const RenderResources::Diagnostic& emit);
}  // namespace rawrcam::develop::rendered
