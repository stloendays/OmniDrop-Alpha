#pragma once

#include "app/translation_request.hpp"

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <atomic>
#include <functional>

namespace omnidrop {

struct WorkerResult {
  bool ok{false};
  QString output;
  QString error;
};

class PythonWorkerClient {
 public:
  explicit PythonWorkerClient(QString workerScript = {});

  WorkerResult ping() const;
  WorkerResult capabilities() const;
  WorkerResult runAction(const QString& actionId, const QString& path) const;
  WorkerResult runBatchAction(const QString& actionId, const QStringList& paths) const;
  WorkerResult translateFile(const TranslationRequest& request) const;
  WorkerResult workflowCommand(const QString& command,
                               const QJsonObject& document,
                               const QStringList& paths) const;
  using WorkflowEventCallback = std::function<void(const QJsonObject&)>;
  WorkerResult streamWorkflow(const QJsonObject& document,
                              const QStringList& paths,
                              const WorkflowEventCallback& onEvent,
                              const std::atomic_bool* stopRequested = nullptr) const;

 private:
  WorkerResult invoke(const QByteArray& request, int timeoutMs = 120000) const;
  QString resolvePython() const;
  QString resolveWorkerScript() const;

  QString workerScript_;
};

}  // namespace omnidrop
