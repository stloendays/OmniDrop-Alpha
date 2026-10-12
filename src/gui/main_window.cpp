#include "gui/main_window.hpp"

#include "gui/drop_zone.hpp"
#include "gui/translation_dialog.hpp"
#include "gui/workflow_dialog.hpp"
#include "gui/rename_preview_dialog.hpp"
#include "gui/rename_history_dialog.hpp"
#include "gui/settings_dialog.hpp"
#include "gui/update_check_dialog.hpp"

#include "adapters/diagnostics_service.hpp"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QColor>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenuBar>
#include <QMetaObject>
#include <QMessageBox>
#include <QPair>
#include <QProgressBar>
#include <QSplitter>
#include <QSizePolicy>
#include <QPushButton>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QWidget>
#include <QtConcurrent/QtConcurrentRun>

namespace omnidrop {

namespace {

QSet<QString> parseCapabilities(const WorkerResult& result) {
  QSet<QString> out;
  if (!result.ok) return out;
  const auto doc = QJsonDocument::fromJson(result.output.toUtf8());
  for (const auto& value : doc.object().value("actions").toArray()) out.insert(value.toString());
  return out;
}

QString workerResultDetail(const ActionDescriptor& action, const WorkerResult& result) {
  if (!result.ok) return result.error;

  const auto object = QJsonDocument::fromJson(result.output.toUtf8()).object();
  if (action.id == "file.sha256") {
    return "SHA-256: " + object.value("sha256").toString();
  }
  return "Created: " + object.value("output_path").toString();
}

using BatchWorkerResults = QList<QPair<QString, WorkerResult>>;

struct PerFileBatchExecution {
  BatchWorkerResults results;
  BatchProgress progress;
};

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
  setWindowTitle(QString("OmniDrop %1").arg(OMNIDROP_VERSION));
  resize(1080, 740);
  setMinimumSize(800, 570);

  auto* root = new QWidget(this);
  root->setObjectName("workspaceRoot");
  auto* layout = new QVBoxLayout(root);
  layout->setContentsMargins(24, 20, 24, 16);
  layout->setSpacing(12);

  // The quick actions and main workflow remain visible without opening menus.
  auto* header = new QHBoxLayout;
  header->setSpacing(10);
  auto* brandBlock = new QVBoxLayout;
  brandBlock->setSpacing(2);
  auto* brand = new QLabel("OmniDrop", root);
  brand->setObjectName("brand");
  brandBlock->addWidget(brand);
  auto* releaseLabel = new QLabel(
      QString("LOCAL WORKSPACE   /   v%1").arg(OMNIDROP_VERSION), root);
  releaseLabel->setObjectName("eyebrow");
  brandBlock->addWidget(releaseLabel);
  header->addLayout(brandBlock);
  header->addStretch();
  auto* workflowButton = new QPushButton("Workflow", root);
  workflowButton->setObjectName("secondaryButton");
  workflowButton->setToolTip("Build and inspect reusable local workflows");
  header->addWidget(workflowButton);
  auto* translateButton = new QPushButton("Translate", root);
  translateButton->setObjectName("secondaryButton");
  translateButton->setToolTip("Translate text or subtitles with selected providers");
  header->addWidget(translateButton);
  auto* openButton = new QPushButton("Open files", root);
  openButton->setObjectName("openFilesButton");
  openButton->setToolTip("Choose one or more local files (Ctrl+O)");
  header->addWidget(openButton);
  layout->addLayout(header);

  auto* intro = new QLabel("Select files to discover available actions", root);
  intro->setObjectName("workspaceIntro");
  layout->addWidget(intro);

  // Two independent, resizable surfaces keep the file selection and its
  // applicable actions visible at the same time, even for long file lists.
  auto* splitter = new QSplitter(Qt::Horizontal, root);
  splitter->setObjectName("workspaceSplitter");
  splitter->setChildrenCollapsible(false);
  splitter->setHandleWidth(12);

