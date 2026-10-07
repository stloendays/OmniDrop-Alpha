#include "gui/drop_zone.hpp"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QLabel>
#include <QMimeData>
#include <QStyle>
#include <QUrl>
#include <QVBoxLayout>

namespace omnidrop {

DropZone::DropZone(QWidget* parent) : QFrame(parent) {
  setAcceptDrops(true);
  setObjectName("dropZone");
  setMinimumHeight(250);

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(40, 40, 40, 40);
  layout->setSpacing(10);
  layout->addStretch();

  title_ = new QLabel("Drop anything here", this);
  title_->setObjectName("dropTitle");
  title_->setAlignment(Qt::AlignCenter);
  layout->addWidget(title_);

  auto* subtitle = new QLabel("files  /  images  /  PDFs  /  media", this);
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
    event->acceptProposedAction();
    setActive(true);
  }
}

void DropZone::dragLeaveEvent(QDragLeaveEvent* event) {
  setActive(false);
  QFrame::dragLeaveEvent(event);
}

void DropZone::dropEvent(QDropEvent* event) {
  setActive(false);
  QStringList paths;
  for (const auto& url : event->mimeData()->urls()) {
    if (url.isLocalFile()) paths.push_back(url.toLocalFile());
  }
  if (!paths.isEmpty()) {
    event->acceptProposedAction();
    emit filesDropped(paths);
  }
}

}  // namespace omnidrop
