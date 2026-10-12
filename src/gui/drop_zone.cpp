#include "gui/drop_zone.hpp"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QLabel>
#include <QMimeData>
#include <QStyle>
#include <QUrl>
#include <QVBoxLayout>

namespace omnidrop {

DropZone::DropZone(QWidget* parent) : QFrame(parent) {
  setAcceptDrops(true);
  setObjectName("dropZone");
  setMinimumHeight(182);
  setToolTip("Drop local regular files. Folders and web links are not supported.");

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(22, 28, 22, 28);
  layout->setSpacing(10);
  layout->addStretch();

  title_ = new QLabel("Drop files here", this);
  title_->setObjectName("dropTitle");
  title_->setAlignment(Qt::AlignCenter);
  layout->addWidget(title_);

  auto* subtitle = new QLabel("Local files only  /  or use Open files (Ctrl+O)", this);
  subtitle->setObjectName("dropSubtitle");
  subtitle->setAlignment(Qt::AlignCenter);
  layout->addWidget(subtitle);
  layout->addStretch();
}

void DropZone::setActive(bool active) {
  setProperty("dragActive", active);
  style()->unpolish(this);
  style()->polish(this);
}

void DropZone::dragEnterEvent(QDragEnterEvent* event) {
  if (event->mimeData()->hasUrls()) {
    for (const auto& url : event->mimeData()->urls()) {
      if (!url.isLocalFile()) continue;
      event->acceptProposedAction();
      setActive(true);
      return;
    }
  }
  event->ignore();
}

void DropZone::dragLeaveEvent(QDragLeaveEvent* event) {
  setActive(false);
  QFrame::dragLeaveEvent(event);
}

void DropZone::dropEvent(QDropEvent* event) {
  setActive(false);
  QStringList paths;
  int rejected = 0;
  for (const auto& url : event->mimeData()->urls()) {
    if (!url.isLocalFile()) {
      ++rejected;
      continue;
    }
    const auto path = url.toLocalFile();
    const QFileInfo file(path);
    if (!file.exists() || !file.isFile()) {
      ++rejected;
      continue;
    }
    paths.push_back(path);
  }
  if (paths.isEmpty()) {
    event->ignore();
    emit filesRejected(
        "No local files were accepted. Folders and web links are not supported.");
    return;
  }
  event->acceptProposedAction();
  emit filesDropped(paths);
  if (rejected > 0) {
    emit filesRejected(
        QString("%1 unsupported item(s) ignored (folders or web links).")
            .arg(rejected));
  }
}

}  // namespace omnidrop