  auto* inputPane = new QFrame(splitter);
  inputPane->setObjectName("inputPane");
  auto* inputLayout = new QVBoxLayout(inputPane);
  inputLayout->setContentsMargins(18, 16, 18, 16);
  inputLayout->setSpacing(10);
  auto* inputHeading = new QLabel("INPUT FILES", inputPane);
  inputHeading->setObjectName("sectionTitle");
  inputLayout->addWidget(inputHeading);
  dropZone_ = new DropZone(inputPane);
  dropZone_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  inputLayout->addWidget(dropZone_, 1);
  recentTitle_ = new QLabel("RECENT FILES", inputPane);
  recentTitle_->setObjectName("sectionTitle");
  recentList_ = new QListWidget(inputPane);
  recentList_->setObjectName("recentFilesList");
  recentList_->setMaximumHeight(138);
  recentList_->setToolTip("Double-click to inspect a recent file");
  inputLayout->addWidget(recentTitle_);
  inputLayout->addWidget(recentList_);

  auto* actionPane = new QFrame(splitter);
  actionPane->setObjectName("actionPane");
  auto* actionLayout = new QVBoxLayout(actionPane);
  actionLayout->setContentsMargins(18, 16, 18, 16);
  actionLayout->setSpacing(10);
  auto* actionHeading = new QLabel("AVAILABLE ACTIONS", actionPane);
  actionHeading->setObjectName("sectionTitle");
  actionLayout->addWidget(actionHeading);

  fileTitle_ = new QLabel(actionPane);
  fileTitle_->setObjectName("fileTitle");
  fileMeta_ = new QLabel(actionPane);
  fileMeta_->setObjectName("muted");
  fileMeta_->setWordWrap(true);
  actionSummary_ = new QLabel("No files selected", actionPane);
  actionSummary_->setObjectName("actionSummary");
  actionLayout->addWidget(fileTitle_);
  actionLayout->addWidget(fileMeta_);
  actionLayout->addWidget(actionSummary_);

  actionSearch_ = new QLineEdit(actionPane);
  actionSearch_->setObjectName("actionSearch");
  actionSearch_->setPlaceholderText("Find an action by name or ID  (Ctrl+F)");
  actionSearch_->setClearButtonEnabled(true);
  actionLayout->addWidget(actionSearch_);

  emptyActionHint_ = new QLabel(
      "Choose local files or drag them into the workspace.\n"
      "OmniDrop will show applicable actions before processing.", actionPane);
  emptyActionHint_->setObjectName("actionEmptyState");
  emptyActionHint_->setWordWrap(true);
  emptyActionHint_->setAlignment(Qt::AlignCenter);
  actionLayout->addWidget(emptyActionHint_, 1);

  actionList_ = new QListWidget(actionPane);
  actionList_->setObjectName("actionList");
  actionList_->setToolTip("Unavailable actions are shown in grey; hover for details");
  actionLayout->addWidget(actionList_, 1);

  runButton_ = new QPushButton("Select an action", actionPane);
  runButton_->setObjectName("runActionButton");
  runButton_->setEnabled(false);
  runButton_->setToolTip("Run the selected available action (Ctrl+Return)");
  stopButton_ = new QPushButton("Stop after current file", actionPane);
  stopButton_->setObjectName("secondaryButton");
  stopButton_->hide();
  progressBar_ = new QProgressBar(actionPane);
  progressBar_->setObjectName("batchProgress");
  progressBar_->setTextVisible(true);
  progressBar_->hide();

  auto* actionControls = new QHBoxLayout;
  actionControls->addWidget(progressBar_, 1);
  actionControls->addWidget(stopButton_);
  actionControls->addWidget(runButton_);
  actionLayout->addLayout(actionControls);

  splitter->addWidget(inputPane);
  splitter->addWidget(actionPane);
  splitter->setStretchFactor(0, 4);
  splitter->setStretchFactor(1, 6);
  splitter->setSizes({410, 610});
  layout->addWidget(splitter, 1);

  historyTitle_ = new QLabel("RECENT ACTIVITY", root);
  historyTitle_->setObjectName("sectionTitle");
  historyList_ = new QListWidget(root);
  historyList_->setObjectName("activityList");
  historyList_->setMaximumHeight(108);
  layout->addWidget(historyTitle_);
  layout->addWidget(historyList_);

  fileTitle_->hide();
  fileMeta_->hide();
  actionSearch_->hide();
  actionList_->hide();
  runButton_->hide();

