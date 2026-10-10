#ifdef NDEBUG
#undef NDEBUG
#endif

#include "gui/rename_preview_dialog.hpp"

#include <QApplication>
#include <QAbstractButton>
#include <QCheckBox>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>

#include <functional>

#include <cassert>

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  QTemporaryDir temp;
  assert(temp.isValid());
  const QString source = temp.filePath("sample.txt");
  {
    QFile file(source);
    assert(file.open(QIODevice::WriteOnly));
    assert(file.write("original\n") == 9);
  }

  omnidrop::RenamePreviewDialog dialog({source});
  auto* find = dialog.findChild<QLineEdit*>("renameFindInput");
  auto* replace = dialog.findChild<QLineEdit*>("renameReplacementInput");
  auto* table = dialog.findChild<QTableWidget*>("renamePreviewTable");
  auto* summary = dialog.findChild<QLabel*>("renamePreviewSummary");
  assert(find && replace && table && summary);

  find->setText("sample");
  replace->setText("draft");
  assert(table->rowCount() == 1);
  assert(table->item(0, 1)->text() == "draft.txt");
  assert(table->item(0, 2)->text() == "Ready");
  assert(summary->text().contains("1 ready"));
  auto* applyButton = dialog.findChild<QPushButton*>("renameApplyButton");
  auto* undoButton = dialog.findChild<QPushButton*>("renameUndoButton");
  assert(applyButton && undoButton);
  assert(applyButton->isEnabled());
  assert(!undoButton->isEnabled());
  assert(QFile::exists(source));
  assert(!QFile::exists(temp.filePath("draft.txt")));

  // An invalid regex is visible and clears stale proposals.
  QCheckBox* regex = nullptr;
  for (auto* box : dialog.findChildren<QCheckBox*>()) {
    if (box->text() == "Use regular expression") regex = box;
  }
  assert(regex);
  regex->setChecked(true);
  find->setText("(");
  assert(table->rowCount() == 0);
  assert(summary->text().contains("Invalid regular expression"));
  assert(!applyButton->isEnabled());  // Invalid regex cannot apply.

  // Editing rules and viewing a preview never modifies input files.
  QFile original(source);
  assert(original.open(QIODevice::ReadOnly));
  assert(original.readAll() == "original\n");
  original.close();  // Windows requires releasing the read handle before rename.
  assert(!QFile::exists(temp.filePath("draft.txt")));
  for (auto* button : dialog.findChildren<QPushButton*>()) {
    assert(button->text() != "Apply");
    assert(button->text() != "Rename");
  }

  // Exercise the complete opt-in GUI path with a controlled confirmation.
  // The test runs in QT_QPA_PLATFORM=offscreen and never alters user files.
  qputenv("OMNIDROP_RENAME_JOURNAL_DIR",
          QFile::encodeName(temp.filePath("journal")));
  regex->setChecked(false);
  find->setText("sample");
  replace->setText("draft");
  assert(applyButton->isEnabled());
  assert(!QFile::exists(temp.filePath("draft.txt")));

  auto awaitState = [&](const QString& confirmationTitle,
                        const std::function<bool()>& complete) {
    QEventLoop loop;
    QElapsedTimer elapsed;
    elapsed.start();
    QTimer poll;
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
      for (QWidget* widget : QApplication::topLevelWidgets()) {
        auto* question = qobject_cast<QMessageBox*>(widget);
        if (question && question->isVisible() &&
            question->windowTitle() == confirmationTitle) {
          auto* yes = question->button(QMessageBox::Yes);
          assert(yes);
          yes->click();  // Exercise a real QMessageBox button, not QDialog::done().
        }
      }
      if (complete() || elapsed.elapsed() > 20000) loop.quit();
    });
    poll.start(25);
    loop.exec();
    return complete();
  };

  applyButton->click();
  assert(awaitState("Confirm batch rename", [&] {
    return undoButton->isEnabled() ||
           summary->text().startsWith("Rename incomplete") ||
           summary->text().startsWith("Plan saved") ||
           summary->text().startsWith("File selection changed");
  }));
  assert(undoButton->isEnabled());  // Only a committed rename enables Undo.
  assert(summary->text().contains("Undo is available"));
  assert(!QFile::exists(source));
  assert(QFile::exists(temp.filePath("draft.txt")));

  // Undo opens its confirmation dialog synchronously. Dispatch the click via
  // the event loop so the test's modal-dialog responder is already active.
  // Clicking directly here deadlocks the offscreen Windows CI test before
  // awaitState can install its polling timer.
  QTimer::singleShot(0, undoButton, [undoButton] { undoButton->click(); });
  assert(awaitState("Undo batch rename", [&] {
    return summary->text().contains("Original filenames restored") ||
           summary->text().contains("Undo refused");
  }));
  assert(summary->text().contains("Original filenames restored"));
  assert(QFile::exists(source));
  assert(!QFile::exists(temp.filePath("draft.txt")));
  QFile afterUndo(source);
  assert(afterUndo.open(QIODevice::ReadOnly));
  assert(afterUndo.readAll() == "original\n");
  return 0;
}
