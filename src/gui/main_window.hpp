#pragma once

#include "adapters/action_history_store.hpp"
#include "adapters/python_worker_client.hpp"
#include "adapters/recent_files_store.hpp"
#include "app/omnidrop_service.hpp"
#include "app/batch_job.hpp"

#include <QMainWindow>
#include <QSet>

#include <memory>

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QProgressBar;

namespace omnidrop {

class DropZone;

class MainWindow final : public QMainWindow {
  Q_OBJECT

 public:
  explicit MainWindow(QWidget* parent = nullptr);

 private:
  void inspectPath(const QString& path);
  void inspectPaths(const QStringList& paths);
  void showBatchInspection(const BatchInspection& inspection);
  void runSelectedAction();
  void probeWorkerCapabilities();
  void refreshRecentFiles();
  void refreshHistory();
  void filterActions(const QString& query);
  void setOperationRunning(bool running, bool cancellable, int totalItems = 0);
  void requestStopCurrentBatch();
  QString formatSize(qint64 bytes) const;

  OmniDropService service_;
  PythonWorkerClient worker_;
  RecentFilesStore recentFiles_;
  ActionHistoryStore history_;
  BatchInspection currentBatch_;
  QSet<QString> runtimeCapabilities_;
  bool capabilityProbeComplete_{false};
  bool operationRunning_{false};
  std::shared_ptr<BatchCancellation> currentCancellation_;

  DropZone* dropZone_{nullptr};
  QLabel* fileTitle_{nullptr};
  QLabel* fileMeta_{nullptr};
  QLineEdit* actionSearch_{nullptr};
  QListWidget* actionList_{nullptr};
  QListWidget* recentList_{nullptr};
  QLabel* recentTitle_{nullptr};
  QListWidget* historyList_{nullptr};
  QLabel* historyTitle_{nullptr};
  QPushButton* runButton_{nullptr};
  QPushButton* stopButton_{nullptr};
  QProgressBar* progressBar_{nullptr};
  QLabel* statusLabel_{nullptr};
};

}  // namespace omnidrop
