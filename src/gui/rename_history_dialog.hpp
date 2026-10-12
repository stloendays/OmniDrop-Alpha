#pragma once

#include "app/rename_transaction_service.hpp"

#include <QDialog>
#include <QString>

class QCloseEvent;
class QDialogButtonBox;
class QLabel;
class QPushButton;
class QTableWidget;

namespace omnidrop {

// Displays durable rename transactions, but delegates all verification and
// filesystem mutations to the same service used by the CLI.
class RenameHistoryDialog final : public QDialog {
 public:
  explicit RenameHistoryDialog(QWidget* parent = nullptr,
                               QString journalDirectory = {});
  void reject() override;

 protected:
  void closeEvent(QCloseEvent* event) override;

 private:
  QString selectedTransactionId() const;
  void setBusy(bool busy);
  void refreshTransactions();
  void showSelectedTransaction();
  void enableActions(const RenameTransactionResult& result);
  void runSelectedOperation(const QString& operation);

  RenameTransactionService service_;
  bool busy_{false};
  QString lastOutcome_;
  QString lastOutcomeId_;
  QTableWidget* transactions_{nullptr};
  QTableWidget* details_{nullptr};
  QLabel* summary_{nullptr};
  QLabel* detailTitle_{nullptr};
  QPushButton* refreshButton_{nullptr};
  QPushButton* applyButton_{nullptr};
  QPushButton* undoButton_{nullptr};
  QPushButton* recoverButton_{nullptr};
  QDialogButtonBox* footer_{nullptr};
};

}  // namespace omnidrop
