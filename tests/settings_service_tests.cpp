#ifdef NDEBUG
#undef NDEBUG
#endif

#include "app/settings_service.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QTemporaryDir>

#include <cassert>

namespace {

void writeBytes(const QString& path, const QByteArray& bytes) {
  QFile file(path);
  assert(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
  assert(file.write(bytes) == bytes.size());
  file.close();
}

QByteArray readBytes(const QString& path) {
  QFile file(path);
  assert(file.open(QIODevice::ReadOnly));
  return file.readAll();
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QTemporaryDir temporary;
  assert(temporary.isValid());
  const QString dir = temporary.path();
  const QString file = dir + "/config/settings.json";
  const QString exportPath = dir + "/settings-export.json";
  const QString unrelated = dir + "/jobs.json";
  assert(QDir().mkpath(dir + "/config"));
  writeBytes(unrelated, "Do not touch saved jobs.");

  const omnidrop::SettingsService settings(file);
  auto result = settings.read();
  assert(result.ok && result.source == "defaults");
  assert(result.preferences.recentFilesLimit == 8);
  assert(result.preferences.activityHistoryLimit == 20);
  assert(!QFile::exists(file));

  assert(!settings.set("unknown", 10).ok);
  assert(!settings.set("recent_files_limit", 0).ok);
  assert(!settings.set("recent_files_limit", 33).ok);
  assert(!settings.set("activity_history_limit", 101).ok);
  assert(!settings.reset(false).ok);
  assert(!settings.recover(false).ok);
  assert(!QFile::exists(file));

  // Settings are additive; changing one key must preserve the other.
  result = settings.set("recent_files_limit", 12);
  assert(result.ok && result.preferences.recentFilesLimit == 12);
  assert(QFile::exists(file));
  assert(QFile::exists(file + ".good.json"));
  result = settings.set("activity_history_limit", 36);
  assert(result.ok);
  assert(result.preferences.recentFilesLimit == 12);
  assert(result.preferences.activityHistoryLimit == 36);
  const auto persisted = omnidrop::SettingsService(file).read();
  assert(persisted.ok && persisted.source == "file");
  assert(persisted.preferences.recentFilesLimit == 12);

  // The exported file must contain only allowlisted preferences, never
  // native QSettings history or unknown future credentials.
  result = settings.exportTo(exportPath);
  assert(result.ok);
  const auto bytes = readBytes(exportPath);
  const auto backupDoc = QJsonDocument::fromJson(bytes);
  assert(backupDoc.isObject());
  assert(backupDoc.object().value("schema_version").toInt() == 1);
  assert(backupDoc.object().size() == 2);
  assert(!bytes.contains("history/recentFiles"));
  assert(!bytes.contains("history/actions"));
  assert(!settings.exportTo(exportPath).ok);
  assert(readBytes(exportPath) == bytes);

  omnidrop::AppPreferences alternate;
  alternate.recentFilesLimit = 4;
  alternate.activityHistoryLimit = 7;
  assert(settings.update(alternate).ok);
  assert(!settings.importFrom(exportPath, false).ok);
  result = settings.read();
  assert(result.ok && result.preferences.recentFilesLimit == 4);

  // Import replaces both preference fields but not another app data file.
  result = settings.importFrom(exportPath, true);
  assert(result.ok);
  assert(result.preferences.recentFilesLimit == 12);
  assert(result.preferences.activityHistoryLimit == 36);
  assert(readBytes(unrelated) == "Do not touch saved jobs.");

  // Recover deliberately reverts to the previous valid configuration.
  result = settings.recover(true);
  assert(result.ok && result.source == "last-known-good");
  assert(result.preferences.recentFilesLimit == 4);
  assert(result.preferences.activityHistoryLimit == 7);

  // An invalid/unknown input must not be accepted or partially applied.
  const QString invalid = dir + "/unsupported.json";
  writeBytes(invalid, "{\"schema_version\":2,\"preferences\":{}}");
  const QByteArray before = readBytes(file);
  assert(!settings.importFrom(invalid, true).ok);
  assert(readBytes(file) == before);
  writeBytes(invalid, "{\"schema_version\":1,\"preferences\":{"
      "\"recent_files_limit\":8,\"activity_history_limit\":20,\"token\":\"secret\"}}");
  assert(!settings.importFrom(invalid, true).ok);
  assert(readBytes(file) == before);

  // Reset takes explicit approval and preserves the previous valid snapshot.
  assert(!settings.reset(false).ok);
  result = settings.reset(true);
  assert(result.ok);
  assert(result.preferences.recentFilesLimit == 8);
  assert(result.preferences.activityHistoryLimit == 20);

  // Corrupt primary must not be automatically rewritten by read/set/import.
  // Manual recovery preserves the damaged bytes separately.
  writeBytes(file, "{invalid JSON");
  result = settings.read();
  assert(!result.ok);
  assert(result.preferences.recentFilesLimit == 8);
  assert(!settings.set("recent_files_limit", 16).ok);
  assert(!settings.importFrom(exportPath, true).ok);
  assert(!settings.reset(true).ok);
  assert(readBytes(file) == "{invalid JSON");
  result = settings.recover(true);
  assert(result.ok && result.preferences.recentFilesLimit == 4);
  assert(result.preferences.activityHistoryLimit == 7);
  assert(settings.read().ok);
  QDir configDir(dir + "/config");
  const auto preserved = configDir.entryList({"settings.json.corrupt-*.json"}, QDir::Files);
  assert(preserved.size() == 1);
  assert(readBytes(configDir.filePath(preserved.first())) == "{invalid JSON");

  // If the last-good backup is itself damaged, recovery refuses mutation.
  writeBytes(file, "{damaged again");
  writeBytes(file + ".good.json", "{broken backup");
  assert(!settings.recover(true).ok);
  assert(readBytes(file) == "{damaged again");

  // Cross-process writes are serialized (the same lock name as the service).
  writeBytes(file, bytes);
  QLockFile lock(file + ".lock");
  lock.setStaleLockTime(30000);
  assert(lock.tryLock(0));
  assert(!settings.set("recent_files_limit", 16).ok);
  lock.unlock();
  assert(settings.set("recent_files_limit", 16).ok);
  assert(settings.read().preferences.recentFilesLimit == 16);
  assert(readBytes(unrelated) == "Do not touch saved jobs.");
  return 0;
}
