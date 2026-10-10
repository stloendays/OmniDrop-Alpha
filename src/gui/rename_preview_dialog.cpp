#include "gui/rename_preview_dialog.hpp"

#include <QCheckBox>
#include <QCloseEvent>
#include <QFutureWatcher>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QtConcurrent>

#include <utility>

namespace omnidrop {

RenamePreviewDialog::RenamePreviewDialog(
    QStringList selectedPaths, QWidget* parent)
    : QDialog(parent), selectedPaths_(std::move(selectedPaths)) {
  setAttribute(Qt::WA_DeleteOnClose);
  setWindowTitle("Batch rename preview - OmniDrop");
  resize(1000, 570);
  setMinimumSize(700, 430);

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(20, 18, 20, 18);
  layout->setSpacing(12);

  auto* title = new QLabel("Preview bulk filenames", this);
  title->setObjectName("sectionTitle");
  layout->addWidget(title);

  auto* notice = new QLabel(
      "Preview only: OmniDrop will not rename, move or overwrite any files.",
      this);
  notice->setWordWrap(true);
  layout->addWidget(notice);

  auto* selectedRow = new QHBoxLayout;
  selectionInfo_ = new QLabel(this);
  selectedRow->addWidget(selectionInfo_, 1);
  chooseButton_ = new QPushButton("Choose files...", this);
  chooseButton_->setObjectName("secondaryButton");
  selectedRow->addWidget(chooseButton_);
  layout->addLayout(selectedRow);

  auto* form = new QFormLayout;
  findInput_ = new QLineEdit(this);
  findInput_->setObjectName("renameFindInput");
  findInput_->setPlaceholderText("Text or regular expression to find");
  replacementInput_ = new QLineEdit(this);
  replacementInput_->setObjectName("renameReplacementInput");
  replacementInput_->setPlaceholderText("Replacement (can be empty)");
  form->addRow("Find", findInput_);
  form->addRow("Replace with", replacementInput_);
  layout->addLayout(form);

  auto* flags = new QHBoxLayout;
  regex_ = new QCheckBox("Use regular expression", this);
  ignoreCase_ = new QCheckBox("Ignore case", this);
  includeExtension_ = new QCheckBox("Include file extension", this);
  flags->addWidget(regex_);
  flags->addWidget(ignoreCase_);
  flags->addWidget(includeExtension_);
  flags->addStretch();
  layout->addLayout(flags);

  summary_ = new QLabel(this);
  summary_->setObjectName("renamePreviewSummary");
  summary_->setWordWrap(true);
  layout->addWidget(summary_);

  table_ = new QTableWidget(this);
  table_->setObjectName("renamePreviewTable");
  table_->setColumnCount(4);
  table_->setHorizontalHeaderLabels(
      {"Original name", "Proposed name", "Status", "Notes"});
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table_->setSelectionBehavior(QAbstractItemView::SelectRows);
  table_->setSelectionMode(QAbstractItemView::SingleSelection);
  table_->verticalHeader()->hide();
  table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
  table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
  table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
  table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
  layout->addWidget(table_, 1);

  auto* operations = new QHBoxLayout;
  operations->addStretch();
  undoButton_ = new QPushButton("Undo last batch", this);
  undoButton_->setObjectName("renameUndoButton");
  undoButton_->setToolTip(
      "Only available if renamed files have not changed and original names are free.");
  undoButton_->setEnabled(false);
  prepareButton_ = new QPushButton("Prepare & apply...", this);
  prepareButton_->setObjectName("renameApplyButton");
  prepareButton_->setToolTip("Verify files, show confirmation and save an undo journal.");
  prepareButton_->setEnabled(false);
  operations->addWidget(undoButton_);
  operations->addWidget(prepareButton_);
  layout->addLayout(operations);

  footer_ = new QDialogButtonBox(QDialogButtonBox::Close, this);
  layout->addWidget(footer_);

  connect(footer_, &QDialogButtonBox::rejected, this, &QDialog::close);
  connect(chooseButton_, &QPushButton::clicked, this,
          [this] { chooseFiles(); });
  connect(prepareButton_, &QPushButton::clicked, this,
          [this] { prepareTransaction(); });
  connect(undoButton_, &QPushButton::clicked, this,
          [this] { undoLastTransaction(); });
  connect(findInput_, &QLineEdit::textChanged, this,
          [this] { refreshPreview(); });
  connect(replacementInput_, &QLineEdit::textChanged, this,
          [this] { refreshPreview(); });
  connect(regex_, &QCheckBox::toggled, this,
          [this] { refreshPreview(); });
  connect(ignoreCase_, &QCheckBox::toggled, this,
          [this] { refreshPreview(); });
  connect(includeExtension_, &QCheckBox::toggled, this,
          [this] { refreshPreview(); });

  refreshPreview();
}

void RenamePreviewDialog::chooseFiles() {
  const auto selected = QFileDialog::getOpenFileNames(
      this, "Select files to preview renaming");
  if (selected.isEmpty()) return;
  selectedPaths_ = selected;
  refreshPreview();
}

RenamePreviewOptions RenamePreviewDialog::currentOptions() const {
  RenamePreviewOptions options;
  options.find = findInput_->text();
  options.replacement = replacementInput_->text();
  options.useRegex = regex_->isChecked();
  options.caseSensitive = !ignoreCase_->isChecked();
  options.includeExtension = includeExtension_->isChecked();
  return options;
}

void RenamePreviewDialog::setBusy(bool busy) {
  busy_ = busy;
  chooseButton_->setEnabled(!busy);
  findInput_->setEnabled(!busy);
  replacementInput_->setEnabled(!busy);
  regex_->setEnabled(!busy);
  ignoreCase_->setEnabled(!busy);
  includeExtension_->setEnabled(!busy);
  footer_->setEnabled(!busy);
  prepareButton_->setEnabled(!busy && lastPreview_.ok &&
                             lastPreview_.readyCount > 0 &&
                             lastPreview_.conflictCount == 0);
  undoButton_->setEnabled(!busy && !lastCommittedId_.isEmpty());
}

void RenamePreviewDialog::prepareTransaction() {
  if (busy_ || !lastPreview_.ok || lastPreview_.conflictCount ||
      lastPreview_.readyCount == 0) return;
  const auto expected = lastPreview_;
  const auto paths = selectedPaths_;
  const auto options = currentOptions();
  setBusy(true);
  summary_->setText("Hashing selected files and preparing a private undo journal...");

  auto* watcher = new QFutureWatcher<RenameTransactionResult>(this);
  connect(watcher, &QFutureWatcher<RenameTransactionResult>::finished,
          this, [this, watcher, expected] {
    const auto result = watcher->result();
    watcher->deleteLater();
    setBusy(false);
    if (!result.ok) {
      summary_->setText("Cannot prepare rename: " + result.error);
      return;
    }
    QStringList expectedPairs;
    for (const auto& row : expected.rows) {
      if (row.status == "ready")
        expectedPairs.append(row.sourcePath + QStringLiteral("\n") + row.targetPath);
    }
    QStringList preparedPairs;
    for (const auto& item : result.rows) {
      const auto row = item.toObject();
      preparedPairs.append(row.value("source_path").toString() +
                           QStringLiteral("\n") +
                           row.value("target_path").toString());
    }
    if (expectedPairs != preparedPairs) {
      summary_->setText("File selection changed during preparation. "
                        "No filenames were changed; review the new preview.");
      refreshPreview();
      return;
    }

    const QString question =
        QString("Rename %1 files on disk?\n\n"
                "This will change the filenames, not their contents. "
                "No existing destination is overwritten. An Undo record is "
                "saved, but Undo is refused if files change afterward.\n\n"
                "Transaction ID: %2")
            .arg(result.rows.size())
            .arg(result.transactionId);
    if (QMessageBox::question(
            this, "Confirm batch rename", question,
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No) != QMessageBox::Yes) {
      summary_->setText("Plan saved, no filenames changed. "
                        "You can prepare a fresh plan or close this window.");
      return;
    }
    commitPrepared(result.transactionId);
  });
  watcher->setFuture(QtConcurrent::run([paths, options] {
    return RenameTransactionService{}.prepare(paths, options);
  }));
}

void RenamePreviewDialog::commitPrepared(const QString& transactionId) {
  setBusy(true);
  summary_->setText("Applying the confirmed rename plan; do not close this window...");
  auto* watcher = new QFutureWatcher<RenameTransactionResult>(this);
  connect(watcher, &QFutureWatcher<RenameTransactionResult>::finished,
          this, [this, watcher] {
    const auto result = watcher->result();
    watcher->deleteLater();
    setBusy(false);
    if (!result.ok) {
      summary_->setText("Rename incomplete: " + result.error +
                        "  Transaction: " + result.transactionId);
      return;
    }
    lastCommittedId_ = result.transactionId;
    refreshPreview();
    undoButton_->setEnabled(true);
    summary_->setText(QString("%1 filenames changed. Undo is available if "
                              "the files stay unchanged. Transaction: %2")
                          .arg(result.rows.size())
                          .arg(result.transactionId));
  });
  watcher->setFuture(QtConcurrent::run([transactionId] {
    return RenameTransactionService{}.apply(transactionId, true);
  }));
}

void RenamePreviewDialog::undoLastTransaction() {
  if (busy_ || lastCommittedId_.isEmpty()) return;
  if (QMessageBox::question(
          this, "Undo batch rename",
          "Restore the original filenames? OmniDrop will verify every file "
          "and will not overwrite any new file at its old name.",
          QMessageBox::Yes | QMessageBox::No,
          QMessageBox::No) != QMessageBox::Yes) return;

  const QString transactionId = lastCommittedId_;
  setBusy(true);
  summary_->setText("Verifying files and restoring original filenames...");
  auto* watcher = new QFutureWatcher<RenameTransactionResult>(this);
  connect(watcher, &QFutureWatcher<RenameTransactionResult>::finished,
          this, [this, watcher] {
    const auto result = watcher->result();
    watcher->deleteLater();
    setBusy(false);
    if (!result.ok) {
      summary_->setText("Undo refused: " + result.error +
                        "  Transaction: " + result.transactionId);
      return;
    }
    lastCommittedId_.clear();
    undoButton_->setEnabled(false);
    refreshPreview();
    summary_->setText("Original filenames restored after verification.");
  });
  watcher->setFuture(QtConcurrent::run([transactionId] {
    return RenameTransactionService{}.undo(transactionId, true);
  }));
}

void RenamePreviewDialog::reject() {
  if (busy_) {
    QMessageBox::information(
        this, "Rename in progress",
        "A rename transaction is still running. The journal will remain "
        "available for recovery if the process is interrupted.");
    return;
  }
  QDialog::reject();
}

void RenamePreviewDialog::closeEvent(QCloseEvent* event) {
  if (busy_) {
    event->ignore();
    return;
  }
  QDialog::closeEvent(event);
}

void RenamePreviewDialog::refreshPreview() {
  selectionInfo_->setText(
      QString("%1 selected files (maximum 128)").arg(selectedPaths_.size()));
  table_->setRowCount(0);
  lastPreview_ = {};
  prepareButton_->setEnabled(false);

  if (selectedPaths_.isEmpty()) {
    summary_->setText("Choose one or more files to begin.");
    return;
  }
  if (findInput_->text().isEmpty()) {
    summary_->setText("Enter text to find. A preview will appear below.");
    return;
  }

  RenamePreviewOptions options;
  options.find = findInput_->text();
  options.replacement = replacementInput_->text();
  options.useRegex = regex_->isChecked();
  options.caseSensitive = !ignoreCase_->isChecked();
  options.includeExtension = includeExtension_->isChecked();
  const auto result = service_.preview(selectedPaths_, options);
  if (!result.ok) {
    summary_->setText("Cannot preview: " + result.error);
    return;
  }

  lastPreview_ = result;
  prepareButton_->setEnabled(!busy_ && result.readyCount > 0 &&
                              result.conflictCount == 0);
  const int unchanged = result.rows.size() - result.proposedChangeCount;
  summary_->setText(
      QString("%1 ready  |  %2 conflicts  |  %3 unchanged. "
              "No rename will be applied.")
          .arg(result.readyCount)
          .arg(result.conflictCount)
          .arg(unchanged));

  table_->setRowCount(result.rows.size());
  for (int index = 0; index < result.rows.size(); ++index) {
    const auto& row = result.rows.at(index);
    const QString status = row.status == "ready"
                               ? "Ready"
                               : (row.status == "conflict" ? "Conflict" : "Unchanged");
    const QStringList columns{
        row.currentName, row.proposedName, status, row.reason};
    for (int column = 0; column < columns.size(); ++column) {
      auto* cell = new QTableWidgetItem(columns.at(column));
      cell->setToolTip(
          QString("%1\nProposed: %2").arg(row.sourcePath, row.targetPath));
      table_->setItem(index, column, cell);
    }
  }
}

}  // namespace omnidrop
