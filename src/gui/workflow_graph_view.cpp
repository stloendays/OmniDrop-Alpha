#include "gui/workflow_graph_view.hpp"

#include <QGraphicsEllipseItem>
#include <QJsonArray>
#include <QFont>
#include <QGraphicsPathItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QScrollBar>
#include <QSet>
#include <QStyleOptionGraphicsItem>

#include <algorithm>
#include <functional>
#include <cmath>

namespace omnidrop {
namespace {

constexpr qreal kWidth = 190.0;
constexpr qreal kHeight = 84.0;
constexpr qreal kXGap = 246.0;
constexpr qreal kYGap = 134.0;

class MovableNode final : public QGraphicsRectItem {
 public:
  MovableNode(const QString& id, WorkflowGraphView* host)
      : QGraphicsRectItem(0, 0, kWidth, kHeight), id_(id), host_(host) {
    setFlag(QGraphicsItem::ItemIsMovable, true);
    setFlag(QGraphicsItem::ItemIsSelectable, true);
    setFlag(QGraphicsItem::ItemSendsGeometryChanges, true);
    setZValue(2);
    setBrush(QColor("#ffffff"));
    setPen(QPen(QColor("#c5c5c5"), 1.3));
  }

 protected:
  QVariant itemChange(GraphicsItemChange change, const QVariant& value) override {
    const auto result = QGraphicsRectItem::itemChange(change, value);
    if (change == ItemPositionHasChanged && host_) host_->refreshEdges();
    return result;
  }

