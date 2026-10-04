#pragma once
#include "develop/DevelopSettings.h"
#include "develop/render/StillImageRenderer.h"
namespace rawrcam::develop {
void applyDevelopSettings(rendered::RenderedStillContext& context, const DevelopSettings& settings);
}
