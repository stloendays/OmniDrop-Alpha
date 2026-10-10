#include "gui/workflow_dialog.hpp"

#include "app/action_catalog.hpp"
#include "gui/workflow_graph_view.hpp"

#include <QColor>
#include <QDateTime>
#include <QCloseEvent>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QSet>
#include <QSplitter>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <QtConcurrent/QtConcurrentRun>

namespace omnidrop {

WorkflowDialog::WorkflowDialog(const QStringList& selectedFiles, QWidget* parent)
    : QDialog(parent), inputs_(selectedFiles) {
  setWindowTitle("OmniDrop Workflow");
  setMinimumSize(820, 560);
  resize(1000, 690);
  setAttribute(Qt::WA_DeleteOnClose);

  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(24, 20, 24, 20);
  root->setSpacing(12);

  auto* title = new QLabel("Workflow Builder", this);
  title->setObjectName("workflowTitle");
  root->addWidget(title);

  auto* summary = new QLabel(
      "Connect local file actions. Preview the plan before executing. Originals stay untouched.",
      this);
  summary->setObjectName("muted");
  root->addWidget(summary);

  auto* top = new QHBoxLayout;
  nameEdit_ = new QLineEdit("New workflow", this);
  auto* nameLabel = new QLabel("Name", this);
  top->addWidget(nameLabel);
  top->addWidget(nameEdit_, 1);
  newButton_ = new QPushButton("New", this);
  loadButton_ = new QPushButton("Load...", this);
  saveButton_ = new QPushButton("Save...", this);
  top->addWidget(newButton_);
  top->addWidget(loadButton_);
  top->addWidget(saveButton_);
  root->addLayout(top);

  auto* inputLine = new QHBoxLayout;
  inputsLabel_ = new QLabel(this);
  inputsLabel_->setObjectName("muted");
  inputsLabel_->setWordWrap(true);
  chooseButton_ = new QPushButton("Choose files...", this);
  inputLine->addWidget(inputsLabel_, 1);
  inputLine->addWidget(chooseButton_);
  root->addLayout(inputLine);
  watchStatusLabel_ = new QLabel(this);
  watchStatusLabel_->setObjectName("muted");
  watchStatusLabel_->setWordWrap(true);
  watchStatusLabel_->setText("Folder watcher: off");
  watchStatusLabel_->hide();
  root->addWidget(watchStatusLabel_);
  watchTimer_ = new QTimer(this);
  watchTimer_->setInterval(650);

  auto* split = new QSplitter(Qt::Horizontal, this);
  auto* left = new QWidget(split);
  auto* leftLayout = new QVBoxLayout(left);
  leftLayout->setContentsMargins(0, 0, 8, 0);
  leftLayout->setSpacing(9);

  auto* stepsTitle = new QLabel("Steps", left);
  stepsTitle->setObjectName("workflowSection");
  leftLayout->addWidget(stepsTitle);

  templateCombo_ = new QComboBox(left);
  templateCombo_->addItems({
      "Start from a blank workflow",
      "Recipe: Clean and deduplicate text",
      "Recipe: Convert and compress images",
      "Recipe: PDF to clean text",
      "Recipe: Merge PDFs and extract text",
      "Recipe: PDF text to spoken WAV (voice pack required)",
  });
  leftLayout->addWidget(templateCombo_);

  stepsList_ = new QListWidget(left);
  stepsList_->setSelectionMode(QAbstractItemView::SingleSelection);
  stepsList_->setToolTip("Steps are connected in order. Load a JSON DAG for branching workflows.");
  leftLayout->addWidget(stepsList_, 1);

  actionCombo_ = new QComboBox(left);
  actionCombo_->setMaxVisibleItems(18);
  QSet<QString> seen;
  ActionCatalog catalog;
  for (const auto kind : {FileKind::Text, FileKind::Developer, FileKind::Pdf,
                          FileKind::Image, FileKind::Archive}) {
    for (const auto& action : catalog.recommendedActions(kind)) {
      if (action.backend == "planned" || seen.contains(action.id)) continue;
      seen.insert(action.id);
      actionCombo_->addItem(action.label + "  ·  " + action.id, action.id);
    }
  }
  for (const auto& action : catalog.recommendedBatchActions({FileKind::Pdf, FileKind::Pdf})) {
    if (seen.contains(action.id) || action.backend == "planned") continue;
    actionCombo_->addItem(action.label + "  ·  " + action.id, action.id);
  }
  leftLayout->addWidget(actionCombo_);

  auto* stepButtons = new QHBoxLayout;
  addButton_ = new QPushButton("Add step", left);
  removeButton_ = new QPushButton("Remove", left);
  upButton_ = new QPushButton("Move up", left);
  downButton_ = new QPushButton("Move down", left);
  for (auto* button : {addButton_, removeButton_, upButton_, downButton_}) {
    stepButtons->addWidget(button);
  }
  leftLayout->addLayout(stepButtons);
  graphEditButton_ = new QPushButton("Edit connections on graph", left);
  graphEditButton_->setToolTip(
      "Switch to visual DAG editing: drag nodes and connect their ports.");
  leftLayout->addWidget(graphEditButton_);

  auto* right = new QWidget(split);
  auto* rightLayout = new QVBoxLayout(right);
  rightLayout->setContentsMargins(8, 0, 0, 0);
  auto* outputTitle = new QLabel("Workflow graph and execution", right);
  outputTitle->setObjectName("workflowSection");
  rightLayout->addWidget(outputTitle);
  rightTabs_ = new QTabWidget(right);
  graphView_ = new WorkflowGraphView(rightTabs_);
  rightTabs_->addTab(graphView_, "Graph");
  outputView_ = new QPlainTextEdit(rightTabs_);
  outputView_->setReadOnly(true);
  outputView_->setPlaceholderText(
      "Preview checks dependencies without writing files.\n"
      "Run shows completed nodes, output paths and partial results.");
  rightTabs_->addTab(outputView_, "Execution results");
  auto* jobsPanel = new QWidget(rightTabs_);
  auto* jobsLayout = new QVBoxLayout(jobsPanel);
  jobsLayout->setContentsMargins(10, 8, 10, 8);
  auto* jobsHint = new QLabel(
      "Jobs are stored locally and survive restarts. Retry reruns all steps with new outputs.",
      jobsPanel);
  jobsHint->setObjectName("muted");
  jobsHint->setWordWrap(true);
  jobsLayout->addWidget(jobsHint);
  jobsList_ = new QListWidget(jobsPanel);
  jobsList_->setSelectionMode(QAbstractItemView::SingleSelection);
  jobsLayout->addWidget(jobsList_, 1);
  auto* eventsLabel = new QLabel("Selected job history", jobsPanel);
  eventsLabel->setObjectName("workflowSection");
  jobsLayout->addWidget(eventsLabel);
  jobEventsView_ = new QPlainTextEdit(jobsPanel);
  jobEventsView_->setReadOnly(true);
  jobEventsView_->setMaximumHeight(155);
  jobEventsView_->setPlaceholderText("Select a saved job to view its local event timeline.");
  jobsLayout->addWidget(jobEventsView_);
  auto* jobControls = new QHBoxLayout;
  refreshJobsButton_ = new QPushButton("Refresh", jobsPanel);
  executeJobButton_ = new QPushButton("Run / Retry", jobsPanel);
  removeJobButton_ = new QPushButton("Remove record", jobsPanel);
  jobControls->addWidget(refreshJobsButton_);
  jobControls->addWidget(executeJobButton_);
  jobControls->addWidget(removeJobButton_);
  jobsLayout->addLayout(jobControls);
  rightTabs_->addTab(jobsPanel, "Jobs");
  rightLayout->addWidget(rightTabs_, 1);
  split->addWidget(left);
  split->addWidget(right);
  split->setStretchFactor(0, 1);
  split->setStretchFactor(1, 1);
  root->addWidget(split, 1);

  statusLabel_ = new QLabel("Ready. Choose a recipe or add a step.", this);
  statusLabel_->setWordWrap(true);
  statusLabel_->setObjectName("muted");
  root->addWidget(statusLabel_);

  runProgress_ = new QProgressBar(this);
  runProgress_->setTextVisible(true);
  runProgress_->hide();
  root->addWidget(runProgress_);

  auto* footer = new QHBoxLayout;
  auto* cancel = new QPushButton("Close", this);
  watchButton_ = new QPushButton("Watch folder...", this);
  watchButton_->setToolTip(
      "Opt in to monitoring one folder. Existing files are ignored; "
      "only new stable arrivals run through local Workflow Jobs.");
  queueButton_ = new QPushButton("Add to Jobs", this);
  queueButton_->setToolTip("Queue the current workflow and file selection for later execution.");
  planButton_ = new QPushButton("Preview plan", this);
  stopButton_ = new QPushButton("Stop after current file", this);
  stopButton_->setEnabled(false);
  stopButton_->hide();
  runButton_ = new QPushButton("Run workflow", this);
  footer->addStretch();
  footer->addWidget(cancel);
  footer->addWidget(watchButton_);
  footer->addWidget(queueButton_);
  footer->addWidget(planButton_);
  footer->addWidget(stopButton_);
  footer->addWidget(runButton_);
  root->addLayout(footer);

  setStyleSheet(R"(
    QDialog, QWidget { background: #f5f5f5; color: #171717; font-family: "Segoe UI"; font-size: 13px; }
    QLabel#workflowTitle { font-size: 21px; font-weight: 650; }
    QLabel#workflowSection { font-size: 14px; font-weight: 600; }
    QLabel#muted { color: #646464; }
    QLineEdit, QComboBox, QListWidget, QPlainTextEdit {
      background: #ffffff; border: 1px solid #d2d2d2; border-radius: 7px; padding: 7px;
      selection-background-color: #e0e0e0; selection-color: #111111;
    }
    QListWidget::item { padding: 8px; }
    QPushButton {
      background: #222222; color: white; border: none; border-radius: 7px;
      min-height: 34px; padding: 0 10px;
    }
    QPushButton:disabled { background: #d0d0d0; color: #666666; }
  )");

  connect(newButton_, &QPushButton::clicked, this, &WorkflowDialog::newWorkflow);
  connect(loadButton_, &QPushButton::clicked, this, &WorkflowDialog::loadWorkflow);
  connect(saveButton_, &QPushButton::clicked, this, &WorkflowDialog::saveWorkflow);
  connect(chooseButton_, &QPushButton::clicked, this, &WorkflowDialog::selectInputs);
  connect(addButton_, &QPushButton::clicked, this, [this] {
    addStep(actionCombo_->currentData().toString());
  });
  connect(removeButton_, &QPushButton::clicked, this, &WorkflowDialog::removeStep);
  connect(graphEditButton_, &QPushButton::clicked, this, &WorkflowDialog::convertToGraph);
  connect(graphView_, &WorkflowGraphView::editRejected, this,
          [this](const QString& message) { showStatus(message, true); });
  connect(graphView_, &WorkflowGraphView::workflowChanged, this,
          [this](const QJsonObject& document) {
            if (busy_) return;
            loadedDefinition_ = document;
            graphMode_ = true;
            refreshGraphSteps();
            showStatus("Connection updated. Preview formats before running.");
          });
  connect(graphView_, &WorkflowGraphView::nodeSelected, this,
          [this](const QString& nodeId) {
            for (int i = 0; i < stepsList_->count(); ++i) {
              if (stepsList_->item(i)->data(Qt::UserRole + 1).toString() == nodeId) {
                stepsList_->setCurrentRow(i);
                break;
              }
            }
          });
  connect(upButton_, &QPushButton::clicked, this, [this] { moveStep(-1); });
  connect(downButton_, &QPushButton::clicked, this, [this] { moveStep(1); });
  connect(templateCombo_, qOverload<int>(&QComboBox::currentIndexChanged),
          this, &WorkflowDialog::applyTemplate);
  connect(stepsList_, &QListWidget::itemSelectionChanged,
          this, &WorkflowDialog::refreshButtons);
  connect(watchButton_, &QPushButton::clicked, this, &WorkflowDialog::toggleFolderWatch);
  connect(watchTimer_, &QTimer::timeout, this, &WorkflowDialog::pollWatchedFolder);
  connect(queueButton_, &QPushButton::clicked, this, &WorkflowDialog::queueWorkflow);
  connect(refreshJobsButton_, &QPushButton::clicked, this, &WorkflowDialog::refreshJobs);
  connect(executeJobButton_, &QPushButton::clicked, this, &WorkflowDialog::executeSelectedJob);
  connect(removeJobButton_, &QPushButton::clicked, this, &WorkflowDialog::removeSelectedJob);
  connect(jobsList_, &QListWidget::itemSelectionChanged, this, &WorkflowDialog::refreshButtons);
  connect(jobsList_, &QListWidget::itemSelectionChanged, this, &WorkflowDialog::showSelectedJobEvents);
  connect(planButton_, &QPushButton::clicked, this, [this] { begin(false); });
  connect(runButton_, &QPushButton::clicked, this, [this] { begin(true); });
  connect(stopButton_, &QPushButton::clicked, this, [this] {
    if (!busy_ || !stopRequested_) return;
    stopRequested_->store(true, std::memory_order_release);
    if (watchEnabled_) {
      watchEnabled_ = false;
      watchTimer_->stop();
      pendingWatchInputs_.clear();
      watchStatusLabel_->hide();
    }
    stopButton_->setEnabled(false);
    showStatus("Stopping safely after the current file action finishes...");
  });
  connect(cancel, &QPushButton::clicked, this, &WorkflowDialog::reject);

  refreshInputs();
  refreshButtons();
  refreshGraph();
  refreshJobs();
}

void WorkflowDialog::refreshInputs() {
  if (inputs_.isEmpty()) {
    inputsLabel_->setText("Input: no files selected");
    return;
  }
  const auto first = QFileInfo(inputs_.first()).fileName();
  inputsLabel_->setText(
      inputs_.size() == 1 ? "Input: " + first
                          : QString("Inputs: %1 files (first: %2)").arg(inputs_.size()).arg(first));
  inputsLabel_->setToolTip(inputs_.join("\n"));
}

void WorkflowDialog::addStep(const QString& actionId) {
  if (busy_ || actionId.isEmpty()) return;
  if (stepsList_->count() >= 24) {
    showStatus("A workflow cannot exceed 24 nodes.", true);
    return;
  }
  if (graphMode_) {
    auto nodes = loadedDefinition_.value("nodes").toArray();
    QSet<QString> usedIds;
    for (const auto& raw : nodes) usedIds.insert(raw.toObject().value("id").toString());
    int index = 1;
    QString id;
    do { id = QString("node_%1").arg(index++); } while (usedIds.contains(id));
    nodes.append(QJsonObject{
        {"id", id},
        {"action_id", actionId},
        {"sources", QJsonArray{QStringLiteral("$input")}},
    });
    loadedDefinition_.insert("nodes", nodes);
    refreshGraphSteps();
    refreshGraph();
    showStatus("Node added. Connect its input to another output, or leave it connected to Files.");
    return;
  }
  auto* item = new QListWidgetItem(
      QString("%1. %2").arg(stepsList_->count() + 1).arg(actionId));
  item->setData(Qt::UserRole, actionId);
  stepsList_->addItem(item);
  stepsList_->setCurrentRow(stepsList_->count() - 1);
  refreshButtons();
  refreshGraph();
}

void WorkflowDialog::removeStep() {
  if (busy_ || stepsList_->currentRow() < 0) return;
  if (graphMode_) {
    const QString id = stepsList_->currentItem()->data(Qt::UserRole + 1).toString();
    const auto previous = loadedDefinition_.value("nodes").toArray();
    QJsonArray updated;
    for (const auto& raw : previous) {
      auto node = raw.toObject();
      if (node.value("id").toString() == id) continue;
      QJsonArray refs;
      for (const auto& ref : node.value("sources").toArray()) {
        if (ref.toString() != id) refs.append(ref);
      }
      if (refs.isEmpty()) refs.append("$input");
      node.insert("sources", refs);
      updated.append(node);
    }
    loadedDefinition_.insert("nodes", updated);
    refreshGraphSteps();
  } else {
    delete stepsList_->takeItem(stepsList_->currentRow());
    for (int index = 0; index < stepsList_->count(); ++index) {
      auto* item = stepsList_->item(index);
      item->setText(QString("%1. %2").arg(index + 1)
                        .arg(item->data(Qt::UserRole).toString()));
    }
  }
  refreshGraph();
  refreshButtons();
}

void WorkflowDialog::convertToGraph() {
  if (busy_ || graphMode_ || stepsList_->count() == 0) return;
  loadedDefinition_ = definition();
  graphMode_ = true;
  refreshGraphSteps();
  refreshGraph();
  rightTabs_->setCurrentWidget(graphView_);
  showStatus("Graph mode: drag from a right port to a left port; right-click links to disconnect.");
  refreshButtons();
}

void WorkflowDialog::refreshGraphSteps() {
  if (!graphMode_) return;
  const auto nodes = loadedDefinition_.value("nodes").toArray();
  stepsList_->clear();
  for (const auto& raw : nodes) {
    const auto node = raw.toObject();
    const auto id = node.value("id").toString();
    const auto action = node.value("action_id").toString();
    auto* item = new QListWidgetItem(id + "  ·  " + action);
    item->setData(Qt::UserRole, action);
    item->setData(Qt::UserRole + 1, id);
    item->setToolTip("Inputs: " + QString::fromUtf8(
        QJsonDocument(node.value("sources").toArray()).toJson(QJsonDocument::Compact)));
    stepsList_->addItem(item);
  }
  refreshButtons();
}

void WorkflowDialog::refreshGraph() {
  if (graphView_) graphView_->setDocument(definition());
}

void WorkflowDialog::moveStep(int offset) {
  if (graphMode_ || busy_) return;
  const auto current = stepsList_->currentRow();
  const auto destination = current + offset;
  if (current < 0 || destination < 0 || destination >= stepsList_->count()) return;
  auto* item = stepsList_->takeItem(current);
  stepsList_->insertItem(destination, item);
  stepsList_->setCurrentRow(destination);
  for (int i = 0; i < stepsList_->count(); ++i) {
    auto* step = stepsList_->item(i);
    step->setText(QString("%1. %2").arg(i + 1).arg(step->data(Qt::UserRole).toString()));
  }
  refreshButtons();
  refreshGraph();
}

void WorkflowDialog::applyTemplate(int preset) {
  if (preset == 0 || busy_) return;
  newWorkflow();
  templateCombo_->blockSignals(true);
  templateCombo_->setCurrentIndex(preset);
  templateCombo_->blockSignals(false);

  const QList<QStringList> recipes{
      {},
      {"text.normalize", "text.deduplicate"},
      {"image.convert_webp", "image.compress"},
      {"pdf.extract_text", "text.normalize"},
      {"pdf.merge", "pdf.extract_text"},
      {"pdf.extract_text", "text.to_speech"},
  };
  if (preset < 0 || preset >= recipes.size()) return;
  nameEdit_->setText(templateCombo_->itemText(preset).mid(QString("Recipe: ").size()));
  for (const auto& actionId : recipes.at(preset)) addStep(actionId);
  showStatus("Recipe loaded. Preview before running.");
}

void WorkflowDialog::newWorkflow() {
  if (busy_) return;
  loadedDefinition_ = {};
  graphMode_ = false;
  nameEdit_->setEnabled(true);
  stepsList_->clear();
  nameEdit_->setText("New workflow");
  templateCombo_->blockSignals(true);
  templateCombo_->setCurrentIndex(0);
  templateCombo_->blockSignals(false);
  outputView_->clear();
  showStatus("Blank workflow ready.");
  refreshButtons();
  refreshGraph();
}

void WorkflowDialog::selectInputs() {
  if (busy_) return;
  const auto paths = QFileDialog::getOpenFileNames(this, "Select workflow input files");
  if (paths.isEmpty()) return;
  inputs_ = paths;
  refreshInputs();
}

QJsonObject WorkflowDialog::definition() const {
  if (graphMode_) {
    auto document = loadedDefinition_;
    document.insert("name", nameEdit_->text().trimmed());
    return document;
  }

  QJsonArray nodes;
  for (int i = 0; i < stepsList_->count(); ++i) {
    QJsonArray sources;
    sources.append(i == 0 ? QStringLiteral("$input")
                          : QString("step_%1").arg(i));
    nodes.append(QJsonObject{
        {"id", QString("step_%1").arg(i + 1)},
        {"action_id", stepsList_->item(i)->data(Qt::UserRole).toString()},
        {"sources", sources},
    });
  }
  return QJsonObject{
      {"schema_version", 1},
      {"name", nameEdit_->text().trimmed()},
      {"nodes", nodes},
  };
}

void WorkflowDialog::loadWorkflow() {
  if (busy_) return;
  const auto path = QFileDialog::getOpenFileName(
      this, "Load workflow", {}, "OmniDrop Workflow (*.omniworkflow.json *.json)");
  if (path.isEmpty()) return;

  const auto loaded = service_.load(path);
  if (!loaded.ok) {
    showStatus(loaded.error, true);
    return;
  }
  const auto check = service_.validate(loaded.document);
  if (!check.ok) {
    showStatus("Invalid workflow: " + check.error, true);
    return;
  }
  loadedDefinition_ = loaded.document;
  graphMode_ = true;
  nameEdit_->setText(loadedDefinition_.value("name").toString());
  refreshGraphSteps();
  outputView_->clear();
  refreshGraph();
  rightTabs_->setCurrentWidget(graphView_);
  showStatus("Loaded editable DAG. Drag from output ports to input ports to edit dependencies.");
  refreshButtons();
}

void WorkflowDialog::saveWorkflow() {
  if (busy_) return;
  const auto workflow = definition();
  const auto path = QFileDialog::getSaveFileName(
      this, "Save workflow", nameEdit_->text().trimmed() + ".omniworkflow.json",
      "OmniDrop Workflow (*.omniworkflow.json)");
  if (path.isEmpty()) return;
  const auto target = path.endsWith(".json", Qt::CaseInsensitive)
      ? path : path + ".omniworkflow.json";
  const auto error = service_.save(target, workflow);
  showStatus(error.isEmpty() ? "Saved: " + target : error, !error.isEmpty());
}

void WorkflowDialog::queueWorkflow() {
  if (busy_ || inputs_.isEmpty() || stepsList_->count() == 0) return;
  busy_ = true;
  refreshButtons();
  showStatus("Validating and saving the workflow job...");

  const auto document = definition();
  const auto paths = inputs_;
  auto* watcher = new QFutureWatcher<WorkerResult>(this);
  connect(watcher, &QFutureWatcher<WorkerResult>::finished, this, [this, watcher] {
    const auto result = watcher->result();
    watcher->deleteLater();
    busy_ = false;
    refreshJobs();
    refreshButtons();
    if (!result.ok) {
      showStatus("Could not queue workflow: " + result.error, true);
      return;
    }
    rightTabs_->setCurrentIndex(2);
    showStatus("Workflow saved to Jobs. Run it now or after restarting OmniDrop.");
  });
  watcher->setFuture(QtConcurrent::run([document, paths] {
    WorkflowJobService jobs;
    return jobs.enqueue(document, paths);
  }));
}

void WorkflowDialog::showSelectedJobEvents() {
  if (!jobEventsView_) return;
  jobEventsView_->clear();
  if (!jobsList_->currentItem()) return;
  const QString id = jobsList_->currentItem()->data(Qt::UserRole).toString();
  const auto response = jobs_.events(id);
  if (!response.ok) {
    jobEventsView_->setPlainText("Timeline is unavailable: " + response.error);
    return;
  }
  const auto document = QJsonDocument::fromJson(response.output.toUtf8()).object();
  QStringList lines;
  for (const auto& raw : document.value("events").toArray()) {
    const auto event = raw.toObject();
    const auto timestamp = event.value("at").toString();
    const auto type = event.value("event").toString();
    const auto node = event.value("node_id").toString();
    const int attempt = event.value("attempt").toInt();
    QString line = QString("%1  |  %2  |  try %3")
        .arg(timestamp, type).arg(attempt);
    if (!node.isEmpty()) line += "  |  " + node;
    if (event.contains("completed_operations") && event.contains("total_operations")) {
      line += QString("  |  %1/%2")
          .arg(event.value("completed_operations").toInt())
          .arg(event.value("total_operations").toInt());
    }
    lines.append(line);
  }
  jobEventsView_->setPlainText(
      lines.isEmpty() ? "No saved events for this older job." : lines.join("\n"));
}

void WorkflowDialog::refreshJobs() {
  const QString selectedId = jobsList_->currentItem()
      ? jobsList_->currentItem()->data(Qt::UserRole).toString() : QString{};
  const auto response = jobs_.listJobs();
  if (!response.ok) {
    showStatus("Workflow job history unavailable: " + response.error, true);
    return;
  }
  const auto data = QJsonDocument::fromJson(response.output.toUtf8()).object();
  const auto jobs = data.value("jobs").toArray();
  jobsList_->clear();
  int restoreRow = -1;
  for (const auto& raw : jobs) {
    const auto entry = raw.toObject();
    const auto state = entry.value("status").toString();
    const auto id = entry.value("id").toString();
    const QString title = QString("%1  |  %2  |  %3/%4  |  attempts: %5")
        .arg(entry.value("name").toString(), state)
        .arg(entry.value("completed_operations").toInt())
        .arg(entry.value("total_operations").toInt())
        .arg(entry.value("attempts").toInt());
    auto* item = new QListWidgetItem(title);
    item->setData(Qt::UserRole, id);
    item->setData(Qt::UserRole + 1, state);
    item->setToolTip(QString("%1\n%2\n%3")
        .arg(id, entry.value("updated_at").toString(),
             entry.value("last_error").toString()));
    if (state == "failed" || state == "interrupted") {
      item->setForeground(QColor("#8f3434"));
    }
    if (id == selectedId) restoreRow = jobsList_->count();
    jobsList_->addItem(item);
  }
  if (restoreRow >= 0) jobsList_->setCurrentRow(restoreRow);
  else if (jobsList_->count() > 0) jobsList_->setCurrentRow(0);
  refreshButtons();
}

void WorkflowDialog::executeSelectedJob() {
  if (busy_ || !jobsList_->currentItem()) return;
  const QString id = jobsList_->currentItem()->data(Qt::UserRole).toString();
  busy_ = true;
  stopRequested_ = std::make_shared<std::atomic_bool>(false);
  runProgress_->setVisible(true);
  runProgress_->setRange(0, 0);
  stopButton_->show();
  outputView_->clear();
  rightTabs_->setCurrentWidget(outputView_);
  refreshButtons();
  showStatus("Running queued workflow...");

  QPointer<WorkflowDialog> guard(this);
  const auto cancellation = stopRequested_;
  auto* watcher = new QFutureWatcher<WorkerResult>(this);
  connect(watcher, &QFutureWatcher<WorkerResult>::finished, this, [this, watcher] {
    const auto result = watcher->result();
    watcher->deleteLater();
    finish(true, result);
    refreshJobs();
  });
  watcher->setFuture(QtConcurrent::run([id, cancellation, guard] {
    WorkflowJobService service;
    return service.execute(
        id,
        [guard](const QJsonObject& event) {
          if (!guard) return;
          QMetaObject::invokeMethod(
              guard.data(),
              [guard, event] {
                if (!guard || !guard->busy_) return;
                const int count = event.value("completed_operations").toInt();
                const int total = event.value("total_operations").toInt();
                if (total > 0) {
                  guard->runProgress_->setRange(0, total);
                  guard->runProgress_->setValue(count);
                  guard->runProgress_->setFormat(QString("%1 / %2").arg(count).arg(total));
                }
                if (!event.value("node_id").toString().isEmpty()) {
                  guard->showStatus(QString("Job: %1  (%2/%3)")
                      .arg(event.value("node_id").toString())
                      .arg(count)
                      .arg(total));
                }
              },
              Qt::QueuedConnection);
        },
        cancellation.get());
  }));
}

void WorkflowDialog::removeSelectedJob() {
  if (busy_ || !jobsList_->currentItem()) return;
  const QString id = jobsList_->currentItem()->data(Qt::UserRole).toString();
  if (QMessageBox::question(
          this, "Remove saved job",
          "Remove this job from local history? Generated output files will be kept.",
          QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) {
    return;
  }
  const auto result = jobs_.remove(id);
  showStatus(result.ok ? "Job history record removed; outputs kept."
                       : "Cannot remove job: " + result.error, !result.ok);
  refreshJobs();
}

void WorkflowDialog::toggleFolderWatch() {
  if (watchEnabled_) {
    watchEnabled_ = false;
    watchTimer_->stop();
    pendingWatchInputs_.clear();
    // Keep the exclusive folder lock until an active file action exits.
    if (!busy_) folderWatch_.stop();
    watchStatusLabel_->hide();
    showStatus(busy_ ? "Folder watching stopped; the active local job will finish."
                     : "Folder watching stopped.");
    refreshButtons();
    return;
  }
  if (busy_ || stepsList_->count() == 0) return;
  const auto folder = QFileDialog::getExistingDirectory(
      this, "Choose one folder to monitor for new files");
  if (folder.isEmpty()) return;

  QString error;
  if (!folderWatch_.arm(folder, definition(), &error)) {
    showStatus("Could not watch folder: " + error, true);
    refreshButtons();
    return;
  }
  watchEnabled_ = true;
  pendingWatchInputs_.clear();
  watchStatusLabel_->setText(
      "Watching: " + folderWatch_.folder() +
      "  |  existing files ignored  |  new stable files only");
  watchStatusLabel_->setToolTip(
      "Non-recursive, local-only monitoring. Stops when this editor is closed. "
      "Each qualifying file is saved as a new Workflow Job.");
  watchStatusLabel_->show();
  watchTimer_->start();
  showStatus("Watching enabled. Drop a new file into this folder to create a job.");
  refreshButtons();
}

void WorkflowDialog::pollWatchedFolder() {
  if (!watchEnabled_ || busy_) return;  // Prevent observing our own new outputs.
  const auto result = folderWatch_.scan(QDateTime::currentMSecsSinceEpoch());
  if (!result.ok()) {
    watchEnabled_ = false;
    watchTimer_->stop();
    pendingWatchInputs_.clear();
    folderWatch_.stop();
    watchStatusLabel_->hide();
    showStatus("Folder watcher stopped: " + result.error, true);
    refreshButtons();
    return;
  }
  pendingWatchInputs_.append(result.readyPaths);
  if (!pendingWatchInputs_.isEmpty()) {
    runNextWatchedFile();
  }
}

void WorkflowDialog::runNextWatchedFile() {
  if (busy_ || !watchEnabled_ || pendingWatchInputs_.isEmpty()) return;
  const QString path = pendingWatchInputs_.takeFirst();
  const QJsonObject workflow = folderWatch_.workflow();
  busy_ = true;
  stopRequested_ = std::make_shared<std::atomic_bool>(false);
  runProgress_->setVisible(true);
  runProgress_->setRange(0, 0);
  runProgress_->setFormat("Preparing new file...");
  stopButton_->show();
  refreshButtons();
  showStatus("New stable file: " + QFileInfo(path).fileName() + ". Queuing local job...");

  struct JobOutcome {
    QString path;
    QString jobId;
    WorkerResult result;
  };

  QPointer<WorkflowDialog> guard(this);
  const auto cancellation = stopRequested_;
  auto* watcher = new QFutureWatcher<JobOutcome>(this);
  connect(watcher, &QFutureWatcher<JobOutcome>::finished, this, [this, watcher] {
    const auto outcome = watcher->result();
    watcher->deleteLater();

    const auto report = QJsonDocument::fromJson(outcome.result.output.toUtf8()).object();
    QStringList outputs;
    for (const auto& value : report.value("created_output_paths").toArray()) {
      outputs.append(value.toString());
    }
    // Suppress generated siblings before resuming the filesystem scan.
    folderWatch_.ignoreCreatedOutputs(outputs);

    finish(true, outcome.result);
    refreshJobs();
    if (!watchEnabled_) {
      folderWatch_.stop();
    } else {
      watchStatusLabel_->setText(
          "Watching: " + folderWatch_.folder() +
          "  |  processed " + QFileInfo(outcome.path).fileName() +
          "  |  waiting for new stable files");
      runNextWatchedFile();
    }
  });

  watcher->setFuture(QtConcurrent::run([workflow, path, guard, cancellation]() -> JobOutcome {
    WorkflowJobService jobs;
    const auto queued = jobs.enqueue(workflow, {path});
    if (!queued.ok) return {path, {}, queued};
    const auto parsed = QJsonDocument::fromJson(queued.output.toUtf8()).object();
    const QString jobId = parsed.value("job").toObject().value("id").toString();
    if (jobId.isEmpty()) {
      return {path, {}, WorkerResult{false, {},
                                    "Local queue did not return a job ID."}};
    }
    auto callback = [guard](const QJsonObject& event) {
      if (!guard) return;
      QMetaObject::invokeMethod(
          guard.data(), [guard, event] {
            if (!guard || !guard->busy_) return;
            const int count = event.value("completed_operations").toInt();
            const int total = event.value("total_operations").toInt();
            if (total > 0) {
              guard->runProgress_->setRange(0, total);
              guard->runProgress_->setValue(count);
              guard->runProgress_->setFormat(
                  QString("%1 / %2").arg(count).arg(total));
            }
            if (!event.value("node_id").toString().isEmpty()) {
              guard->showStatus(QString("Watching: %1 (%2 / %3)")
                  .arg(event.value("node_id").toString())
                  .arg(count)
                  .arg(total));
            }
          }, Qt::QueuedConnection);
    };
    return {path, jobId, jobs.execute(jobId, callback, cancellation.get())};
  }));
}

void WorkflowDialog::refreshButtons() {
  const bool editing = !busy_ && !watchEnabled_;
  const bool linear = !graphMode_ && editing;
  const int index = stepsList_->currentRow();
  nameEdit_->setEnabled(editing);
  actionCombo_->setEnabled(editing);
  templateCombo_->setEnabled(linear);
  stepsList_->setEnabled(editing);
  graphView_->setEnabled(editing);
  addButton_->setEnabled(editing);
  removeButton_->setEnabled(editing && index >= 0);
  upButton_->setEnabled(linear && index > 0);
  downButton_->setEnabled(linear && index >= 0 && index + 1 < stepsList_->count());
  graphEditButton_->setEnabled(linear && stepsList_->count() > 0);
  graphEditButton_->setVisible(!graphMode_);
  chooseButton_->setEnabled(editing);
  loadButton_->setEnabled(editing);
  saveButton_->setEnabled(editing && stepsList_->count() > 0);
  newButton_->setEnabled(editing);
  planButton_->setEnabled(editing && stepsList_->count() > 0 && !inputs_.isEmpty());
  runButton_->setEnabled(planButton_->isEnabled());
  watchButton_->setText(watchEnabled_ ? "Stop Watching" : "Watch folder...");
  watchButton_->setEnabled(watchEnabled_ || (!busy_ && stepsList_->count() > 0));
  queueButton_->setEnabled(planButton_->isEnabled());
  const auto selectedJob = jobsList_->currentItem();
  const QString state = selectedJob ? selectedJob->data(Qt::UserRole + 1).toString() : QString{};
  executeJobButton_->setEnabled(editing && selectedJob &&
      (state == "queued" || state == "failed" ||
       state == "stopped" || state == "interrupted"));
  removeJobButton_->setEnabled(editing && selectedJob && state != "running");
  refreshJobsButton_->setEnabled(!busy_);
  if (stopButton_) stopButton_->setEnabled(busy_ && stopRequested_ &&
                                          !stopRequested_->load(std::memory_order_acquire));
}

void WorkflowDialog::begin(bool execute) {
  if (busy_ || inputs_.isEmpty()) return;
  const auto workflow = definition();
  busy_ = true;
  stopRequested_ = execute ? std::make_shared<std::atomic_bool>(false) : nullptr;
  runProgress_->setRange(0, 0);
  runProgress_->setValue(0);
  runProgress_->setFormat("Preparing workflow...");
  runProgress_->setVisible(execute);
  stopButton_->setVisible(execute);
  refreshButtons();
  showStatus(execute ? "Running local workflow..." : "Preparing read-only execution plan...");
  outputView_->clear();
  rightTabs_->setCurrentWidget(outputView_);

  auto* watcher = new QFutureWatcher<WorkerResult>(this);
  connect(watcher, &QFutureWatcher<WorkerResult>::finished, this, [this, watcher, execute] {
    const auto result = watcher->result();
    watcher->deleteLater();
    finish(execute, result);
  });
  const auto paths = inputs_;
  const auto cancellation = stopRequested_;
  QPointer<WorkflowDialog> guard(this);
  watcher->setFuture(QtConcurrent::run([workflow, paths, execute, cancellation, guard] {
    WorkflowService service;
    if (!execute) return service.plan(workflow, paths);
    return service.runStreaming(
        workflow, paths,
        [guard](const QJsonObject& event) {
          if (!guard) return;
          QMetaObject::invokeMethod(
              guard.data(),
              [guard, event] {
                if (!guard || !guard->busy_) return;
                const auto type = event.value("event").toString();
                const int completed = event.value("completed_operations").toInt(0);
                const int total = event.value("total_operations").toInt(0);
                if (total > 0) {
                  guard->runProgress_->setRange(0, total);
                  guard->runProgress_->setValue(completed);
                  guard->runProgress_->setFormat(QString("%1 / %2").arg(completed).arg(total));
                }
                if (type == "workflow.node_started") {
                  guard->graphView_->setNodeStatus(event.value("node_id").toString(), "running");
                } else if (type == "workflow.node_completed") {
                  guard->graphView_->setNodeStatus(event.value("node_id").toString(), "completed");
                } else if (type == "workflow.failed") {
                  guard->graphView_->setNodeStatus(event.value("node_id").toString(), "failed");
                }
                if (type == "workflow.node_started" ||
                    type == "workflow.action_started" ||
                    type == "workflow.action_completed") {
                  guard->showStatus(
                      QString("%1 — %2 / %3 file actions completed")
                          .arg(event.value("node_id").toString())
                          .arg(completed)
                          .arg(total));
                } else if (type == "workflow.stopped") {
                  guard->showStatus("Stopped safely. Completed output files are retained.");
                }
              },
              Qt::QueuedConnection);
        },
        cancellation.get());
  }));
}

void WorkflowDialog::finish(bool executed, const WorkerResult& result) {
  busy_ = false;
  stopRequested_.reset();
  stopButton_->hide();
  if (!executed) runProgress_->hide();
  refreshButtons();

  if (result.output.isEmpty()) {
    showStatus(result.error.isEmpty() ? "Worker returned no details." : result.error, true);
    return;
  }
  const auto parsed = QJsonDocument::fromJson(result.output.toUtf8());
  if (parsed.isObject()) {
    outputView_->setPlainText(QString::fromUtf8(parsed.toJson(QJsonDocument::Indented)));
    const auto data = parsed.object();
    if (result.ok) {
      if (executed) {
        showStatus(QString("Completed. %1 output(s) saved; originals preserved.")
                       .arg(data.value("output_paths").toArray().size()));
      } else {
        showStatus(QString("Plan ready: %1 node(s), %2 local operation(s). No files changed.")
                       .arg(data.value("steps").toArray().size())
                       .arg(data.value("operation_count").toInt()));
      }
    } else if (data.value("status").toString() == "stopped") {
      showStatus("Stopped safely after the current action. Intermediate outputs are retained.");
    } else {
      showStatus("Workflow failed: " + result.error +
                     " (see completed steps and created outputs below)", true);
    }
  } else {
    showStatus("Worker response could not be parsed.", true);
  }
}

void WorkflowDialog::showStatus(const QString& message, bool error) {
  statusLabel_->setText(message);
  statusLabel_->setStyleSheet(error ? "color:#902b2b;" : "color:#666666;");
}

void WorkflowDialog::reject() {
  if (busy_) {
    QMessageBox::information(this, "Workflow running",
                             "A local action is still running. Close this editor once it finishes.");
    return;
  }
  if (watchEnabled_) {
    if (QMessageBox::question(
            this, "Stop folder watching?",
            "Closing this editor stops folder monitoring. Continue?",
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No) != QMessageBox::Yes) {
      return;
    }
    watchEnabled_ = false;
    watchTimer_->stop();
    folderWatch_.stop();
  }
  QDialog::reject();
}

void WorkflowDialog::closeEvent(QCloseEvent* event) {
  if (busy_) {
    event->ignore();
    return;
  }
  if (watchEnabled_) {
    const auto answer = QMessageBox::question(
        this, "Stop folder watching?",
        "Closing the Workflow editor stops monitoring this folder. Close?",
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) {
      event->ignore();
      return;
    }
    watchEnabled_ = false;
    watchTimer_->stop();
    folderWatch_.stop();
  }
  QDialog::closeEvent(event);
}

}  // namespace omnidrop
