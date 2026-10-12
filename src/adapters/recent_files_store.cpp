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
  for (const auto& path : stored) {
    const QFileInfo info(path);
    if (info.exists() && info.isFile()) existing.push_back(info.absoluteFilePath());
  }
  if (existing != stored) settings.setValue(kRecentFilesKey, existing);
  // Display limits must not silently discard healthy older entries just
  // because the user reduced the preference. New file additions still
  // enforce the configured retention cap.
  return existing.mid(0, effectiveLimit());
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
