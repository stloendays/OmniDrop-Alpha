#ifdef NDEBUG
#undef NDEBUG
#endif

#include "gui/workflow_graph_view.hpp"

#include <QApplication>
#include <QJsonArray>
#include <QJsonObject>

#include <cassert>

int main(int argc, char* argv[]) {
  QApplication app(argc, argv);

  const QJsonObject document{
      {"schema_version", 1},
      {"name", "Graph editing test"},
      {"nodes", QJsonArray{
          QJsonObject{
              {"id", "first"},
              {"action_id", "text.normalize"},
              {"sources", QJsonArray{QStringLiteral("$input")}},
          },
          QJsonObject{
              {"id", "second"},
              {"action_id", "text.deduplicate"},
              {"sources", QJsonArray{QStringLiteral("$input")}},
          },
      }},
  };

  omnidrop::WorkflowGraphView view;
  view.setDocument(document);
  assert(view.document() == document);
  int edits = 0;
  int rejected = 0;
  QObject::connect(&view, &omnidrop::WorkflowGraphView::workflowChanged,
                   [&](const QJsonObject&) { ++edits; });
  QObject::connect(&view, &omnidrop::WorkflowGraphView::editRejected,
                   [&](const QString&) { ++rejected; });

  view.requestConnection("first", "second");
  assert(edits == 1);
  const auto nodesAfterJoin = view.document().value("nodes").toArray();
  assert(nodesAfterJoin.at(1).toObject().value("sources").toArray().first().toString() == "first");

  // Cycles must never overwrite the last valid graph.
  const auto stable = view.document();
  view.requestConnection("second", "first");
  assert(rejected == 1);
  assert(view.document() == stable);

  view.requestDisconnection("first", "second");
  assert(edits == 2);
  const auto afterRemoval = view.document().value("nodes").toArray();
  assert(afterRemoval.at(1).toObject().value("sources").toArray().first().toString() == "$input");

  view.requestConnection("$input", "second");
  assert(edits == 2);  // Duplicate dependency remains unchanged.
  assert(view.document().value("nodes").toArray() == afterRemoval);
  view.setNodeStatus("first", "running");
  view.setNodeStatus("first", "completed");

  return 0;
}
