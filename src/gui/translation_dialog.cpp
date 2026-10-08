#include "gui/translation_dialog.hpp"

#include "app/translation_service.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

namespace omnidrop {

TranslationDialog::TranslationDialog(const QString& initialPath, QWidget* parent)
    : QDialog(parent) {
  setWindowTitle("Translate with OmniDrop");
  setMinimumSize(540, 410);
  setAttribute(Qt::WA_DeleteOnClose);

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(24, 22, 24, 22);
  layout->setSpacing(13);

  auto* heading = new QLabel("Translate text and subtitles", this);
  heading->setObjectName("heading");
  layout->addWidget(heading);

  auto* fileLine = new QHBoxLayout;
  fileEdit_ = new QLineEdit(initialPath, this);
  fileEdit_->setPlaceholderText("Choose a TXT, Markdown, SRT or VTT file");
  auto* browseButton = new QPushButton("Browse", this);
  fileLine->addWidget(fileEdit_, 1);
  fileLine->addWidget(browseButton);
  layout->addLayout(fileLine);

  auto* form = new QFormLayout;
  providerBox_ = new QComboBox(this);
  providerBox_->addItem("MyMemory - free, no key", "mymemory");
  providerBox_->addItem("LibreTranslate - your endpoint", "libretranslate");
  providerBox_->addItem("DeepL API Free - requires key", "deepl-free");
  providerBox_->addItem("Argos - offline models", "argos");

  sourceBox_ = new QComboBox(this);
  targetBox_ = new QComboBox(this);

  auto populate = [](QComboBox* combo, bool includeAuto) {
    if (includeAuto) combo->addItem("Auto-detect (online)", "auto");
    combo->addItem("English", "en");
    combo->addItem("Chinese", "zh");
    combo->addItem("Japanese", "ja");
    combo->addItem("Spanish", "es");
    combo->addItem("French", "fr");
    combo->addItem("German", "de");
  };
  populate(sourceBox_, true);
  populate(targetBox_, false);
  sourceBox_->setCurrentIndex(sourceBox_->findData("en"));
  targetBox_->setCurrentIndex(targetBox_->findData("zh"));

  form->addRow("Service", providerBox_);
  form->addRow("From", sourceBox_);
  form->addRow("To", targetBox_);

  endpointLabel_ = new QLabel("Server URL", this);
  endpointEdit_ = new QLineEdit(this);
  endpointEdit_->setPlaceholderText("http://127.0.0.1:5000/translate");
  endpointEdit_->setToolTip("Use HTTPS for online servers, or HTTP for localhost only.");
  form->addRow(endpointLabel_, endpointEdit_);

  keyLabel_ = new QLabel("API key", this);
  keyEdit_ = new QLineEdit(this);
  keyEdit_->setEchoMode(QLineEdit::Password);
  keyEdit_->setPlaceholderText("Optional when configured via environment");
  form->addRow(keyLabel_, keyEdit_);
  layout->addLayout(form);

  privacyLabel_ = new QLabel(
      "Online translation sends text segments to the selected external service. "
      "Do not submit confidential files. Files remain on your device; a new copy is created.",
      this);
  privacyLabel_->setWordWrap(true);
  privacyLabel_->setObjectName("caution");
  layout->addWidget(privacyLabel_);

  consentBox_ = new QCheckBox("I approve transmitting this file's text for translation.", this);
  layout->addWidget(consentBox_);

  progress_ = new QProgressBar(this);
  progress_->setRange(0, 0);
  progress_->hide();
  layout->addWidget(progress_);

  statusLabel_ = new QLabel("The original file will not be overwritten.", this);
  statusLabel_->setWordWrap(true);
  statusLabel_->setObjectName("hint");
  layout->addWidget(statusLabel_);

  auto* footer = new QHBoxLayout;
  openFolderButton_ = new QPushButton("Open output folder", this);
  openFolderButton_->setEnabled(false);
  footer->addWidget(openFolderButton_);
  footer->addStretch();

  auto* closeButton = new QPushButton("Close", this);
  translateButton_ = new QPushButton("Translate", this);
  footer->addWidget(closeButton);
  footer->addWidget(translateButton_);
  layout->addLayout(footer);

  setStyleSheet(R"(
    QDialog { background:#f5f5f5; color:#151515; font-family:"Segoe UI"; font-size:13px; }
    QLabel#heading { font-size:19px; font-weight:600; }
    QLabel#hint { color:#656565; }
    QLabel#caution { color:#484848; }
    QLineEdit,QComboBox { background:#fff; border:1px solid #c8c8c8; border-radius:6px; min-height:27px; padding:5px; }
    QPushButton { background:#1a1a1a; color:#fff; border:0; border-radius:6px; min-height:32px; padding:0 12px; }
    QPushButton:disabled { background:#d5d5d5; color:#777; }
    QProgressBar { background:#fff; border:1px solid #d0d0d0; border-radius:5px; }
    QProgressBar::chunk { background:#444; }
  )");

  connect(browseButton, &QPushButton::clicked, this, [this] {
    const auto selected = QFileDialog::getOpenFileName(
        this, "Choose file", {}, "Text and subtitles (*.txt *.md *.markdown *.srt *.vtt)");
    if (!selected.isEmpty()) fileEdit_->setText(selected);
  });
  connect(closeButton, &QPushButton::clicked, this, &QDialog::reject);
  connect(openFolderButton_, &QPushButton::clicked, this, &TranslationDialog::openOutputFolder);
  connect(translateButton_, &QPushButton::clicked, this, &TranslationDialog::startTranslation);
  connect(providerBox_, qOverload<int>(&QComboBox::currentIndexChanged),
          this, [this] { consentBox_->setChecked(false); refreshProvider(); });
  connect(consentBox_, &QCheckBox::toggled, this, [this] { refreshProvider(); });
  refreshProvider();
}

void TranslationDialog::refreshProvider() {
  const auto provider = providerBox_->currentData().toString();
  const bool remote = TranslationService::isRemoteProvider(provider);
  const bool custom = provider == "libretranslate";
  const bool keyed = provider == "deepl-free" || custom;
  endpointLabel_->setVisible(custom);
  endpointEdit_->setVisible(custom);
  keyLabel_->setVisible(keyed);
  keyEdit_->setVisible(keyed);
  privacyLabel_->setVisible(remote);
  consentBox_->setVisible(remote);
  translateButton_->setEnabled(!busy_ && (!remote || consentBox_->isChecked()));
}

void TranslationDialog::startTranslation() {
  if (busy_) return;

  TranslationRequest request;
  request.path = fileEdit_->text().trimmed();
  request.sourceLanguage = sourceBox_->currentData().toString();
  request.targetLanguage = targetBox_->currentData().toString();
  request.provider = providerBox_->currentData().toString();
  request.allowRemote = TranslationService::isRemoteProvider(request.provider) &&
                        consentBox_->isChecked();
  request.endpoint = request.provider == "libretranslate" ? endpointEdit_->text().trimmed() : QString{};
  request.apiKey = keyEdit_->text();

  TranslationService service;
  const auto issue = service.validate(request);
  if (!issue.isEmpty()) {
    statusLabel_->setText(issue);
    return;
  }

  busy_ = true;
  outputPath_.clear();
  openFolderButton_->setEnabled(false);
  fileEdit_->setEnabled(false);
  providerBox_->setEnabled(false);
  sourceBox_->setEnabled(false);
  targetBox_->setEnabled(false);
  endpointEdit_->setEnabled(false);
  keyEdit_->setEnabled(false);
  consentBox_->setEnabled(false);
  translateButton_->setEnabled(false);
  progress_->show();
  statusLabel_->setText(request.allowRemote
      ? "Translating via your selected service..." : "Translating offline...");

  // Keys remain ephemeral and are sent via stdin to the local worker.
  auto* watcher = new QFutureWatcher<WorkerResult>(this);
  connect(watcher, &QFutureWatcher<WorkerResult>::finished, this, [this, watcher] {
    const auto result = watcher->result();
    watcher->deleteLater();
    finishTranslation(result);
  });
  watcher->setFuture(QtConcurrent::run([request] {
    TranslationService backgroundService;
    return backgroundService.translateFile(request);
  }));
}

void TranslationDialog::finishTranslation(const WorkerResult& result) {
  busy_ = false;
  progress_->hide();
  fileEdit_->setEnabled(true);
  providerBox_->setEnabled(true);
  sourceBox_->setEnabled(true);
  targetBox_->setEnabled(true);
  endpointEdit_->setEnabled(true);
  keyEdit_->setEnabled(true);
  consentBox_->setEnabled(true);
  refreshProvider();

  // Never show or log API keys. Only the output path and status are displayed.
  if (!result.ok) {
    statusLabel_->setText("Translation failed: " + result.error);
    return;
  }

  const auto doc = QJsonDocument::fromJson(result.output.toUtf8());
  outputPath_ = doc.object().value("output_path").toString();
  if (outputPath_.isEmpty()) {
    statusLabel_->setText("Translation completed but output path was not returned.");
    return;
  }
  openFolderButton_->setEnabled(true);
  statusLabel_->setText("Saved translated copy: " + QFileInfo(outputPath_).fileName());
}

void TranslationDialog::openOutputFolder() {
  if (outputPath_.isEmpty()) return;
  const auto dir = QFileInfo(outputPath_).absolutePath();
  QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

void TranslationDialog::reject() {
  if (busy_) {
    QMessageBox::information(this, "Translation in progress",
                             "Translation is still running. Close this window after it finishes.");
    return;
  }
  QDialog::reject();
}

}  // namespace omnidrop
