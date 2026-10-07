#include "gui/main_window.hpp"

#include "gui/drop_zone.hpp"

#include <QAction>
#include <QColor>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
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
}

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
  setWindowTitle(QString("OmniDrop %1").arg(OMNIDROP_VERSION));
  resize(900, 720);
  setMinimumSize(720, 560);

  auto* root = new QWidget(this);
  auto* layout = new QVBoxLayout(root);
  layout->setContentsMargins(28, 24, 28, 22);
  layout->setSpacing(12);

  auto* header = new QHBoxLayout;
  auto* brand = new QLabel("OmniDrop", root);
  brand->setObjectName("brand");
  header->addWidget(brand);
  header->addStretch();
  auto* openButton = new QPushButton("Open file", root);
  header->addWidget(openButton);
  layout->addLayout(header);

  dropZone_ = new DropZone(root);
  layout->addWidget(dropZone_);

  recentTitle_ = new QLabel("Recent", root);
  recentTitle_->setObjectName("sectionTitle");
  recentList_ = new QListWidget(root);
  recentList_->setMaximumHeight(100);
  layout->addWidget(recentTitle_);
  layout->addWidget(recentList_);

  fileTitle_ = new QLabel(root);
  fileTitle_->setObjectName("fileTitle");
  fileMeta_ = new QLabel(root);
  fileMeta_->setObjectName("muted");
  actionSearch_ = new QLineEdit(root);
  actionSearch_->setPlaceholderText("Search actions");
  actionSearch_->setClearButtonEnabled(true);
  actionList_ = new QListWidget(root);
  runButton_ = new QPushButton("Select an action", root);
  runButton_->setEnabled(false);

  layout->addWidget(fileTitle_);
  layout->addWidget(fileMeta_);
  layout->addWidget(actionSearch_);
  layout->addWidget(actionList_, 1);
  layout->addWidget(runButton_);

  historyTitle_ = new QLabel("Activity", root);
  historyTitle_->setObjectName("sectionTitle");
  historyList_ = new QListWidget(root);
  historyList_->setMaximumHeight(100);
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
  auto* openAction = fileMenu->addAction("Open file...");
  fileMenu->addSeparator();
  auto* quitAction = fileMenu->addAction("Exit");

  auto* historyMenu = menuBar()->addMenu("History");
  auto* clearRecent = historyMenu->addAction("Clear recent files");
  auto* clearActivity = historyMenu->addAction("Clear activity");

  auto* helpMenu = menuBar()->addMenu("Help");
  auto* aboutAction = helpMenu->addAction("About OmniDrop");

  const auto chooseFile = [this] {
    const auto path = QFileDialog::getOpenFileName(this, "Open file");
    if (!path.isEmpty()) inspectPath(path);
  };

  connect(openButton, &QPushButton::clicked, this, chooseFile);
  connect(openAction, &QAction::triggered, this, chooseFile);
  connect(quitAction, &QAction::triggered, this, &QWidget::close);
  connect(dropZone_, &DropZone::filesDropped, this, [this](const QStringList& paths) {
    if (!paths.isEmpty()) inspectPath(paths.first());
  });
  connect(recentList_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
    inspectPath(item->data(Qt::UserRole).toString());
  });
  connect(actionSearch_, &QLineEdit::textChanged, this, &MainWindow::filterActions);
  connect(runButton_, &QPushButton::clicked, this, &MainWindow::runSelectedAction);
  connect(actionList_, &QListWidget::currentRowChanged, this, [this](int row) {
    const bool valid = row >= 0 && row < current_.actions.size() &&
                       !actionList_->item(row)->isHidden() && current_.actions.at(row).available;
    runButton_->setEnabled(valid);
    if (valid) runButton_->setText(QString("Run %1").arg(current_.actions.at(row).label));
  });
  connect(clearRecent, &QAction::triggered, this, [this] { recentFiles_.clear(); refreshRecentFiles(); });
  connect(clearActivity, &QAction::triggered, this, [this] { history_.clear(); refreshHistory(); });
  connect(aboutAction, &QAction::triggered, this, [this] {
    QMessageBox::about(this, "About OmniDrop",
      QString("<b>OmniDrop %1</b><br><br>Local-first file utilities. Core operations do not require an account or file upload.")
        .arg(OMNIDROP_VERSION));
  });

  auto* findAction = new QAction(this);
  findAction->setShortcut(QKeySequence::Find);
  addAction(findAction);
  connect(findAction, &QAction::triggered, this, [this] {
    if (actionSearch_->isVisible()) { actionSearch_->setFocus(); actionSearch_->selectAll(); }
  });

  setStyleSheet(R"(
    QMainWindow, QWidget { background:#f5f5f5; color:#111; font-family:"Segoe UI"; font-size:14px; }
    QLabel#brand { font-size:24px; font-weight:650; }
    QLabel#sectionTitle { color:#666; font-size:12px; font-weight:600; }
    QLabel#fileTitle { font-size:18px; font-weight:600; }
    QLabel#muted, QStatusBar { color:#666; }
    QListWidget, QLineEdit { background:#fff; border:1px solid #d8d8d8; border-radius:8px; padding:6px; }
    QPushButton { background:#111; color:#fff; border:0; border-radius:8px; min-height:36px; padding:0 14px; }
    QPushButton:disabled { background:#d0d0d0; color:#777; }
  )");

  refreshRecentFiles();
  refreshHistory();
  probeWorkerCapabilities();
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
    runtimeCapabilities_ = parseCapabilities(watcher->result());
    capabilityProbeComplete_ = true;
    watcher->deleteLater();
    statusLabel_->setText(QString("Local processors ready - %1 actions available.").arg(runtimeCapabilities_.size()));
    if (!current_.path.isEmpty()) showInspection(service_.inspect(current_.path, runtimeCapabilities_));
  });
  watcher->setFuture(QtConcurrent::run([worker] { return worker.capabilities(); }));
}

void MainWindow::inspectPath(const QString& path) {
  QFileInfo info(path);
  if (!info.exists() || !info.isFile()) {
    statusLabel_->setText("That path is not a readable file.");
    return;
  }
  recentFiles_.add(info.absoluteFilePath());
  refreshRecentFiles();
  showInspection(capabilityProbeComplete_ ? service_.inspect(info.absoluteFilePath(), runtimeCapabilities_)
                                          : service_.inspect(info.absoluteFilePath(), {}));
}

void MainWindow::showInspection(const FileInspection& inspection) {
  current_ = inspection;
  fileTitle_->setText(inspection.name);
  fileMeta_->setText(QString("%1  /  %2").arg(toString(inspection.kind), formatSize(inspection.sizeBytes)));
  fileTitle_->show();
  fileMeta_->show();
  actionSearch_->show();
  actionList_->show();
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
  statusLabel_->setText(QString("Ready: %1").arg(inspection.path));
}

void MainWindow::filterActions(const QString& query) {
  const auto needle = query.trimmed();
  int first = -1;
  for (int row = 0; row < current_.actions.size(); ++row) {
    const auto& action = current_.actions.at(row);
    const bool match = needle.isEmpty() || action.label.contains(needle, Qt::CaseInsensitive) ||
      action.description.contains(needle, Qt::CaseInsensitive) || action.id.contains(needle, Qt::CaseInsensitive);
    actionList_->item(row)->setHidden(!match);
    if (match && action.available && first < 0) first = row;
  }
  actionList_->setCurrentRow(first);
}

void MainWindow::runSelectedAction() {
  const int row = actionList_->currentRow();
  if (row < 0 || row >= current_.actions.size()) return;
  const auto action = current_.actions.at(row);
  if (!action.available) return;

  runButton_->setEnabled(false);
  actionList_->setEnabled(false);
  actionSearch_->setEnabled(false);
  dropZone_->setEnabled(false);
  statusLabel_->setText(QString("Running %1...").arg(action.label));

  const auto worker = worker_;
  const auto path = current_.path;
  auto* watcher = new QFutureWatcher<WorkerResult>(this);
  connect(watcher, &QFutureWatcher<WorkerResult>::finished, this, [this, watcher, action, path] {
    const auto result = watcher->result();
    watcher->deleteLater();
    actionList_->setEnabled(true);
    actionSearch_->setEnabled(true);
    dropZone_->setEnabled(true);

    QString detail;
    if (result.ok) {
      const auto object = QJsonDocument::fromJson(result.output.toUtf8()).object();
      detail = action.id == "file.sha256" ? "SHA-256: " + object.value("sha256").toString()
                                           : "Created: " + object.value("output_path").toString();
      statusLabel_->setText(detail);
    } else {
      detail = result.error;
      statusLabel_->setText("Failed: " + detail);
    }
    history_.record(action.label, path, result.ok, detail);
    refreshHistory();
    filterActions(actionSearch_->text());
  });
  watcher->setFuture(QtConcurrent::run([worker, actionId = action.id, path] { return worker.runAction(actionId, path); }));
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

} // namespace omnidrop
