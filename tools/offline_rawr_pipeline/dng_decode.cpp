// Host entry point to the app's production DngSource decoder and DNG opcodes.
#include "renderer/DngSource.h"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void save(const std::filesystem::path& path, const std::vector<float>& values) {
  std::ofstream file(path,std::ios::binary|std::ios::trunc);
  file.write(reinterpret_cast<const char*>(values.data()),std::streamsize(values.size()*sizeof(float)));
  if (!file) throw std::runtime_error("Cannot save " + path.string());
}
}
int main(int argc,char** argv) {
  try {
    if (argc!=3) throw std::runtime_error("Usage: rawr_dng_decode INPUT.dng OUTPUT_DIR");
    const std::filesystem::path out(argv[2]);
    std::filesystem::create_directories(out);
    rawrcam::renderer::DngSource source(argv[1]);
    const auto& info=source.info();
    auto raw=source.normalized(0,0,info.width,info.height,false);
    auto shaded=source.normalized(0,0,info.width,info.height,true);
    std::vector<float> gain(raw.size());
    for (size_t i=0;i<raw.size();++i) {
      gain[i]=raw[i]!=0.0f ? shaded[i]/raw[i] : 1.0f;
      raw[i]/=255.0f;
    }
    save(out/"input_cfa.f32",raw);
    save(out/"shading_multiplier.f32",gain);
    std::ofstream meta(out/"dng_info.txt");
    meta << std::setprecision(9);
    meta << "width=" << info.width << "\nheight=" << info.height << "\n"
         << "crop=" << source.crop[0] << ',' << source.crop[1] << ',' << source.crop[2] << ',' << source.crop[3] << "\n"
         << "cfa=" << source.cfa << "\n"
         << "wb=" << source.wb[0] << ',' << source.wb[1] << ',' << source.wb[2] << "\n"
         << "orientation=" << source.orientation << "\n"
         << "gainmaps=" << info.raw.gainmap_count << "\n";
    std::ofstream provenance(out/"provenance.json");
    provenance << source.provenance;
    std::cout << "decoded=" << info.width << 'x' << info.height
              << " gainmaps=" << info.raw.gainmap_count
              << " peak_bytes=" << source.memoryPeak() << '\n';
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "DNG decode: " << error.what() << '\n'; return 1;
  }
}
