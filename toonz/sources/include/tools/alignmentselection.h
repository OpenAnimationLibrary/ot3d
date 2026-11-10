#pragma once

#include "tools/vectoralignment.h"
#include "toonzqt/selection.h"
#include "toonzqt/selectioncommandids.h"

#undef DVAPI
#ifdef TNZTOOLS_EXPORTS
#define DVAPI DV_EXPORT_API
#else
#define DVAPI DV_IMPORT_API
#endif

// Selections that own their alignment reference independently of the tool.
class DVAPI AlignmentSelection : public TSelection {
  VectorAlignment::Method m_alignmentMethod = VectorAlignment::SELECT_AREA;

protected:
  virtual void align(VectorAlignment::Type type) = 0;

public:
  void enableCommands() override;

  virtual std::size_t alignmentCount() const = 0;
  VectorAlignment::Method alignmentMethod() const { return m_alignmentMethod; }
  void setAlignmentMethod(VectorAlignment::Method method) {
    m_alignmentMethod = method;
  }
};
