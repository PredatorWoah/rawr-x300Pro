// galosh_smoke: MoltenVK construction smoke for the native port.
// Loads every SHADER_LIST module, constructs GaloshRawPipeline (builds all
// compute pipelines + layouts) and exits. Catches SPV/DSL/layout errors
// before any app wiring. Numerical parity runs in validation/galosh_validate
// (native port vs CPU oracle) and tools/verify_galosh_parity.py (ref exes).
//
// Usage: galosh_smoke --spv-dir DIR

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "galosh/GaloshRawPipeline.hpp"
#include "galosh_vk_test.h"

int main(int argc, char** argv) {
    try {
        std::string spvDir;
        for (int i = 1; i + 1 < argc; ++i)
            if (std::string(argv[i]) == "--spv-dir") spvDir = argv[i + 1];
        if (spvDir.empty()) throw std::runtime_error("usage: galosh_smoke --spv-dir DIR");

        galoshtest::Ctx ctx = galoshtest::ctx();
        std::vector<std::vector<char>> keep;
        galosh::GaloshShaderMap shaders = galoshtest::spvMap(spvDir, keep);
        {
            galosh::GaloshRawPipeline pipe(ctx.pd, ctx.dev, ctx.qf, shaders);
            const uint64_t bytes = pipe.scratchBytes(4080, 3064);
            std::cout << "SMOKE: PASS pipelines built, scratch4080x3064=" << bytes << "B\n";
        }
        galoshtest::delCtx(ctx);
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "SMOKE: FAIL " << e.what() << "\n";
        return 1;
    }
}
