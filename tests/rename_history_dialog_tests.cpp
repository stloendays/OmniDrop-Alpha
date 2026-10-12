#ifdef NDEBUG
#undef NDEBUG
#endif

#include "gui/rename_history_dialog.hpp"

#include <QAbstractButton>
#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>

#include <cassert>
#include <functional>

namespace {

void writeFile(const QString& path, const QByteArray& payload) {
  QFile file(path);
  assert(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
  assert(file.write(payload) == payload.size());
  file.close();
}

QByteArray readFile(const QString& path) {
  QFile file(path);
  assert(file.open(QIODevice::ReadOnly));
  return file.readAll();
}

// Runs event processing and can answer a *real* modal QMessageBox.
// Keeping the responder active before clicking is essential for Windows CI.
bool await(const std::function<bool()>& completed,
           const QString& confirmationTitle = {},
           QMessageBox::StandardButton choice = QMessageBox::No) {
  if (completed()) return true;
  QEventLoop loop;
  QElapsedTimer elapsed;
  elapsed.start();
  QTimer poll;
  QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
    for (QWidget* widget : QApplication::topLevelWidgets()) {
      auto* question = qobject_cast<QMessageBox*>(widget);
      if (!question || !question->isVisible() ||
          question->windowTitle() != confirmationTitle) continue;
      auto* button = question->button(choice);
      assert(button);
      button->click();
    }
    if (completed() || elapsed.elapsed() > 16000) loop.quit();
  });
  poll.start(25);
  loop.exec();
  return completed();
}

