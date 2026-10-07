#pragma once

#include <QString>

namespace omnidrop {

enum class ActionScope {
  PerFile,
  Batch,
};

struct ActionDescriptor {
  QString id;
  QString label;
  QString description;
  int priority{100};
  bool available{false};
  QString backend;
  ActionScope scope{ActionScope::PerFile};
};

}  // namespace omnidrop
