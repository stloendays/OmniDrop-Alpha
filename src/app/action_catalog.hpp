#pragma once

#include "domain/action.hpp"
#include "domain/file_kind.hpp"

#include <QList>

namespace omnidrop {

class ActionCatalog {
 public:
  QList<ActionDescriptor> recommendedActions(FileKind kind) const;
  QList<ActionDescriptor> recommendedBatchActions(const QList<FileKind>& kinds) const;
};

}  // namespace omnidrop
