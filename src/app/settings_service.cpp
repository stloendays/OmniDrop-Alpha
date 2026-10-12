#include "app/settings_service.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLockFile>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>

#include <cmath>
#include <utility>

namespace omnidrop {
namespace {

constexpr qint64 kMaxSettingsBytes = 8192;
constexpr qint64 kMaxCorruptCopyBytes = 65536;

struct LoadedSettings {
  bool exists{false};
  bool valid{false};
  AppPreferences preferences;
  QString error;
  QByteArray bytes;
};

bool validRange(const AppPreferences& values) {
  return values.recentFilesLimit >= 1 &&
         values.recentFilesLimit <= 32 &&
         values.activityHistoryLimit >= 1 &&
         values.activityHistoryLimit <= 100;
}

bool readInteger(const QJsonObject& object, const char* key, int* value) {
  const auto field = object.value(QLatin1String(key));
  if (!field.isDouble()) return false;
  const double number = field.toDouble();
  if (!std::isfinite(number) || std::floor(number) != number) return false;
  *value = field.toInt(-1);
  return *value >= 0;
}

QJsonObject settingsDocument(const AppPreferences& values) {
  return QJsonObject{
      {"schema_version", 1},
      {"preferences", values.toJson()},
  };
}

QByteArray serializeSettings(const AppPreferences& values) {
  return QJsonDocument(settingsDocument(values)).toJson(QJsonDocument::Indented);
}

LoadedSettings loadSettings(const QString& path) {
  LoadedSettings result;
  const QFileInfo info(path);
  if (info.isSymLink()) {
    result.exists = true;
    result.error = "Refusing a symbolic-link settings file.";
    return result;
  }
  if (!info.exists()) {
    result.valid = true;
    return result;
  }
  result.exists = true;
  if (!info.isFile() || info.size() > kMaxSettingsBytes) {
    result.error = "Settings file is not a regular file or exceeds 8 KiB.";
    return result;
  }
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    result.error = "Settings file cannot be opened.";
    return result;
  }
  result.bytes = file.read(kMaxSettingsBytes + 1);
  if (result.bytes.size() > kMaxSettingsBytes) {
    result.error = "Settings file exceeds 8 KiB.";
    return result;
  }
  QJsonParseError parseError;
  const auto parsed = QJsonDocument::fromJson(result.bytes, &parseError);
  if (parseError.error != QJsonParseError::NoError || !parsed.isObject()) {
    result.error = "Settings JSON is damaged. Recover from the last good copy.";
    return result;
  }
  const auto root = parsed.object();
  if (root.size() != 2 ||
      root.value("schema_version").toInt(-1) != 1 ||
      !root.value("preferences").isObject()) {
    result.error = "Unsupported settings schema or unknown fields; no migration was attempted.";
    return result;
  }
  const auto preferences = root.value("preferences").toObject();
  AppPreferences values;
  if (preferences.size() != 2 ||
      !readInteger(preferences, "recent_files_limit", &values.recentFilesLimit) ||
      !readInteger(preferences, "activity_history_limit", &values.activityHistoryLimit) ||
      !validRange(values)) {
    result.error = "Settings values are missing, out of range or unsupported.";
    return result;
  }
  result.valid = true;
  result.preferences = values;
  return result;
}

QString writeAtomic(const QString& path, const QByteArray& payload) {
  if (!QDir::isAbsolutePath(path)) {
    return "Settings destination must be an absolute path.";
  }
  const QFileInfo info(path);
  if (info.isSymLink() || (info.exists() && !info.isFile())) {
    return "Refusing a symlink or non-file settings destination.";
  }
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly)) {
    return "Cannot open the settings destination for atomic writing.";
  }
  if (file.write(payload) != payload.size()) {
    file.cancelWriting();
    return "Cannot write the complete settings document.";
  }
  if (!file.commit()) {
    return "Cannot atomically finish the settings write.";
  }
  QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
  return {};
}

