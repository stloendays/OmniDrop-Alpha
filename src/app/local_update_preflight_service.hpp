#pragma once

#include <QJsonObject>
#include <QString>

namespace omnidrop {

// A local-only, read-only ZIP preflight. Success means the file matches the
// accompanying checksum and its ZIP directory is structurally acceptable.
// It DOES NOT authenticate the publisher, verify GitHub provenance, extract,
// stage, install, execute, or replace any file.
struct LocalUpdatePreflightResult {
  bool ok{false};
  QString state{"blocked"};
  QString packageVersion;
  QString archiveSha256;
  QString error;
  qint64 archiveBytes{0};
  int entryCount{0};
  bool targetSafe{false};
  QJsonObject toJson() const;
};

class LocalUpdatePreflightService {
 public:
  // installDirectory defaults to the directory of the running application.
  // Any .git directory/file or symlink in the target ancestry fails closed.
  static LocalUpdatePreflightResult verify(const QString& zipPath,
                                           const QString& checksumPath,
                                           const QString& installDirectory = {});
  static QString validateInstallDirectory(const QString& installDirectory);
};

}  // namespace omnidrop
