#pragma once

#include "adapters/python_worker_client.hpp"
#include "app/workflow_service.hpp"
#include "app/workflow_job_service.hpp"
#include "app/workflow_folder_watch_service.hpp"

#include <QDialog>
#include <QJsonObject>
#include <QStringList>

#include <atomic>
#include <memory>

class QCloseEvent;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QProgressBar;
class QTabWidget;
class QTimer;

namespace omnidrop {

// A lightweight workflow editor: sequential authoring plus read-only
// inspection/execution of arbitrary v1 DAG manifests.
class WorkflowGraphView;

class WorkflowDialog final : public QDialog {
 public:
  explicit WorkflowDialog(const QStringList& selectedFiles = {}, QWidget* parent = nullptr);

 protected:
  void reject() override;
  void closeEvent(QCloseEvent* event) override;

 private:
  void addStep(const QString& actionId);
  void removeStep();
  void convertToGraph();
  void refreshGraph();
  void refreshGraphSteps();
  void queueWorkflow();
  void refreshJobs();
  void showSelectedJobEvents();
  void executeSelectedJob();
  void removeSelectedJob();
  void toggleFolderWatch();
  void pollWatchedFolder();
  void runNextWatchedFile();
  void moveStep(int offset);
  void applyTemplate(int preset);
  void loadWorkflow();
  void newWorkflow();
  void saveWorkflow();
  void selectInputs();
  void refreshInputs();
  void refreshButtons();
  void begin(bool run);
  void finish(bool run, const WorkerResult& result);

  QJsonObject definition() const;
  void showStatus(const QString& message, bool error = false);

  WorkflowService service_;
  WorkflowJobService jobs_;
  WorkflowFolderWatchService folderWatch_;
  QStringList pendingWatchInputs_;
  bool watchEnabled_{false};
  QStringList inputs_;
  QJsonObject loadedDefinition_;
  bool graphMode_{false};
  bool busy_{false};

  QLineEdit* nameEdit_{nullptr};
  QLabel* inputsLabel_{nullptr};
  QLabel* statusLabel_{nullptr};
  QComboBox* actionCombo_{nullptr};
  QComboBox* templateCombo_{nullptr};
  QListWidget* stepsList_{nullptr};
  QPlainTextEdit* outputView_{nullptr};
  WorkflowGraphView* graphView_{nullptr};
  QListWidget* jobsList_{nullptr};
  QPlainTextEdit* jobEventsView_{nullptr};
  QTabWidget* rightTabs_{nullptr};
  QPushButton* graphEditButton_{nullptr};
  QPushButton* queueButton_{nullptr};
  QPushButton* refreshJobsButton_{nullptr};
  QPushButton* executeJobButton_{nullptr};
  QPushButton* removeJobButton_{nullptr};
  QPushButton* watchButton_{nullptr};
  QLabel* watchStatusLabel_{nullptr};
  QTimer* watchTimer_{nullptr};

  QPushButton* addButton_{nullptr};
  QPushButton* removeButton_{nullptr};
  QPushButton* upButton_{nullptr};
  QPushButton* downButton_{nullptr};
  QPushButton* chooseButton_{nullptr};
  QPushButton* loadButton_{nullptr};
  QPushButton* saveButton_{nullptr};
  QPushButton* newButton_{nullptr};
  QPushButton* planButton_{nullptr};
  QPushButton* runButton_{nullptr};
  QPushButton* stopButton_{nullptr};
  QProgressBar* runProgress_{nullptr};
  std::shared_ptr<std::atomic_bool> stopRequested_;

};

}  // namespace omnidrop
