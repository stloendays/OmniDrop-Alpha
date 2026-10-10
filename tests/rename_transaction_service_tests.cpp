#ifdef NDEBUG
#undef NDEBUG
#endif

#include "app/rename_transaction_service.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <cassert>

namespace {

void write(const QString& path, const QByteArray& content) {
  QFile file(path);
  assert(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
  assert(file.write(content) == content.size());
  file.close();
}

QByteArray read(const QString& path) {
  QFile file(path);
  assert(file.open(QIODevice::ReadOnly));
  return file.readAll();
}

QJsonObject journal(const QString& directory, const QString& id) {
  QFile file(directory + "/" + id + ".json");
  assert(file.open(QIODevice::ReadOnly));
  const auto parsed = QJsonDocument::fromJson(file.readAll());
  assert(parsed.isObject());
  return parsed.object();
}

void saveJournal(const QString& directory, const QString& id,
                 const QJsonObject& doc) {
  QFile file(directory + "/" + id + ".json");
  assert(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
  const auto body = QJsonDocument(doc).toJson(QJsonDocument::Compact);
  assert(file.write(body) == body.size());
  file.close();
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QTemporaryDir temporary;
  assert(temporary.isValid());
  const QString root = temporary.path();
  const QString journals = root + "/journal";

  omnidrop::RenameTransactionService service(journals);
  omnidrop::RenamePreviewOptions options;
  options.find = "draft-";
  options.replacement = "final-";

  const QString a = root + "/draft-a.txt";
  const QString b = root + "/draft-b.txt";
  const QString targetA = root + "/final-a.txt";
  const QString targetB = root + "/final-b.txt";
  write(a, "alpha 123\n");
  write(b, "beta 456\n");

  // Preparing captures a journal and full source hashes, but never renames.
  const auto prepared = service.prepare({a, b}, options);
  assert(prepared.ok);
  assert(prepared.state == "prepared");
  assert(prepared.rows.size() == 2);
  assert(prepared.toJson().value("operation").toString() == "rename.transaction");
  assert(!prepared.transactionId.isEmpty());
  assert(QFile::exists(a) && QFile::exists(b));
  assert(!QFile::exists(targetA) && !QFile::exists(targetB));
  assert(read(a) == "alpha 123\n");
  assert(read(b) == "beta 456\n");

  omnidrop::RenameTransactionService afterRestart(journals);
  assert(afterRestart.status(prepared.transactionId).state == "prepared");
  assert(!afterRestart.apply(prepared.transactionId, false).ok);
  assert(QFile::exists(a) && QFile::exists(b));

  const auto applied = afterRestart.apply(prepared.transactionId, true);
  assert(applied.ok);
  assert(applied.state == "committed");
  assert(!QFile::exists(a) && !QFile::exists(b));
  assert(QFile::exists(targetA) && QFile::exists(targetB));
  assert(read(targetA) == "alpha 123\n");
  assert(read(targetB) == "beta 456\n");
  assert(afterRestart.status(prepared.transactionId).state == "committed");
  assert(!afterRestart.apply(prepared.transactionId, true).ok);
  assert(!afterRestart.undo(prepared.transactionId, false).ok);

  const auto undone = afterRestart.undo(prepared.transactionId, true);
  assert(undone.ok);
  assert(undone.state == "undone");
  assert(QFile::exists(a) && QFile::exists(b));
  assert(!QFile::exists(targetA) && !QFile::exists(targetB));
  assert(read(a) == "alpha 123\n");
  assert(read(b) == "beta 456\n");
  assert(!afterRestart.undo(prepared.transactionId, true).ok);

  // Refuse stale targets at apply time: nothing may be overwritten.
  const auto stale = service.prepare({a, b}, options);
  assert(stale.ok);
  write(targetB, "someone else's file");
  const auto blocked = service.apply(stale.transactionId, true);
  assert(!blocked.ok);
  assert(blocked.state == "prepared");
  assert(read(targetB) == "someone else's file");
  assert(QFile::exists(a) && QFile::exists(b));
  assert(QFile::remove(targetB));

  // A modified file must not be renamed under an old fingerprint.
  const auto changed = service.prepare({a}, options);
  assert(changed.ok);
  write(a, "changed after preparing\n");
  assert(!service.apply(changed.transactionId, true).ok);
  assert(QFile::exists(a) && !QFile::exists(targetA));
  write(a, "alpha 123\n");

  // Undo refuses ALL files if any destination has been edited or if its
  // original pathname is now occupied.
  const auto later = service.prepare({a, b}, options);
  assert(later.ok);
  assert(service.apply(later.transactionId, true).ok);
  write(targetB, "edited after renaming\n");
  const auto deniedUndo = service.undo(later.transactionId, true);
  assert(!deniedUndo.ok);
  assert(deniedUndo.state == "committed");
  assert(!QFile::exists(a) && !QFile::exists(b));
  assert(read(targetB) == "edited after renaming\n");

  // A crash can happen after the first actual move but before the journal is
  // updated. Recovery checks disk + file hashes, not a fragile step index.
  const QString interruptedSource = root + "/draft-interrupted.txt";
  const QString interruptedTarget = root + "/final-interrupted.txt";
  const QString secondInterruptedSource = root + "/draft-still-here.txt";
  const QString secondInterruptedTarget = root + "/final-still-here.txt";
  write(interruptedSource, "recover me");
  write(secondInterruptedSource, "still here");
  const auto interrupted = service.prepare(
      {interruptedSource, secondInterruptedSource}, options);
  assert(interrupted.ok);
  auto raw = journal(journals, interrupted.transactionId);
  raw.insert("state", "committing");
  saveJournal(journals, interrupted.transactionId, raw);
  // Simulate crash after the first native rename of a two-file batch.
  assert(QFile::rename(interruptedSource, interruptedTarget));
  assert(!QFile::exists(interruptedSource));
  assert(QFile::exists(secondInterruptedSource));
  const auto recovered = service.recover(interrupted.transactionId, true);
  assert(recovered.ok);
  assert(recovered.state == "recovered");
  assert(QFile::exists(interruptedSource));
  assert(!QFile::exists(interruptedTarget));
  assert(QFile::exists(secondInterruptedSource));
  assert(!QFile::exists(secondInterruptedTarget));
  assert(read(interruptedSource) == "recover me");
  assert(read(secondInterruptedSource) == "still here");
  assert(!service.recover(interrupted.transactionId, true).ok);

  // Also reconcile a crash after partial Undo: some sources restored,
  // other targets remain renamed. Never overwrite either.
  const auto undoInterrupted = service.prepare(
      {interruptedSource, secondInterruptedSource}, options);
  assert(undoInterrupted.ok);
  assert(service.apply(undoInterrupted.transactionId, true).ok);
  auto undoRaw = journal(journals, undoInterrupted.transactionId);
  undoRaw.insert("state", "undoing");
  saveJournal(journals, undoInterrupted.transactionId, undoRaw);
  assert(QFile::rename(interruptedTarget, interruptedSource));
  const auto undoneCrash = service.recover(undoInterrupted.transactionId, true);
  assert(undoneCrash.ok);
  assert(undoneCrash.state == "recovered");
  assert(QFile::exists(interruptedSource));
  assert(QFile::exists(secondInterruptedSource));
  assert(!QFile::exists(interruptedTarget));
  assert(!QFile::exists(secondInterruptedTarget));

  // A third party must not acquire an original name during recovery.
  const QString conflictSource = root + "/draft-conflict.txt";
  const QString conflictTarget = root + "/final-conflict.txt";
  write(conflictSource, "original file");
  const auto pending = service.prepare({conflictSource}, options);
  assert(pending.ok);
  auto rawPending = journal(journals, pending.transactionId);
  rawPending.insert("state", "committing");
  saveJournal(journals, pending.transactionId, rawPending);
  assert(QFile::rename(conflictSource, conflictTarget));
  write(conflictSource, "new occupant");
  const auto deniedRecovery = service.recover(pending.transactionId, true);
  assert(!deniedRecovery.ok);
  assert(read(conflictSource) == "new occupant");
  assert(read(conflictTarget) == "original file");
  assert(service.status(pending.transactionId).state == "recovery_required");

  // Malformed IDs and journals fail closed.
  assert(!service.status("../escaped").ok);
  assert(!service.apply("../escaped", true).ok);
  assert(!service.undo("../escaped", true).ok);
  assert(!service.recover("../escaped", true).ok);
  assert(read(targetA) == "alpha 123\n");
  assert(read(targetB) == "edited after renaming\n");
  return 0;
}
