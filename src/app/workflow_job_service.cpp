#include "app/workflow_job_service.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLockFile>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>

#include <utility>

namespace omnidrop {
namespace {

constexpr qint64 kMaxStateBytes = 6 * 1024 * 1024;
constexpr int kMaxJobs = 100;
constexpr int kMaxEventsPerJob = 160;
const QRegularExpression kJobId("^[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}$");

QString now() {
  return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

QJsonObject makeEvent(const QString& kind, int attempt,
                      const QString& node = {}, int completed = -1,
                      int total = -1) {
  QJsonObject event{
      {"at", now()}, {"event", kind}, {"attempt", attempt},
  };
  if (!node.isEmpty()) event.insert("node_id", node);
  if (completed >= 0) event.insert("completed_operations", completed);
  if (total >= 0) event.insert("total_operations", total);
  return event;
}

void appendEvent(QJsonObject& job, const QJsonObject& event) {
  if (event.isEmpty()) return;
  auto entries = job.value("events").toArray();
  entries.append(event);
  // Bounds keep event history useful without growing persisted Jobs unboundedly.
  while (entries.size() > kMaxEventsPerJob) entries.removeFirst();
  job.insert("events", entries);
}


WorkerResult okResult(const QJsonObject& value) {
  QJsonObject payload = value;
  payload.insert("ok", true);
  return {true, QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact)), {}};
}

WorkerResult errorResult(const QString& error) {
  const QJsonObject payload{{"ok", false}, {"error", error}};
  return {false, QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact)), error};
}

bool lockForFile(const QString& path, QLockFile& lock) {
  if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
  lock.setStaleLockTime(0);  // Never evict a process just because an action runs slowly.
  return lock.tryLock(5000);
}

QJsonObject initialState() {
  return QJsonObject{{"schema_version", 1}, {"jobs", QJsonArray{}}};
}

}  // namespace

WorkflowJobService::WorkflowJobService(QString storagePath)
    : storagePath_(std::move(storagePath)) {
  if (storagePath_.isEmpty()) {
    const auto overridePath = QProcessEnvironment::systemEnvironment()
                                  .value("OMNIDROP_WORKFLOW_JOBS");
    if (!overridePath.isEmpty()) {
      storagePath_ = overridePath;
    } else {
      const QDir base(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation));
      storagePath_ = base.filePath("OmniDrop/workflows/jobs.json");
    }
  }
}

QJsonObject WorkflowJobService::readStore(QString* error) const {
  if (error) error->clear();
  QFile file(storagePath_);
  if (!file.exists()) return initialState();
  if (!file.open(QIODevice::ReadOnly)) {
    if (error) *error = "Cannot read the local workflow job store.";
    return {};
  }
  if (file.size() > kMaxStateBytes) {
    if (error) *error = "Workflow job store exceeds the supported size limit.";
    return {};
  }
  QJsonParseError parseError;
  const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isObject() ||
      document.object().value("schema_version").toInt(-1) != 1 ||
      !document.object().value("jobs").isArray()) {
    if (error) *error = "Workflow job store is corrupt or uses an unsupported schema.";
    return {};
  }
  return document.object();
}

