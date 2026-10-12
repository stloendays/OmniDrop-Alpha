#pragma once

#include "app/update_check_service.hpp"
#include "app/local_update_preflight_service.hpp"

#include <QDialog>
#include <QString>

class QCloseEvent;
class QComboBox;
class QLabel;
class QPushButton;

namespace omnidrop {

class UpdateCheckDialog final : public QDialog {
 public:
  explicit UpdateCheckDialog(QWidget* parent = nullptr);
  void reject() override;

 protected:
  void closeEvent(QCloseEvent* event) override;

 private:
  void startCheck();
  void verifyLocalPackage();
  void showPreflightResult(const LocalUpdatePreflightResult& result);
  void showResult(const UpdateCheckResult& result);

  bool checking_{false};
  bool verifying_{false};
  QString releaseUrl_;
  QComboBox* channel_{nullptr};
  QLabel* status_{nullptr};
  QPushButton* checkButton_{nullptr};
  QPushButton* verifyButton_{nullptr};
  QPushButton* releaseButton_{nullptr};
};

}  // namespace omnidrop
