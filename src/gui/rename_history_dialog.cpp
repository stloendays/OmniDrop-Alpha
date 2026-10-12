#include "gui/rename_history_dialog.hpp"

#include <QAbstractItemView>
#include <QCloseEvent>
#include <QDialogButtonBox>
#include <QFutureWatcher>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QtConcurrent>

#include <utility>

namespace omnidrop {

RenameHistoryDialog::RenameHistoryDialog(QWidget* parent,
                                         QString journalDirectory)
    : QDialog(parent), service_(std::move(journalDirectory)) {
  setWindowTitle("Rename history and recovery - OmniDrop");
  resize(1020, 630);
  setMinimumSize(720, 460);

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(20, 18, 20, 18);
  layout->setSpacing(10);

  auto* title = new QLabel("Rename transactions", this);
  title->setObjectName("sectionTitle");
  layout->addWidget(title);

  auto* explanation = new QLabel(
      "Saved rename journals are local to this computer. Select one to inspect "
      "its original and target paths. Only eligible transactions can be applied, "
      "undone or recovered. Every file change requires confirmation and "
      "verification; existing filenames are never overwritten.", this);
  explanation->setWordWrap(true);
  layout->addWidget(explanation);

  summary_ = new QLabel(this);
  summary_->setObjectName("renameHistorySummary");
  summary_->setWordWrap(true);
  layout->addWidget(summary_);

  transactions_ = new QTableWidget(this);
  transactions_->setObjectName("renameHistoryTable");
  transactions_->setColumnCount(4);
  transactions_->setHorizontalHeaderLabels(
      {"Transaction ID", "Created (UTC)", "State", "Files"});
  transactions_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  transactions_->setSelectionMode(QAbstractItemView::SingleSelection);
  transactions_->setSelectionBehavior(QAbstractItemView::SelectRows);
  transactions_->verticalHeader()->hide();
  transactions_->horizontalHeader()->setSectionResizeMode(
      0, QHeaderView::Stretch);
  transactions_->horizontalHeader()->setSectionResizeMode(
      1, QHeaderView::ResizeToContents);
  transactions_->horizontalHeader()->setSectionResizeMode(
      2, QHeaderView::ResizeToContents);
  transactions_->horizontalHeader()->setSectionResizeMode(
      3, QHeaderView::ResizeToContents);
  layout->addWidget(transactions_, 1);

  detailTitle_ = new QLabel("Select a transaction to view file paths.", this);
  detailTitle_->setWordWrap(true);
  layout->addWidget(detailTitle_);

  details_ = new QTableWidget(this);
  details_->setObjectName("renameHistoryDetails");
  details_->setColumnCount(3);
  details_->setHorizontalHeaderLabels(
      {"Original path", "Renamed path", "Bytes"});
  details_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  details_->setSelectionMode(QAbstractItemView::NoSelection);
  details_->verticalHeader()->hide();
  details_->horizontalHeader()->setSectionResizeMode(
      0, QHeaderView::Stretch);
  details_->horizontalHeader()->setSectionResizeMode(
      1, QHeaderView::Stretch);
  details_->horizontalHeader()->setSectionResizeMode(
      2, QHeaderView::ResizeToContents);
  layout->addWidget(details_, 1);

  auto* actions = new QHBoxLayout;
  refreshButton_ = new QPushButton("Refresh", this);
  refreshButton_->setObjectName("renameHistoryRefresh");
  applyButton_ = new QPushButton("Apply prepared...", this);
  applyButton_->setObjectName("renameHistoryApply");
  undoButton_ = new QPushButton("Undo selected...", this);
  undoButton_->setObjectName("renameHistoryUndo");
  recoverButton_ = new QPushButton("Recover interrupted...", this);
  recoverButton_->setObjectName("renameHistoryRecover");
  applyButton_->setToolTip("Only a prepared, unchanged plan can be applied.");
  undoButton_->setToolTip("Only a committed, unchanged batch can be undone.");
  recoverButton_->setToolTip(
      "Only an interrupted transaction can be restored to original names.");
  actions->addWidget(refreshButton_);
  actions->addStretch();
  actions->addWidget(applyButton_);
  actions->addWidget(undoButton_);
  actions->addWidget(recoverButton_);
  layout->addLayout(actions);

  footer_ = new QDialogButtonBox(QDialogButtonBox::Close, this);
  layout->addWidget(footer_);

  connect(transactions_, &QTableWidget::itemSelectionChanged,
          this, [this] { showSelectedTransaction(); });
  connect(refreshButton_, &QPushButton::clicked,
          this, [this] { refreshTransactions(); });
  connect(applyButton_, &QPushButton::clicked,
          this, [this] { runSelectedOperation("apply"); });
  connect(undoButton_, &QPushButton::clicked,
          this, [this] { runSelectedOperation("undo"); });
  connect(recoverButton_, &QPushButton::clicked,
          this, [this] { runSelectedOperation("recover"); });
  connect(footer_, &QDialogButtonBox::rejected, this, &QDialog::close);

  setBusy(false);
  refreshTransactions();
}

QString RenameHistoryDialog::selectedTransactionId() const {
  const auto rows = transactions_->selectionModel()->selectedRows();
  if (rows.isEmpty()) return {};
  const int row = rows.first().row();
  if (!transactions_->item(row, 0)) return {};
  return transactions_->item(row, 0)->data(Qt::UserRole).toString();
}

void RenameHistoryDialog::setBusy(bool busy) {
  busy_ = busy;
  transactions_->setEnabled(!busy);
  refreshButton_->setEnabled(!busy);
  footer_->setEnabled(!busy);
  // A fresh status() lookup must approve each button after a selection.
  applyButton_->setEnabled(false);
  undoButton_->setEnabled(false);
  recoverButton_->setEnabled(false);
}

void RenameHistoryDialog::refreshTransactions() {
  if (busy_) return;
  const QString previous = selectedTransactionId();
  setBusy(true);
  details_->setRowCount(0);
  detailTitle_->setText("Reading saved transaction IDs...");
  summary_->setText("Loading local rename history...");
  auto* watcher = new QFutureWatcher<QJsonObject>(this);
  connect(watcher, &QFutureWatcher<QJsonObject>::finished,
          this, [this, watcher, previous] {
    const auto listing = watcher->result();
    watcher->deleteLater();
    {
      const QSignalBlocker guard(transactions_);
      transactions_->setRowCount(0);
      if (listing.value("ok").toBool(false)) {
        const auto items = listing.value("transactions").toArray();
        transactions_->setRowCount(items.size());
        for (int i = 0; i < items.size(); ++i) {
          const auto record = items.at(i).toObject();
          const QString id = record.value("transaction_id").toString();
          auto* idItem = new QTableWidgetItem(id);
          idItem->setData(Qt::UserRole, id);
          transactions_->setItem(i, 0, idItem);
          transactions_->setItem(i, 1, new QTableWidgetItem(
              record.value("created_at").toString()));
          transactions_->setItem(i, 2, new QTableWidgetItem(
              record.value("state").toString()));
          transactions_->setItem(i, 3, new QTableWidgetItem(
              QString::number(record.value("row_count").toInt())));
        }
      }
    }
    setBusy(false);
    if (!listing.value("ok").toBool(false)) {
      detailTitle_->setText("History unavailable.");
      summary_->setText("Cannot read rename history: " +
                        listing.value("error").toString());
      return;
    }
    const int count = transactions_->rowCount();
    if (count == 0) {
      detailTitle_->setText("No saved transactions.");
      summary_->setText(lastOutcome_.isEmpty()
          ? "No rename transactions have been saved."
          : lastOutcome_);
      return;
    }
    int selected = 0;
    for (int i = 0; i < count; ++i) {
      if (transactions_->item(i, 0)->data(Qt::UserRole).toString()
          == previous) {
        selected = i;
        break;
      }
    }
    transactions_->selectRow(selected);
  });
  const QString directory = service_.journalDirectory();
  watcher->setFuture(QtConcurrent::run([directory] {
    return RenameTransactionService(directory).listTransactions();
  }));
}

void RenameHistoryDialog::enableActions(const RenameTransactionResult& result) {
  if (busy_ || !result.ok) return;
  applyButton_->setEnabled(result.state == "prepared");
  undoButton_->setEnabled(result.state == "committed");
  recoverButton_->setEnabled(
      result.state == "committing" ||
      result.state == "undoing" ||
      result.state == "recovery_required");
}

void RenameHistoryDialog::showSelectedTransaction() {
  if (busy_) return;
  const QString id = selectedTransactionId();
  details_->setRowCount(0);
  if (id.isEmpty()) {
    detailTitle_->setText("Select a transaction to view file paths.");
    setBusy(false);
    return;
  }
  setBusy(true);
  detailTitle_->setText("Verifying journal " + id + "...");
  summary_->setText("Reading saved transaction details...");
  auto* watcher = new QFutureWatcher<RenameTransactionResult>(this);
  connect(watcher, &QFutureWatcher<RenameTransactionResult>::finished,
          this, [this, watcher, id] {
    const auto result = watcher->result();
    watcher->deleteLater();
    setBusy(false);
    if (selectedTransactionId() != id) return;
    if (!result.ok) {
      detailTitle_->setText("Transaction " + id + " could not be read.");
      summary_->setText("Journal unavailable: " + result.error);
      return;
    }
    detailTitle_->setText(
        QString("Transaction %1 - %2 (%3 files)")
            .arg(id, result.state).arg(result.rows.size()));
    details_->setRowCount(result.rows.size());
    for (int i = 0; i < result.rows.size(); ++i) {
      const auto row = result.rows.at(i).toObject();
      const QString original = row.value("source_path").toString();
      const QString renamed = row.value("target_path").toString();
      auto* sourceItem = new QTableWidgetItem(original);
      auto* targetItem = new QTableWidgetItem(renamed);
      sourceItem->setToolTip(original);
      targetItem->setToolTip(renamed);
      details_->setItem(i, 0, sourceItem);
      details_->setItem(i, 1, targetItem);
      details_->setItem(i, 2, new QTableWidgetItem(
          QString::number(static_cast<qint64>(
              row.value("size_bytes").toDouble()))));
    }
    summary_->setText(lastOutcome_.isEmpty() || lastOutcomeId_ != id
        ? "Select an enabled operation to review its confirmation."
        : lastOutcome_);
    enableActions(result);
  });
  const QString directory = service_.journalDirectory();
  watcher->setFuture(QtConcurrent::run([directory, id] {
    return RenameTransactionService(directory).status(id);
  }));
}

void RenameHistoryDialog::runSelectedOperation(const QString& operation) {
  if (busy_) return;
  const bool enabled =
      (operation == "apply" && applyButton_->isEnabled()) ||
      (operation == "undo" && undoButton_->isEnabled()) ||
      (operation == "recover" && recoverButton_->isEnabled());
  if (!enabled) return;
  const QString id = selectedTransactionId();
  if (id.isEmpty()) return;

  const QString title = operation == "apply" ? "Confirm prepared rename"
      : operation == "undo" ? "Confirm history undo"
                            : "Confirm history recovery";
  const QString description = operation == "apply"
      ? "Apply this saved rename plan to the selected files?"
      : operation == "undo"
          ? "Restore the original names from this committed batch?"
          : "Restore original names from this interrupted transaction?";
  const QString prompt = description +
      "\n\nTransaction: " + id +
      "\nFiles: " + QString::number(details_->rowCount()) +
      "\n\nOmniDrop verifies all files and refuses occupied filenames. "
      "No file will be overwritten. A partially interrupted batch may "
      "require further manual review. Continue?";
  if (QMessageBox::question(this, title, prompt,
                            QMessageBox::Yes | QMessageBox::No,
                            QMessageBox::No) != QMessageBox::Yes) {
    summary_->setText("Canceled. No file operation was requested.");
    return;
  }

  setBusy(true);
  summary_->setText("Verifying files and running " + operation + "...");
  auto* watcher = new QFutureWatcher<RenameTransactionResult>(this);
  connect(watcher, &QFutureWatcher<RenameTransactionResult>::finished,
          this, [this, watcher, operation] {
    const auto result = watcher->result();
    watcher->deleteLater();
    lastOutcomeId_ = result.transactionId;
    lastOutcome_ = result.ok
        ? QString("%1 completed after verification. Transaction: %2")
              .arg(operation, result.transactionId)
        : QString("%1 refused or incomplete: %2. Transaction: %3")
              .arg(operation, result.error, result.transactionId);
    setBusy(false);
    refreshTransactions();
  });
  const QString directory = service_.journalDirectory();
  watcher->setFuture(QtConcurrent::run([directory, id, operation] {
    const RenameTransactionService service(directory);
    if (operation == "apply") return service.apply(id, true);
    if (operation == "undo") return service.undo(id, true);
    return service.recover(id, true);
  }));
}

void RenameHistoryDialog::reject() {
  if (busy_) return;
  QDialog::reject();
}

void RenameHistoryDialog::closeEvent(QCloseEvent* event) {
  if (busy_) {
    event->ignore();
    return;
  }
  QDialog::closeEvent(event);
}

}  // namespace omnidrop
