#include "gui/update_check_dialog.hpp"

#include "adapters/github_release_client.hpp"

#include <QCloseEvent>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

namespace omnidrop {

UpdateCheckDialog::UpdateCheckDialog(QWidget* parent) : QDialog(parent) {
  setWindowTitle("Check for updates - OmniDrop");
  setMinimumWidth(540);
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(22, 20, 22, 18);
  layout->setSpacing(12);

  auto* title = new QLabel("Check for updates", this);
  title->setObjectName("updateHeading");
  layout->addWidget(title);

  auto* current = new QLabel(
      QString("Installed version: %1").arg(OMNIDROP_VERSION), this);
  current->setObjectName("updateCurrentVersion");
  layout->addWidget(current);

  auto* explanation = new QLabel(
      "Only when you click Check now, OmniDrop contacts the official GitHub "
      "release API to read published versions. No files or document names "
      "are uploaded. OmniDrop cannot yet download, replace or install updates. "
      "Review any release yourself before downloading it.", this);
  explanation->setObjectName("updateExplanation");
  explanation->setWordWrap(true);
  layout->addWidget(explanation);

  auto* channels = new QHBoxLayout;
  auto* label = new QLabel("Release channel", this);
  channels->addWidget(label);
  channel_ = new QComboBox(this);
  channel_->setObjectName("updateChannel");
  channel_->addItem("Alpha (includes stable)", "alpha");
  channel_->addItem("Stable only", "stable");
  channels->addWidget(channel_, 1);
  layout->addLayout(channels);

  status_ = new QLabel("No network request has been sent.", this);
  status_->setObjectName("updateStatus");
  status_->setWordWrap(true);
  status_->setMinimumHeight(48);
  layout->addWidget(status_);

  auto* actions = new QHBoxLayout;
  checkButton_ = new QPushButton("Check now", this);
  checkButton_->setObjectName("updateCheckNow");
  verifyButton_ = new QPushButton("Verify downloaded ZIP...", this);
  verifyButton_->setObjectName("updateVerifyLocalZip");
  verifyButton_->setToolTip("Read-only local checksum and archive-path check. Does not authenticate the publisher or install files.");
  releaseButton_ = new QPushButton("View release in browser", this);
  releaseButton_->setObjectName("updateOpenRelease");
  releaseButton_->setEnabled(false);
  actions->addWidget(checkButton_);
  actions->addWidget(verifyButton_);
  actions->addStretch();
  actions->addWidget(releaseButton_);
  layout->addLayout(actions);

  auto* footer = new QDialogButtonBox(QDialogButtonBox::Close, this);
  connect(footer, &QDialogButtonBox::rejected, this, &QDialog::close);
  layout->addWidget(footer);

  connect(checkButton_, &QPushButton::clicked, this,
          [this] { startCheck(); });
  connect(verifyButton_, &QPushButton::clicked, this,
          [this] { verifyLocalPackage(); });
  connect(releaseButton_, &QPushButton::clicked, this, [this] {
    // The service constructs this URL solely from a validated official tag.
    if (!releaseUrl_.startsWith(
            "https://github.com/stloendays/OmniDrop-Alpha/releases/tag/v"))
      return;
    QDesktopServices::openUrl(QUrl(releaseUrl_));
  });

  setStyleSheet(R"(
    QDialog { background: #f5f5f5; color: #171717; }
    QLabel#updateHeading { font-size: 18px; font-weight: 650; }
    QLabel#updateCurrentVersion, QLabel#updateExplanation { color: #606060; }
    QLabel#updateStatus { color: #202020; }
    QComboBox { background: #fff; border: 1px solid #ccc; padding: 6px; }
    QPushButton { padding: 6px 12px; }
    QPushButton:disabled { color: #888; }
  )");
}

void UpdateCheckDialog::startCheck() {
  if (checking_ || verifying_) return;
  checking_ = true;
  releaseUrl_.clear();
  checkButton_->setEnabled(false);
  verifyButton_->setEnabled(false);
  releaseButton_->setEnabled(false);
  channel_->setEnabled(false);
  status_->setText("Checking official GitHub release metadata...");

  const auto selected = channel_->currentData().toString();
  auto* watcher = new QFutureWatcher<UpdateCheckResult>(this);
  connect(watcher, &QFutureWatcher<UpdateCheckResult>::finished,
          this, [this, watcher] {
    const auto result = watcher->result();
    watcher->deleteLater();
    checking_ = false;
    checkButton_->setEnabled(true);
    verifyButton_->setEnabled(true);
    channel_->setEnabled(true);
    showResult(result);
  });
  watcher->setFuture(QtConcurrent::run([selected] {
    return GitHubReleaseClient::check(OMNIDROP_VERSION, selected);
  }));
}

void UpdateCheckDialog::verifyLocalPackage() {
  if (checking_ || verifying_) return;
  const QString zip = QFileDialog::getOpenFileName(
      this, "Select a downloaded OmniDrop ZIP", {},
      "OmniDrop ZIP (OmniDrop-*-windows-dev.zip)");
  if (zip.isEmpty()) return;
  QString checksum = zip + ".sha256";
  if (!QFileInfo::exists(checksum)) {
    checksum = QFileDialog::getOpenFileName(
        this, "Select the matching SHA-256 file",
        QFileInfo(zip).absolutePath(), "Checksum files (*.sha256)");
    if (checksum.isEmpty()) return;
  }

  verifying_ = true;
  checkButton_->setEnabled(false);
  verifyButton_->setEnabled(false);
  releaseButton_->setEnabled(false);
  channel_->setEnabled(false);
  status_->setText("Checking local archive SHA-256 and ZIP paths...");

  auto* watcher = new QFutureWatcher<LocalUpdatePreflightResult>(this);
  connect(watcher, &QFutureWatcher<LocalUpdatePreflightResult>::finished,
          this, [this, watcher] {
    const auto result = watcher->result();
    watcher->deleteLater();
    verifying_ = false;
    checkButton_->setEnabled(true);
    verifyButton_->setEnabled(true);
    channel_->setEnabled(true);
    showPreflightResult(result);
  });
  watcher->setFuture(QtConcurrent::run([zip, checksum] {
    return LocalUpdatePreflightService::verify(zip, checksum);
  }));
}

void UpdateCheckDialog::showPreflightResult(
    const LocalUpdatePreflightResult& result) {
  if (!result.ok) {
    status_->setText("Local ZIP preflight blocked: " + result.error +
                     " No files were installed or modified.");
  } else {
    status_->setText(QString(
        "Local ZIP v%1 matches its supplied SHA-256 and %2 archive entries "
        "passed structural safety checks. Publisher provenance is NOT "
        "authenticated. Nothing was extracted or installed.")
        .arg(result.packageVersion)
        .arg(result.entryCount));
  }
}

void UpdateCheckDialog::showResult(const UpdateCheckResult& result) {
  releaseUrl_ = result.releaseUrl;
  releaseButton_->setEnabled(result.ok && !releaseUrl_.isEmpty());
  if (!result.ok) {
    status_->setText("Update check failed: " + result.error +
                     " No files were changed.");
  } else if (result.state == "update_available") {
    status_->setText(QString(
        "Version %1 is available (%2). Open the official release page to "
        "review, download and verify it manually. Nothing is installed.")
        .arg(result.availableVersion,
             result.prerelease ? "Alpha" : "Stable"));
  } else if (result.state == "up_to_date") {
    status_->setText(QString(
        "No newer eligible release found. Latest %1 release: %2.")
        .arg(result.channel, result.availableVersion));
  } else {
    status_->setText("No eligible published release with the expected "
                     "Windows ZIP and SHA-256 assets was found.");
  }
}

void UpdateCheckDialog::reject() {
  if (checking_ || verifying_) return;
  QDialog::reject();
}

void UpdateCheckDialog::closeEvent(QCloseEvent* event) {
  if (checking_ || verifying_) {
    event->ignore();
    return;
  }
  QDialog::closeEvent(event);
}

}  // namespace omnidrop
