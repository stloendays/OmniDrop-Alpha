#pragma once

#include "app/workflow_service.hpp"

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <atomic>

namespace omnidrop {

// Durable local-only workflow job queue. Job definitions and local paths stay
// on the user's machine. GUI and CLI use one shared data location and schema.
// Retries deliberately restart the full workflow (no unsafe implicit resume).
class WorkflowJobService {
 public:
  explicit WorkflowJobService(QString storagePath = {});

  WorkerResult enqueue(const QJsonObject& definition, const QStringList& paths) const;
  WorkerResult listJobs() const;
  WorkerResult events(const QString& id) const;
  WorkerResult execute(const QString& id,
                       const PythonWorkerClient::WorkflowEventCallback& onEvent = {},
                       const std::atomic_bool* stopRequested = nullptr) const;
  WorkerResult remove(const QString& id) const;
  QString storagePath() const { return storagePath_; }

 private:
  QJsonObject readStore(QString* error) const;
  QString writeStore(const QJsonObject& document) const;
  static QJsonObject summary(const QJsonObject& job);
  QString storagePath_;
  WorkflowService workflows_;
};

}  // namespace omnidrop
