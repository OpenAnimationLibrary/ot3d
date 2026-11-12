

#include "toonz/txshlevelcolumn.h"
#include "amaction.h"
#include "historytypes.h"
#include "tundo.h"
#include "toonz/tscenehandle.h"
#include "toonz/txsheet.h"
#include "toonz/txsheethandle.h"
#include "toonz/vectoraction.h"

// TnzLib includes
#include "toonz/txshsimplelevel.h"
#include "toonz/txshcell.h"
#include "toonz/tcolumnfxset.h"
#include "toonz/tcolumnfx.h"
#include "toonz/txshleveltypes.h"

// TnzCore includes
#include "tstream.h"

// Qt includes
#include <QByteArray>
#include <QRegularExpression>

#include <atomic>
#include <stdexcept>
#include <utility>

//-----------------------------------------------------------------------------

namespace {
TFrameId qstringToFrameId(const QString &str) {
  if (str.isEmpty() || str == "-1") {
    return TFrameId::EMPTY_FRAME;
  } else if (str == "-" || str == "-2") {
    return TFrameId::NO_FRAME;
  }

  QString regExpStr = QString("^%1$").arg(TFilePath::fidRegExpStr());
  QRegularExpression rx(regExpStr);
  QRegularExpressionMatch match = rx.match(str);

  if (!match.hasMatch()) {
    return TFrameId();
  }

  if (match.captured(2).isEmpty()) {
    return TFrameId(match.captured(1).toInt());
  } else {
    return TFrameId(match.captured(1).toInt(), match.captured(2));
  }
}
}  // namespace

//-----------------------------------------------------------------------------

//=============================================================================
// TXshLevelColumn

TXshLevelColumn::TXshLevelColumn()
    : m_fx(new TLevelColumnFx()), m_iconVisible(false) {
  m_fx->addRef();
  m_fx->setColumn(this);
}

//-----------------------------------------------------------------------------

TXshLevelColumn::~TXshLevelColumn() {
  m_fx->setColumn(nullptr);
  m_fx->release();
  m_fx = nullptr;
}

//-----------------------------------------------------------------------------

TXshColumn::ColumnType TXshLevelColumn::getColumnType() const {
  return ColumnType::eLevelType;
}

//----------------------------------------------------------------------------

bool TXshLevelColumn::canSetCell(const TXshCell &cell) const {
  if (cell.isEmpty()) {
    return true;
  }

  if (!isEmpty()) {
    int first, last;
    getRange(first, last);
    if (nativeModelColumnKind(getCell(first).m_level->getType()) !=
        nativeModelColumnKind(cell.m_level->getType()))
      return false;
  }
  if (isNativeModelLevel(cell.m_level->getType())) return true;
  TXshSimpleLevel *sl = cell.getSimpleLevel();
  if (sl) {
    return (sl->getType() & LEVELCOLUMN_XSHLEVEL) != 0;
  }

  return cell.getChildLevel() != nullptr;
}

//-----------------------------------------------------------------------------

TLevelColumnFx *TXshLevelColumn::getLevelColumnFx() const { return m_fx; }

//-----------------------------------------------------------------------------

TFx *TXshLevelColumn::getFx() const { return m_fx; }

//-----------------------------------------------------------------------------

AMActionBinding TXshLevelColumn::getAMAction() const {
  const auto binding = std::atomic_load(&m_amAction);
  return binding ? *binding : AMActionBinding{};
}

void TXshLevelColumn::setAMAction(const AMActionBinding &binding) {
  if (binding.action &&
      (!binding.level || !binding.level->getAMLevel() || binding.startRow < 0))
    throw TException("A:M action: invalid column attachment");
  std::shared_ptr<const AMActionBinding> replacement;
  if (binding.action)
    replacement = std::make_shared<const AMActionBinding>(binding);
  std::atomic_store(&m_amAction, std::move(replacement));
}

//-----------------------------------------------------------------------------

struct VectorActionPreview {
  std::uint64_t revision;
  int row;
  TImageP image;
};

