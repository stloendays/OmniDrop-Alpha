#include "adapters/python_worker_client.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>

#include <utility>

namespace omnidrop {

PythonWorkerClient::PythonWorkerClient(QString workerScript)
    : workerScript_(std::move(workerScript)) {}

QString PythonWorkerClient::resolvePython() const {
  const auto env = QProcessEnvironment::systemEnvironment();
  const auto configured = env.value("OMNIDROP_PYTHON");
  if (!configured.isEmpty()) return configured;
#ifdef Q_OS_WIN
  return QStringLiteral("python");
#else
  return QStringLiteral("python3");
#endif
}

QString PythonWorkerClient::resolveWorkerScript() const {
  if (!workerScript_.isEmpty()) return workerScript_;

  const auto env = QProcessEnvironment::systemEnvironment();
  const auto configured = env.value("OMNIDROP_WORKER");
  if (!configured.isEmpty()) return configured;

  const QDir exeDir(QCoreApplication::applicationDirPath());
  const QStringList candidates{
      exeDir.filePath("python/worker.py"),
      exeDir.filePath("../python/worker.py"),
      exeDir.filePath("../../python/worker.py"),
      QDir::current().filePath("python/worker.py"),
  };
  for (const auto& candidate : candidates) {
    if (QFileInfo::exists(candidate)) return QFileInfo(candidate).absoluteFilePath();
  }
  return candidates.constLast();
}

WorkerResult PythonWorkerClient::invoke(const QByteArray& request, int timeoutMs) const {
  QProcess process;
  process.setProgram(resolvePython());
  process.setArguments({resolveWorkerScript()});
  process.start();
  if (!process.waitForStarted(3000)) {
    return {false, {}, "Unable to start Python worker. Configure OMNIDROP_PYTHON if needed."};
  }

  process.write(request);
  process.closeWriteChannel();
  if (!process.waitForFinished(timeoutMs)) {
    process.kill();
    process.waitForFinished();
    return {false, {}, "Python worker timed out."};
  }

  const auto stdoutBytes = process.readAllStandardOutput().trimmed();
  const auto stderrBytes = process.readAllStandardError().trimmed();
  QJsonParseError parseError;
  const auto doc = QJsonDocument::fromJson(stdoutBytes, &parseError);
  if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
    return {false, QString::fromUtf8(stdoutBytes),
            stderrBytes.isEmpty() ? "Worker returned invalid JSON." : QString::fromUtf8(stderrBytes)};
  }

  const auto root = doc.object();
  const bool success = root.value("ok").toBool(false);
  if (!success) {
    const auto errorObject = root.value("error").toObject();
    return {false, QString::fromUtf8(stdoutBytes), errorObject.value("message").toString("Worker action failed.")};
  }
  return {true, QString::fromUtf8(stdoutBytes), {}};
}

WorkerResult PythonWorkerClient::ping() const {
  QJsonObject root{{"command", "ping"}};
  return invoke(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

WorkerResult PythonWorkerClient::capabilities() const {
  QJsonObject root{{"command", "capabilities"}};
  return invoke(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

WorkerResult PythonWorkerClient::runAction(const QString& actionId, const QString& path) const {
  QJsonObject root{{"command", "run"}, {"action_id", actionId}, {"path", path}};
  return invoke(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

WorkerResult PythonWorkerClient::runBatchAction(
    const QString& actionId,
    const QStringList& paths) const {
  QJsonArray pathArray;
  for (const auto& path : paths) pathArray.append(path);
  QJsonObject root{{"command", "run_batch"}, {"action_id", actionId}, {"paths", pathArray}};
  return invoke(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

WorkerResult PythonWorkerClient::translateFile(const TranslationRequest& request) const {
  // Sensitive fields are sent to the worker through stdin, not command-line
  // arguments, diagnostic logs or persistent settings.
  QJsonObject root{
      {"command", "translate_file"},
      {"path", request.path},
      {"source_lang", request.sourceLanguage},
      {"target_lang", request.targetLanguage},
      {"provider", request.provider},
      {"allow_remote", request.allowRemote},
      {"endpoint", request.endpoint},
      {"api_key", request.apiKey},
  };
  return invoke(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

WorkerResult PythonWorkerClient::workflowCommand(
    const QString& command,
    const QJsonObject& document,
    const QStringList& paths) const {
  QJsonArray inputs;
  for (const auto& path : paths) inputs.append(path);
  const QJsonObject request{
      {"command", command},
      {"workflow", document},
      {"paths", inputs},
  };
  // Running several sequential actions can take longer than a single action;
  // Qt callers invoke this service on a worker thread, not the GUI thread.
  return invoke(QJsonDocument(request).toJson(QJsonDocument::Compact),
                command == "workflow.run" ? 600000 : 120000);
}

}  // namespace omnidrop
