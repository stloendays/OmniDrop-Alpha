#pragma once

#include <QGraphicsView>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QPainterPath>
#include <QPointF>
#include <QString>
#include <QStringList>

class QGraphicsItem;
class QGraphicsPathItem;
class QGraphicsRectItem;
class QMouseEvent;

namespace omnidrop {

// Editable DAG view; the serialized workflow graph remains the only source
// of truth. Layout positions are ephemeral and do not alter schema v1.
class WorkflowGraphView final : public QGraphicsView {
  Q_OBJECT

 public:
  explicit WorkflowGraphView(QWidget* parent = nullptr);
  void setDocument(const QJsonObject& workflow);
  QJsonObject document() const { return document_; }
  void setNodeStatus(const QString& id, const QString& status);
  void refreshEdges();
  // Public editing commands also support headless semantic GUI tests.
  void requestConnection(const QString& source, const QString& target);
  void requestDisconnection(const QString& source, const QString& target);

 signals:
  void workflowChanged(const QJsonObject& document);
  void editRejected(const QString& message);
  void nodeSelected(const QString& nodeId);

 protected:
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;

 private:
  void rebuild();
  void connectPorts(const QString& source, const QString& target);
  void removeLink(const QString& source, const QString& target);
  bool validGraph(const QJsonObject& document) const;
  QGraphicsItem* hitPort(const QPointF& scenePoint, const QString& kind) const;
  void applyChange(const QJsonObject& document);
  QPainterPath connectionPath(const QPointF& source, const QPointF& target) const;

  QJsonObject document_;
  QHash<QString, QGraphicsRectItem*> nodes_;
  QHash<QString, QPointF> positions_;
  QList<QGraphicsPathItem*> edges_;
  QGraphicsPathItem* preview_{nullptr};
  QString dragSource_;
};

}  // namespace omnidrop
