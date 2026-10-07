#include "adapters/recent_files_store.hpp"

#include <QFileInfo>
#include <QSettings>

namespace omnidrop {
namespace {
constexpr auto kRecentFilesKey = "history/recentFiles";
}

RecentFilesStore::RecentFilesStore(int maxEntries) : maxEntries_(maxEntries) {}

QStringList RecentFilesStore::load() const {
  QSettings settings;
  const auto stored = settings.value(kRecentFilesKey).toStringList();
  QStringList existing;
  for (const auto& path : stored) {
    const QFileInfo info(path);
    if (info.exists() && info.isFile()) existing.push_back(info.absoluteFilePath());
    if (existing.size() >= maxEntries_) break;
  }
  if (existing != stored) settings.setValue(kRecentFilesKey, existing);
  return existing;
}

void RecentFilesStore::add(const QString& path) const {
  const QFileInfo info(path);
  if (!info.exists() || !info.isFile()) return;

  auto recent = load();
  const auto normalized = info.absoluteFilePath();
  recent.removeAll(normalized);
  recent.prepend(normalized);
  while (recent.size() > maxEntries_) recent.removeLast();

  QSettings settings;
  settings.setValue(kRecentFilesKey, recent);
}

void RecentFilesStore::clear() const {
  QSettings settings;
  settings.remove(kRecentFilesKey);
}

}  // namespace omnidrop