bool VectorActionBinding::matches(const TXshCell &cell) const {
  return action && level == cell.m_level && fid == cell.m_frameId;
}

VectorActionBinding TXshLevelColumn::getVectorAction() const {
  const auto binding = std::atomic_load(&m_vectorAction);
  return binding ? *binding : VectorActionBinding{};
}

void TXshLevelColumn::setVectorAction(const VectorActionBinding &binding) {
  static std::atomic<std::uint64_t> nextRevision{1};
  std::shared_ptr<const VectorActionBinding> replacement;
  if (binding.action) {
    if (!binding.level || binding.level->getType() != PLI_XSHLEVEL ||
        binding.startRow < 0)
      throw TException("Invalid vector Action attachment.");
    auto revised     = binding;
    revised.revision = nextRevision++;
    replacement =
        std::make_shared<const VectorActionBinding>(std::move(revised));
  }
  std::atomic_store(&m_vectorAction, std::move(replacement));
}

TImageP TXshLevelColumn::vectorActionPose(const TXshCell &cell,
                                          const TImageP &image,
                                          double frame) const {
  const auto binding = getVectorAction();
  if (!binding.matches(cell) || frame < binding.startRow) return image;
  const double time = vectorActionTime(getXsheet(), binding.startRow, frame);
  return binding.action->pose(TVectorImageP(image), time);
}

void TXshLevelColumn::setVectorActionPreview(std::uint64_t revision, int row,
                                             const TImageP &image) {
  std::shared_ptr<const VectorActionPreview> preview;
  if (image)
    preview = std::make_shared<const VectorActionPreview>(
        VectorActionPreview{revision, row, image});
  std::atomic_store(&m_vectorActionPreview, std::move(preview));
}

TImageP TXshLevelColumn::vectorActionPreview(const VectorActionBinding &binding,
                                             int row) const {
  const auto preview = std::atomic_load(&m_vectorActionPreview);
  return preview && preview->revision == binding.revision && preview->row == row
             ? preview->image
             : TImageP();
}

namespace {
class VectorActionUndo final : public TUndo {
  TXshLevelColumnP m_column;
  TSceneHandle *m_sceneHandle;
  TXsheetHandle *m_xsheetHandle;
  ToonzScene *m_scene;
  VectorActionBinding m_before, m_after;
  QString m_label;

  void apply(const VectorActionBinding &binding) const {
    const int index = m_column->getIndex();
    auto *sheet     = m_column->getXsheet();
    if (m_sceneHandle->getScene() != m_scene || !sheet || index < 0 ||
        sheet->getColumn(index) != m_column.getPointer())
      return;
    m_column->setVectorAction(binding);
    m_sceneHandle->notifySceneChanged();
    m_xsheetHandle->notifyXsheetChanged();
  }

public:
  VectorActionUndo(TXshLevelColumn *column, const VectorActionBinding &binding,
                   TSceneHandle *sceneHandle, TXsheetHandle *xsheetHandle,
                   const QString &label)
      : m_column(column)
      , m_sceneHandle(sceneHandle)
      , m_xsheetHandle(xsheetHandle)
      , m_scene(sceneHandle->getScene())
      , m_before(column->getVectorAction())
      , m_after(binding)
      , m_label(label) {}

  void undo() const override { apply(m_before); }
  void redo() const override { apply(m_after); }
  int getSize() const override {
    std::size_t bytes = sizeof(*this);
    for (const auto &binding : {m_before, m_after}) {
      if (!binding.action) continue;
      bytes += binding.action->topology.size() * sizeof(int);
      for (const auto &track : binding.action->channels)
        bytes += sizeof(track) + 4 * sizeof(void *) +
                 track.second.size() * (sizeof(VectorAction::Keys::value_type) +
                                        4 * sizeof(void *));
    }
    return int(bytes);
  }
  QString getHistoryString() override { return m_label; }
  int getHistoryType() override { return HistoryType::Xsheet; }
};
}  // namespace

