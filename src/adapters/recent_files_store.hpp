#pragma once

#include <QString>
#include <QStringList>

namespace omnidrop {

class RecentFilesStore {
 public:
  explicit RecentFilesStore(int maxEntries = 8);

  QStringList load() const;
  void add(const QString& path) const;
  void clear() const;

 private:
  int effectiveLimit() const;
  int maxEntries_{8};
};

}  // namespace omnidrop