SettingsResult errorResult(const QString& action, const QString& message) {
  SettingsResult result;
  result.operation = action;
  result.error = message;
  return result;
}

SettingsResult successResult(const QString& action,
                             const QString& source,
                             const AppPreferences& preferences) {
  SettingsResult result;
  result.ok = true;
  result.operation = action;
  result.source = source;
  result.preferences = preferences;
  return result;
}

}  // namespace

QJsonObject AppPreferences::toJson() const {
  return QJsonObject{
      {"recent_files_limit", recentFilesLimit},
      {"activity_history_limit", activityHistoryLimit},
  };
}

QJsonObject SettingsResult::toJson() const {
  return QJsonObject{
      {"schema_version", 1},
      {"operation", operation},
      {"ok", ok},
      {"source", source},
      {"settings", preferences.toJson()},
      {"error", error},
  };
}

SettingsService::SettingsService(QString filePath)
    : filePath_(std::move(filePath)) {
  if (filePath_.isEmpty()) {
    filePath_ = qEnvironmentVariable("OMNIDROP_SETTINGS_PATH");
  }
  if (filePath_.isEmpty()) {
    // GenericConfigLocation is independent of applicationName. The GUI uses
    // "OmniDrop" while the CLI identifies as "OmniDrop CLI".
    filePath_ = QDir(QStandardPaths::writableLocation(
        QStandardPaths::GenericConfigLocation))
                    .filePath("OmniDrop/settings.json");
  }
}

SettingsResult SettingsService::read() const {
  if (!QDir::isAbsolutePath(filePath_))
    return errorResult("settings.get", "Settings path must be absolute.");
  const auto loaded = loadSettings(filePath_);
  if (!loaded.valid) return errorResult("settings.get", loaded.error);
  return successResult("settings.get",
                       loaded.exists ? "file" : "defaults",
                       loaded.preferences);
}

SettingsResult SettingsService::set(const QString& key, int value) const {
  AppPreferences update;
  if (key == "recent_files_limit") update.recentFilesLimit = value;
  else if (key == "activity_history_limit") update.activityHistoryLimit = value;
  else return errorResult("settings.set", "Unknown preference name.");
  if (value < 1 ||
      (key == "recent_files_limit" && value > 32) ||
      (key == "activity_history_limit" && value > 100)) {
    return errorResult("settings.set", "Value is outside the allowed range.");
  }
  return save(update, "settings.set", key);
}

SettingsResult SettingsService::save(const AppPreferences& preferences,
                                      const QString& operation,
                                      const QString& singleKey) const {
  if (!QDir::isAbsolutePath(filePath_))
    return errorResult(operation, "Settings path must be absolute.");
  const QString directory = QFileInfo(filePath_).absolutePath();
  if (!QDir().mkpath(directory))
    return errorResult(operation, "Cannot create the private settings directory.");
  QLockFile lock(filePath_ + ".lock");
  lock.setStaleLockTime(30000);
  if (!lock.tryLock(0))
    return errorResult(operation, "Settings are busy in another process.");
  const auto current = loadSettings(filePath_);
  if (!current.valid) {
    return errorResult(operation,
                       current.error + " Refusing to overwrite; use settings recover.");
  }

  AppPreferences next = preferences;
  if (singleKey == "recent_files_limit") {
    next = current.preferences;
    next.recentFilesLimit = preferences.recentFilesLimit;
  } else if (singleKey == "activity_history_limit") {
    next = current.preferences;
    next.activityHistoryLimit = preferences.activityHistoryLimit;
  }
  if (!validRange(next))
    return errorResult(operation, "Settings values exceed allowed limits.");

  // The previous valid snapshot must be durable *before* the new file is
  // committed, so an interrupted or corrupted write can be rolled back.
  const auto backupError =
      writeAtomic(filePath_ + ".good.json", serializeSettings(current.preferences));
  if (!backupError.isEmpty()) return errorResult(operation, backupError);
  const auto error = writeAtomic(filePath_, serializeSettings(next));
  if (!error.isEmpty()) return errorResult(operation, error);
  return successResult(operation, "file", next);
}