void changeVectorActionBinding(TXshLevelColumn *column,
                               const VectorActionBinding &binding,
                               TSceneHandle *sceneHandle,
                               TXsheetHandle *xsheetHandle,
                               const QString &label) {
  auto undo = std::make_unique<VectorActionUndo>(column, binding, sceneHandle,
                                                 xsheetHandle, label);
  undo->redo();
  TUndoManager::manager()->add(undo.release());
}

//-----------------------------------------------------------------------------

TXshColumn *TXshLevelColumn::clone() const {
  TXshLevelColumn *column = new TXshLevelColumn;
  column->setStatusWord(getStatusWord());
  column->setOpacity(getOpacity());
  column->m_cells = m_cells;
  column->m_first = m_first;
  column->setColorTag(getColorTag());
  column->setColorFilterId(getColorFilterId());
  column->setAMAction(getAMAction());
  column->setVectorAction(getVectorAction());

  return column;
}

//-----------------------------------------------------------------------------

void TXshLevelColumn::loadData(TIStream &is) {
  std::string tagName;
  while (is.openChild(tagName)) {
    if (tagName == "status") {
      int status = 0;
      is >> status;
      setStatusWord(status);
      if ((status & eCamstandTransparent43) != 0) {
        setOpacity(128);
        status = status & ~eCamstandTransparent43;
      }
    } else if (tagName == "camerastand_opacity") {
      int opacity = 0;
      is >> opacity;
      setOpacity(static_cast<UCHAR>(opacity));
    } else if (tagName == "filter_color_id") {
      int id = 0;
      is >> id;
      setColorFilterId(id);
    } else if (tagName == "cells") {
      while (is.openChild(tagName)) {
        if (tagName == "cell") {
          TPersist *p = nullptr;
          std::string str;
          int row = 1, rowCount = 1, increment = 0;
          TFilePath path;
          is >> row >> rowCount >> p >> str >> increment;

          TFrameId fid = qstringToFrameId(QString::fromStdString(str));
          assert((fid.getLetter().isEmpty() && rowCount >= 0) ||
                 (!fid.getLetter().isEmpty() && rowCount == 1));

          TXshLevel *xshLevel = dynamic_cast<TXshLevel *>(p);
          if (xshLevel) {
            int fidNumber = fid.getNumber();
            for (int i = 0; i < rowCount; i++) {
              TXshCell cell(xshLevel, fid);
              setCell(row++, cell);
              // rowCount > 1 => fid has no letter.
              fidNumber += increment;
              fid = TFrameId(fidNumber);
            }
          }
        } else {
          throw TException("TXshLevelColumn, unknown tag(2): " + tagName);
        }
        is.closeChild();
      }
    } else if (tagName == "vectorAction") {
      if (getVectorAction().action)
        throw TException("Duplicate vector Action attachment.");
      TPersist *persist = nullptr;
      VectorActionBinding binding;
      std::string fid, encoded;
      is >> persist >> fid >> binding.path >> binding.startRow >> encoded;
      if (encoded.size() > (VectorAction::MaxBytes + 2) / 3 * 4)
        throw TException("Vector Action exceeds the file size limit.");
      binding.level = dynamic_cast<TXshLevel *>(persist);
      binding.fid   = qstringToFrameId(QString::fromStdString(fid));
      const auto bytes =
          QByteArray::fromBase64(QByteArray::fromStdString(encoded),
                                 QByteArray::AbortOnBase64DecodingErrors);
      binding.action =
          std::make_shared<const VectorAction>(VectorAction::read(bytes));
      setVectorAction(binding);
    } else if (tagName == "amAction") {
      if (getAMAction().action)
        throw TException("A:M action: duplicate column attachment");
      TPersist *persist = nullptr;
      AMActionBinding binding;
      std::string encoded;
      is >> persist >> binding.path >> binding.startRow >> encoded;
      auto *level = dynamic_cast<TXshLevel *>(persist);
      if (!level || !level->getAMLevel() || binding.startRow < 0 ||
          encoded.size() > (otam::Limits().bytes + 2) / 3 * 4)
        throw TException("A:M action: invalid column attachment");
      const auto bytes =
          QByteArray::fromBase64(QByteArray::fromStdString(encoded),
                                 QByteArray::AbortOnBase64DecodingErrors);
      try {
        binding.action = std::make_shared<const otam::Action>(
            otam::parseAction(bytes.toStdString()));
      } catch (const std::runtime_error &e) {
        throw TException(e.what());
      }
      binding.level = level;
      setAMAction(binding);
    } else if (tagName == "fx") {
      TPersist *p = nullptr;
      is >> p;
      if (TLevelColumnFx *lcf = dynamic_cast<TLevelColumnFx *>(p)) {
        lcf->addRef();
        if (m_fx) {
          m_fx->release();
        }
        m_fx = lcf;
        lcf->setColumn(this);
      }
    } else if (tagName ==
               "fxnodes") {  // For compatibility with 1.x and earlier
      TFxSet fxSet;
      fxSet.loadData(is);
    } else if (loadCellMarks(tagName, is)) {
      // Do nothing
    } else {
      throw TException("TXshLevelColumn, unknown tag: " + tagName);
    }
    is.closeChild();
  }
}

