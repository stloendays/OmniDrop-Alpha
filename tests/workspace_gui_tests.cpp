#ifdef NDEBUG
#undef NDEBUG
#endif

#include "gui/main_window.hpp"
#include "gui/drop_zone.hpp"

#include <QAction>
#include <QApplication>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMimeData>
#include <QSplitter>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUrl>

#include <cassert>

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  QApplication::setOrganizationName("OmniDrop");
  QApplication::setApplicationName("OmniDrop");
  QStandardPaths::setTestModeEnabled(true);

  QTemporaryDir temp;
  assert(temp.isValid());
  const auto filePath = temp.filePath("a local file.txt");
  {
    QFile file(filePath);
    assert(file.open(QIODevice::WriteOnly));
    assert(file.write("hello\n") == 6);
  }

  omnidrop::MainWindow window;
  window.show();
  QApplication::processEvents();
  auto* splitter = window.findChild<QSplitter*>("workspaceSplitter");
  auto* zone = window.findChild<omnidrop::DropZone*>("dropZone");
  auto* search = window.findChild<QLineEdit*>("actionSearch");
  auto* list = window.findChild<QListWidget*>("actionList");
  auto* empty = window.findChild<QLabel*>("actionEmptyState");
  auto* summary = window.findChild<QLabel*>("actionSummary");
  auto* recent = window.findChild<QListWidget*>("recentFilesList");
  assert(splitter && splitter->count() == 2);
  assert(zone && search && list && empty && summary && recent);
  assert(empty->isVisible() && !search->isVisible());
  assert(!list->isVisible());
  assert(zone->findChild<QLabel*>("dropTitle")->text() ==
         "Drop files here");

  // Shortcut discoverability without activating a native file picker.
  bool hasOpenShortcut = false;
  bool hasFindShortcut = false;
  bool hasRunShortcut = false;
  for (auto* action : window.findChildren<QAction*>()) {
    hasOpenShortcut |= action->shortcut() == QKeySequence::Open;
    hasFindShortcut |= action->shortcut() == QKeySequence::Find;
    hasRunShortcut |= action->shortcut() ==
                      QKeySequence(Qt::CTRL | Qt::Key_Return);
  }
  assert(hasOpenShortcut && hasFindShortcut && hasRunShortcut);

  window.openPaths({filePath});
  QApplication::processEvents();
  assert(search->isVisible());
  assert(list->isVisible());
  assert(!empty->isVisible());
  assert(summary->text().contains("matching actions"));
  assert(recent->count() >= 1);
  assert(recent->item(0)->data(Qt::UserRole).toString() == filePath);

  // Search should expose a helpful explicit empty state without running
  // anything or hiding the search field itself.
  search->setText("this-action-id-does-not-exist");
  QApplication::processEvents();
  assert(empty->isVisible());
  assert(!list->isVisible());
  assert(empty->text().contains("No actions match"));
  search->clear();
  QApplication::processEvents();
  assert(!empty->isVisible());
  assert(list->isVisible());

  // Remote-only drag is never accepted as a local file operation.
  QMimeData remote;
  remote.setUrls({QUrl("https://example.invalid/sample.pdf")});
  QDragEnterEvent remoteEnter(QPoint(3, 3), Qt::CopyAction,
                              &remote, Qt::NoButton, Qt::NoModifier);
  QApplication::sendEvent(zone, &remoteEnter);
  assert(!remoteEnter.isAccepted());

  QStringList received;
  QString rejected;
  QObject::connect(zone, &omnidrop::DropZone::filesDropped,
                   &window, [&](const QStringList& paths) { received = paths; });
  QObject::connect(zone, &omnidrop::DropZone::filesRejected,
                   &window, [&](const QString& reason) { rejected = reason; });
  QMimeData local;
  local.setUrls({QUrl::fromLocalFile(filePath)});
  QDragEnterEvent localEnter(QPoint(3, 3), Qt::CopyAction,
                             &local, Qt::NoButton, Qt::NoModifier);
  QApplication::sendEvent(zone, &localEnter);
  assert(localEnter.isAccepted());
  QDropEvent localDrop(QPointF(3, 3), Qt::CopyAction,
                       &local, Qt::NoButton, Qt::NoModifier);
  QApplication::sendEvent(zone, &localDrop);
  assert(received == QStringList{filePath});

  // Local directory drops are explicitly rejected instead of being
  // interpreted as files or silently ignored.
  QMimeData folder;
  folder.setUrls({QUrl::fromLocalFile(temp.path())});
  QDragEnterEvent folderEnter(QPoint(3, 3), Qt::CopyAction,
                              &folder, Qt::NoButton, Qt::NoModifier);
  QApplication::sendEvent(zone, &folderEnter);
  QDropEvent folderDrop(QPointF(3, 3), Qt::CopyAction,
                        &folder, Qt::NoButton, Qt::NoModifier);
  QApplication::sendEvent(zone, &folderDrop);
  assert(rejected.contains("Folders"));

  return 0;
}
