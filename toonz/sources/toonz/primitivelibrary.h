#pragma once

#include "tgeometry.h"

class QMimeData;

namespace PrimitiveLibrary {
bool isDrag(const QMimeData *mime);
bool canDrop(const QMimeData *mime);
bool drop(const QMimeData *mime, const TPointD &position, bool bypassSnap);
}  // namespace PrimitiveLibrary
