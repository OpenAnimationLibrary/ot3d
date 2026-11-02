// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace otexr {

struct Window {
  int minX = 0, minY = 0, maxX = -1, maxY = -1;
};

enum class SampleType { Half, Float, UInt };
struct DeepChannel {
  std::string name;
  SampleType type = SampleType::Float;
  // HALF expands exactly to float. UINT stays integer, including IDs > 2^24.
  std::variant<std::vector<float>, std::vector<std::uint32_t>> samples;
};

struct DeepImage {
  Window dataWindow, displayWindow;
  // Row-major in the data window, including pixels with zero samples.
  // Each channel concatenates the same per-pixel sample spans.
  std::vector<std::uint32_t> sampleCounts;
  std::vector<DeepChannel> channels;
};

struct DeepLimits {
  std::uint64_t pixels          = 16 * 1024 * 1024;
  std::uint64_t samples         = 16 * 1024 * 1024;
  std::uint64_t samplesPerPixel = 65536;
  std::uint64_t bytes           = 256 * 1024 * 1024;
  std::uint64_t channels        = 256;
};

struct DeepLayout {
  std::uint64_t width = 0, height = 0;
  // One extra sentinel offset; zero-sample pixels have equal adjacent offsets.
  std::vector<std::uint64_t> offsets;
};

// Count/prefix planning is available BEFORE a decoder allocates sample planes.
DeepLayout planDeepStorage(Window window,
                           const std::vector<std::uint32_t> &counts,
                           std::size_t channelCount,
                           const DeepLimits &limits = DeepLimits());
DeepLayout validateDeepImage(const DeepImage &image,
                             const DeepLimits &limits = DeepLimits());

// Explicit preview-only flattening of premultiplied POINT samples. Uses raw
// scene-linear RGB, stable front-to-back depth sorting and alpha-over. Volume
// intervals (ZBack != Z) are rejected until a volume integration policy exists.
// The input deep image and all auxiliary/UINT channels remain unchanged.
std::vector<std::array<float, 4>> flattenPointSamples(
    const DeepImage &image, const DeepLimits &limits = DeepLimits());

}  // namespace otexr
