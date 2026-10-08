#pragma once

#include "app/translation_request.hpp"

#include <QByteArray>
#include <QString>
#include <QStringList>

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

 private:
  WorkerResult invoke(const QByteArray& request) const;
  QString resolvePython() const;
  QString resolveWorkerScript() const;

  QString workerScript_;
};

}  // namespace omnidrop
