// Text adapter around the production metadata-only color calibration solver.
#include "color/ColorCalibration.h"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
int main() {
    using namespace rawrcam;
    auto camera = std::make_shared<metadata::CameraContextMetadata>();
    metadata::FrameMetadataSnapshot frame{};
    frame.cameraContext = camera;
    std::cin >> camera->color.referenceIlluminant1 >> camera->color.referenceIlluminant2;
    for (auto* m : {&camera->color.colorTransform1, &camera->color.colorTransform2,
                   &camera->color.calibrationTransform1, &camera->color.calibrationTransform2,
                   &camera->color.forwardMatrix1, &camera->color.forwardMatrix2}) {
        for (float& v : m->rowMajor) std::cin >> v;
        m->valid = true;
    }
    for (float& v : frame.neutralColorPoint) std::cin >> v;
    frame.hasNeutralColorPoint = true;
    for (float& v : frame.colorCorrectionGainsRggb) std::cin >> v;
    if (!std::cin) throw std::runtime_error("Incomplete calibration metadata");
    const auto result = color::deriveFrameColorTransform(frame, color::PreviewColorMode::Auto);
    std::cout << std::setprecision(9);
    for (size_t i=0; i<9; ++i) std::cout << (i ? "," : "") << result.cameraToLinearSrgbRowMajor[i];
    std::cout << '\n' << result.source << '\n';
}
