#pragma once

#include "toonz/txshlevel.h"
#include "ammodel.h"
#include "tpalette.h"
#include <map>
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

// Native A:M source with scene-local edits. Column/stage objects
// own placement and animation. No conversion to a TXshSimpleLevel or GLB.
class DVAPI TXshAMLevel final : public TXshLevel {
  PERSIST_DECLARATION(TXshAMLevel)
  DECLARE_CLASS_CODE
  TFilePath m_path;
  mutable std::mutex m_mutex;
  std::shared_ptr<const otam::Document> m_document;
  std::shared_ptr<const otam::Mesh> m_mesh;
  std::string m_revision;
  std::string m_error;

public:
  struct PointValue {
    otam::Vec3 position;
    std::array<double, 6> bias;

    bool operator==(const PointValue &other) const {
      return position == other.position && bias == other.bias;
    }
  };

private:
  std::map<otam::Id, PointValue> m_pointEdits;
  std::string m_editRevision;
  std::shared_ptr<const otam::Model> m_editableModel;

  struct SurfaceStyle {
    int id;
    TPixel32 sourceColor;
  };
  TPaletteP m_palette;
  std::map<std::string, SurfaceStyle> m_groupStyles;
  std::vector<int> m_styleByGroup;
  TPaletteP m_renderPalette;
  mutable std::map<int, TPixel32> m_colorOverrides;
  mutable std::shared_ptr<const otam::Mesh> m_renderMesh;

  void refreshPalette(const otam::Model &model, const std::string &revision);
  bool updatePaletteColorsLocked();
  bool updateRenderMeshLocked(const TPalette &palette) const;

public:
  explicit TXshAMLevel(std::wstring name = L"");
  ~TXshAMLevel() override;
  TXshAMLevel *getAMLevel() override { return this; }
  TFilePath getPath() const override { return m_path; }
  void setPath(const TFilePath &path, bool keepCached = false);
  bool isEmpty() const override { return m_path.isEmpty(); }
  int getFrameCount() const override { return isEmpty() ? 0 : 1; }
  void getFids(std::vector<TFrameId> &fids) const override;
  void loadData(TIStream &is) override;
  void saveData(TOStream &os) override;
  void load() override;
  // The scene owns the edits. Level saves never write into the MDL.
  void save() override {}
  std::shared_ptr<const otam::Document> document() const;
  std::shared_ptr<const otam::Model> editableModel() const;
  bool setControlPoint(otam::Id id, const PointValue &value,
                       const std::string &sourceRevision);
  std::shared_ptr<const otam::Mesh> mesh() const;
  std::shared_ptr<const otam::Mesh> renderMesh(int frame = 0) const;
  TPalette *getPalette() const { return m_palette.getPointer(); }
  // Called on the UI thread after palette edits. Renderers use the snapshot.
  bool updatePaletteColors();
  std::map<int, TPixel32> sourceStyleColors() const;
  std::string revision() const;
  std::string error() const;
};
#ifdef _WIN32
template class DV_EXPORT_API TSmartPointerT<TXshAMLevel>;
#endif
using TXshAMLevelP = TSmartPointerT<TXshAMLevel>;
