#include "app/diagnostics.hpp"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>

#include <cassert>

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QCoreApplication::setApplicationName("OmniDrop");
  QCoreApplication::setApplicationVersion(OMNIDROP_VERSION);

  using namespace omnidrop;

  const WorkerDiagnosticsInput healthy{
      true,
      R"({"ok":true,"actions":["file.sha256"],"pillow":true,"pypdf":false})",
      {},
  };

  const auto snapshot = buildDiagnosticsSnapshot(healthy);
  assert(snapshot.value("schema_version").toInt() == kDiagnosticsSchemaVersion);

  const auto appObject = snapshot.value("app").toObject();
  assert(appObject.value("name").toString() == "OmniDrop");
  assert(appObject.value("version").toString() == OMNIDROP_VERSION);

  const auto runtime = snapshot.value("runtime").toObject();
  assert(!runtime.value("qt_version").toString().isEmpty());
  assert(!runtime.value("cpu_architecture").toString().isEmpty());

  const auto worker = snapshot.value("worker").toObject();
  assert(worker.value("ok").toBool());
  assert(worker.value("capabilities").toObject().value("pillow").toBool());

  const WorkerDiagnosticsInput unavailable{
      false,
      {},
      "Unable to start Python worker.",
  };
  const auto unavailableWorker =
      buildDiagnosticsSnapshot(unavailable).value("worker").toObject();
  assert(!unavailableWorker.value("ok").toBool());
  assert(unavailableWorker.value("error").toString() ==
         "Unable to start Python worker.");

  const WorkerDiagnosticsInput malformed{
      false,
      "not-json",
      {},
  };
  const auto malformedWorker =
      buildDiagnosticsSnapshot(malformed).value("worker").toObject();
  assert(!malformedWorker.value("response_valid_json").toBool(true));
  assert(!malformedWorker.value("response_parse_error").toString().isEmpty());

  return 0;
}
