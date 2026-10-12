#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>

namespace omnidrop {

// Read-only release metadata. Never supplies an arbitrary install command or
// local path, and never causes a binary to be downloaded or executed.
struct UpdateCheckResult {
  bool ok{false};
  QString state{"error"};  // update_available | up_to_date | no_release | error
  QString channel{"alpha"};
  QString installedVersion;
  QString availableVersion;
  QString releaseUrl;
  QString error;
  bool prerelease{false};

  QJsonObject toJson() const;
};

class UpdateCheckService {
 public:
  // Pure validation and version selection. Network transport is an adapter.
  // Only official GitHub releases with the expected ZIP and .sha256 asset
  // metadata are considered eligible for announcement.
  static UpdateCheckResult evaluate(
      const QByteArray& releasesJson,
      const QString& installedVersion,
      const QString& channel);
  static bool validChannel(const QString& channel);
};

}  // namespace omnidrop
