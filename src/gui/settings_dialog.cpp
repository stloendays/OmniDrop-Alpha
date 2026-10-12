#include "gui/settings_dialog.hpp"

#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardPaths>
#include <QVBoxLayout>

namespace omnidrop {

SettingsDialog::SettingsDialog(QWidget* parent)
    : QDialog(parent) {
  setWindowTitle("Preferences and backup - OmniDrop");
  setMinimumWidth(520);
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(22, 20, 22, 18);
  layout->setSpacing(14);

  auto* heading = new QLabel("Preferences", this);
  heading->setObjectName("settingsHeading");
  layout->addWidget(heading);

  auto* explanation = new QLabel(
      "Preferences are local to this computer. Export saves only the two "
      "values below; recent-file and Activity records, Jobs, rename "
      "journals, models and credentials are not part of the backup. "
      "Lower limits affect visible history and future retention.",
      this);
  explanation->setWordWrap(true);
  explanation->setObjectName("settingsExplanation");
  layout->addWidget(explanation);

  auto* form = new QFormLayout;
  recentLimit_ = new QSpinBox(this);
  recentLimit_->setObjectName("settingsRecentLimit");
  recentLimit_->setRange(1, 32);
  recentLimit_->setToolTip("Maximum Recent Files entries displayed and retained after a new file is opened.");
  form->addRow("Recent files limit", recentLimit_);
  activityLimit_ = new QSpinBox(this);
  activityLimit_->setObjectName("settingsActivityLimit");
  activityLimit_->setRange(1, 100);
  activityLimit_->setToolTip("Maximum Activity entries displayed and retained when a new action is recorded.");
  form->addRow("Activity entries limit", activityLimit_);
  layout->addLayout(form);

  status_ = new QLabel(this);
  status_->setObjectName("settingsStatus");
  status_->setWordWrap(true);
  layout->addWidget(status_);

  auto* actions = new QHBoxLayout;
  saveButton_ = new QPushButton("Save preferences", this);
  saveButton_->setObjectName("settingsSave");
  actions->addWidget(saveButton_);
  actions->addStretch();
  auto* exportButton = new QPushButton("Export...", this);
  exportButton->setObjectName("settingsExport");
  auto* importButton = new QPushButton("Import...", this);
  importButton->setObjectName("settingsImport");
  actions->addWidget(exportButton);
  actions->addWidget(importButton);
  layout->addLayout(actions);

  auto* maintenance = new QHBoxLayout;
  auto* resetButton = new QPushButton("Reset to defaults...", this);
  resetButton->setObjectName("settingsReset");
  recoverButton_ = new QPushButton("Restore last good...", this);
  recoverButton_->setObjectName("settingsRecover");
  maintenance->addWidget(resetButton);
  maintenance->addWidget(recoverButton_);
  maintenance->addStretch();
  layout->addLayout(maintenance);

  auto* close = new QDialogButtonBox(QDialogButtonBox::Close, this);
  connect(close, &QDialogButtonBox::rejected, this, &QDialog::close);
  layout->addWidget(close);

  connect(saveButton_, &QPushButton::clicked, this, [this] {
    AppPreferences preferences;
    preferences.recentFilesLimit = recentLimit_->value();
    preferences.activityHistoryLimit = activityLimit_->value();
    showResult(service_.update(preferences), "Preferences saved");
  });
  connect(exportButton, &QPushButton::clicked, this, [this] {
    const auto folder = QStandardPaths::writableLocation(
        QStandardPaths::DocumentsLocation);
    const auto selected = QFileDialog::getSaveFileName(
        this, "Export OmniDrop preferences",
        folder + "/OmniDrop-preferences.json",
        "JSON (*.json)");
    if (selected.isEmpty()) return;
    showResult(service_.exportTo(selected), "Preferences exported");
  });
  connect(importButton, &QPushButton::clicked, this, [this] {
    const auto selected = QFileDialog::getOpenFileName(
        this, "Import OmniDrop preferences", {}, "JSON (*.json)");
    if (selected.isEmpty()) return;
    if (!askConfirmation("Import preferences",
          "Replace the two application preferences with values from this "
          "JSON file? The current configuration will be backed up first. "
          "Recent file records, action history and Jobs are unaffected.")) {
      return;
    }
    showResult(service_.importFrom(selected, true), "Preferences imported");
  });
  connect(resetButton, &QPushButton::clicked, this, [this] {
    if (!askConfirmation("Reset preferences",
          "Reset both preference limits to their defaults (8 recent files, "
          "20 activity entries)? The previous valid configuration is kept "
          "as a recovery snapshot. Existing history is not deleted.")) {
      return;
    }
    showResult(service_.reset(true), "Preferences reset");
  });
  connect(recoverButton_, &QPushButton::clicked, this, [this] {
    if (!askConfirmation("Restore previous preferences",
          "Replace the current preferences with the last-known-good copy? "
          "If the current JSON is damaged, a copy will be preserved before "
          "restoring. Existing recent/activity records and Jobs are not deleted.")) {
      return;
    }
    showResult(service_.recover(true), "Previous preferences restored");
  });

  setStyleSheet(R"(
    QDialog { background: #f5f5f5; color: #161616; }
    QLabel#settingsHeading { font-size: 18px; font-weight: 600; }
    QLabel#settingsExplanation { color: #666; }
    QLabel#settingsStatus { color: #333; }
    QSpinBox { background: white; border: 1px solid #bbb; padding: 5px; }
    QPushButton { padding: 7px 10px; }
  )");
  refresh();
}

bool SettingsDialog::askConfirmation(const QString& title,
                                      const QString& explanation) {
  return QMessageBox::question(this, title, explanation,
                               QMessageBox::Yes | QMessageBox::No,
                               QMessageBox::No) == QMessageBox::Yes;
}

void SettingsDialog::refresh() {
  const auto current = service_.read();
  recentLimit_->setValue(current.preferences.recentFilesLimit);
  activityLimit_->setValue(current.preferences.activityHistoryLimit);
  saveButton_->setEnabled(current.ok);
  if (current.ok) {
    status_->setText(current.source == "defaults"
        ? "Using defaults. No settings file exists yet."
        : "Preferences loaded from local settings file.");
  } else {
    status_->setText("Settings cannot be loaded: " + current.error +
                     " Recovery is manual; the damaged file was not overwritten.");
  }
}

void SettingsDialog::showResult(const SettingsResult& result,
                                const QString& action) {
  if (!result.ok) {
    status_->setText(action + " failed: " + result.error);
    // Do not conceal the failure by replacing it with defaults.
    return;
  }
  refresh();
  status_->setText(action + ". Preferences are saved locally.");
}

}  // namespace omnidrop
