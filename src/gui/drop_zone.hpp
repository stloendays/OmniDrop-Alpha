#pragma once

#include <QFrame>
#include <QStringList>

class QLabel;

namespace omnidrop {

class DropZone final : public QFrame {
  Q_OBJECT

 public:
  explicit DropZone(QWidget* parent = nullptr);

 signals:
  void filesDropped(const QStringList& paths);

 protected:
  void dragEnterEvent(QDragEnterEvent* event) override;
  void dragLeaveEvent(QDragLeaveEvent* event) override;
  void dropEvent(QDropEvent* event) override;

 private:
  void setActive(bool active);
  QLabel* title_{nullptr};
};

}  // namespace omnidrop
