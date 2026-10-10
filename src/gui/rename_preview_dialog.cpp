#include "gui/rename_preview_dialog.hpp"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

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
  auto* chooseButton = new QPushButton("Choose files...", this);
  chooseButton->setObjectName("secondaryButton");
  selectedRow->addWidget(chooseButton);
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

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
  layout->addWidget(buttons);

  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
  connect(chooseButton, &QPushButton::clicked, this,
          [this] { chooseFiles(); });
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

void RenamePreviewDialog::refreshPreview() {
  selectionInfo_->setText(
      QString("%1 selected files (maximum 128)").arg(selectedPaths_.size()));
  table_->setRowCount(0);

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
