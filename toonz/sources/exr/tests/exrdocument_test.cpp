// SPDX-License-Identifier: BSD-3-Clause

#include "deepimage.h"
#include "exrdocument.h"

#include <OpenEXR/ImfChannelList.h>
#include <OpenEXR/ImfDeepFrameBuffer.h>
#include <OpenEXR/ImfDeepScanLineOutputFile.h>
#include <OpenEXR/ImfHeader.h>
#include <OpenEXR/ImfPartType.h>

#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace Exr = OPENEXR_IMF_NAMESPACE;
using namespace otexr;
namespace {
void check(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}
template <class F>
void rejects(F f) {
  bool caught = false;
  try {
    f();
  } catch (const std::exception &) {
    caught = true;
  }
  check(caught, "Expected explicit rejection");
}
DeepImage fixture() {
  DeepImage image;
  image.dataWindow = image.displayWindow = {-5, -7, -4, -7};
  image.sampleCounts                     = {2, 0};
  auto channel = [&](const char *name, std::vector<float> values) {
    image.channels.push_back({name, SampleType::Float, std::move(values)});
  };
  channel("R", {4, 0});
  channel("G", {0, 1});
  channel("B", {-2, 0});
  channel("A", {0.5f, 0.5f});
  channel("Z", {2, 1});
  channel("ZBack", {2, 1});
  image.channels.push_back(
      {"ID", SampleType::UInt,
       std::vector<std::uint32_t>{0xffffffffu, 16777217u}});
  return image;
}
void writeDeepFixture(const std::filesystem::path &path) {
  Exr::Header header(1, 1);
  header.setType(Exr::DEEPSCANLINE);
  header.compression() = Exr::ZIPS_COMPRESSION;
  header.channels().insert("Z", Exr::Channel(Exr::FLOAT));
  header.channels().insert("A", Exr::Channel(Exr::FLOAT));
  Exr::DeepScanLineOutputFile output(path.string().c_str(), header);
  std::uint32_t count = 2;
  float depths[] = {2, 1}, alpha[] = {0.5f, 0.5f};
  float *z = depths, *a = alpha;
  Exr::DeepFrameBuffer buffer;
  buffer.insertSampleCountSlice(Exr::Slice(Exr::UINT,
                                           reinterpret_cast<char *>(&count),
                                           sizeof(count), sizeof(count)));
  buffer.insert("Z", Exr::DeepSlice(Exr::FLOAT, reinterpret_cast<char *>(&z),
                                    sizeof(z), sizeof(z), sizeof(float)));
  buffer.insert("A", Exr::DeepSlice(Exr::FLOAT, reinterpret_cast<char *>(&a),
                                    sizeof(a), sizeof(a), sizeof(float)));
  output.setFrameBuffer(buffer);
  output.writePixels(1);
}
}  // namespace

int main() {
  std::filesystem::path directory;
  try {
    auto image        = fixture();
    const auto layout = validateDeepImage(image);
    check(layout.width == 2 && layout.height == 1 &&
              layout.offsets == std::vector<std::uint64_t>({0, 2, 2}),
          "Offset window or zero-sample span lost");
    const auto flat = flattenPointSamples(image);
    check(flat[0] == std::array<float, 4>({2, 1, -1, 0.75f}) &&
              flat[1] == std::array<float, 4>({0, 0, 0, 0}),
          "Depth sorting, HDR, signed values or alpha-over differ");
    const auto &ids =
        std::get<std::vector<std::uint32_t>>(image.channels[6].samples);
    check(ids[0] == 0xffffffffu && ids[1] == 16777217u,
          "UINT IDs lost precision");
    check(image.sampleCounts == std::vector<std::uint32_t>({2, 0}),
          "Flatten modified source samples");
    DeepLimits limits;
    limits.samples = 1;
    rejects([&] { validateDeepImage(image, limits); });
    limits       = DeepLimits();
    limits.bytes = 1;
    rejects([&] {
      planDeepStorage(image.dataWindow, image.sampleCounts, 7, limits);
    });
    image.channels[5].samples = std::vector<float>{3, 1};
    rejects([&] { flattenPointSamples(image); });
    image                      = fixture();
    image.channels.back().name = "Z";
    rejects([&] { validateDeepImage(image); });
    image                 = fixture();
    image.sampleCounts[0] = 3;
    rejects([&] { validateDeepImage(image); });

    directory =
        std::filesystem::temp_directory_path() /
        ("ot3d-exr-" + std::to_string(std::chrono::high_resolution_clock::now()
                                          .time_since_epoch()
                                          .count()));
    std::filesystem::create_directories(directory);
    const auto path = directory / "deep.exr";
    writeDeepFixture(path);
    // Rename after writing to exercise the native-path inspector on Windows.
    const auto unicode =
        directory / std::filesystem::u8path(u8"deep-\u00e9.exr");
    std::filesystem::rename(path, unicode);
    const auto metadata = inspectDocument(unicode);
    check(metadata.size() == 1 && metadata[0].deep &&
              metadata[0].storage == Exr::DEEPSCANLINE &&
              metadata[0].channels.size() == 2,
          "Official OpenEXR deep metadata discovery failed");
    std::ofstream(directory / "bad.exr") << "invalid";
    rejects([&] { inspectDocument(directory / "bad.exr"); });
    std::filesystem::remove_all(directory);
    std::cout
        << "PASS deep transport, exact UINT IDs, point flattening, limits, "
           "volume rejection and native OpenEXR metadata inspection\n";
    return 0;
  } catch (const std::exception &error) {
    if (!directory.empty()) std::filesystem::remove_all(directory);
    std::cerr << error.what() << '\n';
    return 1;
  }
}
