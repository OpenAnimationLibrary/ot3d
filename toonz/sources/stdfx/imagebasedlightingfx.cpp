#include "environmentfx.h"

class ImageBasedLightingFx final : public EnvironmentFxBase {
  FX_PLUGIN_DECLARATION(ImageBasedLightingFx)
protected:
  bool suppliesLighting() const override { return true; }
  otglb::EnvironmentLayer layer(
      double frame,
      std::shared_ptr<const otglb::EnvironmentMap> map) const override {
    auto result            = EnvironmentFxBase::layer(frame, std::move(map));
    result.diffuseLighting = true;
    return result;
  }

public:
  ImageBasedLightingFx() : EnvironmentFxBase(false) {}
};

FX_PLUGIN_IDENTIFIER(ImageBasedLightingFx, "imageBasedLightingFx")