  setCentralWidget(root);
  statusLabel_ = new QLabel("Checking local processors...", this);
  statusBar()->addWidget(statusLabel_, 1);

  auto* fileMenu = menuBar()->addMenu("File");
  auto* openAction = fileMenu->addAction("Open files...");
  openAction->setShortcut(QKeySequence::Open);
  fileMenu->addSeparator();
  auto* quitAction = fileMenu->addAction("Exit");

  auto* toolsMenu = menuBar()->addMenu("Tools");
  auto* translateAction = toolsMenu->addAction("Translate text or subtitles...");
  auto* workflowAction = toolsMenu->addAction("Workflow builder...");
  auto* renamePreviewAction = toolsMenu->addAction("Batch rename preview...");
  auto* renameHistoryAction = toolsMenu->addAction("Rename history and recovery...");
  toolsMenu->addSeparator();
  auto* settingsAction = toolsMenu->addAction("Preferences and backup...");

  auto* historyMenu = menuBar()->addMenu("History");
  historyMenu->addAction(renameHistoryAction);
  historyMenu->addSeparator();
  auto* clearRecent = historyMenu->addAction("Clear recent files");
  auto* clearActivity = historyMenu->addAction("Clear activity");

  auto* helpMenu = menuBar()->addMenu("Help");
  auto* updateAction = helpMenu->addAction("Check for updates...");
  auto* copyDiagnosticsAction = helpMenu->addAction("Copy diagnostics");
  helpMenu->addSeparator();
  auto* aboutAction = helpMenu->addAction("About OmniDrop");

  const auto chooseFiles = [this] {
    const auto paths = QFileDialog::getOpenFileNames(this, "Open files");
    if (!paths.isEmpty()) inspectPaths(paths);
  };

