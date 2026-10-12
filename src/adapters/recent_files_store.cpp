#include "adapters/recent_files_store.hpp"
#include "app/settings_service.hpp"

#include <QFileInfo>
#include <QSettings>

namespace omnidrop {
namespace {
constexpr auto kRecentFilesKey = "history/recentFiles";
}

RecentFilesStore::RecentFilesStore(int maxEntries) : maxEntries_(maxEntries) {}

int RecentFilesStore::effectiveLimit() const {
  if (maxEntries_ > 0) return maxEntries_;
  const auto settings = SettingsService{}.read();
  return settings.ok ? settings.preferences.recentFilesLimit : 8;
}

QStringList RecentFilesStore::load() const {
  QSettings settings;
  const auto stored = settings.value(kRecentFilesKey).toStringList();
  QStringList existing;
  const int limit = effectiveLimit();
  for (const auto& path : stored) {
    const QFileInfo info(path);
    if (info.exists() && info.isFile()) existing.push_back(info.absoluteFilePath());
    if (existing.size() >= limit) break;
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
  while (recent.size() > effectiveLimit()) recent.removeLast();

  QSettings settings;
  settings.setValue(kRecentFilesKey, recent);
}

void RecentFilesStore::clear() const {
  QSettings settings;
  settings.remove(kRecentFilesKey);
}

}  // namespace omnidrop