 private:
  QString id_;
  WorkflowGraphView* host_;
};

QGraphicsEllipseItem* createPort(QGraphicsItem* parent, const QString& id,
                                  const QString& kind, const QPointF& position) {
  auto* port = new QGraphicsEllipseItem(0, 0, 14, 14, parent);
  port->setPos(position);
  port->setPen(QPen(QColor("#444444"), 1.4));
  port->setBrush(QColor("#e6e6e6"));
  port->setZValue(5);
  port->setCursor(Qt::CrossCursor);
  port->setData(0, kind);
  port->setData(1, id);
  port->setToolTip(kind == "out" ? "Drag from this output to another node's input."
                                  : "Drop an output here to connect a source.");
  return port;
}

}  // namespace

WorkflowGraphView::WorkflowGraphView(QWidget* parent) : QGraphicsView(parent) {
  auto* graphicsScene = new QGraphicsScene(this);
  setScene(graphicsScene);
  setRenderHint(QPainter::Antialiasing, true);
  setBackgroundBrush(QBrush(QColor("#f7f7f7")));
  setFrameShape(QFrame::NoFrame);
  setDragMode(QGraphicsView::RubberBandDrag);
  setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
  setToolTip("Drag nodes to arrange. Drag from a right port to a left port to link. "
             "Right-click an edge to disconnect.");
}

void WorkflowGraphView::setDocument(const QJsonObject& workflow) {
  // Preserve manually arranged positions while editing the same node IDs.
  for (auto it = nodes_.cbegin(); it != nodes_.cend(); ++it) {
    if (it.value()) positions_.insert(it.key(), it.value()->pos());
  }
  document_ = workflow;
  rebuild();
}

QPainterPath WorkflowGraphView::connectionPath(const QPointF& source, const QPointF& target) const {
  QPainterPath path(source);
  const qreal bend = std::max(qreal(42), std::abs(target.x() - source.x()) * 0.45);
  path.cubicTo(source + QPointF(bend, 0), target - QPointF(bend, 0), target);
  return path;
}

void WorkflowGraphView::rebuild() {
  scene()->clear();
  preview_ = nullptr;
  edges_.clear();
  nodes_.clear();
  dragSource_.clear();

  auto makeNode = [this](const QString& id, const QString& label,
                         int depth, int row, bool input) {
    auto* node = new MovableNode(id, this);
    node->setData(0, "node");
    node->setData(1, id);
    scene()->addItem(node);

    auto* caption = new QGraphicsSimpleTextItem(label.left(27), node);
    QFont titleFont = font();
    titleFont.setPixelSize(13);
    titleFont.setBold(true);
    caption->setFont(titleFont);
    caption->setBrush(QColor("#181818"));
    caption->setPos(17, 18);
    caption->setToolTip(label);

    auto* helper = new QGraphicsSimpleTextItem(
        input ? "Selected file inputs" : id, node);
    QFont helperFont = font();
    helperFont.setPixelSize(11);
    helper->setFont(helperFont);
    helper->setBrush(QColor("#747474"));
    helper->setPos(17, 45);

    if (!input) createPort(node, id, "in", {-7, kHeight / 2 - 7});
    createPort(node, id, "out", {kWidth - 7, kHeight / 2 - 7});

    const QPointF position = positions_.value(id, QPointF(35 + depth * kXGap, 50 + row * kYGap));
    node->setPos(position);
    nodes_.insert(id, node);
  };

  makeNode("$input", "Files", 0, 0, true);
  const auto rawNodes = document_.value("nodes").toArray();
  QHash<QString, int> depths;
  depths.insert("$input", 0);
  QList<QJsonObject> pending;
  for (const auto& raw : rawNodes) pending.append(raw.toObject());

  int guard = 0;
  while (!pending.isEmpty() && ++guard <= pending.size() + rawNodes.size() + 1) {
    bool progressed = false;
    for (auto it = pending.begin(); it != pending.end();) {
      const auto sources = it->value("sources").toArray();
      bool ready = true;
      int depth = 0;
      for (const auto& source : sources) {
        if (!depths.contains(source.toString())) {
          ready = false;
          break;
        }
        depth = std::max(depth, depths.value(source.toString()) + 1);
      }
      if (ready) {
        depths.insert(it->value("id").toString(), depth);
        it = pending.erase(it);
        progressed = true;
      } else {
        ++it;
      }
    }
    if (!progressed) break;
  }

  QHash<int, int> vertical;
  for (const auto& raw : rawNodes) {
    const auto node = raw.toObject();
    const QString id = node.value("id").toString();
    const int depth = depths.value(id, 1);
    makeNode(id, node.value("action_id").toString(), depth, vertical[depth]++, false);
  }

  for (const auto& raw : rawNodes) {
    const auto node = raw.toObject();
    const QString target = node.value("id").toString();
    for (const auto& sourceRaw : node.value("sources").toArray()) {
      const QString source = sourceRaw.toString();
      if (!nodes_.contains(source) || !nodes_.contains(target)) continue;
      auto* edge = scene()->addPath(QPainterPath(), QPen(QColor("#8d8d8d"), 2.4));
      edge->setData(0, "edge");
      edge->setData(1, source);
      edge->setData(2, target);
      edge->setZValue(-1);
      edge->setToolTip("Right-click to disconnect this dependency.");
      edges_.append(edge);
    }
  }
  refreshEdges();

  QRectF extent = scene()->itemsBoundingRect().adjusted(-30, -30, 70, 80);
  if (extent.width() < 600) extent.setWidth(600);
  if (extent.height() < 260) extent.setHeight(260);
  scene()->setSceneRect(extent);
}

void WorkflowGraphView::refreshEdges() {
  if (!scene()) return;
  for (auto* edge : edges_) {
    const QString source = edge->data(1).toString();
    const QString target = edge->data(2).toString();
    if (!nodes_.contains(source) || !nodes_.contains(target)) continue;
    const QPointF from = nodes_.value(source)->scenePos() + QPointF(kWidth, kHeight / 2);
    const QPointF to = nodes_.value(target)->scenePos() + QPointF(0, kHeight / 2);
    edge->setPath(connectionPath(from, to));
  }
}

QGraphicsItem* WorkflowGraphView::hitPort(const QPointF& position,
                                          const QString& kind) const {
  auto* item = scene()->itemAt(position, transform());
  if (item && item->data(0).toString() == kind) return item;
  return nullptr;
}

bool WorkflowGraphView::validGraph(const QJsonObject& document) const {
  QHash<QString, QStringList> sources;
  for (const auto& raw : document.value("nodes").toArray()) {
    const auto item = raw.toObject();
    const QString id = item.value("id").toString();
    QStringList refs;
    for (const auto& source : item.value("sources").toArray()) {
      const QString value = source.toString();
      if (value != "$input") refs.append(value);
    }
    sources.insert(id, refs);
  }
  QHash<QString, int> visited;
  std::function<bool(const QString&)> visit = [&](const QString& id) {
    const int state = visited.value(id, 0);
    if (state == 1) return false;
    if (state == 2) return true;
    if (!sources.contains(id)) return false;
    visited[id] = 1;
    for (const auto& child : sources.value(id)) {
      if (!visit(child)) return false;
    }
    visited[id] = 2;
    return true;
  };
  for (const auto& id : sources.keys()) {
    if (!visit(id)) return false;
  }
  return true;
}

void WorkflowGraphView::applyChange(const QJsonObject& document) {
  if (!validGraph(document)) {
    emit editRejected("The new connection would introduce an invalid dependency cycle.");
    return;
  }
  setDocument(document);
  emit workflowChanged(document_);
}

void WorkflowGraphView::requestConnection(const QString& source, const QString& target) {
  connectPorts(source, target);
}

void WorkflowGraphView::requestDisconnection(const QString& source, const QString& target) {
  removeLink(source, target);
}

void WorkflowGraphView::connectPorts(const QString& source, const QString& target) {
  if (source.isEmpty() || target.isEmpty() || source == target ||
      (source != "$input" && !nodes_.contains(source)) || !nodes_.contains(target)) {
    emit editRejected("Choose different valid source and target nodes.");
    return;
  }
  QJsonObject changed = document_;
  QJsonArray values = changed.value("nodes").toArray();
  for (int i = 0; i < values.size(); ++i) {
    auto node = values.at(i).toObject();
    if (node.value("id").toString() != target) continue;
    QJsonArray sources = node.value("sources").toArray();
    for (const auto& previous : sources) {
      if (previous.toString() == source) return;
    }
    if (sources.size() == 1 && sources.at(0).toString() == "$input" && source != "$input") {
      sources = QJsonArray{};
    }
    sources.append(source);
    node.insert("sources", sources);
    values.replace(i, node);
    changed.insert("nodes", values);
    applyChange(changed);
    return;
  }
}

void WorkflowGraphView::removeLink(const QString& source, const QString& target) {
  QJsonObject changed = document_;
  QJsonArray values = changed.value("nodes").toArray();
  for (int i = 0; i < values.size(); ++i) {
    auto node = values.at(i).toObject();
    if (node.value("id").toString() != target) continue;
    QJsonArray sources;
    for (const auto& item : node.value("sources").toArray()) {
      if (item.toString() != source) sources.append(item);
    }
    if (sources.isEmpty()) sources.append("$input");
    node.insert("sources", sources);
    values.replace(i, node);
    changed.insert("nodes", values);
    applyChange(changed);
    return;
  }
}

void WorkflowGraphView::setNodeStatus(const QString& id, const QString& status) {
  auto* item = nodes_.value(id, nullptr);
  if (!item) return;
  if (status == "running") {
    item->setPen(QPen(QColor("#222222"), 3));
  } else if (status == "failed") {
    item->setPen(QPen(QColor("#9f3636"), 2.5));
  } else if (status == "completed") {
    item->setPen(QPen(QColor("#606060"), 1.8));
  } else {
    item->setPen(QPen(QColor("#c5c5c5"), 1.3));
  }
}

void WorkflowGraphView::mousePressEvent(QMouseEvent* event) {
  const QPointF pos = mapToScene(event->position().toPoint());
  if (event->button() == Qt::LeftButton) {
    if (auto* port = hitPort(pos, "out")) {
      dragSource_ = port->data(1).toString();
      preview_ = scene()->addPath(QPainterPath(),
                                  QPen(QColor("#494949"), 1.8, Qt::DashLine));
      preview_->setZValue(-1);
      event->accept();
      return;
    }
    auto* node = scene()->itemAt(pos, transform());
    while (node && node->data(0).toString() != "node") node = node->parentItem();
    if (node) emit nodeSelected(node->data(1).toString());
  }
  if (event->button() == Qt::RightButton) {
    auto* item = scene()->itemAt(pos, transform());
    if (item && item->data(0).toString() == "edge") {
      QMenu menu(this);
      auto* remove = menu.addAction("Disconnect");
      if (menu.exec(event->globalPosition().toPoint()) == remove) {
        removeLink(item->data(1).toString(), item->data(2).toString());
      }
      event->accept();
      return;
    }
  }
  QGraphicsView::mousePressEvent(event);
}

void WorkflowGraphView::mouseMoveEvent(QMouseEvent* event) {
  if (!dragSource_.isEmpty() && preview_ && nodes_.contains(dragSource_)) {
    const QPointF from = nodes_.value(dragSource_)->scenePos() + QPointF(kWidth, kHeight / 2);
    preview_->setPath(connectionPath(from, mapToScene(event->position().toPoint())));
    event->accept();
    return;
  }
  QGraphicsView::mouseMoveEvent(event);
}

void WorkflowGraphView::mouseReleaseEvent(QMouseEvent* event) {
  if (!dragSource_.isEmpty()) {
    const QString source = dragSource_;
    dragSource_.clear();
    if (preview_) {
      scene()->removeItem(preview_);
      delete preview_;
      preview_ = nullptr;
    }
    if (auto* target = hitPort(mapToScene(event->position().toPoint()), "in")) {
      connectPorts(source, target->data(1).toString());
    }
    event->accept();
    return;
  }
  QGraphicsView::mouseReleaseEvent(event);
}

}  // namespace omnidrop
