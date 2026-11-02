// SPDX-License-Identifier: BSD-3-Clause

#include "exrdocument.h"

#include <OpenEXR/ImfChannelList.h>
#include <OpenEXR/ImfHeader.h>
#include <OpenEXR/ImfIO.h>
#include <OpenEXR/ImfMultiPartInputFile.h>
#include <OpenEXR/ImfPartType.h>

#include <fstream>
#include <stdexcept>

namespace otexr {
namespace {
namespace Exr = OPENEXR_IMF_NAMESPACE;
class NativeInputStream final : public Exr::IStream {
  std::ifstream m_file;
  std::int64_t m_size = 0;

public:
  explicit NativeInputStream(const std::filesystem::path &path)
      : Exr::IStream("OT3D EXR document"), m_file(path, std::ios::binary) {
    if (!m_file) throw std::runtime_error("Cannot open EXR document");
    const auto size = std::filesystem::file_size(path);
    if (size > 512ull * 1024 * 1024)
      throw std::runtime_error("EXR inspection file budget exceeded");
    m_size = std::int64_t(size);
  }
  bool read(char buffer[], int size) override {
    if (size < 0) throw std::runtime_error("Invalid EXR read size");
    m_file.read(buffer, size);
    if (m_file.gcount() != size)
      throw std::runtime_error("Truncated EXR document");
    return tellg() < std::uint64_t(m_size);
  }
  std::uint64_t tellg() override {
    const auto position = m_file.tellg();
    if (position < 0)
      throw std::runtime_error("Cannot query EXR stream position");
    return std::uint64_t(position);
  }
  void seekg(std::uint64_t position) override {
    if (position > std::uint64_t(m_size))
      throw std::runtime_error("EXR seek exceeds document size");
    m_file.seekg(std::streamoff(position));
    if (!m_file) throw std::runtime_error("Cannot seek in EXR document");
  }
  void clear() override { m_file.clear(); }
  std::int64_t size() override { return m_size; }
};

Window window(const IMATH_NAMESPACE::Box2i &value) {
  return {value.min.x, value.min.y, value.max.x, value.max.y};
}
}  // namespace

std::vector<PartDescription> inspectDocument(
    const std::filesystem::path &path) {
  NativeInputStream stream(path);
  // Header inspection does not need chunk-table reconstruction or worker jobs.
  Exr::MultiPartInputFile document(stream, 1, false);
  if (document.parts() > 256)
    throw std::runtime_error("EXR inspection part budget exceeded");
  std::vector<PartDescription> result;
  for (int part = 0; part < document.parts(); ++part) {
    const auto &header = document.header(part);
    PartDescription description;
    description.name          = header.hasName() ? header.name() : "";
    description.storage       = header.type();
    description.deep          = Exr::isDeepData(description.storage);
    description.dataWindow    = window(header.dataWindow());
    description.displayWindow = window(header.displayWindow());
    for (auto c = header.channels().begin(); c != header.channels().end();
         ++c) {
      if (description.channels.size() == 256)
        throw std::runtime_error("EXR inspection channel budget exceeded");
      const auto type = c.channel().type;
      description.channels.push_back({c.name(),
                                      type == Exr::UINT   ? SampleType::UInt
                                      : type == Exr::HALF ? SampleType::Half
                                                          : SampleType::Float,
                                      c.channel().xSampling,
                                      c.channel().ySampling});
    }
    result.push_back(std::move(description));
  }
  return result;
}
}  // namespace otexr
