#include "app/rename_transaction_service.hpp"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QVector>
#include <QStandardPaths>
#include <QUuid>

#include <string>
#include <utility>

#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#elif defined(Q_OS_LINUX)
#include <cerrno>
#include <fcntl.h>
#include <linux/fs.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace omnidrop {
namespace {

constexpr qint64 kMaxInputBytes = 512LL * 1024 * 1024;
constexpr qint64 kMaxJournalBytes = 1024 * 1024;
constexpr int kMaxJournalCount = 500;
constexpr int kMaxFiles = 128;
const QRegularExpression kTransactionId(
    QStringLiteral("^[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}$"));
const QRegularExpression kSha256(QStringLiteral("^[0-9a-f]{64}$"));

QString nowUtc() {
  return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

QString journalPath(const QString& directory, const QString& id) {
  return QDir(directory).filePath(id + ".json");
}

bool locked(const QString& directory, QLockFile& lock, QString* error) {
  if (!QDir::isAbsolutePath(directory)) {
    *error = "Rename journal storage must use an absolute local directory.";
    return false;
  }
  if (!QDir().mkpath(directory)) {
    *error = "Cannot create the private rename journal directory.";
    return false;
  }
  lock.setStaleLockTime(0);
  if (!lock.tryLock(5000)) {
    *error = "Another OmniDrop rename transaction is active.";
    return false;
  }
  return true;
}

bool occupied(const QString& path) {
  const QFileInfo entry(path);
  return entry.exists() || entry.isSymLink();
}

struct Fingerprint {
  qint64 size{-1};
  qint64 modifiedMs{-1};
  QString sha256;
};

bool fingerprint(const QString& path, Fingerprint* result, QString* error) {
  const QFileInfo before(path);
  if (!before.isFile() || before.isSymLink() || before.size() < 0) {
    *error = "A source file is missing, inaccessible or is a symbolic link.";
    return false;
  }
  if (before.size() > kMaxInputBytes) {
    *error = "A file exceeds the 512 MiB transaction limit.";
    return false;
  }
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    *error = "Cannot open a file for verification.";
    return false;
  }
  QCryptographicHash digest(QCryptographicHash::Sha256);
  QByteArray buffer(1024 * 1024, '\0');
  while (true) {
    const qint64 bytes = file.read(buffer.data(), buffer.size());
    if (bytes < 0) {
      *error = "Cannot finish hashing a selected file.";
      return false;
    }
    if (bytes == 0) break;
    digest.addData(buffer.constData(), bytes);
  }
  file.close();
  const QFileInfo after(path);
  if (!after.isFile() || after.isSymLink() ||
      before.size() != after.size() ||
      before.lastModified().toMSecsSinceEpoch() !=
          after.lastModified().toMSecsSinceEpoch()) {
    *error = "A file changed during verification; review and prepare again.";
    return false;
  }
  result->size = after.size();
  result->modifiedMs = after.lastModified().toMSecsSinceEpoch();
  result->sha256 = QString::fromLatin1(digest.result().toHex());
  return true;
}

bool matchesFingerprint(const QString& path, const QJsonObject& record,
                        QString* error) {
  const auto canonicalParent = QDir(QFileInfo(path).absolutePath()).canonicalPath();
  if (canonicalParent.isEmpty() ||
      canonicalParent != record.value("parent_canonical").toString()) {
    *error = "A parent directory changed since the plan was prepared.";
    return false;
  }
  Fingerprint actual;
  if (!fingerprint(path, &actual, error)) return false;
  if (actual.size != static_cast<qint64>(record.value("size_bytes").toDouble(-1)) ||
      actual.modifiedMs !=
          static_cast<qint64>(record.value("modified_ms").toDouble(-1)) ||
      actual.sha256 != record.value("sha256").toString()) {
    *error = "A file changed since its rename plan was prepared. No overwrite attempted.";
    return false;
  }
  return true;
}

bool moveNoReplace(const QString& source, const QString& target, QString* error) {
  if (occupied(target)) {
    *error = "A target filename is already in use; no file was overwritten.";
    return false;
  }
#ifdef Q_OS_WIN
  const std::wstring from = QDir::toNativeSeparators(source).toStdWString();
  const std::wstring to = QDir::toNativeSeparators(target).toStdWString();
  // Do not set MOVEFILE_REPLACE_EXISTING or MOVEFILE_COPY_ALLOWED.
  if (!MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_WRITE_THROUGH)) {
    *error = "Windows refused an exclusive same-directory rename.";
    return false;
  }
  return true;
#elif defined(Q_OS_LINUX) && defined(SYS_renameat2)
  const auto from = QFile::encodeName(source);
  const auto to = QFile::encodeName(target);
  // Kernel-enforced RENAME_NOREPLACE closes target-creation TOCTOU windows.
  if (syscall(SYS_renameat2, AT_FDCWD, from.constData(),
              AT_FDCWD, to.constData(), RENAME_NOREPLACE) != 0) {
    *error = errno == EEXIST
                 ? "A destination already exists; no file was overwritten."
                 : "The operating system refused a no-replace file rename.";
    return false;
  }
  return true;
#else
  *error = "No verified atomic no-replace rename primitive on this platform.";
  return false;
#endif
}

