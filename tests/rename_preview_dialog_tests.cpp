#ifdef NDEBUG
#undef NDEBUG
#endif

#include "gui/rename_preview_dialog.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTemporaryDir>

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

  // No preview operation can overwrite, move or rename selected source files.
  QFile original(source);
  assert(original.open(QIODevice::ReadOnly));
  assert(original.readAll() == "original\n");
  assert(!QFile::exists(temp.filePath("draft.txt")));
  for (auto* button : dialog.findChildren<QPushButton*>()) {
    assert(button->text() != "Apply");
    assert(button->text() != "Rename");
  }
  return 0;
}
