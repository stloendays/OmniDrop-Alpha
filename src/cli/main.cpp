#include "adapters/python_worker_client.hpp"
#include "app/omnidrop_service.hpp"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QTextStream>

namespace {

int usage() {
  QTextStream out(stdout);
  out << "OmniDrop CLI\n\n"
      << "Usage:\n"
      << "  omnidrop-cli inspect <file>\n"
      << "  omnidrop-cli actions <file>\n"
      << "  omnidrop-cli capabilities\n"
      << "  omnidrop-cli run <action-id> <file>\n"
      << "  omnidrop-cli worker-ping\n";
  return 1;
}

QSet<QString> parseCapabilities(const omnidrop::WorkerResult& result) {
  QSet<QString> capabilities;
  if (!result.ok) return capabilities;
  const auto document = QJsonDocument::fromJson(result.output.toUtf8());
  if (!document.isObject()) return capabilities;
  for (const auto& value : document.object().value("actions").toArray()) {
    capabilities.insert(value.toString());
  }
  return capabilities;
}

}  // namespace

int main(int argc, char* argv[]) {
  QCoreApplication app(argc, argv);
  QCoreApplication::setApplicationName("OmniDrop CLI");
  QCoreApplication::setOrganizationName("OmniDrop");
  QCoreApplication::setApplicationVersion(OMNIDROP_VERSION);

  const auto args = app.arguments();
  if (args.size() < 2) return usage();

  QTextStream out(stdout);
  QTextStream err(stderr);
  const QString command = args.at(1);

  if (command == "worker-ping") {
    omnidrop::PythonWorkerClient worker;
    const auto result = worker.ping();
    if (!result.ok) {
      err << result.error << '\n';
      return 2;
    }
    out << result.output << '\n';
    return 0;
  }

  if (command == "capabilities" && args.size() == 2) {
    omnidrop::PythonWorkerClient worker;
    const auto result = worker.capabilities();
    if (!result.ok) {
      err << result.error << '\n';
      return 2;
    }
    out << result.output << '\n';
    return 0;
  }

  if ((command == "inspect" || command == "actions") && args.size() == 3) {
    omnidrop::OmniDropService service;
    if (command == "inspect") {
      const auto inspection = service.inspect(args.at(2));
      out << "path=" << inspection.path << '\n'
          << "name=" << inspection.name << '\n'
          << "kind=" << omnidrop::toString(inspection.kind) << '\n'
          << "size_bytes=" << inspection.sizeBytes << '\n';
      return 0;
    }

    omnidrop::PythonWorkerClient worker;
    const auto runtimeCapabilities = parseCapabilities(worker.capabilities());
    const auto inspection = service.inspect(args.at(2), runtimeCapabilities);
    for (const auto& action : inspection.actions) {
      const auto state = action.available ? QStringLiteral("available")
                                          : (action.backend == "planned" ? QStringLiteral("planned")
                                                                          : QStringLiteral("unavailable"));
      out << action.id << '\t' << action.label << '\t' << state << '\t' << action.backend << '\n';
    }
    return 0;
  }

  if (command == "run" && args.size() == 4) {
    omnidrop::PythonWorkerClient worker;
    const auto result = worker.runAction(args.at(2), args.at(3));
    if (!result.ok) {
      err << result.error << '\n';
      return 2;
    }
    out << result.output << '\n';
    return 0;
  }

  return usage();
}