//-----------------------------------------------------------------------------

void TXshLevelColumn::saveData(TOStream &os) {
  os.child("status") << getStatusWord();

  if (getOpacity() < 255) {
    os.child("camerastand_opacity") << static_cast<int>(getOpacity());
  }

  if (getColorFilterId() != 0) {
    os.child("filter_color_id") << static_cast<int>(getColorFilterId());
  }

  int r0 = 0, r1 = 0;
  if (getRange(r0, r1)) {
    os.openChild("cells");
    for (int r = r0; r <= r1; r++) {
      TXshCell cell = getCell(r);
      if (cell.isEmpty()) {
        continue;
      }

      TFrameId fid = cell.m_frameId;
      int n = 1, inc = 0, dr = fid.getNumber();

      // If fid has no letter, save more than one cell and its increment;
      // otherwise save one cell.
      if (r < r1 && fid.getLetter().isEmpty()) {
        TXshCell cell2 = getCell(r + 1);
        TFrameId fid2  = cell2.m_frameId;
        if (cell2.m_level.getPointer() == cell.m_level.getPointer() &&
            fid2.getLetter().isEmpty()) {
          inc = cell2.m_frameId.getNumber() - dr;
          n++;
          for (;;) {
            if (r + n > r1) {
              break;
            }
            cell2         = getCell(r + n);
            TFrameId fid2 = cell2.m_frameId;
            if (cell2.m_level.getPointer() != cell.m_level.getPointer() ||
                !fid2.getLetter().isEmpty()) {
              break;
            }
            if (fid2.getNumber() != dr + n * inc) {
              break;
            }
            n++;
          }
        }
      }

      os.child("cell") << r << n << cell.m_level.getPointer() << fid.expand()
                       << inc;
      r += n - 1;
    }
    os.closeChild();
  }

  const auto binding = getAMAction();
  if (binding.action) {
    const auto bytes =
        QByteArray::fromStdString(otam::writeAction(*binding.action));
    // Keep the native ACTIONFILE payload separate from the scene's tags.
    os.child("amAction") << binding.level.getPointer() << binding.path
                         << binding.startRow << bytes.toBase64().toStdString();
  }

  const auto vectorBinding = getVectorAction();
  if (vectorBinding.action) {
    os.child("vectorAction")
        << vectorBinding.level.getPointer() << vectorBinding.fid.expand()
        << vectorBinding.path << vectorBinding.startRow
        << vectorBinding.action->write().toBase64().toStdString();
  }

  os.child("fx") << m_fx;

  // Save cell marks
  saveCellMarks(os);
}