SettingsResult SettingsService::exportTo(const QString& absolutePath) const {
  const auto current = read();
  if (!current.ok) return errorResult("settings.export", current.error);
  if (!QDir::isAbsolutePath(absolutePath))
    return errorResult("settings.export", "Export destination must be absolute.");
  const QString destination = QDir::cleanPath(absolutePath);
  if (destination == QDir::cleanPath(filePath_) ||
      destination == QDir::cleanPath(filePath_ + ".good.json")) {
    return errorResult("settings.export", "Cannot export over the live settings or recovery copy.");
  }
  const QFileInfo info(destination);
  if (info.exists() || info.isSymLink())
    return errorResult("settings.export", "Export file already exists; select a new name.");
  if (!QDir(info.absolutePath()).exists())
    return errorResult("settings.export", "Export folder does not exist.");
  const auto error = writeAtomic(destination, serializeSettings(current.preferences));
  if (!error.isEmpty()) return errorResult("settings.export", error);
  return successResult("settings.export", current.source, current.preferences);
}

SettingsResult SettingsService::importFrom(const QString& absolutePath,
                                           bool explicitlyApproved) const {
  if (!explicitlyApproved)
    return errorResult("settings.import", "Import requires explicit --confirm approval.");
  if (!QDir::isAbsolutePath(absolutePath))
    return errorResult("settings.import", "Import file path must be absolute.");
  const auto imported = loadSettings(absolutePath);
  if (!imported.exists || !imported.valid)
    return errorResult("settings.import", imported.error.isEmpty()
        ? "Import file does not exist." : imported.error);
  return save(imported.preferences, "settings.import");
}

SettingsResult SettingsService::reset(bool explicitlyApproved) const {
  if (!explicitlyApproved)
    return errorResult("settings.reset", "Reset requires explicit --confirm approval.");
  return save(AppPreferences{}, "settings.reset");
}

SettingsResult SettingsService::recover(bool explicitlyApproved) const {
  if (!explicitlyApproved)
    return errorResult("settings.recover", "Recovery requires explicit --confirm approval.");
  if (!QDir::isAbsolutePath(filePath_))
    return errorResult("settings.recover", "Settings path must be absolute.");
  QLockFile lock(filePath_ + ".lock");
  lock.setStaleLockTime(30000);
  if (!lock.tryLock(0))
    return errorResult("settings.recover", "Settings are busy in another process.");

  const auto backup = loadSettings(filePath_ + ".good.json");
  if (!backup.exists || !backup.valid)
    return errorResult("settings.recover", backup.error.isEmpty()
        ? "No valid last-known-good settings are available." : backup.error);

  const auto primary = loadSettings(filePath_);
  if (primary.exists && !primary.valid) {
    const QFileInfo info(filePath_);
    if (info.isSymLink() || !info.isFile() ||
        info.size() > kMaxCorruptCopyBytes) {
      return errorResult("settings.recover",
                         "Cannot safely preserve the damaged settings file. Move it aside manually.");
    }
    QFile damaged(filePath_);
    if (!damaged.open(QIODevice::ReadOnly))
      return errorResult("settings.recover", "Cannot preserve unreadable damaged settings.");
    const QByteArray evidence = damaged.readAll();
    const QString suffix = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString preserved = filePath_ + ".corrupt-" + suffix + ".json";
    const auto evidenceError = writeAtomic(preserved, evidence);
    if (!evidenceError.isEmpty()) return errorResult("settings.recover", evidenceError);
  }
  const auto error = writeAtomic(filePath_, serializeSettings(backup.preferences));
  if (!error.isEmpty()) return errorResult("settings.recover", error);
  return successResult("settings.recover", "last-known-good", backup.preferences);
}

}  // namespace omnidrop