QJsonArray displayRows(const QJsonArray& records) {
  QJsonArray visible;
  for (const auto& value : records) {
    const QJsonObject item = value.toObject();
    visible.append(QJsonObject{
        {"source_path", item.value("source")},
        {"target_path", item.value("target")},
        {"size_bytes", item.value("size_bytes")},
    });
  }
  return visible;
}

RenameTransactionResult report(const QJsonObject& document,
                               bool ok = true, const QString& error = {}) {
  return RenameTransactionResult{
      ok, document.value("id").toString(), document.value("state").toString(),
      error, displayRows(document.value("rows").toArray()),
  };
}

RenameTransactionResult failure(const QString& error, const QString& id = {}) {
  return RenameTransactionResult{false, id, {}, error, {}};
}

QString writeJournal(const QString& directory, const QJsonObject& document) {
  const auto payload = QJsonDocument(document).toJson(QJsonDocument::Compact);
  if (payload.size() > kMaxJournalBytes)
    return "Rename transaction record is larger than 1 MiB.";
  QSaveFile target(journalPath(directory, document.value("id").toString()));
  if (!target.open(QIODevice::WriteOnly))
    return "Cannot open the rename journal for atomic writing.";
  if (target.write(payload) != payload.size()) {
    target.cancelWriting();
    return "Cannot write the complete rename journal.";
  }
  if (!target.commit())
    return "Cannot atomically commit the rename journal.";
  QFile::setPermissions(target.fileName(),
                        QFileDevice::ReadOwner | QFileDevice::WriteOwner);
  return {};
}

QJsonObject readJournal(const QString& directory, const QString& id,
                        QString* error) {
  if (!QDir::isAbsolutePath(directory)) {
    *error = "Rename journal storage must use an absolute local directory.";
    return {};
  }
  if (!kTransactionId.match(id).hasMatch()) {
    *error = "Invalid rename transaction ID.";
    return {};
  }
  const QString location = journalPath(directory, id);
  if (QFileInfo(location).isSymLink()) {
    *error = "Refusing to read a symlinked rename journal.";
    return {};
  }
  QFile file(location);
  if (!file.open(QIODevice::ReadOnly) || file.size() > kMaxJournalBytes) {
    *error = "Rename journal is missing, unreadable or too large.";
    return {};
  }
  QJsonParseError parseError;
  const auto value = QJsonDocument::fromJson(file.readAll(), &parseError);
  if (parseError.error != QJsonParseError::NoError || !value.isObject()) {
    *error = "Rename journal is invalid; refusing filesystem changes.";
    return {};
  }
  const auto document = value.object();
  const auto rows = document.value("rows").toArray();
  if (document.value("schema_version").toInt(-1) != 1 ||
      document.value("operation").toString() != "rename.transaction" ||
      document.value("id").toString() != id ||
      rows.isEmpty() || rows.size() > kMaxFiles) {
    *error = "Rename journal schema or row count is invalid.";
    return {};
  }
  QSet<QString> sources;
  QSet<QString> targets;
  for (const auto& item : rows) {
    if (!item.isObject()) {
      *error = "A rename journal entry is not an object.";
      return {};
    }
    const auto row = item.toObject();
    const QString source = row.value("source").toString();
    const QString target = row.value("target").toString();
    const QString parent = row.value("parent_canonical").toString();
    if (!QDir::isAbsolutePath(parent) ||
        parent != QFileInfo(source).absolutePath() ||
        !QDir::isAbsolutePath(source) || !QDir::isAbsolutePath(target) ||
        QDir::cleanPath(source) != source || QDir::cleanPath(target) != target ||
        source == target ||
        QFileInfo(source).absolutePath() != QFileInfo(target).absolutePath() ||
        !kSha256.match(row.value("sha256").toString()).hasMatch() ||
        row.value("size_bytes").toDouble(-1) < 0 ||
        row.value("size_bytes").toDouble(-1) > kMaxInputBytes ||
        row.value("modified_ms").toDouble(-1) < 0) {
      *error = "Rename journal contains an unsafe path or invalid fingerprint.";
      return {};
    }
    const QString sourceKey = source.toCaseFolded();
    const QString targetKey = target.toCaseFolded();
    if (sources.contains(sourceKey) || targets.contains(targetKey)) {
      *error = "Rename journal contains duplicate sources or targets.";
      return {};
    }
    sources.insert(sourceKey);
    targets.insert(targetKey);
  }
  for (const auto& source : sources) {
    if (targets.contains(source)) {
      *error = "Rename journal tries to rename onto another selected source.";
      return {};
    }
  }
  return document;
}

