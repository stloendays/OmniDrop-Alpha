#include "adapters/python_worker_client.hpp"
#include "app/omnidrop_service.hpp"

#include <QCoreApplication>
#include <QFileInfo>
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
      << "  omnidrop-cli --version\n"
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

bool validateInputFile(const QString& path, QTextStream& err) {
  const QFileInfo info(path);
  if (!info.exists() || !info.isFile()) {
    err << "Input path is not a readable file: " << path << '\n';
    return false;
  }
  return true;
}

const omnidrop::ActionDescriptor* findAction(const omnidrop::FileInspection& inspection,
                                             const QString& actionId) {
  for (const auto& action : inspection.actions) {
    if (action.id == actionId) return &action;
  }
  return nullptr;
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

  if ((command == "--version" || command == "version") && args.size() == 2) {
    out << OMNIDROP_VERSION << '\n';
    return 0;
  }

  if (command == "worker-ping" && args.size() == 2) {
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
    if (!validateInputFile(args.at(2), err)) return 3;

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
    const auto capabilityResult = worker.capabilities();
    if (!capabilityResult.ok) {
      err << capabilityResult.error << '\n';
      return 2;
    }

    const auto runtimeCapabilities = parseCapabilities(capabilityResult);
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
    const auto actionId = args.at(2);
    const auto path = args.at(3);
    if (!validateInputFile(path, err)) return 3;

    omnidrop::PythonWorkerClient worker;
    const auto capabilityResult = worker.capabilities();
    if (!capabilityResult.ok) {
      err << capabilityResult.error << '\n';
      return 2;
    }

    omnidrop::OmniDropService service;
    const auto inspection = service.inspect(path, parseCapabilities(capabilityResult));
    const auto* action = findAction(inspection, actionId);
    if (action == nullptr) {
      err << "Action is not applicable to this file type: " << actionId << '\n';
      return 4;
    }
    if (!action->available) {
      err << "Action is not currently available: " << actionId << '\n';
      return 5;
    }

    const auto result = worker.runAction(actionId, path);
    if (!result.ok) {
      err << result.error << '\n';
      return 2;
    }
    out << result.output << '\n';
    return 0;
  }

  return usage();
}
