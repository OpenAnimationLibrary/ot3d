#include "ammaterial.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}
void near(double a, double b) {
  check(std::abs(a - b) < 1e-6, "numeric mismatch");
}
std::string material(const std::string &attributes) {
  return "<MATERIALFILE>\nProductVersion=19.5\n<MATERIALS><MATERIAL>\n"
         "Name=Fixture\n<ATTRIBUTE>\n" +
         attributes +
         "\n</ATTRIBUTE><FileInfo><Preview><DATA>ignored</DATA></Preview>"
         "</FileInfo></MATERIAL></MATERIALS></MATERIALFILE>";
}
void reject(const std::string &source) {
  try {
    otam::parseMaterial(source);
  } catch (const std::runtime_error &) {
    return;
  }
  throw std::runtime_error("invalid material accepted");
}
}  // namespace
int main() {
  try {
    auto m = otam::parseMaterial(material(
        "DiffuseColor=64 32 255\nAmbianceColor=0 255 0\nAmbiance=25\n"
        "AmbianceBlend=50\nDiffuseFalloff=75\nSpecularColor=255 255 255\n"
        "SpecularSize=150\nSpecularIntensity=200\nTransparency=60\n"
        "<Reflectivity>\nValue=40\n</Reflectivity>\nRefraction=1.5\nDensity=2\n"
        "Roughness=14\nRoughnessScale=50\nTranslucency=30\nGlow=TRUE\nRadiance="
        "110"));
    check(m.name == "Fixture" && m.version == "19.5", "material identity");
    near((*m.surface.diffuse)[0], 64.0 / 255);
    near(m.surface.specularSize, 1.5);
    near(m.surface.transparency, .6);
    near(m.surface.refraction, 1.5);
    near(m.surface.density, 2);
    near(m.surface.roughness, .14);
    check(m.unsupported.size() == 7,
          "unsupported surface effects not identified");
    check(!m.source.empty(), "source not retained");
    const auto empty = otam::parseMaterial(material(""));
    check(empty.unsupported.empty() && !empty.surface.diffuse,
          "unset color inheritance");
    check(!otam::parseMaterial(material("SpecularColor=Not Set"))
               .surface.specularColor,
          "explicit unset color");
    reject(material("Transparency=101"));
    reject(material("Ambiance=nan"));
    reject(material("DiffuseColor=1 2"));
    reject(material("DiffuseColor=256 0 0"));
    reject(material("Refraction=0"));
    reject(material("Ambiance=20\nAmbiance=30"));
    reject(material("<Transparency><CHANNEL>0 50</CHANNEL></Transparency>"));
    auto procedural = material("");
    procedural.insert(procedural.find("<ATTRIBUTE>"), "<CHECKER>");
    procedural.insert(procedural.find("<FileInfo>"), "</CHECKER>");
    reject(procedural);
    reject(material("") + "<MATERIALFILE/>");
    reject("[MATERIALFILE]\nProductVersion=8.52\n[ENDMATERIALFILE]");
    auto unknown = otam::parseMaterial(material("FutureShader=TRUE"));
    check(unknown.unsupported.size() == 1,
          "unknown attribute silently discarded");
    otam::Limits limits;
    limits.bytes = 10;
    bool limited = false;
    try {
      otam::parseMaterial(material(""), limits);
    } catch (const std::runtime_error &) {
      limited = true;
    }
    check(limited, "material size limit ignored");
    const otam::Vec3 n{{0, 0, 1}}, v{{0, 0, 1}};
    const otam::Color white{{1, 1, 1}};
    otam::Surface s;
    otam::SurfaceLight light;
    near(otam::shadeSurface(s, white, n, v, {light}, 0, 1)[0], 1);
    near(otam::shadeSurface(s, white, n, v, {}, 0, 1)[0], 0);
    s.ambiance = .25;
    near(otam::shadeSurface(s, white, n, v, {}, 0, 0)[0], .25);
    s.ambianceBlend = 1;
    near(otam::shadeSurface(s, white, n, v, {light}, 0, 1)[0], 1);
    s.ambiance     = 0;
    s.specularSize = .25;
    near(otam::shadeSurface(s, white, n, v, {light}, 0, 1)[0], 2);
    s.specularSize  = 0;
    light.direction = {{std::sqrt(.75), 0, .5}};
    near(otam::shadeSurface(s, white, n, v, {light}, 0, 1)[0], .5);
    s.diffuseFalloff = .25;
    near(otam::shadeSurface(s, white, n, v, {light}, 0, 1)[0], 1);
    s.diffuseFalloff  = 1;
    s.specularSize    = .1;
    const auto narrow = otam::shadeSurface(s, white, n, v, {light}, 0, 1)[0];
    s.specularSize    = 1;
    check(otam::shadeSurface(s, white, n, v, {light}, 0, 1)[0] > narrow,
          "specular size does not widen highlight");
    std::cout << "A:M material parser, diagnostics and local shading passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