bool verifyPositions(const QJsonArray& rows, bool expectTargets,
                     QString* error) {
  for (const auto& entry : rows) {
    const auto item = entry.toObject();
    const QString current = expectTargets ? item.value("target").toString()
                                          : item.value("source").toString();
    const QString absent = expectTargets ? item.value("source").toString()
                                         : item.value("target").toString();
    if (occupied(absent)) {
      *error = "A source or target path is unexpectedly occupied. Review files before retry.";
      return false;
    }
    if (!matchesFingerprint(current, item, error)) return false;
  }
  return true;
}

bool restoreToSources(const QJsonArray& rows, QString* error) {
  QVector<bool> atTarget(rows.size(), false);
  // Preflight ALL paths before any reverse move; refuse any ambiguous state.
  for (int i = 0; i < rows.size(); ++i) {
    const auto row = rows.at(i).toObject();
    const QString source = row.value("source").toString();
    const QString target = row.value("target").toString();
    const bool sourcePresent = occupied(source);
    const bool targetPresent = occupied(target);
    if (sourcePresent == targetPresent) {
      *error = "Cannot recover: one file is missing or both source and target exist.";
      return false;
    }
    if (!matchesFingerprint(sourcePresent ? source : target, row, error))
      return false;
    atTarget[i] = targetPresent;
  }

  for (int i = rows.size() - 1; i >= 0; --i) {
    if (!atTarget.at(i)) continue;
    const auto row = rows.at(i).toObject();
    if (!moveNoReplace(row.value("target").toString(),
                       row.value("source").toString(), error))
      return false;
  }
  return true;
}

bool updateState(const QString& directory, QJsonObject* document,
                 const QString& state, QString* error) {
  document->insert("state", state);
  document->insert("updated_at", nowUtc());
  *error = writeJournal(directory, *document);
  return error->isEmpty();
}

}  // namespace

QJsonObject RenameTransactionResult::toJson() const {
  return QJsonObject{
      {"schema_version", 1},
      {"operation", "rename.transaction"},
      {"ok", ok},
      {"transaction_id", transactionId},
      {"state", state},
      {"error", error},
      {"row_count", rows.size()},
      {"rows", rows},
  };
}

RenameTransactionService::RenameTransactionService(QString journalDirectory)
    : journalDirectory_(std::move(journalDirectory)) {
  if (journalDirectory_.isEmpty()) {
    journalDirectory_ = qEnvironmentVariable("OMNIDROP_RENAME_JOURNAL_DIR");
  }
  if (journalDirectory_.isEmpty()) {
    const QString data =
        QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    journalDirectory_ = QDir(data).filePath("OmniDrop/rename-transactions");
  }
}

RenameTransactionResult RenameTransactionService::prepare(
    const QStringList& selectedPaths,
    const RenamePreviewOptions& options) const {
  QLockFile lock(QDir(journalDirectory_).filePath("transactions.lock"));
  QString error;
  if (!locked(journalDirectory_, lock, &error)) return failure(error);
  const auto preview = RenamePreviewService{}.preview(selectedPaths, options);
  if (!preview.ok) return failure(preview.error);
  if (preview.conflictCount != 0 || preview.readyCount == 0)
    return failure("Only a conflict-free preview with changes can be prepared.");

  if (QDir(journalDirectory_).entryList({"*.json"}, QDir::Files).size() >=
      kMaxJournalCount) {
    return failure("Rename history contains 500 records. Archive old records first.");
  }

  QJsonArray rows;
  qint64 totalBytes = 0;
  for (const auto& candidate : preview.rows) {
    if (candidate.status != "ready") continue;
    if (QFileInfo(candidate.sourcePath).absolutePath() !=
        QFileInfo(candidate.targetPath).absolutePath()) {
      return failure("Moving between directories is not supported.");
    }
    if (occupied(candidate.targetPath))
      return failure("A target was created after preview; try again.");

    Fingerprint stamp;
    if (!fingerprint(candidate.sourcePath, &stamp, &error))
      return failure(error);
    totalBytes += stamp.size;
    if (totalBytes > kMaxInputBytes)
      return failure("Selected files exceed the 512 MiB transaction limit.");

    const auto parent = QDir(QFileInfo(candidate.sourcePath).absolutePath())
                            .canonicalPath();
    if (parent.isEmpty())
      return failure("Cannot resolve the original parent directory.");

    rows.append(QJsonObject{
        {"source", candidate.sourcePath},
        {"target", candidate.targetPath},
        {"parent_canonical", parent},
        {"size_bytes", stamp.size},
        {"modified_ms", stamp.modifiedMs},
        {"sha256", stamp.sha256},
    });
  }

  const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
  QJsonObject document{
      {"schema_version", 1},
      {"operation", "rename.transaction"},
      {"id", id},
      {"state", "prepared"},
      {"created_at", nowUtc()},
      {"updated_at", nowUtc()},
      {"rows", rows},
  };
  error = writeJournal(journalDirectory_, document);
  if (!error.isEmpty()) return failure(error);
  return report(document);
}

