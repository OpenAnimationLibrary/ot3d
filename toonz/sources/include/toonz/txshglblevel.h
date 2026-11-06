#pragma once

#include "toonz/txshlevel.h"
#include "glbviewer.h"
#include <mutex>

#undef DVAPI
#undef DVVAR
#ifdef TOONZLIB_EXPORTS
#define DVAPI DV_EXPORT_API
#define DVVAR DV_EXPORT_VAR
#else
#define DVAPI DV_IMPORT_API
#define DVVAR DV_IMPORT_VAR
#endif

// Referenced GLB asset. Each exposure owns its placement through the stage.
class DVAPI TXshGlbLevel final : public TXshLevel {
  PERSIST_DECLARATION(TXshGlbLevel)
  DECLARE_CLASS_CODE
  TFilePath m_path;
  mutable std::mutex m_mutex;
  std::shared_ptr<const otglb::Asset> m_asset;
  std::shared_ptr<const otglb::ViewerGeometry> m_geometry;
  std::string m_error;

public:
  explicit TXshGlbLevel(std::wstring name = L"");
  ~TXshGlbLevel() override;
  TXshGlbLevel *getGlbLevel() override { return this; }
  TFilePath getPath() const override { return m_path; }
  void setPath(const TFilePath &path, bool keepCached = false);
  bool isEmpty() const override { return m_path.isEmpty(); }
  int getFrameCount() const override { return isEmpty() ? 0 : 1; }
  void getFids(std::vector<TFrameId> &fids) const override;
  void loadData(TIStream &is) override;
  void saveData(TOStream &os) override;
  void load() override;
  // Saving an OT3D scene saves the reference only; never write into the GLB.
  void save() override {}
  std::shared_ptr<const otglb::Asset> asset() const;
  std::shared_ptr<const otglb::ViewerGeometry> geometry() const;
  std::string error() const;
};
#ifdef _WIN32
template class DV_EXPORT_API TSmartPointerT<TXshGlbLevel>;
#endif
using TXshGlbLevelP = TSmartPointerT<TXshGlbLevel>;
