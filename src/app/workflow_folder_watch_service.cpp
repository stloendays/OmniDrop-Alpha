#include "app/workflow_folder_watch_service.hpp"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QLockFile>
#include <QStandardPaths>

#include <utility>

namespace omnidrop {
namespace {

constexpr int kMaximumEntries = 4096;
constexpr qint64 kMaximumFileBytes = 256LL * 1024 * 1024;
constexpr qint64 kSettleMilliseconds = 1800;

bool isTemporary(const QString& filename) {
  const QString lower = filename.toLower();
  return lower.endsWith(".tmp") || lower.endsWith(".part") ||
         lower.endsWith(".crdownload") || lower.endsWith(".download") ||
         lower.endsWith(".swp") || lower.endsWith(".lock") ||
         lower.endsWith('~');
}

QFileInfoList visibleFiles(const QString& folder) {
  return QDir(folder).entryInfoList(
      QDir::Files | QDir::Readable | QDir::NoSymLinks | QDir::NoDotAndDotDot,
      QDir::Name | QDir::IgnoreCase);
}

QString normalizedFilePath(const QFileInfo& item) {
  return QDir::cleanPath(item.absoluteFilePath());
}

QString watchLockPath(const QString& folder) {
  const auto storage = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
  if (storage.isEmpty()) return {};
  const auto directory = QDir(storage).filePath("OmniDrop/workflows/watch-locks");
  if (!QDir().mkpath(directory)) return {};
#ifdef Q_OS_WIN
  const auto identity = folder.toCaseFolded().toUtf8();
#else
  const auto identity = folder.toUtf8();
#endif
  const auto digest = QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex();
  return QDir(directory).filePath(QString::fromLatin1(digest) + ".lock");
}

}  // namespace

WorkflowFolderWatchService::~WorkflowFolderWatchService() {
  stop();
}

bool WorkflowFolderWatchService::arm(
    const QString& folder, const QJsonObject& workflow, QString* error) {
  stop();
  if (error) error->clear();
  const QFileInfo selected(folder);
  if (!selected.exists() || !selected.isDir() || !selected.isReadable()) {
    if (error) *error = "Select an existing readable folder.";
    return false;
  }
  const QDir directory(selected.canonicalFilePath());
  if (!directory.exists()) {
    if (error) *error = "The selected folder could not be resolved.";
    return false;
  }

  // Each folder arrival is a separate one-file workflow job. Reject actions
  // that require a multi-file batch, rather than queueing doomed jobs.
  for (const auto& rawNode : workflow.value("nodes").toArray()) {
    if (rawNode.toObject().value("action_id").toString() == "pdf.merge") {
      if (error) *error = "Folder watching does not support multi-file PDF merging.";
      return false;
    }
  }
  const auto valid = WorkflowService{}.validate(workflow);
  if (!valid.ok) {
    if (error) *error = "Invalid local workflow: " + valid.error;
    return false;
  }

  const auto path = watchLockPath(directory.absolutePath());
  if (path.isEmpty()) {
    if (error) *error = "Cannot create local folder-watch lock directory.";
    return false;
  }
  auto exclusive = std::make_unique<QLockFile>(path);
  exclusive->setStaleLockTime(0);
  if (!exclusive->tryLock(0)) {
    if (error) *error = "This folder is already watched by another OmniDrop process.";
    return false;
  }

  const auto files = visibleFiles(directory.absolutePath());
  if (files.size() > kMaximumEntries) {
    if (error) *error = "Folder exceeds the 4,096-file watch safety limit.";
    return false;
  }

  watchLock_ = std::move(exclusive);
  folder_ = directory.absolutePath();
  workflow_ = workflow;
  // Baseline files always remain suppressed, even if their contents change
  // later. Explicit re-arming is required to start a new watch session.
  for (const auto& item : files) suppressed_.insert(normalizedFilePath(item));
  return true;
}

FolderWatchScan WorkflowFolderWatchService::scan(qint64 nowMs) {
  FolderWatchScan scan;
  if (!active()) {
    scan.error = "Folder watch is not enabled.";
    return scan;
  }
  const auto directory = QDir(folder_);
  if (!directory.exists()) {
    scan.error = "Watched folder is no longer accessible.";
    stop();
    return scan;
  }
  const auto files = visibleFiles(folder_);
  if (files.size() > kMaximumEntries) {
    scan.error = "Folder exceeds the 4,096-file watch safety limit.";
    stop();
    return scan;
  }

  QSet<QString> present;
  for (const auto& item : files) {
    const QString path = normalizedFilePath(item);
    present.insert(path);
    if (suppressed_.contains(path)) continue;
    if (isTemporary(item.fileName()) || item.size() > kMaximumFileBytes) {
      suppressed_.insert(path);
      pending_.remove(path);
      continue;
    }

    const qint64 modified = item.lastModified().toMSecsSinceEpoch();
    const qint64 size = item.size();
    auto it = pending_.find(path);
    if (it == pending_.end() || it->size != size ||
        it->modifiedMs != modified) {
      pending_.insert(path, Fingerprint{size, modified, nowMs});
      continue;
    }
    const bool unchangedLongEnough = nowMs - it->firstSeenMs >= kSettleMilliseconds;
    const bool lastWriteOldEnough = nowMs - modified >= kSettleMilliseconds;
    if (!unchangedLongEnough || !lastWriteOldEnough) continue;

    // On Windows this will fail while a writer holds an exclusive lock.
    // On other platforms stable mtime+length provides a conservative gate.
    QFile candidate(path);
    if (!candidate.open(QIODevice::ReadOnly)) continue;
    candidate.close();

    suppressed_.insert(path);
    pending_.remove(path);
    scan.readyPaths.append(path);
  }
  for (auto it = pending_.begin(); it != pending_.end();) {
    if (!present.contains(it.key())) it = pending_.erase(it);
    else ++it;
  }
  scan.pendingCount = pending_.size();
  return scan;
}

void WorkflowFolderWatchService::ignoreCreatedOutputs(const QStringList& paths) {
  if (!active()) return;
  for (const auto& path : paths) {
    const QFileInfo item(path);
    if (item.absoluteDir().canonicalPath() == folder_) {
      const QString clean = normalizedFilePath(item);
      suppressed_.insert(clean);
      pending_.remove(clean);
    }
  }
}

void WorkflowFolderWatchService::stop() {
  if (watchLock_) {
    watchLock_->unlock();
    watchLock_.reset();
  }
  folder_.clear();
  workflow_ = {};
  pending_.clear();
  suppressed_.clear();
}

}  // namespace omnidrop
