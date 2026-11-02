// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include "deepimage.h"
#include <filesystem>

namespace otexr {
struct ChannelDescription {
  std::string name;
  SampleType type;
  int xSampling = 1, ySampling = 1;
};
struct PartDescription {
  std::string name, storage;
  Window dataWindow, displayWindow;
  bool deep = false;
  std::vector<ChannelDescription> channels;
};

// Metadata only: no color transform, implicit flattening or pixel allocation.
// Native-path streams preserve Unicode filenames on Windows.
std::vector<PartDescription> inspectDocument(const std::filesystem::path &path);
}  // namespace otexr
