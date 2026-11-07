#include "ammaterial.h"

#include <algorithm>
#include <cmath>
#include <locale>
#include <map>
#include <sstream>
#include <stdexcept>

namespace otam {
namespace {
[[noreturn]] void fail(const std::string &message) {
  throw std::runtime_error("A:M Material: " + message);
}
std::string trim(const std::string &s) {
  auto first = s.find_first_not_of(" \r\n\t");
  return first == std::string::npos
             ? std::string()
             : s.substr(first, s.find_last_not_of(" \r\n\t") - first + 1);
}
std::map<std::string, std::string> properties(const Document &d,
                                              const SourceNode &node) {
  std::string source;
  auto begin = node.contentBegin;
  for (auto i : node.children) {
    source += d.source.substr(begin, d.nodes[i].begin - begin) + '\n';
    begin = d.nodes[i].end;
  }
  source += d.source.substr(begin, node.contentEnd - begin);
  std::istringstream in(source);
  std::map<std::string, std::string> result;
  std::string line;
  while (std::getline(in, line)) {
    line = trim(line);
    if (line.empty()) continue;
    auto eq = line.find('=');
    if (eq == std::string::npos) fail("invalid property record");
    if (!result.emplace(trim(line.substr(0, eq)), trim(line.substr(eq + 1)))
             .second)
      fail("duplicate property");
  }
  return result;
}
double scalar(const std::string &text, double minimum, double maximum) {
  std::istringstream in(text);
  in.imbue(std::locale::classic());
  double value;
  char extra;
  if (!(in >> value) || in >> extra || !std::isfinite(value) ||
      value < minimum || value > maximum)
    fail("invalid or out-of-range value: " + text.substr(0, 80));
  return value;
}
std::optional<Color> color(const std::string &text) {
  if (text == "Not Set") return {};
  std::istringstream in(text);
  in.imbue(std::locale::classic());
  Color result;
  double value;
  for (auto &c : result) {
    if (!(in >> value) || !std::isfinite(value) || value < 0 || value > 255)
      fail("invalid color");
    c = float(value / 255.0);
  }
  std::string extra;
  if (in >> extra) fail("invalid color arity");
  return result;
}
double dot(const Vec3 &a, const Vec3 &b) {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
float linear(float v) {
  return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f);
}
}  // namespace

Material parseMaterial(std::string source, const Limits &limits) {
  const auto d = parseSource(std::move(source), "MATERIALFILE", limits);
  Material result;
  result.source              = d.source;
  result.version             = d.version;
  const SourceNode *material = nullptr;
  for (auto i : d.nodes[0].children) {
    const auto &container = d.nodes[i];
    if (container.name != "MATERIALS") continue;
    for (auto j : container.children) {
      if (d.nodes[j].name != "MATERIAL" || material)
        fail("expected exactly one MATERIAL");
      material = &d.nodes[j];
    }
  }
  if (!material) fail("missing MATERIAL");
  const auto info = properties(d, *material);
  auto name       = info.find("Name");
  if (name != info.end()) result.name = name->second;
  const SourceNode *attribute = nullptr;
  for (auto i : material->children) {
    const auto &child = d.nodes[i];
    if (child.name == "FileInfo") continue;
    if (child.name != "ATTRIBUTE" || attribute)
      fail("procedural, layered and plugin material trees are not supported (" +
           child.name + ")");
    attribute = &child;
  }
  if (!attribute) fail("missing constant ATTRIBUTE");
  auto values = properties(d, *attribute);
  // A:M writes some properties as a child with Value=, even when unanimated.
  for (auto i : attribute->children) {
    const auto &child = d.nodes[i];
    auto p            = properties(d, child);
    if (!child.children.empty() || p.size() != 1 || !p.count("Value"))
      fail("animated or structured attribute is not supported: " + child.name);
    if (!values.emplace(child.name, p.at("Value")).second)
      fail("duplicate attribute: " + child.name);
  }
  auto &s         = result.surface;
  const auto take = [&](const std::string &key) {
    auto i = values.find(key);
    if (i == values.end()) return std::optional<std::string>();
    auto value = i->second;
    values.erase(i);
    return std::optional<std::string>(std::move(value));
  };
  for (auto entry : {std::make_pair("DiffuseColor", &s.diffuse),
                     std::make_pair("AmbianceColor", &s.ambianceColor),
                     std::make_pair("SpecularColor", &s.specularColor)})
    if (auto value = take(entry.first)) *entry.second = color(*value);
  const auto read = [&](const char *key, double &target, double maximum,
                        double divisor = 100.0, double minimum = 0.0) {
    if (auto value = take(key))
      target = scalar(*value, minimum, maximum) / divisor;
  };
  read("DiffuseFalloff", s.diffuseFalloff, 100);
  read("Ambiance", s.ambiance, 100);
  read("AmbianceBlend", s.ambianceBlend, 100);
  read("SpecularSize", s.specularSize, 10000);
  read("SpecularIntensity", s.specularIntensity, 100000);
  read("Roughness", s.roughness, 100000);
  read("RoughnessScale", s.roughnessScale, 100000);
  read("Transparency", s.transparency, 100);
  read("Density", s.density, 100, 1);
  read("Refraction", s.refraction, 2, 1, 1);
  read("Translucency", s.translucency, 100);
  read("Reflectivity", s.reflectivity, 100);
  read("ReflectivityFalloff", s.reflectivityFalloff, 100000);
  read("ReflectiveBlend", s.reflectiveBlend, 100);
  read("Radiance", s.radiance, 100000);
  if (auto value = take("Glow")) {
    if (*value != "TRUE" && *value != "FALSE") fail("invalid Glow value");
    s.glow = *value == "TRUE";
  }
  for (const auto &entry : values) {
    if (entry.first == "Name" || entry.first == "ClassName" ||
        entry.first == "Modified" || entry.first == "Expanded")
      continue;
    if ((entry.first == "IsDiffuseShader" ||
         entry.first == "IsSpecularShader") &&
        entry.second == "FALSE")
      continue;
    result.unsupported.push_back(entry.first);
  }
  if (s.roughness > 0) result.unsupported.push_back("Roughness turbulence");
  if (s.reflectivity > 0)
    result.deferred.push_back("scene reflections (Reflectivity/Falloff/Blend)");
  if (s.transparency > 0 && s.refraction != 1)
    result.unsupported.push_back("raytraced Refraction");
  if (s.transparency > 0 && s.density > 0)
    result.unsupported.push_back("volumetric Density");
  if (s.translucency > 0)
    result.unsupported.push_back("raytraced Translucency");
  if (s.glow) result.unsupported.push_back("Glow post effect");
  if (s.radiance != 1) result.unsupported.push_back("Radiosity Radiance");
  return result;
}

Color shadeSurface(const Surface &s, const Color &diffuse, const Vec3 &normal,
                   const Vec3 &view, const std::vector<SurfaceLight> &lights,
                   double ambient, double master) {
  Color energy{}, specular{};
  for (const auto &light : lights) {
    const double ndotl = std::max(0.0, dot(normal, light.direction));
    // HAttrProperty's Lambert SDK example uses min(1, N.L/falloff).
    const double shade =
        ndotl > 0
            ? (s.diffuseFalloff > 0 ? std::min(1.0, ndotl / s.diffuseFalloff)
                                    : 1.0)
            : 0;
    Vec3 half{};
    for (int k = 0; k < 3; ++k) half[k] = view[k] + light.direction[k];
    const double length = std::sqrt(dot(half, half));
    double highlight    = 0;
    if (s.specularSize > 0 && s.specularIntensity > 0 && ndotl > 0 &&
        dot(normal, view) > 0 && length > 1e-12) {
      for (double &v : half) v /= length;
      // Monotonic Blinn preview width. Hash's exact lobe is not published.
      const double exponent = std::max(0.01, 2.0 / s.specularSize);
      highlight             = s.specularIntensity * ndotl *
                  std::pow(std::max(0.0, dot(normal, half)), exponent);
    }
    for (int k = 0; k < 3; ++k) {
      energy[k] += float(shade * light.color[k] * light.intensity);
      specular[k] += float(highlight * light.color[k] * light.intensity);
    }
  }
  Color result;
  const auto &ac = s.ambianceColor ? *s.ambianceColor : diffuse;
  const auto &sc = s.specularColor ? *s.specularColor : diffuse;
  for (int k = 0; k < 3; ++k)
    result[k] = float(linear(diffuse[k]) * (ambient + energy[k]) * master *
                          (1 - s.ambiance * s.ambianceBlend) +
                      linear(ac[k]) * s.ambiance +
                      linear(sc[k]) * specular[k] * master);
  return result;
}
}  // namespace otam
