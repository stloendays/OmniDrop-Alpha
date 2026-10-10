#pragma once

#include "app/rename_preview_service.hpp"

#include <QDialog>
#include <QStringList>

class QCheckBox;
class QLabel;
class QLineEdit;
class QTableWidget;

namespace omnidrop {

// Presentation-only dialog: all preview rules live in RenamePreviewService.
class RenamePreviewDialog final : public QDialog {
 public:
  explicit RenamePreviewDialog(QStringList selectedPaths, QWidget* parent = nullptr);

 private:
  void refreshPreview();
  void chooseFiles();

  RenamePreviewService service_;
  QStringList selectedPaths_;
  QLineEdit* findInput_{nullptr};
  QLineEdit* replacementInput_{nullptr};
  QCheckBox* regex_{nullptr};
  QCheckBox* ignoreCase_{nullptr};
  QCheckBox* includeExtension_{nullptr};
  QTableWidget* table_{nullptr};
  QLabel* summary_{nullptr};
  QLabel* selectionInfo_{nullptr};
};

}  // namespace omnidrop
