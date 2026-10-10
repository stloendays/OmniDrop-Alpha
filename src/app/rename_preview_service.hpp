#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

namespace omnidrop {

// A deliberately read-only operation. This release does not expose file rename
// execution, and a preview must never imply that a rename has been committed.
struct RenamePreviewOptions {
  QString find;
  QString replacement;
  bool useRegex{false};
  bool caseSensitive{true};
  bool includeExtension{false};
};

struct RenamePreviewRow {
  QString sourcePath;
  QString currentName;
  QString proposedName;
  QString targetPath;
  QString status;  // unchanged, ready, conflict
  QString reason;
};

struct RenamePreviewResult {
  bool ok{false};
  QString error;
  QList<RenamePreviewRow> rows;
  int proposedChangeCount{0};
  int readyCount{0};
  int conflictCount{0};

  QJsonObject toJson() const;
};

class RenamePreviewService {
 public:
  // Inspect 1..128 existing regular files in caller-defined order. No writes,
  // moves, directory creation, shell execution, or network access.
  RenamePreviewResult preview(
      const QStringList& selectedPaths, const RenamePreviewOptions& options) const;
};

}  // namespace omnidrop
