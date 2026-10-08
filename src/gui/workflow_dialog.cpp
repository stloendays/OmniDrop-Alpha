#include "gui/workflow_dialog.hpp"

#include "app/action_catalog.hpp"
#include "gui/workflow_graph_view.hpp"

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
  planButton_ = new QPushButton("Preview plan", this);
  stopButton_ = new QPushButton("Stop after current file", this);
  stopButton_->setEnabled(false);
  stopButton_->hide();
  runButton_ = new QPushButton("Run workflow", this);
  footer->addStretch();
  footer->addWidget(cancel);
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
  connect(planButton_, &QPushButton::clicked, this, [this] { begin(false); });
  connect(runButton_, &QPushButton::clicked, this, [this] { begin(true); });
  connect(stopButton_, &QPushButton::clicked, this, [this] {
    if (!busy_ || !stopRequested_) return;
    stopRequested_->store(true, std::memory_order_release);
    stopButton_->setEnabled(false);
    showStatus("Stopping after the current file action finishes...");
  });
  connect(cancel, &QPushButton::clicked, this, &WorkflowDialog::reject);

  refreshInputs();
  refreshButtons();
  refreshGraph();
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
  if (graphMode_ || busy_ || actionId.isEmpty()) return;
  auto* item = new QListWidgetItem(
      QString("%1. %2").arg(stepsList_->count() + 1).arg(actionId));
  item->setData(Qt::UserRole, actionId);
  stepsList_->addItem(item);
  stepsList_->setCurrentRow(stepsList_->count() - 1);
  refreshButtons();
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
}

void WorkflowDialog::selectInputs() {
  if (busy_) return;
  const auto paths = QFileDialog::getOpenFileNames(this, "Select workflow input files");
  if (paths.isEmpty()) return;
  inputs_ = paths;
  refreshInputs();
}

QJsonObject WorkflowDialog::definition() const {
  if (graphMode_) return loadedDefinition_;

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
  nameEdit_->setEnabled(false);
  stepsList_->clear();
  const auto nodes = loadedDefinition_.value("nodes").toArray();
  for (const auto& raw : nodes) {
    const auto node = raw.toObject();
    auto* item = new QListWidgetItem(
        node.value("id").toString() + "  ·  " + node.value("action_id").toString());
    item->setToolTip("Sources: " +
                      QString::fromUtf8(QJsonDocument(node.value("sources").toArray())
                                            .toJson(QJsonDocument::Compact)));
    stepsList_->addItem(item);
  }
  outputView_->clear();
  showStatus("Loaded a saved DAG in read-only graph mode. New creates an editable linear workflow.");
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

void WorkflowDialog::refreshButtons() {
  const bool editing = !graphMode_ && !busy_;
  const int index = stepsList_->currentRow();
  nameEdit_->setEnabled(editing);
  actionCombo_->setEnabled(editing);
  templateCombo_->setEnabled(editing);
  stepsList_->setEnabled(!busy_);
  addButton_->setEnabled(editing);
  removeButton_->setEnabled(editing && index >= 0);
  upButton_->setEnabled(editing && index > 0);
  downButton_->setEnabled(editing && index >= 0 && index + 1 < stepsList_->count());
  chooseButton_->setEnabled(!busy_);
  loadButton_->setEnabled(!busy_);
  saveButton_->setEnabled(!busy_ && (graphMode_ || stepsList_->count() > 0));
  newButton_->setEnabled(!busy_);
  planButton_->setEnabled(!busy_ && (graphMode_ || stepsList_->count() > 0) && !inputs_.isEmpty());
  runButton_->setEnabled(planButton_->isEnabled());
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
  QDialog::reject();
}

void WorkflowDialog::closeEvent(QCloseEvent* event) {
  if (busy_) {
    event->ignore();
    return;
  }
  QDialog::closeEvent(event);
}

}  // namespace omnidrop