//-----------------------------------------------------------------------------
// Used in TCellData::getNumbers
// reservedLevel can be nonzero if the preferences option
// "LinkColumnNameWithLevel" is ON and the column name is the same as some level
// in the scene cast.
bool TXshLevelColumn::setNumbers(int row, int rowCount, const TXshCell cells[],
                                 TXshLevel *reservedLevel) {
  if (m_cells.empty() && !reservedLevel) {
    return false;
  }

  // Check availability.
  // - if source cells are all empty, do nothing
  // - also, if source or target cells contain NO_FRAME, do nothing
  bool isSrcAllEmpty = true;
  for (int i = 0; i < rowCount; i++) {
    // Checking target cells
    int currentTgtIndex = row + i - m_first;
    if (currentTgtIndex < static_cast<int>(m_cells.size())) {
      TXshCell tgtCell = m_cells[currentTgtIndex];
      if (!tgtCell.isEmpty() && tgtCell.m_frameId == TFrameId::NO_FRAME) {
        return false;
      }
    }

    // Checking source cells
    TXshCell srcCell = cells[i];
    if (!srcCell.isEmpty()) {
      if (srcCell.m_frameId == TFrameId::NO_FRAME) {
        return false;
      }
      isSrcAllEmpty = false;
    }
  }

  if (isSrcAllEmpty) {
    return false;
  }

  // Find a level to input.
  // If the first target cell is empty, search the upper cells, and lower cells
  // and use a level of first found occupied neighbor cell.
  TXshLevelP currentLevel = reservedLevel;
  int tmpIndex = std::min(row - m_first, static_cast<int>(m_cells.size()) - 1);

  // Search upper cells
  if (!currentLevel) {
    while (tmpIndex >= 0) {
      TXshCell tmpCell = m_cells[tmpIndex];
      if (!tmpCell.isEmpty() && tmpCell.m_frameId != TFrameId::NO_FRAME) {
        currentLevel = tmpCell.m_level;
        break;
      }
      tmpIndex--;
    }
  }

  // If not found any level in upper cells, then search the lower cells
  if (!currentLevel) {
    tmpIndex = std::max(row - m_first, 0);
    while (tmpIndex < static_cast<int>(m_cells.size())) {
      TXshCell tmpCell = m_cells[tmpIndex];
      if (!tmpCell.isEmpty() && tmpCell.m_frameId != TFrameId::NO_FRAME) {
        currentLevel = tmpCell.m_level;
        break;
      }
      tmpIndex++;
    }

    // In the case any level for input could not be found
    if (!currentLevel) {
      return false;
    }
  }

  // Resize the cell container
  int ra   = row;
  int rb   = row + rowCount - 1;
  int c_rb = m_first + static_cast<int>(m_cells.size()) - 1;

  if (row > c_rb) {
    int newCellCount = row - m_first + rowCount;
    m_cells.resize(newCellCount);
  } else if (row < m_first) {
    int delta = m_first - row;
    m_cells.insert(m_cells.begin(), delta, TXshCell());
    m_first = row;
  }

  if (rb > c_rb) {
    for (int i = 0; i < rb - c_rb; ++i) {
      m_cells.push_back(TXshCell());
    }
  }

  // Paste numbers
  for (int i = 0; i < rowCount; i++) {
    int dstIndex     = row - m_first + i;
    TXshCell dstCell = m_cells[dstIndex];
    TXshCell srcCell = cells[i];

    if (srcCell.isEmpty()) {
      m_cells[dstIndex] = TXshCell();
    } else {
      if (!dstCell.isEmpty()) {
        currentLevel = dstCell.m_level;
      }
      m_cells[dstIndex] = TXshCell(currentLevel, srcCell.m_frameId);
    }
  }

  // Update the cell container
  while (!m_cells.empty() && m_cells.back().isEmpty()) {
    m_cells.pop_back();
  }

  while (!m_cells.empty() && m_cells.front().isEmpty()) {
    m_cells.erase(m_cells.begin());
    m_first++;
  }

  if (m_cells.empty()) {
    m_first = 0;
  }

  return true;
}

//-----------------------------------------------------------------------------

PERSIST_IDENTIFIER(TXshLevelColumn, "levelColumn")
