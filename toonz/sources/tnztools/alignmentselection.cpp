#include "tools/alignmentselection.h"

void AlignmentSelection::enableCommands() {
  using namespace VectorAlignment;
  enableCommand(this, MI_AlignLeft, &AlignmentSelection::align, ALIGN_LEFT);
  enableCommand(this, MI_AlignRight, &AlignmentSelection::align, ALIGN_RIGHT);
  enableCommand(this, MI_AlignTop, &AlignmentSelection::align, ALIGN_TOP);
  enableCommand(this, MI_AlignBottom, &AlignmentSelection::align, ALIGN_BOTTOM);
  enableCommand(this, MI_AlignCenterHorizontal, &AlignmentSelection::align,
                ALIGN_CENTER_H);
  enableCommand(this, MI_AlignCenterVertical, &AlignmentSelection::align,
                ALIGN_CENTER_V);
  enableCommand(this, MI_DistributeHorizontal, &AlignmentSelection::align,
                DISTRIBUTE_H);
  enableCommand(this, MI_DistributeVertical, &AlignmentSelection::align,
                DISTRIBUTE_V);
}
