#include "adapters/python_worker_client.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QElapsedTimer>
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

WorkerResult PythonWorkerClient::streamWorkflow(
    const QJsonObject& document,
    const QStringList& paths,
    const WorkflowEventCallback& onEvent,
    const std::atomic_bool* stopRequested) const {
  // Separate streaming command, preserving one-shot JSON protocol semantics.
  QJsonArray inputArray;
  for (const auto& path : paths) inputArray.append(path);
  const QJsonObject request{
      {"command", "workflow.run_stream"},
      {"workflow", document},
      {"paths", inputArray},
  };

  QProcess process;
  process.setProgram(resolvePython());
  process.setArguments({resolveWorkerScript()});
  process.start();
  if (!process.waitForStarted(3000)) {
    return {false, {}, "Unable to start local Python workflow worker."};
  }

  QByteArray frame = QJsonDocument(request).toJson(QJsonDocument::Compact);
  frame.append('\n');
  if (process.write(frame) != frame.size() || !process.waitForBytesWritten(3000)) {
    process.kill();
    process.waitForFinished();
    return {false, {}, "Unable to send workflow request to local worker."};
  }

  QElapsedTimer clock;
  clock.start();
  QByteArray pending;
  bool gotFinal = false;
  bool cancelled = false;
  bool invalidFrame = false;
  QJsonObject finalResult;
  constexpr qint64 kMaximumOutputBuffer = 4 * 1024 * 1024;
  constexpr qint64 kMaximumRunMs = 30 * 60 * 1000;

  const auto consume = [&] {
    pending.append(process.readAllStandardOutput());
    if (pending.size() > kMaximumOutputBuffer) {
      invalidFrame = true;
      return;
    }
    qsizetype newlineIndex;
    while ((newlineIndex = pending.indexOf('\n')) >= 0) {
      const auto line = pending.left(newlineIndex);
      pending.remove(0, newlineIndex + 1);
      if (line.trimmed().isEmpty()) continue;

      QJsonParseError error;
      const auto parsed = QJsonDocument::fromJson(line, &error);
      if (error.error != QJsonParseError::NoError || !parsed.isObject()) {
        invalidFrame = true;
        return;
      }
      const auto event = parsed.object();
      if (event.value("event").toString() == "workflow.finished") {
        if (gotFinal || !event.value("result").isObject()) {
          invalidFrame = true;
          return;
        }
        finalResult = event.value("result").toObject();
        gotFinal = true;
      } else {
        if (gotFinal) {
          invalidFrame = true;
          return;
        }
        if (onEvent) onEvent(event);
      }
    }
  };

  while (process.state() != QProcess::NotRunning && !invalidFrame) {
    if (!cancelled && stopRequested &&
        stopRequested->load(std::memory_order_acquire)) {
      const QByteArray command("{\"command\":\"workflow.cancel\"}\n");
      if (process.write(command) == command.size()) {
        process.waitForBytesWritten(500);
      }
      cancelled = true;
    }

    if (clock.elapsed() >= kMaximumRunMs) {
      process.kill();
      process.waitForFinished();
      return {false, {}, "Workflow exceeded the 30-minute safety timeout."};
    }

    process.waitForReadyRead(80);
    consume();
  }

  consume();
  if (invalidFrame || !gotFinal || !pending.trimmed().isEmpty()) {
    if (process.state() != QProcess::NotRunning) {
      process.kill();
      process.waitForFinished();
    }
    return {false, {}, "Workflow streaming protocol returned an invalid or incomplete response."};
  }

  const QByteArray output = QJsonDocument(finalResult).toJson(QJsonDocument::Compact);
  const auto issue = finalResult.value("error").toObject();
  return {finalResult.value("ok").toBool(false), QString::fromUtf8(output),
          issue.value("message").toString()};
}

}  // namespace omnidrop
