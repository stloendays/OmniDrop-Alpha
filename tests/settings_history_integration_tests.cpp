#ifdef NDEBUG
#undef NDEBUG
#endif

#include "adapters/action_history_store.hpp"
#include "adapters/recent_files_store.hpp"
#include "app/settings_service.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>

#include <cassert>

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  app.setApplicationName("OmniDrop");
  app.setOrganizationName("OmniDrop");
  QTemporaryDir temp;
  assert(temp.isValid());
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                     temp.filePath("native"));
  const QString preferencesPath = temp.filePath("settings/preferences.json");
  qputenv("OMNIDROP_SETTINGS_PATH", QFile::encodeName(preferencesPath));

  omnidrop::SettingsService service;
  assert(service.set("recent_files_limit", 12).ok);
  assert(service.set("activity_history_limit", 26).ok);

  // GUI uses the dynamic (0) constructor; other callers retaining explicit
  // limits keep existing behavior.
  omnidrop::RecentFilesStore recent(0);
  const QString folder = temp.filePath("files");
  assert(QDir().mkpath(folder));
  for (int i = 0; i < 15; ++i) {
    const QString path = folder + "/file-" + QString::number(i) + ".txt";
    QFile file(path);
    assert(file.open(QIODevice::WriteOnly));
    assert(file.write("x") == 1);
    file.close();
    recent.add(path);
  }
  assert(recent.load().size() == 12);

  // Lowering the display limit alone is non-destructive. A new file
  // enforces retention using the same QSettings key as previous versions.
  assert(service.set("recent_files_limit", 3).ok);
  assert(recent.load().size() == 3);
  QSettings native;
  assert(native.value("history/recentFiles").toStringList().size() == 12);
  const QString extra = folder + "/new.txt";
  QFile newFile(extra);
  assert(newFile.open(QIODevice::WriteOnly));
  assert(newFile.write("more") == 4);
  newFile.close();
  recent.add(extra);
  assert(recent.load().size() == 3);
  native.sync();
  assert(native.value("history/recentFiles").toStringList().size() == 3);

  omnidrop::ActionHistoryStore activity(0);
  for (int i = 0; i < 30; ++i) {
    activity.record("Test action", extra, true, QString::number(i));
  }
  assert(activity.loadDisplayEntries().size() == 26);
  assert(service.set("activity_history_limit", 4).ok);
  assert(activity.loadDisplayEntries().size() == 4);
  native.sync();
  assert(native.value("history/actions").toStringList().size() == 26);
  activity.record("Another action", extra, true, "after limit change");
  assert(activity.loadDisplayEntries().size() == 4);
  native.sync();
  assert(native.value("history/actions").toStringList().size() == 4);

  // Preference export/reset cannot remove the native history keys.
  assert(service.exportTo(temp.filePath("config-export.json")).ok);
  assert(service.reset(true).ok);
  native.sync();
  assert(native.contains("history/recentFiles"));
  assert(native.contains("history/actions"));
  return 0;
}
