#include "app/workflow_service.hpp"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>

namespace omnidrop {
namespace {
constexpr qint64 kMaxWorkflowBytes = 64 * 1024;
}

WorkflowLoadResult WorkflowService::load(const QString& path) const {
  const QFileInfo info(path);
  if (!info.exists() || !info.isFile() || info.size() > kMaxWorkflowBytes) {
    return {{}, "Workflow file is missing or exceeds the 64 KB limit.", false};
  }

  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return {{}, "Cannot read the workflow file.", false};
  }

  QJsonParseError error;
  const auto parsed = QJsonDocument::fromJson(file.readAll(), &error);
  if (error.error != QJsonParseError::NoError || !parsed.isObject()) {
    return {{}, "Workflow file must contain a valid JSON object.", false};
  }
  return {parsed.object(), {}, true};
}

QString WorkflowService::save(const QString& path, const QJsonObject& document) const {
  if (path.trimmed().isEmpty()) return "Choose a workflow file path.";
  const auto rendered = QJsonDocument(document).toJson(QJsonDocument::Indented);
  if (rendered.size() > kMaxWorkflowBytes) return "Workflow is larger than 64 KB.";

  // Atomic replace avoids truncated manifests after a crash/power loss.
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly)) return "Cannot write the workflow file.";
  if (file.write(rendered) != rendered.size()) {
    file.cancelWriting();
    return "Cannot finish writing the workflow file.";
  }
  if (!file.commit()) return "Cannot atomically save the workflow file.";
  return {};
}

WorkerResult WorkflowService::validate(const QJsonObject& document) const {
  return worker_.workflowCommand("workflow.validate", document, {});
}

WorkerResult WorkflowService::plan(const QJsonObject& document,
                                   const QStringList& paths) const {
  return worker_.workflowCommand("workflow.plan", document, paths);
}

WorkerResult WorkflowService::run(const QJsonObject& document,
                                  const QStringList& paths) const {
  return worker_.workflowCommand("workflow.run", document, paths);
}

}  // namespace omnidrop
