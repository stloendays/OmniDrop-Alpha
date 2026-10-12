#pragma once

#include "app/settings_service.hpp"

#include <QDialog>

class QLabel;
class QSpinBox;
class QPushButton;

namespace omnidrop {

class SettingsDialog final : public QDialog {
 public:
  explicit SettingsDialog(QWidget* parent = nullptr);

 private:
  void refresh();
  void showResult(const SettingsResult& result, const QString& action);
  bool askConfirmation(const QString& title, const QString& explanation);

  SettingsService service_;
  QLabel* status_{nullptr};
  QSpinBox* recentLimit_{nullptr};
  QSpinBox* activityLimit_{nullptr};
  QPushButton* saveButton_{nullptr};
  QPushButton* recoverButton_{nullptr};
};

}  // namespace omnidrop
