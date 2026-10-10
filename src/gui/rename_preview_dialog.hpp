#pragma once

#include "app/rename_preview_service.hpp"
#include "app/rename_transaction_service.hpp"

#include <QDialog>
#include <QStringList>

class QCheckBox;
class QCloseEvent;
class QDialogButtonBox;
class QPushButton;
class QLabel;
class QLineEdit;
class QTableWidget;

namespace omnidrop {

// Presentation-only dialog: all preview rules live in RenamePreviewService.
class RenamePreviewDialog final : public QDialog {
 public:
  explicit RenamePreviewDialog(QStringList selectedPaths, QWidget* parent = nullptr);

 public:
  void reject() override;

 protected:
  void closeEvent(QCloseEvent* event) override;

 private:
  void refreshPreview();
  void chooseFiles();
  void setBusy(bool busy);
  void prepareTransaction();
  void commitPrepared(const QString& transactionId);
  void undoLastTransaction();
  RenamePreviewOptions currentOptions() const;

  RenamePreviewService service_;
  QStringList selectedPaths_;
  RenamePreviewResult lastPreview_;
  QString lastCommittedId_;
  bool busy_{false};
  QLineEdit* findInput_{nullptr};
  QLineEdit* replacementInput_{nullptr};
  QCheckBox* regex_{nullptr};
  QCheckBox* ignoreCase_{nullptr};
  QCheckBox* includeExtension_{nullptr};
  QTableWidget* table_{nullptr};
  QLabel* summary_{nullptr};
  QLabel* selectionInfo_{nullptr};
  QPushButton* chooseButton_{nullptr};
  QPushButton* prepareButton_{nullptr};
  QPushButton* undoButton_{nullptr};
  QDialogButtonBox* footer_{nullptr};
};

}  // namespace omnidrop
