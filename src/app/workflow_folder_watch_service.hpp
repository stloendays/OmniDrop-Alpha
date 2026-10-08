#pragma once

#include "app/workflow_service.hpp"

#include <QHash>
#include <QJsonObject>
#include <QSet>
#include <QString>
#include <QStringList>

#include <memory>

class QLockFile;

namespace omnidrop {

// Per-session, explicitly armed, non-recursive folder scanner.
// No filesystem events are treated as sufficient proof that a file is ready:
// a candidate must remain unchanged across two polls and pass the age gate.
// A fresh arm ignores all existing files. No automatic start after reboot.
struct FolderWatchScan {
  QStringList readyPaths;
  QString error;
  int pendingCount{0};
  bool ok() const { return error.isEmpty(); }
};

class WorkflowFolderWatchService {
 public:
  ~WorkflowFolderWatchService();
  bool arm(const QString& folder, const QJsonObject& workflow,
           QString* error = nullptr);
  FolderWatchScan scan(qint64 nowMs);
  void ignoreCreatedOutputs(const QStringList& paths);
  void stop();

  bool active() const { return !folder_.isEmpty(); }
  QString folder() const { return folder_; }
  QJsonObject workflow() const { return workflow_; }

 private:
  struct Fingerprint {
    qint64 size{0};
    qint64 modifiedMs{0};
    qint64 firstSeenMs{0};
  };

  QString folder_;
  QJsonObject workflow_;
  QHash<QString, Fingerprint> pending_;
  QSet<QString> suppressed_;
  std::unique_ptr<QLockFile> watchLock_;
};

}  // namespace omnidrop
