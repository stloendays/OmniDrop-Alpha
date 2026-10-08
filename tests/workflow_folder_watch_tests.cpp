#ifdef NDEBUG
#undef NDEBUG
#endif

#include "app/workflow_folder_watch_service.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>

#include <cassert>

namespace {
void write(const QString& path, const QByteArray& content) {
  QFile file(path);
  assert(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
  assert(file.write(content) == content.size());
  file.close();
}

QJsonObject workflow() {
  return QJsonObject{
      {"schema_version", 1},
      {"name", "Normalize incoming notes"},
      {"nodes", QJsonArray{
          QJsonObject{
              {"id", "clean"},
              {"action_id", "text.normalize"},
              {"sources", QJsonArray{QStringLiteral("$input")}},
          },
      }},
  };
}
}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QTemporaryDir temp;
  assert(temp.isValid());
  const auto folder = temp.path();
  const auto oldFile = folder + "/existing.txt";
  write(oldFile, "existing content");

  omnidrop::WorkflowFolderWatchService watcher;
  QString error;
  assert(!watcher.active());
  assert(!watcher.scan(QDateTime::currentMSecsSinceEpoch()).ok());
  assert(watcher.arm(folder, workflow(), &error));
  assert(error.isEmpty());
  assert(watcher.active());

  omnidrop::WorkflowFolderWatchService second;
  assert(!second.arm(folder, workflow(), &error));
  assert(error.contains("already watched"));

  // Existing files, even after changes, are ignored until explicitly rearmed.
  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  assert(watcher.scan(now).readyPaths.isEmpty());
  write(oldFile, "changed original");
  assert(watcher.scan(now + 2500).readyPaths.isEmpty());

  const auto fresh = folder + "/new.txt";
  write(fresh, "unfinished");
  assert(watcher.scan(now + 2600).readyPaths.isEmpty());

  // A modified file restarts its settle window instead of triggering early.
  write(fresh, "finished input");
  assert(watcher.scan(now + 2800).readyPaths.isEmpty());
  const auto ready = watcher.scan(now + 5200);
  assert(ready.ok());
  assert(ready.readyPaths.size() == 1);
  assert(QFileInfo(ready.readyPaths.first()).fileName() == "new.txt");

  // A given source path is emitted exactly once, even if modified later.
  assert(watcher.scan(now + 8500).readyPaths.isEmpty());
  write(fresh, "rewritten input");
  assert(watcher.scan(now + 12000).readyPaths.isEmpty());

  const auto newOutput = folder + "/new.normalized.txt";
  write(newOutput, "generated");
  watcher.ignoreCreatedOutputs({newOutput});
  assert(watcher.scan(now + 12500).readyPaths.isEmpty());
  assert(watcher.scan(now + 16000).readyPaths.isEmpty());

  // Temporary/incomplete browser downloads are never candidates.
  const auto partial = folder + "/download.crdownload";
  write(partial, "not final");
  assert(watcher.scan(now + 16100).readyPaths.isEmpty());
  assert(watcher.scan(now + 19500).readyPaths.isEmpty());

  // If a pending file vanishes, it is not emitted.
  const auto transient = folder + "/vanished.txt";
  write(transient, "short-lived");
  assert(watcher.scan(now + 20000).readyPaths.isEmpty());
  assert(QFile::remove(transient));
  assert(watcher.scan(now + 23000).readyPaths.isEmpty());

  // Once the watch stops there is no unattended processing or stale state.
  watcher.stop();
  assert(!watcher.active());
  assert(second.arm(folder, workflow(), &error));
  second.stop();
  assert(!watcher.scan(now + 30000).ok());
  assert(watcher.arm(folder, workflow(), &error));  // New baseline.
  assert(watcher.scan(now + 35000).readyPaths.isEmpty());

  const QJsonObject batch{
      {"schema_version", 1},
      {"name", "Batch merge"},
      {"nodes", QJsonArray{QJsonObject{
          {"id", "merge"}, {"action_id", "pdf.merge"},
          {"sources", QJsonArray{QStringLiteral("$input")}},
      }}},
  };
  assert(!watcher.arm(folder, batch, &error));
  assert(!watcher.active());
  assert(error.contains("multi-file"));

  assert(!watcher.arm(folder, QJsonObject{{"schema_version", 123}}, &error));
  assert(!watcher.active());
  assert(!watcher.arm(folder + "/missing", workflow(), &error));
  assert(!watcher.active());

  return 0;
}