bool selectTransaction(QTableWidget* table, const QString& id) {
  for (int row = 0; row < table->rowCount(); ++row) {
    const auto* entry = table->item(row, 0);
    if (entry && entry->data(Qt::UserRole).toString() == id) {
      table->selectRow(row);
      return true;
    }
  }
  return false;
}

}  // namespace

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  QTemporaryDir temp;
  assert(temp.isValid());
  const QString journalDirectory = temp.filePath("journal");
  omnidrop::RenameTransactionService service(journalDirectory);
  omnidrop::RenamePreviewOptions options;
  options.find = "draft-";
  options.replacement = "final-";

  const QString source = temp.filePath("draft-one.txt");
  const QString target = temp.filePath("final-one.txt");
  writeFile(source, "original bytes\n");
  const auto prepared = service.prepare({source}, options);
  assert(prepared.ok);
  const auto committed = service.apply(prepared.transactionId, true);
  assert(committed.ok && committed.state == "committed");

  // This dialog is a fresh process-like view: it was not given the
  // transaction ID and must rediscover the journal on disk.
  omnidrop::RenameHistoryDialog dialog(nullptr, journalDirectory);
  dialog.show();
  auto* table = dialog.findChild<QTableWidget*>("renameHistoryTable");
  auto* details = dialog.findChild<QTableWidget*>("renameHistoryDetails");
  auto* summary = dialog.findChild<QLabel*>("renameHistorySummary");
  auto* refresh = dialog.findChild<QPushButton*>("renameHistoryRefresh");
  auto* apply = dialog.findChild<QPushButton*>("renameHistoryApply");
  auto* undo = dialog.findChild<QPushButton*>("renameHistoryUndo");
  auto* recover = dialog.findChild<QPushButton*>("renameHistoryRecover");
  assert(table && details && summary && refresh && apply && undo && recover);

  assert(await([&] { return table->rowCount() == 1 && undo->isEnabled(); }));
  assert(table->item(0, 0)->data(Qt::UserRole).toString() ==
         prepared.transactionId);
  assert(details->rowCount() == 1);
  assert(details->item(0, 0)->text() == source);
  assert(details->item(0, 1)->text() == target);
  assert(!apply->isEnabled() && !recover->isEnabled());
  assert(!QFile::exists(source) && QFile::exists(target));

  // Dismissing the explicit confirmation cannot cause any mutation.
  QTimer::singleShot(0, undo, [undo] { undo->click(); });
  assert(await([&] { return summary->text().startsWith("Canceled."); },
               "Confirm history undo", QMessageBox::No));
  assert(!QFile::exists(source) && readFile(target) == "original bytes\n");

  // Actual confirmation uses the shared journal-verifying service.
  QTimer::singleShot(0, undo, [undo] { undo->click(); });
  assert(await([&] {
    return summary->text().contains("undo completed") &&
           QFile::exists(source) && !QFile::exists(target);
  }, "Confirm history undo", QMessageBox::Yes));
  assert(readFile(source) == "original bytes\n");
  assert(service.status(prepared.transactionId).state == "undone");
  assert(!undo->isEnabled());

  // A prepared-but-unapplied plan can be explicitly applied after restart.
  const QString secondSource = temp.filePath("draft-two.txt");
  const QString secondTarget = temp.filePath("final-two.txt");
  writeFile(secondSource, "plan payload\n");
  const auto second = service.prepare({secondSource}, options);
  assert(second.ok);
  refresh->click();
  assert(await([&] { return table->rowCount() == 2 && refresh->isEnabled(); }));
  assert(selectTransaction(table, second.transactionId));
  assert(await([&] { return apply->isEnabled(); }));
  assert(!undo->isEnabled() && !recover->isEnabled());
  assert(QFile::exists(secondSource));
  QTimer::singleShot(0, apply, [apply] { apply->click(); });
  assert(await([&] {
    return summary->text().contains("apply completed") &&
           !QFile::exists(secondSource) && QFile::exists(secondTarget);
  }, "Confirm prepared rename", QMessageBox::Yes));
  assert(readFile(secondTarget) == "plan payload\n");

  // Failed Undo on a now-occupied original name must keep both files intact.
  writeFile(secondSource, "unrelated new occupant\n");
  assert(selectTransaction(table, second.transactionId));
  assert(await([&] { return undo->isEnabled(); }));
  QTimer::singleShot(0, undo, [undo] { undo->click(); });
  assert(await([&] {
    return summary->text().contains("undo refused or incomplete");
  }, "Confirm history undo", QMessageBox::Yes));
  assert(readFile(secondSource) == "unrelated new occupant\n");
  assert(readFile(secondTarget) == "plan payload\n");

  // Simulate an interrupted commit. The UI may recover only this state;
  // the service validates the actual file fingerprint before moving.
  const QString thirdSource = temp.filePath("draft-three.txt");
  const QString thirdTarget = temp.filePath("final-three.txt");
  writeFile(thirdSource, "recoverable bytes\n");
  const auto third = service.prepare({thirdSource}, options);
  assert(third.ok);
  const QString journalPath =
      journalDirectory + "/" + third.transactionId + ".json";
  QFile journal(journalPath);
  assert(journal.open(QIODevice::ReadOnly));
  auto document = QJsonDocument::fromJson(journal.readAll()).object();
  journal.close();
  document.insert("state", "committing");
  assert(journal.open(QIODevice::WriteOnly | QIODevice::Truncate));
  const auto payload = QJsonDocument(document).toJson(QJsonDocument::Compact);
  assert(journal.write(payload) == payload.size());
  journal.close();
  assert(QFile::rename(thirdSource, thirdTarget));
  refresh->click();
  assert(await([&] { return table->rowCount() == 3 && refresh->isEnabled(); }));
  assert(selectTransaction(table, third.transactionId));
  assert(await([&] { return recover->isEnabled(); }));
  assert(!apply->isEnabled() && !undo->isEnabled());
  QTimer::singleShot(0, recover, [recover] { recover->click(); });
  assert(await([&] {
    return summary->text().contains("recover completed") &&
           QFile::exists(thirdSource) && !QFile::exists(thirdTarget);
  }, "Confirm history recovery", QMessageBox::Yes));
  assert(readFile(thirdSource) == "recoverable bytes\n");
  assert(service.status(third.transactionId).state == "recovered");
  assert(!recover->isEnabled());

  return 0;
}
