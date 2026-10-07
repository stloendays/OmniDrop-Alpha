#pragma once

#include <QString>
#include <QStringList>

namespace omnidrop {

class ActionHistoryStore {
 public:
  explicit ActionHistoryStore(int maxEntries = 20);

  QStringList loadDisplayEntries() const;
  void record(const QString& actionLabel, const QString& sourcePath, bool success,
              const QString& detail) const;
  void clear() const;

 private:
  int maxEntries_{20};
};

}  // namespace omnidrop
