#include "ammodel.h"
#include <fstream>
#include <iostream>
#include <iterator>
int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "Usage: am_inspect file.mdl\n";
    return 2;
  }
  try {
    std::ifstream file(argv[1], std::ios::binary | std::ios::ate);
    if (!file || file.tellg() > std::streamoff(otam::Limits().bytes))
      throw std::runtime_error("cannot read file or file too large");
    file.seekg(0);
    auto document =
        otam::parse(std::string(std::istreambuf_iterator<char>(file), {}));
    auto mesh = otam::previewMesh(document);
    std::cout << "models=" << document.models.size()
              << " cps=" << document.models[0].points.size()
              << " splines=" << document.models[0].splines.size()
              << " patches=" << document.models[0].patches.size()
              << " preview_triangles=" << mesh.triangles.size() << '\n';
    for (const auto &w : mesh.warnings) std::cout << "warning: " << w << '\n';
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
