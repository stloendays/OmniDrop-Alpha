#pragma once

#include "app/action_catalog.hpp"

#include <QList>
#include <QSet>
#include <QString>

namespace omnidrop {

struct FileInspection {
  QString path;
  QString name;
  qint64 sizeBytes{0};
  FileKind kind{FileKind::Other};
  QList<ActionDescriptor> actions;
};

class OmniDropService {
 public:
  FileInspection inspect(const QString& path) const;
  FileInspection inspect(const QString& path, const QSet<QString>& availableActionIds) const;

 private:
  FileInspection inspectStatic(const QString& path) const;
  ActionCatalog catalog_;
};

}  // namespace omnidrop