RenameTransactionResult RenameTransactionService::status(
    const QString& id) const {
  QString error;
  const auto document = readJournal(journalDirectory_, id, &error);
  return error.isEmpty() ? report(document) : failure(error, id);
}

RenameTransactionResult RenameTransactionService::apply(
    const QString& id, bool explicitlyApproved) const {
  if (!explicitlyApproved)
    return failure("Rename requires an explicit confirmation.", id);
  QLockFile lock(QDir(journalDirectory_).filePath("transactions.lock"));
  QString error;
  if (!locked(journalDirectory_, lock, &error)) return failure(error, id);

  auto document = readJournal(journalDirectory_, id, &error);
  if (!error.isEmpty()) return failure(error, id);
  if (document.value("state").toString() != "prepared")
    return report(document, false, "Only prepared transactions can be applied.");

  const QJsonArray rows = document.value("rows").toArray();
  if (!verifyPositions(rows, false, &error))
    return report(document, false, error);
  if (!updateState(journalDirectory_, &document, "committing", &error))
    return report(document, false, error);

  for (const auto& value : rows) {
    const auto row = value.toObject();
    const QString source = row.value("source").toString();
    const QString target = row.value("target").toString();
    if (!matchesFingerprint(source, row, &error) ||
        !moveNoReplace(source, target, &error)) {
      QString rollbackError;
      const bool restored = restoreToSources(rows, &rollbackError);
      QString stateError;
      const QString state = restored ? "aborted" : "recovery_required";
      updateState(journalDirectory_, &document, state, &stateError);
      const QString message = "Transaction stopped: " + error +
          (restored ? " Original filenames restored."
                    : " Recovery required: " + rollbackError) +
          (stateError.isEmpty() ? QString{} : " Journal update failed: " + stateError);
      return report(document, false, message);
    }
  }
  if (!updateState(journalDirectory_, &document, "committed", &error)) {
    return report(document, false,
                  "Files were renamed, but journal finalization failed. "
                  "Use rename-recover after reviewing the transaction.");
  }
  return report(document);
}

RenameTransactionResult RenameTransactionService::undo(
    const QString& id, bool explicitlyApproved) const {
  if (!explicitlyApproved)
    return failure("Undo requires explicit confirmation.", id);
  QLockFile lock(QDir(journalDirectory_).filePath("transactions.lock"));
  QString error;
  if (!locked(journalDirectory_, lock, &error)) return failure(error, id);
  auto document = readJournal(journalDirectory_, id, &error);
  if (!error.isEmpty()) return failure(error, id);
  if (document.value("state").toString() != "committed")
    return report(document, false, "Only committed rename transactions can be undone.");

  const QJsonArray rows = document.value("rows").toArray();
  if (!verifyPositions(rows, true, &error))
    return report(document, false, error);
  if (!updateState(journalDirectory_, &document, "undoing", &error))
    return report(document, false, error);

  const bool restored = restoreToSources(rows, &error);
  QString writeError;
  updateState(journalDirectory_, &document,
              restored ? "undone" : "recovery_required", &writeError);
  if (!restored || !writeError.isEmpty())
    return report(document, false,
                  "Undo incomplete; use rename-recover after reviewing files. " +
                  error + " " + writeError);
  return report(document);
}

RenameTransactionResult RenameTransactionService::recover(
    const QString& id, bool explicitlyApproved) const {
  if (!explicitlyApproved)
    return failure("Recovery requires explicit confirmation.", id);
  QLockFile lock(QDir(journalDirectory_).filePath("transactions.lock"));
  QString error;
  if (!locked(journalDirectory_, lock, &error)) return failure(error, id);
  auto document = readJournal(journalDirectory_, id, &error);
  if (!error.isEmpty()) return failure(error, id);
  const QString state = document.value("state").toString();
  if (state != "committing" && state != "undoing" &&
      state != "recovery_required") {
    return report(document, false,
                  "Recovery is only allowed for interrupted transactions.");
  }

  const bool restored = restoreToSources(document.value("rows").toArray(), &error);
  QString writeError;
  updateState(journalDirectory_, &document,
              restored ? "recovered" : "recovery_required", &writeError);
  if (!restored || !writeError.isEmpty())
    return report(document, false,
                  "Recovery could not safely restore every original filename. " +
                  error + " " + writeError);
  return report(document);
}

}  // namespace omnidrop
