#pragma once

#include "adapters/python_worker_client.hpp"

#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace omnidrop {

struct WorkflowLoadResult {
  QJsonObject document;
  QString error;
  bool ok{false};
};

// Shared entry point for Qt, CLI, automation and future agent gateways.
// All workflow semantics are implemented by the same versioned local worker.
class WorkflowService {
 public:
  WorkflowLoadResult load(const QString& path) const;
  QString save(const QString& path, const QJsonObject& document) const;

  WorkerResult validate(const QJsonObject& document) const;
  WorkerResult plan(const QJsonObject& document, const QStringList& paths) const;
  WorkerResult run(const QJsonObject& document, const QStringList& paths) const;
  WorkerResult runStreaming(
      const QJsonObject& document,
      const QStringList& paths,
      const PythonWorkerClient::WorkflowEventCallback& onEvent,
      const std::atomic_bool* stopRequested = nullptr) const;

 private:
  PythonWorkerClient worker_;
};

}  // namespace omnidrop