  const auto openTranslation = [this] {
    const auto preferred = currentBatch_.paths.size() == 1
        ? currentBatch_.paths.first()
        : QString{};
    auto* dialog = new TranslationDialog(preferred, this);
    dialog->show();
  };
  const auto openWorkflow = [this] {
    auto* dialog = new WorkflowDialog(currentBatch_.paths, this);
    dialog->show();
  };
  connect(workflowButton, &QPushButton::clicked, this, openWorkflow);
  connect(workflowAction, &QAction::triggered, this, openWorkflow);
  connect(renamePreviewAction, &QAction::triggered, this, [this] {
    auto* dialog = new RenamePreviewDialog(currentBatch_.paths, this);
    dialog->show();
  });
  connect(renameHistoryAction, &QAction::triggered, this, [this] {
    auto* dialog = new RenameHistoryDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
  });
  connect(settingsAction, &QAction::triggered, this, [this] {
    auto* dialog = new SettingsDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog, &QDialog::finished, this, [this](int) {
      refreshRecentFiles();
      refreshHistory();
    });
    dialog->show();
  });
  connect(translateButton, &QPushButton::clicked, this, openTranslation);
  connect(translateAction, &QAction::triggered, this, openTranslation);
  connect(openButton, &QPushButton::clicked, this, chooseFiles);
  connect(openAction, &QAction::triggered, this, chooseFiles);
  connect(quitAction, &QAction::triggered, this, &QWidget::close);
  connect(dropZone_, &DropZone::filesDropped, this, [this](const QStringList& paths) {
    inspectPaths(paths);
  });
  connect(dropZone_, &DropZone::filesRejected, this, [this](const QString& reason) {
    statusLabel_->setText(reason);
  });
  connect(recentList_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
    inspectPath(item->data(Qt::UserRole).toString());
  });
  connect(actionSearch_, &QLineEdit::textChanged, this, &MainWindow::filterActions);
  connect(runButton_, &QPushButton::clicked, this, &MainWindow::runSelectedAction);
  connect(stopButton_, &QPushButton::clicked, this, &MainWindow::requestStopCurrentBatch);
  connect(actionList_, &QListWidget::currentRowChanged, this, [this](int row) {
    const bool valid = row >= 0 && row < currentBatch_.actions.size() &&
                       !actionList_->item(row)->isHidden() &&
                       currentBatch_.actions.at(row).available;
    runButton_->setEnabled(valid);
    if (valid) {
      const auto& action = currentBatch_.actions.at(row);
      runButton_->setText(currentBatch_.paths.size() > 1
          ? QString("Run %1 on %2 files").arg(action.label).arg(currentBatch_.paths.size())
          : QString("Run %1").arg(action.label));
    }
  });
  connect(clearRecent, &QAction::triggered, this, [this] { recentFiles_.clear(); refreshRecentFiles(); });
  connect(clearActivity, &QAction::triggered, this, [this] { history_.clear(); refreshHistory(); });
  connect(updateAction, &QAction::triggered, this, [this] {
    auto* dialog = new UpdateCheckDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
  });
  connect(copyDiagnosticsAction, &QAction::triggered, this, [this] {
    statusLabel_->setText("Collecting diagnostics...");
    auto* watcher = new QFutureWatcher<QByteArray>(this);
    connect(watcher, &QFutureWatcher<QByteArray>::finished, this, [this, watcher] {
      const auto payload = watcher->result();
      watcher->deleteLater();
      QApplication::clipboard()->setText(QString::fromUtf8(payload));
      statusLabel_->setText("Diagnostics copied to clipboard.");
    });
    watcher->setFuture(QtConcurrent::run([] {
      DiagnosticsService diagnostics;
      return diagnostics.collectJson(QJsonDocument::Indented);
    }));
  });
  connect(aboutAction, &QAction::triggered, this, [this] {
    QMessageBox::about(this, "About OmniDrop",
      QString("<b>OmniDrop %1</b><br><br>Local-first file utilities. Core operations do not require an account or file upload.")
        .arg(OMNIDROP_VERSION));
  });

  auto* runShortcut = new QAction(this);
  runShortcut->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return));
  addAction(runShortcut);
  connect(runShortcut, &QAction::triggered, this, [this] {
    if (!operationRunning_ && runButton_->isEnabled()) runSelectedAction();
  });

  auto* findAction = new QAction(this);
  findAction->setShortcut(QKeySequence::Find);
  addAction(findAction);
  connect(findAction, &QAction::triggered, this, [this] {
    if (actionSearch_->isVisible()) {
      actionSearch_->setFocus();
      actionSearch_->selectAll();
    }
  });

  setStyleSheet(R"(
    QMainWindow, QWidget#workspaceRoot { background: #f5f5f5; color: #171717; font-family: "Segoe UI"; font-size: 13px; }
    QMenuBar, QMenu { background: #f5f5f5; color: #171717; }
    QMenuBar::item:selected, QMenu::item:selected { background: #e8e8e8; }
    QLabel#brand { font-size: 25px; font-weight: 700; }
    QLabel#eyebrow { font-size: 10px; color: #717171; font-weight: 600; }
    QLabel#workspaceIntro { font-size: 14px; color: #555; }
    QLabel#sectionTitle { color: #626262; font-size: 11px; font-weight: 700; }
    QLabel#fileTitle { font-size: 18px; font-weight: 650; }
    QLabel#muted, QLabel#actionSummary { color: #6a6a6a; }
    QLabel#actionEmptyState { color: #747474; padding: 20px; }
    QFrame#inputPane, QFrame#actionPane { background: #fff; border: 1px solid #dedede; border-radius: 11px; }
    QFrame#dropZone { background: #f9f9f9; border: 1px dashed #bcbcbc; border-radius: 10px; }
    QFrame#dropZone[dragActive="true"] { background: #ededed; border: 2px dashed #555; }
    QLabel#dropTitle { font-size: 18px; font-weight: 650; }
    QLabel#dropSubtitle { font-size: 12px; color: #747474; }
    QListWidget, QLineEdit { background: #fff; border: 1px solid #dadada; border-radius: 7px; padding: 6px; }
    QListWidget::item { min-height: 25px; padding: 3px 5px; }
    QListWidget::item:selected { color: #111; background: #e8e8e8; border-radius: 4px; }
    QListWidget::item:hover { background: #f2f2f2; }
    QLineEdit:focus, QListWidget:focus { border: 1px solid #666; }
    QPushButton { background: #202020; color: #fff; border: 1px solid #202020; border-radius: 7px; min-height: 34px; padding: 0 14px; font-weight: 600; }
    QPushButton#secondaryButton { background: #f1f1f1; color: #292929; border: 1px solid #d5d5d5; }
    QPushButton:hover:!disabled { background: #444; color: #fff; }
    QPushButton#secondaryButton:hover:!disabled { background: #e5e5e5; color: #151515; }
    QPushButton:focus { border: 2px solid #777; }
    QPushButton:disabled { background: #d8d8d8; border-color: #d8d8d8; color: #777; }
    QSplitter::handle { background: #f5f5f5; }
    QProgressBar { background: #eee; border: 1px solid #dadada; border-radius: 5px; text-align: center; min-height: 27px; }
    QProgressBar::chunk { background: #666; border-radius: 4px; }
    QStatusBar { background: #f5f5f5; color: #555; }
  )");

  refreshRecentFiles();
  refreshHistory();
  probeWorkerCapabilities();
}

void MainWindow::openPaths(const QStringList& paths) {
  inspectPaths(paths);
}

QString MainWindow::formatSize(qint64 bytes) const {
  const double kib = 1024.0, mib = kib * 1024.0, gib = mib * 1024.0;
  if (bytes >= gib) return QString::number(bytes / gib, 'f', 2) + " GB";
  if (bytes >= mib) return QString::number(bytes / mib, 'f', 2) + " MB";
  if (bytes >= kib) return QString::number(bytes / kib, 'f', 1) + " KB";
  return QString::number(bytes) + " B";
}

void MainWindow::probeWorkerCapabilities() {
  const auto worker = worker_;
  auto* watcher = new QFutureWatcher<WorkerResult>(this);
  connect(watcher, &QFutureWatcher<WorkerResult>::finished, this, [this, watcher] {
    const auto result = watcher->result();
    runtimeCapabilities_ = parseCapabilities(result);
    capabilityProbeComplete_ = true;
    watcher->deleteLater();

    if (result.ok) {
      statusLabel_->setText(QString("Local processors ready - %1 actions available.").arg(runtimeCapabilities_.size()));
    } else {
      statusLabel_->setText("Local worker unavailable: " + result.error);
    }

    if (!currentBatch_.paths.isEmpty()) {
      showBatchInspection(service_.inspectMany(currentBatch_.paths, runtimeCapabilities_));
    }
  });
  watcher->setFuture(QtConcurrent::run([worker] { return worker.capabilities(); }));
}

void MainWindow::inspectPath(const QString& path) {
  inspectPaths({path});
}

void MainWindow::inspectPaths(const QStringList& paths) {
  if (operationRunning_) {
    statusLabel_->setText("An operation is already running. Stop it or let it finish before changing files.");
    return;
  }

  QStringList validPaths;
  QSet<QString> seen;
  int rejected = 0;

  for (const auto& path : paths) {
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile()) {
      ++rejected;
      continue;
    }

    const auto normalized = info.absoluteFilePath();
    if (seen.contains(normalized)) continue;
    seen.insert(normalized);
    validPaths.push_back(normalized);
    recentFiles_.add(normalized);
  }

  if (validPaths.isEmpty()) {
    statusLabel_->setText("No readable files were selected.");
    return;
  }

  refreshRecentFiles();
  showBatchInspection(service_.inspectMany(
      validPaths,
      capabilityProbeComplete_ ? runtimeCapabilities_ : QSet<QString>{}));

  if (rejected > 0) {
    statusLabel_->setText(QString("Ready: %1 file(s); %2 unsupported item(s) ignored.")
                              .arg(validPaths.size())
                              .arg(rejected));
  }
}

void MainWindow::showBatchInspection(const BatchInspection& inspection) {
  currentBatch_ = inspection;

  if (inspection.files.size() == 1) {
    const auto& file = inspection.files.first();
    fileTitle_->setText(file.name);
    fileMeta_->setText(QString("%1  /  %2").arg(toString(file.kind), formatSize(file.sizeBytes)));
  } else {
    QSet<QString> kinds;
    for (const auto& file : inspection.files) kinds.insert(toString(file.kind));
    QStringList kindNames;
    for (const auto& kind : kinds) kindNames.push_back(kind);
    kindNames.sort();

    fileTitle_->setText(QString("%1 files selected").arg(inspection.files.size()));
    fileMeta_->setText(QString("%1  /  %2  /  available actions")
                           .arg(formatSize(inspection.totalBytes), kindNames.join(", ")));
  }

  fileTitle_->show();
  fileMeta_->show();
  actionSearch_->show();
  actionList_->show();
  emptyActionHint_->hide();
  runButton_->show();
  actionList_->clear();

  for (const auto& action : inspection.actions) {
    auto* item = new QListWidgetItem(action.label);
    item->setToolTip(action.description);
    if (!action.available) {
      item->setText(action.label + (action.backend == "planned" ? "   [planned]" : "   [unavailable]"));
      item->setForeground(QColor("#777"));
    }
    actionList_->addItem(item);
  }

  filterActions(actionSearch_->text());

  if (inspection.paths.size() == 1) {
    statusLabel_->setText(QString("Ready: %1").arg(inspection.paths.first()));
  } else {
    statusLabel_->setText(
        QString("Ready: %1 files. Actions shown are valid for every selected file.")
            .arg(inspection.paths.size()));
  }
}

void MainWindow::filterActions(const QString& query) {
  const auto needle = query.trimmed();
  int first = -1;
  int matches = 0;
  int ready = 0;
  for (int row = 0; row < currentBatch_.actions.size(); ++row) {
    const auto& action = currentBatch_.actions.at(row);
    const bool match = needle.isEmpty() ||
        action.label.contains(needle, Qt::CaseInsensitive) ||
        action.description.contains(needle, Qt::CaseInsensitive) ||
        action.id.contains(needle, Qt::CaseInsensitive);
    actionList_->item(row)->setHidden(!match);
    if (!match) continue;
    ++matches;
    if (action.available) {
      ++ready;
      if (first < 0) first = row;
    }
  }
  actionList_->setCurrentRow(first);
  if (currentBatch_.paths.isEmpty()) return;

  actionSummary_->setText(
      QString("%1 matching actions   /   %2 available").arg(matches).arg(ready));
  const bool empty = matches == 0;
  emptyActionHint_->setText(empty
      ? "No actions match this search. Try a different name or action ID."
      : QString{});
  emptyActionHint_->setVisible(empty);
  actionList_->setVisible(!empty);
}

void MainWindow::setOperationRunning(bool running, bool cancellable, int totalItems) {
  operationRunning_ = running;
  actionList_->setEnabled(!running);
  actionSearch_->setEnabled(!running);
  dropZone_->setEnabled(!running);
  runButton_->setEnabled(false);

  if (!running) {
    currentCancellation_.reset();
    stopButton_->hide();
    progressBar_->hide();
    progressBar_->setRange(0, 1);
    progressBar_->setValue(0);
    return;
  }

  progressBar_->show();
  if (cancellable) {
    currentCancellation_ = std::make_shared<BatchCancellation>();
    progressBar_->setRange(0, totalItems);
    progressBar_->setValue(0);
    progressBar_->setFormat("%v / %m");
    stopButton_->setEnabled(true);
    stopButton_->show();
  } else {
    currentCancellation_.reset();
    progressBar_->setRange(0, 0);
    progressBar_->setFormat("Working...");
    stopButton_->hide();
  }
}

void MainWindow::requestStopCurrentBatch() {
  if (!currentCancellation_) return;
  currentCancellation_->requestStop();
  stopButton_->setEnabled(false);
  statusLabel_->setText("Stopping after the current file finishes...");
}

void MainWindow::runSelectedAction() {
  const int row = actionList_->currentRow();
  if (row < 0 || row >= currentBatch_.actions.size()) return;

  const auto action = currentBatch_.actions.at(row);
  if (!action.available || currentBatch_.paths.isEmpty()) return;

  const auto worker = worker_;
  const auto paths = currentBatch_.paths;
  const bool cancellable = action.scope == ActionScope::PerFile && paths.size() > 1;
  setOperationRunning(true, cancellable, paths.size());
  statusLabel_->setText(paths.size() > 1
      ? QString("Running %1 on %2 files...").arg(action.label).arg(paths.size())
      : QString("Running %1...").arg(action.label));

  if (action.scope == ActionScope::Batch) {
    auto* watcher = new QFutureWatcher<WorkerResult>(this);
    connect(watcher, &QFutureWatcher<WorkerResult>::finished, this,
            [this, watcher, action, paths] {
      const auto result = watcher->result();
      watcher->deleteLater();

      setOperationRunning(false, false);

      const auto detail = workerResultDetail(action, result);
      for (const auto& path : paths) {
        history_.record(action.label, path, result.ok, detail);
      }

      refreshHistory();
      actionList_->setCurrentRow(-1);
      filterActions(actionSearch_->text());

      statusLabel_->setText(
          result.ok
              ? QString("Batch complete: %1 input files. %2").arg(paths.size()).arg(detail)
              : "Failed: " + detail);
    });

    watcher->setFuture(QtConcurrent::run([worker, actionId = action.id, paths] {
      return worker.runBatchAction(actionId, paths);
    }));
    return;
  }

  const auto cancellation = currentCancellation_
      ? currentCancellation_
      : std::make_shared<BatchCancellation>();

  auto* watcher = new QFutureWatcher<PerFileBatchExecution>(this);
  connect(watcher, &QFutureWatcher<PerFileBatchExecution>::finished, this,
          [this, watcher, action] {
    const auto execution = watcher->result();
    watcher->deleteLater();
    setOperationRunning(false, false);

    QString singleDetail;
    for (const auto& entry : execution.results) {
      const auto& path = entry.first;
      const auto& result = entry.second;
      const auto detail = workerResultDetail(action, result);
      history_.record(action.label, path, result.ok, detail);
      if (execution.results.size() == 1) singleDetail = detail;
    }

    refreshHistory();
    actionList_->setCurrentRow(-1);
    filterActions(actionSearch_->text());

    if (execution.progress.stopped) {
      statusLabel_->setText(
          QString("Stopped: %1 of %2 processed; %3 succeeded, %4 failed.")
              .arg(execution.progress.processed)
              .arg(execution.progress.total)
              .arg(execution.progress.succeeded)
              .arg(execution.progress.failed));
    } else if (execution.results.size() == 1) {
      statusLabel_->setText(execution.progress.failed == 0 ? singleDetail : "Failed: " + singleDetail);
    } else {
      statusLabel_->setText(
          QString("Batch complete: %1 succeeded, %2 failed.")
              .arg(execution.progress.succeeded)
              .arg(execution.progress.failed));
    }
  });

  watcher->setFuture(QtConcurrent::run(
      [this, worker, actionId = action.id, paths, cancellation] {
        PerFileBatchExecution execution;
        execution.results.reserve(paths.size());

        execution.progress = runSequentialBatch(
            paths,
            *cancellation,
            [&](const QString& path) {
              const auto result = worker.runAction(actionId, path);
              execution.results.push_back(qMakePair(path, result));
              return result.ok;
            },
            [this](const BatchProgress& progress) {
              QMetaObject::invokeMethod(
                  this,
                  [this, progress] {
                    if (!operationRunning_) return;
                    if (progressBar_->maximum() > 0) {
                      progressBar_->setValue(progress.processed);
                      progressBar_->setFormat(
                          QString("%1 / %2").arg(progress.processed).arg(progress.total));
                    }
                    statusLabel_->setText(
                        QString("Processed %1 of %2 · %3 succeeded · %4 failed")
                            .arg(progress.processed)
                            .arg(progress.total)
                            .arg(progress.succeeded)
                            .arg(progress.failed));
                  },
                  Qt::QueuedConnection);
            });

        return execution;
      }));
}

void MainWindow::refreshRecentFiles() {
  recentList_->clear();
  const auto recent = recentFiles_.load();
  for (const auto& path : recent) {
    auto* item = new QListWidgetItem(QFileInfo(path).fileName());
    item->setData(Qt::UserRole, path);
    item->setToolTip(path);
    recentList_->addItem(item);
  }
  const bool visible = !recent.isEmpty();
  recentTitle_->setVisible(visible);
  recentList_->setVisible(visible);
}

void MainWindow::refreshHistory() {
  historyList_->clear();
  const auto entries = history_.loadDisplayEntries();
  historyList_->addItems(entries);
  const bool visible = !entries.isEmpty();
  historyTitle_->setVisible(visible);
  historyList_->setVisible(visible);
}

}  // namespace omnidrop
