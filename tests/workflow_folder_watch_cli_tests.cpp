#ifdef NDEBUG
#undef NDEBUG
#endif

#include "app/workflow_job_service.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>

#include <cassert>

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  assert(argc == 3);

  QTemporaryDir temp;
  assert(temp.isValid());
  const QString folder = temp.filePath("incoming");
  assert(QDir().mkpath(folder));
  const QString jobStore = temp.filePath("jobs-state/jobs.json");

  QProcess watcher;
  watcher.setProgram(QString::fromLocal8Bit(argv[1]));
  watcher.setArguments({"workflow", "watch", QString::fromLocal8Bit(argv[2]),
                        folder, "--max-files", "1"});
  auto environment = QProcessEnvironment::systemEnvironment();
  environment.insert("OMNIDROP_WORKFLOW_JOBS", jobStore);
  watcher.setProcessEnvironment(environment);
  watcher.start();
  assert(watcher.waitForStarted(10000));

  QByteArray collected;
  const auto until = QDateTime::currentMSecsSinceEpoch() + 15000;
  while (!collected.contains("watch.armed") &&
         QDateTime::currentMSecsSinceEpoch() < until &&
         watcher.state() != QProcess::NotRunning) {
    watcher.waitForReadyRead(250);
    collected.append(watcher.readAllStandardOutput());
  }
  assert(collected.contains("watch.armed"));

  const QString source = folder + "/new-activity.txt";
  {
    QFile file(source);
    assert(file.open(QIODevice::WriteOnly));
    const QByteArray contents("One  \nOne  \nTwo \n");
    assert(file.write(contents) == contents.size());
  }

  const bool finished = watcher.waitForFinished(30000);
  if (!finished) {
    watcher.kill();
    watcher.waitForFinished(5000);
  }
  assert(finished);
  collected.append(watcher.readAllStandardOutput());
  assert(watcher.exitStatus() == QProcess::NormalExit);
  assert(watcher.exitCode() == 0);
  assert(collected.contains("watch.job_queued"));
  assert(collected.contains("watch.job_finished"));
  assert(collected.contains("watch.stopped"));

  QJsonObject completed;
  for (const auto& raw : collected.split('\n')) {
    const auto json = QJsonDocument::fromJson(raw);
    if (!json.isObject()) continue;
    const auto obj = json.object();
    if (obj.value("event").toString() == "watch.job_finished") {
      completed = obj;
    }
  }
  assert(completed.value("ok").toBool(false));
  const auto outputs = completed.value("created_output_paths").toArray();
  assert(outputs.size() == 2);

  QFile original(source);
  assert(original.open(QIODevice::ReadOnly));
  assert(original.readAll() == "One  \nOne  \nTwo \n");
  original.close();

  for (const auto& output : outputs) {
    assert(QFile::exists(output.toString()));
  }
  omnidrop::WorkflowJobService jobs(jobStore);
  const auto response = jobs.listJobs();
  assert(response.ok);
  const auto record = QJsonDocument::fromJson(response.output.toUtf8()).object();
  const auto history = record.value("jobs").toArray();
  assert(history.size() == 1);
  assert(history.first().toObject().value("status").toString() == "completed");

  // The CLI must query the same durable event timeline as the application
  // service; it must not reconstruct history from transient stdout logs.
  const QString jobId = completed.value("job_id").toString();
  assert(!jobId.isEmpty());
  QProcess timeline;
  timeline.setProgram(QString::fromLocal8Bit(argv[1]));
  timeline.setArguments({"workflow", "events", jobId});
  timeline.setProcessEnvironment(environment);
  timeline.start();
  assert(timeline.waitForStarted(5000));
  assert(timeline.waitForFinished(8000));
  assert(timeline.exitCode() == 0);
  const auto payload = QJsonDocument::fromJson(timeline.readAllStandardOutput());
  assert(payload.isObject());
  const auto eventList = payload.object().value("events").toArray();
  assert(!eventList.isEmpty());
  assert(eventList.first().toObject().value("event").toString() == "job.queued");
  assert(eventList.last().toObject().value("event").toString() == "job.completed");
  assert(payload.object().value("job_id").toString() == jobId);
  return 0;
}
