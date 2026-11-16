#include "stdfx.h"
#include "t3draster.h"
#include "t3dsource.h"
#include "tfxparam.h"
#include "tnotanimatableparam.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QString>

class AMDistortionCageFx final : public TStandardRasterFx,
                                 public T3DRenderSource {
  FX_PLUGIN_DECLARATION(AMDistortionCageFx)

  class AMCageSourcePort final : public T3DSourcePort {
  public:
    bool canConnect(TFx *fx) const override {
      return !fx || isAMModelColumn(fx) ||
             fx->getFxType() == "STD_amDistortionCageFx";
    }
  };

  AMCageSourcePort m_source;
  TStringParamP m_cage;
  TDoubleParamP m_strength;

public:
  AMDistortionCageFx() : m_cage(new TStringParam()), m_strength(100.0) {
    bindParam(this, "cage", m_cage);
    bindParam(this, "strength", m_strength);
    m_strength->setValueRange(0, 100);
    addInputPort("Source", m_source);
    enableComputeInFloat(true);
  }

  std::string getAlias(double frame,
                       const TRenderSettings &info) const override {
    const auto source = m_cage->getValue();
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(reinterpret_cast<const char *>(source.data()),
                 int(source.size() * sizeof(wchar_t)));
    hash.addData(QByteArray::number(frame, 'g', 17));
    hash.addData("/");
    hash.addData(QByteArray::number(m_strength->getValue(frame), 'g', 17));
    std::string alias =
        "amDistortionCage[" + hash.result().toHex().toStdString();
    if (auto *input = dynamic_cast<TRasterFx *>(m_source.getFx()))
      alias += "," + input->getAlias(frame, info);
    return alias + "]";
  }

  std::shared_ptr<const otglb::RenderScene> get3DRenderScene(
      double frame, const int *canceled,
      const otglb::LightingRig *lighting                   = nullptr,
      const std::vector<otglb::ModelTransform> *transforms = nullptr,
      const TRenderSettings *settings                      = nullptr,
      const otam::MaterialBindings *materials              = nullptr,
      const otam::InkBindings *ink                         = nullptr,
      const otam::NormalSmoothingStack *smoothing = nullptr) const override {
    const auto *source = m_source.source();
    if (!source || (canceled && *canceled)) return {};
    const auto state = m_cage->getValue();
    const double strength =
        std::clamp(m_strength->getValue(frame) / 100, 0.0, 1.0);
    if (state.empty() || strength <= 0)
      return source->get3DRenderScene(frame, canceled, lighting, transforms,
                                      settings, materials, ink, smoothing);
    TRenderSettings request = settings ? *settings : TRenderSettings();
    request.m_data.push_back(new TAMCageRenderData(
        QString::fromStdWString(state).toUtf8().toStdString(), frame,
        strength));
    return source->get3DRenderScene(frame, canceled, lighting, transforms,
                                    &request, materials, ink, smoothing);
  }

  bool doGetBBox(double frame, TRectD &bbox,
                 const TRenderSettings &info) override {
    bbox = TRectD();
    const auto scene =
        get3DRenderGeometry(frame, info.m_isCanceled, nullptr, &info);
    if (!scene || scene->empty()) return false;
    const auto &b = scene->bounds;
    bbox          = TRectD(b[0], b[1], b[2], b[3]);
    return !bbox.isEmpty();
  }

  bool canHandle(const TRenderSettings &, double) override { return true; }
  bool toBeComputedInLinearColorSpace(bool, bool) const override {
    return false;
  }

  void doCompute(TTile &tile, double frame,
                 const TRenderSettings &info) override {
    tile.getRaster()->clear();
    try {
      const auto scene =
          get3DRenderScene(frame, info.m_isCanceled, nullptr, nullptr, &info);
      if (scene && !scene->empty()) ot3d::renderScene(*scene, tile, info);
    } catch (const std::exception &error) {
      throw TException(QString("Distortion Cage [%1]: %2")
                           .arg(QString::fromStdWString(getFxId()),
                                QString::fromUtf8(error.what()))
                           .toStdWString());
    }
  }
};

FX_PLUGIN_IDENTIFIER(AMDistortionCageFx, "amDistortionCageFx")
