#ifdef NDEBUG
#undef NDEBUG
#endif

#include "app/workflow_job_service.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QTemporaryDir>

#include <cassert>

namespace {

QJsonObject decode(const omnidrop::WorkerResult& result) {
  return QJsonDocument::fromJson(result.output.toUtf8()).object();
}

QJsonObject workflow() {
  return QJsonObject{
      {"schema_version", 1}, {"name", "Local history test"},
      {"nodes", QJsonArray{
          QJsonObject{
              {"id", "clean"},
              {"action_id", "text.normalize"},
              {"sources", QJsonArray{QStringLiteral("$input")}},
          },
          QJsonObject{
              {"id", "unique"},
              {"action_id", "text.deduplicate"},
              {"sources", QJsonArray{QStringLiteral("clean")}},
          },
      }},
  };
}

void writeInput(const QString& path) {
  QFile source(path);
  assert(source.open(QIODevice::WriteOnly));
  const QByteArray contents("one  \none  \ntwo \n");
  assert(source.write(contents) == contents.size());
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QTemporaryDir temp;
  assert(temp.isValid());

  const auto storePath = temp.filePath("state/jobs.json");
  omnidrop::WorkflowJobService service(storePath);
  const auto source = temp.filePath("notes.txt");
  writeInput(source);

  const auto created = service.enqueue(workflow(), {source});
  assert(created.ok);
  const auto id = decode(created).value("job").toObject().value("id").toString();
  assert(id.size() == 36);

  const auto listed = service.listJobs();
  assert(listed.ok);
  auto entries = decode(listed).value("jobs").toArray();
  assert(entries.size() == 1);
  assert(entries.first().toObject().value("status").toString() == "queued");

  // Multiple processes cannot run the same persisted job concurrently.
  QLockFile concurrent(storePath + "." + id + ".running");
  assert(concurrent.tryLock(0));
  const auto refused = service.execute(id);
  assert(!refused.ok);
  concurrent.unlock();

  int events = 0;
  const auto completed = service.execute(id, [&](const QJsonObject& event) {
    if (event.value("event").toString() == "workflow.action_completed") ++events;
  });
  assert(completed.ok);
  assert(events == 2);

  entries = decode(service.listJobs()).value("jobs").toArray();
  assert(entries.first().toObject().value("status").toString() == "completed");
  assert(entries.first().toObject().value("attempts").toInt() == 1);
  assert(entries.first().toObject().value("completed_operations").toInt() == 2);
  assert(!service.execute(id).ok);  // Completed jobs are immutable, enqueue anew.

  QFile original(source);
  assert(original.open(QIODevice::ReadOnly));
  assert(original.readAll() == "one  \none  \ntwo \n");

  // Failed runs can be retried explicitly. They rerun the entire graph.
  const auto next = service.enqueue(workflow(), {source});
  assert(next.ok);
  const auto nextId = decode(next).value("job").toObject().value("id").toString();
  assert(QFile::remove(source));
  const auto failed = service.execute(nextId);
  assert(!failed.ok);
  entries = decode(service.listJobs()).value("jobs").toArray();
  assert(entries.first().toObject().value("status").toString() == "failed");

  writeInput(source);
  assert(service.execute(nextId).ok);
  entries = decode(service.listJobs()).value("jobs").toArray();
  assert(entries.first().toObject().value("status").toString() == "completed");
  assert(entries.first().toObject().value("attempts").toInt() == 2);

  const auto removed = service.remove(id);
  assert(removed.ok);
  assert(decode(service.listJobs()).value("jobs").toArray().size() == 1);

  // Invalid state is never silently overwritten or replaced by an empty store.
  const auto brokenPath = temp.filePath("corrupt.json");
  {
    QFile broken(brokenPath);
    assert(broken.open(QIODevice::WriteOnly));
    broken.write("not JSON");
  }
  omnidrop::WorkflowJobService broken(brokenPath);
  assert(!broken.listJobs().ok);
  assert(!broken.enqueue(workflow(), {source}).ok);
  QFile check(brokenPath);
  assert(check.open(QIODevice::ReadOnly));
  assert(check.readAll() == "not JSON");
  return 0;
}
