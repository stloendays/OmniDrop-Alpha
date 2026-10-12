#pragma once

#include "app/update_check_service.hpp"

#include <QDialog>
#include <QString>

class QComboBox;
class QLabel;
class QPushButton;

namespace omnidrop {

class UpdateCheckDialog final : public QDialog {
 public:
  explicit UpdateCheckDialog(QWidget* parent = nullptr);
  void reject() override;

 private:
  void startCheck();
  void showResult(const UpdateCheckResult& result);

  bool checking_{false};
  QString releaseUrl_;
  QComboBox* channel_{nullptr};
  QLabel* status_{nullptr};
  QPushButton* checkButton_{nullptr};
  QPushButton* releaseButton_{nullptr};
};

}  // namespace omnidrop
