#include "gui/scientific_preview_dialog.hpp"

#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QImage>
#include <QImageReader>
#include <QPixmap>
#include <QJsonArray>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QTabWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace omnidrop {

ScientificPreviewDialog::ScientificPreviewDialog(const QJsonObject& result, QWidget* parent)
    : QDialog(parent) {
  setWindowTitle("Scientific PDF preview");
  setMinimumSize(740, 540);
  resize(950, 680);

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(20, 18, 20, 18);
  layout->setSpacing(10);

  auto* heading = new QLabel("Scientific PDF extraction", this);
  heading->setStyleSheet("font-size: 18px; font-weight: 600;");
  layout->addWidget(heading);

  auto* notice = new QLabel(
      "Parsed text, tables, figures and LaTeX are candidate data only. "
      "Verify every scientific value against the source PDF and SI.", this);
  notice->setWordWrap(true);
  notice->setStyleSheet("color: #666666;");
  layout->addWidget(notice);

  const QString outputFile = result.value("output_path").toString();
  const QString previewPath = result.value("preview_path").toString();
  auto* location = new QLabel("Result: " + QFileInfo(outputFile).fileName(), this);
  location->setTextInteractionFlags(Qt::TextSelectableByMouse);
  location->setStyleSheet("color: #555555;");
  layout->addWidget(location);

  auto* tabs = new QTabWidget(this);
  auto* text = new QPlainTextEdit(tabs);
  text->setObjectName("scientificPreviewText");
  text->setReadOnly(true);
  text->setLineWrapMode(QPlainTextEdit::WidgetWidth);
  text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));

  QFile previewFile(previewPath);
  if (!previewPath.isEmpty() && previewFile.open(QIODevice::ReadOnly)) {
    constexpr qint64 kMaxPreviewBytes = 128 * 1024;
    const auto contents = previewFile.read(kMaxPreviewBytes);
    text->setPlainText(QString::fromUtf8(contents));
    if (!previewFile.atEnd()) text->appendPlainText("\n[Preview truncated; open the full output file.]");
  } else {
    text->setPlainText("No preview is available. Open the generated output file directly.");
  }
  tabs->addTab(text, "Text / LaTeX source");

  auto* gallery = new QScrollArea(tabs);
  gallery->setObjectName("scientificFigureGallery");
  gallery->setWidgetResizable(true);
  auto* galleryContent = new QWidget(gallery);
  auto* galleryLayout = new QVBoxLayout(galleryContent);
  galleryLayout->setSpacing(12);
  const auto images = result.value("preview_images").toArray();
  int rendered = 0;
  for (const auto& value : images) {
    if (!value.isString()) continue;
    const QString imagePath = value.toString();
    const QFileInfo file(imagePath);
    if (!file.isFile() || file.size() > 2 * 1024 * 1024) continue;

    QImageReader reader(imagePath);
    reader.setAutoTransform(true);
    const QSize size = reader.size();
    if (!size.isValid() || qint64(size.width()) * size.height() > 24'000'000) continue;
    reader.setScaledSize(size.scaled(QSize(460, 260), Qt::KeepAspectRatio));
    const QImage image = reader.read();
    if (image.isNull()) continue;
    auto* picture = new QLabel(galleryContent);
    picture->setAlignment(Qt::AlignCenter);
    picture->setPixmap(QPixmap::fromImage(image));
    galleryLayout->addWidget(picture);
    ++rendered;
  }
  if (rendered == 0) {
    auto* empty = new QLabel(
        "No safe local thumbnail was included. "
        "Open the ZIP result to inspect the complete image set.", galleryContent);
    empty->setWordWrap(true);
    galleryLayout->addWidget(empty);
  }
  galleryLayout->addStretch();
  gallery->setWidget(galleryContent);
  tabs->addTab(gallery, QString("Figures (%1)").arg(rendered));
  layout->addWidget(tabs, 1);

  auto* footer = new QHBoxLayout;
  auto* folderButton = new QPushButton("Open output folder", this);
  folderButton->setObjectName("scientificOpenOutputFolder");
  folderButton->setEnabled(QDir(result.value("output_dir").toString()).exists());
  connect(folderButton, &QPushButton::clicked, this,
          [dir = result.value("output_dir").toString()] {
            if (!dir.isEmpty()) QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
          });
  footer->addWidget(folderButton);
  footer->addStretch();
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  footer->addWidget(buttons);
  layout->addLayout(footer);
}

}  // namespace omnidrop
