#pragma once

#include <QColor>

#include <array>
#include <map>
#include <optional>
#include <vector>

class QWidget;

// The editor uses scalar curves for display. A track owns edits to the
// underlying grouped keys, including their validation, notifications and undo.
class FunctionKeyTrack {
public:
  using Value = std::array<double, 4>;

  struct Snapshot {
    std::map<double, Value> keys;  // Xsheet frames, including subframes.
    Value defaults{};
    bool available = true;
  };

  struct Edit {
    std::optional<double> from;   // Empty when creating a key.
    std::optional<double> to;     // Empty when deleting a key.
    std::optional<double> value;  // Empty to keep the complete original value.
  };

  virtual ~FunctionKeyTrack()   = default;
  virtual Snapshot read() const = 0;
  virtual bool canEdit() const  = 0;
  virtual QColor swatch() const { return {}; }
  virtual bool canReload() const { return false; }
  virtual void reload(QWidget *) {}
  virtual bool edit(int component, const std::vector<Edit> &edits) = 0;
};
