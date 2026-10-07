#pragma once

#include "app/action_catalog.hpp"

#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

namespace omnidrop {

struct FileInspection {
  QString path;
  QString name;
  qint64 sizeBytes{0};
  FileKind kind{FileKind::Other};
  QList<ActionDescriptor> actions;
};

struct BatchInspection {
  QStringList paths;
  QList<FileInspection> files;
  qint64 totalBytes{0};
  QList<ActionDescriptor> commonActions;
  QList<ActionDescriptor> actions;
};

class OmniDropService {
 public:
  FileInspection inspect(const QString& path) const;
  FileInspection inspect(const QString& path, const QSet<QString>& availableActionIds) const;
  BatchInspection inspectMany(const QStringList& paths,
                              const QSet<QString>& availableActionIds = {}) const;

 private:
  FileInspection inspectStatic(const QString& path) const;
  ActionCatalog catalog_;
};

}  // namespace omnidrop
