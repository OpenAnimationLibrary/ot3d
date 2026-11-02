// SPDX-License-Identifier: BSD-3-Clause

#include "exrdocument.h"
#include <iostream>

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "Usage: exr_inspect FILE.exr\n";
    return 2;
  }
  try {
    const auto parts = otexr::inspectDocument(std::filesystem::u8path(argv[1]));
    for (std::size_t i = 0; i < parts.size(); ++i) {
      const auto &part    = parts[i];
      const auto &data    = part.dataWindow;
      const auto &display = part.displayWindow;
      std::cout << "Part " << i << ": " << part.name << " (" << part.storage
                << ")" << (part.deep ? " [deep]" : " [flat]") << '\n';
      std::cout << "  Data window: " << data.minX << ',' << data.minY << " .. "
                << data.maxX << ',' << data.maxY << '\n';
      std::cout << "  Display window: " << display.minX << ',' << display.minY
                << " .. " << display.maxX << ',' << display.maxY << '\n';
      for (const auto &channel : part.channels)
        std::cout << "  " << channel.name << " "
                  << (channel.type == otexr::SampleType::UInt   ? "UINT"
                      : channel.type == otexr::SampleType::Half ? "HALF"
                                                                : "FLOAT")
                  << " sampling " << channel.xSampling << 'x'
                  << channel.ySampling << '\n';
    }
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
