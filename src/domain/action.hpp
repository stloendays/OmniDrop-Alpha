#pragma once

#include <QString>

namespace omnidrop {

struct ActionDescriptor {
  QString id;
  QString label;
  QString description;
  int priority{100};
  bool available{false};
  QString backend;
};

}  // namespace omnidrop
