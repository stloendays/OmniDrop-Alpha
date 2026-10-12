#pragma once

#include <QJsonObject>
#include <QString>

namespace omnidrop {

// Public, versioned preferences. All defaults are conservative and local-only.
// Existing QSettings history/recent-file records are separate user data.
struct AppPreferences {
  int recentFilesLimit{8};
  int activityHistoryLimit{20};
  QJsonObject toJson() const;
};

struct SettingsResult {
  bool ok{false};
  QString operation;
  QString source;
  QString error;
  AppPreferences preferences;
  QJsonObject toJson() const;
};

// Both Qt and CLI call this service, not separate configuration parsers.
// Changes use QSaveFile; every accepted mutation preserves the previous
// valid configuration in a local last-known-good snapshot.
// A corrupt or unsupported primary is never silently reset or overwritten.
class SettingsService {
 public:
  explicit SettingsService(QString filePath = {});

  SettingsResult read() const;
  SettingsResult update(const AppPreferences& preferences) const;
  SettingsResult set(const QString& key, int value) const;
  SettingsResult exportTo(const QString& absolutePath) const;
  SettingsResult importFrom(const QString& absolutePath,
                            bool explicitlyApproved) const;
  SettingsResult reset(bool explicitlyApproved) const;
  SettingsResult recover(bool explicitlyApproved) const;

  QString filePath() const { return filePath_; }

 private:
  SettingsResult save(const AppPreferences& settings,
                      const QString& operation,
                      const QString& singleKey = {}) const;
  QString filePath_;
};

}  // namespace omnidrop
