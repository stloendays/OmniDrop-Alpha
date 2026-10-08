#pragma once

#include "adapters/python_worker_client.hpp"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;

namespace omnidrop {

class TranslationDialog final : public QDialog {
 public:
  explicit TranslationDialog(const QString& initialPath = {}, QWidget* parent = nullptr);

 protected:
  void reject() override;

 private:
  void refreshProvider();
  void startTranslation();
  void finishTranslation(const WorkerResult& result);
  void openOutputFolder();

  QLineEdit* fileEdit_{nullptr};
  QComboBox* providerBox_{nullptr};
  QComboBox* sourceBox_{nullptr};
  QComboBox* targetBox_{nullptr};
  QLineEdit* endpointEdit_{nullptr};
  QLineEdit* keyEdit_{nullptr};
  QLabel* endpointLabel_{nullptr};
  QLabel* keyLabel_{nullptr};
  QLabel* privacyLabel_{nullptr};
  QCheckBox* consentBox_{nullptr};
  QProgressBar* progress_{nullptr};
  QLabel* statusLabel_{nullptr};
  QPushButton* translateButton_{nullptr};
  QPushButton* openFolderButton_{nullptr};

  bool busy_{false};
  QString outputPath_;
};

}  // namespace omnidrop
