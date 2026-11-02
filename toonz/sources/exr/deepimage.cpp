// SPDX-License-Identifier: BSD-3-Clause

#include "deepimage.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <set>
#include <stdexcept>

namespace otexr {
namespace {
void require(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}
void budget(std::uint64_t count, std::uint64_t size, std::uint64_t &used,
            const DeepLimits &limits) {
  require(
      size > 0 && used <= limits.bytes && count <= (limits.bytes - used) / size,
      "Deep image memory budget exceeded");
  used += count * size;
}
const std::vector<float> &floats(const DeepImage &image,
                                 const std::string &name) {
  for (const auto &channel : image.channels)
    if (channel.name == name) {
      const auto *values = std::get_if<std::vector<float>>(&channel.samples);
      require(values != nullptr,
              "Deep preview requires float color/depth channels");
      return *values;
    }
  throw std::runtime_error("Missing deep preview channel: " + name);
}
}  // namespace

DeepLayout planDeepStorage(Window window,
                           const std::vector<std::uint32_t> &counts,
                           std::size_t channelCount, const DeepLimits &limits) {
  require(window.maxX >= window.minX && window.maxY >= window.minY,
          "Invalid deep data window");
  DeepLayout layout;
  layout.width  = std::int64_t(window.maxX) - window.minX + 1;
  layout.height = std::int64_t(window.maxY) - window.minY + 1;
  require(layout.height <= limits.pixels &&
              layout.width <= limits.pixels / layout.height,
          "Deep image pixel budget exceeded");
  const auto pixels = layout.width * layout.height;
  require(pixels == counts.size(), "Deep sample-count dimensions differ");
  require(channelCount > 0 && channelCount <= limits.channels,
          "Invalid deep channel count");
  std::uint64_t used = 0;
  budget(pixels, sizeof(std::uint32_t), used, limits);
  budget(pixels + 1, sizeof(std::uint64_t), used, limits);
  std::uint64_t total = 0;
  // Validate the complete count table before allocating the prefix table.
  for (const auto count : counts) {
    require(count <= limits.samplesPerPixel && total <= limits.samples &&
                count <= limits.samples - total,
            "Deep image sample budget exceeded");
    total += count;
  }
  require(channelCount <=
              std::numeric_limits<std::uint64_t>::max() / sizeof(std::uint32_t),
          "Deep channel stride overflow");
  budget(total, channelCount * sizeof(std::uint32_t), used, limits);
  require(pixels < std::vector<std::uint64_t>().max_size(),
          "Deep prefix table exceeds addressable storage");
  layout.offsets.reserve(std::size_t(pixels + 1));
  layout.offsets.push_back(0);
  for (const auto count : counts)
    layout.offsets.push_back(layout.offsets.back() + count);
  return layout;
}

DeepLayout validateDeepImage(const DeepImage &image, const DeepLimits &limits) {
  const auto layout = planDeepStorage(image.dataWindow, image.sampleCounts,
                                      image.channels.size(), limits);
  require(image.displayWindow.maxX >= image.displayWindow.minX &&
              image.displayWindow.maxY >= image.displayWindow.minY,
          "Invalid deep display window");
  std::set<std::string> names;
  for (const auto &channel : image.channels) {
    require(!channel.name.empty() && names.insert(channel.name).second,
            "Empty or duplicate deep channel name");
    require(channel.type == SampleType::Half ||
                channel.type == SampleType::Float ||
                channel.type == SampleType::UInt,
            "Invalid deep channel type");
    const bool integer =
        std::holds_alternative<std::vector<std::uint32_t>>(channel.samples);
    require(integer == (channel.type == SampleType::UInt),
            "Deep channel storage does not match its declared type");
    const auto size = std::visit(
        [](const auto &values) { return values.size(); }, channel.samples);
    require(size == layout.offsets.back(), "Deep channel sample count differs");
  }
  return layout;
}

std::vector<std::array<float, 4>> flattenPointSamples(
    const DeepImage &image, const DeepLimits &limits) {
  const auto layout              = validateDeepImage(image, limits);
  const auto &z                  = floats(image, "Z");
  const std::vector<float> *back = nullptr;
  for (const auto &channel : image.channels)
    if (channel.name == "ZBack") back = &floats(image, "ZBack");
  const auto &r = floats(image, "R");
  const auto &g = floats(image, "G");
  const auto &b = floats(image, "B");
  const auto &a = floats(image, "A");
  // Preview output and the largest sorting scratch buffer have their own
  // explicit budget in addition to the retained deep-source budget.
  std::uint64_t used = 0;
  budget(image.sampleCounts.size(), sizeof(std::array<float, 4>), used, limits);
  const auto maxCount =
      *std::max_element(image.sampleCounts.begin(), image.sampleCounts.end());
  budget(maxCount, sizeof(std::uint64_t), used, limits);
  std::vector<std::array<float, 4>> output(image.sampleCounts.size());
  std::vector<std::uint64_t> order;
  order.reserve(maxCount);
  for (std::size_t pixel = 0; pixel < output.size(); ++pixel) {
    order.resize(image.sampleCounts[pixel]);
    std::iota(order.begin(), order.end(), layout.offsets[pixel]);
    for (const auto sample : order) {
      require(std::isfinite(z[sample]), "Non-finite deep sample depth");
      require(!back || (*back)[sample] == z[sample],
              "Volume samples require a deep volume flattening policy");
      require(std::isfinite(a[sample]) && a[sample] >= 0 && a[sample] <= 1,
              "Invalid deep alpha");
      require(std::isfinite(r[sample]) && std::isfinite(g[sample]) &&
                  std::isfinite(b[sample]),
              "Non-finite deep color");
    }
    // Include original order as a tie breaker, so ordinary sort is stable in
    // meaning without an unbudgeted stable_sort temporary allocation.
    std::sort(order.begin(), order.end(), [&](auto x, auto y) {
      return z[x] == z[y] ? x < y : z[x] < z[y];
    });
    double transmittance = 1.0;
    std::array<double, 4> color{};
    for (const auto sample : order) {
      color[0] += transmittance * r[sample];
      color[1] += transmittance * g[sample];
      color[2] += transmittance * b[sample];
      color[3] += transmittance * a[sample];
      transmittance *= 1.0 - a[sample];
    }
    for (int c = 0; c < 4; ++c) {
      require(std::isfinite(color[c]) &&
                  std::abs(color[c]) <= std::numeric_limits<float>::max(),
              "Deep preview color overflow");
      output[pixel][c] = float(color[c]);
    }
  }
  return output;
}
}  // namespace otexr