QString WorkflowJobService::writeStore(const QJsonObject& document) const {
  if (!QDir().mkpath(QFileInfo(storagePath_).absolutePath())) {
    return "Cannot create the workflow jobs directory.";
  }
  const auto content = QJsonDocument(document).toJson(QJsonDocument::Compact);
  if (content.size() > kMaxStateBytes) {
    return "Workflow job store has reached its size limit.";
  }
  QSaveFile output(storagePath_);
  if (!output.open(QIODevice::WriteOnly)) {
    return "Cannot open workflow job store for writing.";
  }
  if (output.write(content) != content.size()) {
    output.cancelWriting();
    return "Cannot finish writing the workflow job store.";
  }
  if (!output.commit()) return "Cannot atomically commit the workflow job store.";
  QFile::setPermissions(storagePath_, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
  return {};
}

QJsonObject WorkflowJobService::summary(const QJsonObject& job) {
  const auto result = job.value("last_result").toObject();
  return QJsonObject{
      {"id", job.value("id")},
      {"name", job.value("name")},
      {"status", job.value("status")},
      {"created_at", job.value("created_at")},
      {"updated_at", job.value("updated_at")},
      {"attempts", job.value("attempts")},
      {"completed_operations", job.value("completed_operations")},
      {"total_operations", job.value("total_operations")},
      {"last_error", job.value("last_error")},
      {"output_count", result.value("created_output_paths").toArray().size()},
  };
}

WorkerResult WorkflowJobService::enqueue(
    const QJsonObject& definition, const QStringList& paths) const {
  const auto preflight = workflows_.plan(definition, paths);
  if (!preflight.ok) return errorResult("Invalid workflow or input files: " + preflight.error);

  const auto preview = QJsonDocument::fromJson(preflight.output.toUtf8()).object();
  QStringList absolutePaths;
  for (const auto& path : paths) absolutePaths.push_back(QFileInfo(path).absoluteFilePath());

  QLockFile lock(storagePath_ + ".lock");
  if (!lockForFile(storagePath_, lock)) return errorResult("Workflow job store is locked.");
  QString error;
  auto root = readStore(&error);
  if (!error.isEmpty()) return errorResult(error);
  auto jobs = root.value("jobs").toArray();
  if (jobs.size() >= kMaxJobs) {
    return errorResult("Workflow job limit reached. Remove old jobs before adding more.");
  }

  const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
  QJsonArray inputs;
  for (const auto& path : absolutePaths) inputs.append(path);
  const QJsonObject job{
      {"id", id},
      {"name", definition.value("name").toString()},
      {"status", "queued"},
      {"created_at", now()},
      {"updated_at", now()},
      {"attempts", 0},
      {"completed_operations", 0},
      {"total_operations", preview.value("operation_count").toInt()},
      {"workflow", definition},
      {"inputs", inputs},
      {"last_error", ""},
      {"last_result", QJsonObject{}},
      {"events", QJsonArray{makeEvent("job.queued", 0)}},
  };
  jobs.append(job);
  root.insert("jobs", jobs);
  error = writeStore(root);
  if (!error.isEmpty()) return errorResult(error);
  return okResult(QJsonObject{{"job", summary(job)}});
}

WorkerResult WorkflowJobService::listJobs() const {
  QLockFile lock(storagePath_ + ".lock");
  if (!lockForFile(storagePath_, lock)) return errorResult("Workflow job store is locked.");
  QString error;
  auto root = readStore(&error);
  if (!error.isEmpty()) return errorResult(error);

  auto jobs = root.value("jobs").toArray();
  QJsonArray visible;
  bool changed = false;
  for (int index = 0; index < jobs.size(); ++index) {
    auto job = jobs.at(index).toObject();
    if (job.value("status").toString() == "running") {
      // An available run lock means that the previous owning process ended.
      const auto id = job.value("id").toString();
      QLockFile runLock(storagePath_ + "." + id + ".running");
      runLock.setStaleLockTime(0);
      if (runLock.tryLock(0)) {
        job.insert("status", "interrupted");
        job.insert("last_error", "The previous process exited before completing this job.");
        appendEvent(job, makeEvent("job.interrupted", job.value("attempts").toInt()));
        job.insert("updated_at", now());
        jobs.replace(index, job);
        changed = true;
      }
    }
    visible.prepend(summary(job));  // Newest first.
  }
  if (changed) {
    root.insert("jobs", jobs);
    error = writeStore(root);
    if (!error.isEmpty()) return errorResult(error);
  }
  return okResult(QJsonObject{{"schema_version", 1}, {"jobs", visible}});
}

WorkerResult WorkflowJobService::events(const QString& id) const {
  if (!kJobId.match(id).hasMatch()) return errorResult("Invalid workflow job ID.");
  QLockFile lock(storagePath_ + ".lock");
  if (!lockForFile(storagePath_, lock)) return errorResult("Workflow job store is locked.");
  QString error;
  const auto root = readStore(&error);
  if (!error.isEmpty()) return errorResult(error);

  for (const auto& value : root.value("jobs").toArray()) {
    const auto job = value.toObject();
    if (job.value("id").toString() != id) continue;
    return okResult(QJsonObject{
        {"schema_version", 1},
        {"job_id", id},
        {"status", job.value("status")},
        {"attempts", job.value("attempts")},
        // Old persisted v2.3 entries had no events. Return an empty timeline.
        {"events", job.value("events").toArray()},
    });
  }
  return errorResult("Workflow job was not found.");
}

WorkerResult WorkflowJobService::execute(
    const QString& id,
    const PythonWorkerClient::WorkflowEventCallback& onEvent,
    const std::atomic_bool* stopRequested) const {
  if (!kJobId.match(id).hasMatch()) return errorResult("Invalid workflow job ID.");

  if (!QDir().mkpath(QFileInfo(storagePath_).absolutePath())) {
    return errorResult("Cannot open workflow jobs directory.");
  }
  QLockFile runLock(storagePath_ + "." + id + ".running");
  runLock.setStaleLockTime(0);
  if (!runLock.tryLock(0)) {
    return errorResult("This workflow job is already running in another process.");
  }

  QJsonObject selected;
  {
    QLockFile lock(storagePath_ + ".lock");
    if (!lockForFile(storagePath_, lock)) return errorResult("Workflow job store is locked.");
    QString error;
    auto root = readStore(&error);
    if (!error.isEmpty()) return errorResult(error);
    auto jobs = root.value("jobs").toArray();
    bool found = false;
    for (int i = 0; i < jobs.size(); ++i) {
      auto job = jobs.at(i).toObject();
      if (job.value("id").toString() != id) continue;
      found = true;
      if (job.value("status").toString() == "completed") {
        return errorResult("Completed jobs cannot be retried; enqueue a new run instead.");
      }
      job.insert("status", "running");
      job.insert("attempts", job.value("attempts").toInt(0) + 1);
      job.insert("completed_operations", 0);
      job.insert("last_error", "");
      appendEvent(job, makeEvent("job.started", job.value("attempts").toInt()));
      job.insert("updated_at", now());
      jobs.replace(i, job);
      selected = job;
      break;
    }
    if (!found) return errorResult("Workflow job was not found.");
    root.insert("jobs", jobs);
    error = writeStore(root);
    if (!error.isEmpty()) return errorResult(error);
  }

  auto update = [this, &id](const QJsonObject& changes,
                            const QJsonObject& event = QJsonObject{}) {
    QLockFile lock(storagePath_ + ".lock");
    if (!lockForFile(storagePath_, lock)) return;
    QString error;
    auto root = readStore(&error);
    if (!error.isEmpty()) return;
    auto jobs = root.value("jobs").toArray();
    for (int i = 0; i < jobs.size(); ++i) {
      auto job = jobs.at(i).toObject();
      if (job.value("id").toString() != id) continue;
      for (auto it = changes.begin(); it != changes.end(); ++it) {
        job.insert(it.key(), it.value());
      }
      appendEvent(job, event);
      job.insert("updated_at", now());
      jobs.replace(i, job);
      root.insert("jobs", jobs);
      writeStore(root);
      break;
    }
  };

  const auto definition = selected.value("workflow").toObject();
  QStringList paths;
  for (const auto& value : selected.value("inputs").toArray()) {
    paths.append(value.toString());
  }

  const auto response = workflows_.runStreaming(
      definition, paths,
      [&](const QJsonObject& event) {
        const QString kind = event.value("event").toString();
        // Persist only typed, path-free operational fields. Never capture the
        // original worker payload, which may later contain user data.
        if (kind == "workflow.action_completed" ||
            kind == "workflow.node_started" ||
            kind == "workflow.node_completed" ||
            kind == "workflow.failed" ||
            kind == "workflow.stopped") {
          const QJsonObject changes = kind == "workflow.action_completed"
              ? QJsonObject{
                  {"completed_operations", event.value("completed_operations")},
                  {"total_operations", event.value("total_operations")},
                }
              : QJsonObject{};
          update(changes, makeEvent(
              kind, selected.value("attempts").toInt(),
              event.value("node_id").toString(),
              event.value("completed_operations").toInt(-1),
              event.value("total_operations").toInt(-1)));
        }
        if (onEvent) onEvent(event);
      },
      stopRequested);

  const auto report = QJsonDocument::fromJson(response.output.toUtf8()).object();
  const QString state = response.ok ? "completed"
      : (report.value("status").toString() == "stopped" ? "stopped" : "failed");

  update(QJsonObject{
      {"status", state},
      {"last_result", report},
      {"last_error", response.ok ? QString{} : response.error},
      {"completed_operations", report.value("completed_operations")},
      {"total_operations", report.value("total_operations")},
  }, makeEvent("job." + state, selected.value("attempts").toInt(),
               {}, report.value("completed_operations").toInt(-1),
               report.value("total_operations").toInt(-1)));
  return response;
}

WorkerResult WorkflowJobService::remove(const QString& id) const {
  if (!kJobId.match(id).hasMatch()) return errorResult("Invalid workflow job ID.");
  QLockFile runLock(storagePath_ + "." + id + ".running");
  runLock.setStaleLockTime(0);
  if (!runLock.tryLock(0)) {
    return errorResult("Cannot remove a running workflow job.");
  }

  QLockFile lock(storagePath_ + ".lock");
  if (!lockForFile(storagePath_, lock)) return errorResult("Workflow job store is locked.");
  QString error;
  auto root = readStore(&error);
  if (!error.isEmpty()) return errorResult(error);
  auto jobs = root.value("jobs").toArray();
  QJsonArray kept;
  bool found = false;
  for (const auto& value : jobs) {
    if (value.toObject().value("id").toString() == id) {
      found = true;
      continue;
    }
    kept.append(value);
  }
  if (!found) return errorResult("Workflow job was not found.");
  root.insert("jobs", kept);
  error = writeStore(root);
  if (!error.isEmpty()) return errorResult(error);
  // The job record is removed, not its generated files.
  return okResult(QJsonObject{{"removed_job_id", id}, {"kept_outputs", true}});
}

}  // namespace omnidrop
