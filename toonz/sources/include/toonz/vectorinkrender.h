#pragma once

#include "amink.h"
#include "trasterfx.h"

#include <optional>

#undef DVAPI
#ifdef TOONZLIB_EXPORTS
#define DVAPI DV_EXPORT_API
#else
#define DVAPI DV_IMPORT_API
#endif

class TStroke;
class TVectorImage;
class TTile;

DVAPI bool isVectorInkColumn(TFx *fx, std::optional<int> row = {});

class DVAPI TVectorInkRenderData final : public TRasterFxRenderData {
  otam::InkStyle m_style;
  std::string m_alias;

public:
  explicit TVectorInkRenderData(otam::InkStyle style);
  ~TVectorInkRenderData() override;

  const otam::InkStyle &style() const { return m_style; }
  bool operator==(const TRasterFxRenderData &other) const override;
  std::string toString() const override { return m_alias; }
  float typeIndex() const override { return 4.f; }
  TRectD getBBoxEnlargement(const TRectD &bbox) override;
};

class VectorInkRenderer {
  std::vector<const otam::InkStyle *> m_styles;

public:
  VectorInkRenderer(const TVectorImage &image, const TRenderSettings &info);
  bool empty() const { return m_styles.empty(); }
  bool replaces(UINT index) const { return !empty() && m_styles[index]; }
  void drawStroke(const TStroke &stroke, UINT index, const TAffine &affine,
                  const TRect &clip, int *canceled) const;
  void overlay(const TVectorImage &image, TTile &tile,
               const TRenderSettings &info) const;
};
