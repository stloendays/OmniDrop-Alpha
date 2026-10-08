#include "adapters/diagnostics_service.hpp"
#include "adapters/python_worker_client.hpp"
#include "app/omnidrop_service.hpp"
#include "app/translation_service.hpp"
#include "app/workflow_service.hpp"
#include "app/batch_job.hpp"

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
      << "  omnidrop-cli batch-actions <file> <file> [...]\n"
      << "  omnidrop-cli capabilities\n"
      << "  omnidrop-cli diagnostics\n"
      << "  omnidrop-cli run <action-id> <file>\n"
      << "  omnidrop-cli batch-run <action-id> <file> <file> [...]\n"
      << "  omnidrop-cli translate <file> --from en --to zh --provider argos|mymemory|libretranslate|deepl-free [--allow-upload] [--endpoint https://host/translate]\n"
      << "  omnidrop-cli workflow validate <workflow.json>\n"
      << "  omnidrop-cli workflow plan <workflow.json> <file> [...]\n"
      << "  omnidrop-cli workflow run <workflow.json> <file> [...]\n"
      << "  omnidrop-cli workflow run-stream <workflow.json> <file> [...]\n"
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

bool validateInputFiles(const QStringList& paths, QTextStream& err) {
  for (const auto& path : paths) {
    if (!validateInputFile(path, err)) return false;
  }
  return true;
}

const omnidrop::ActionDescriptor* findAction(const QList<omnidrop::ActionDescriptor>& actions,
                                             const QString& actionId) {
  for (const auto& action : actions) {
    if (action.id == actionId) return &action;
  }
  return nullptr;
}

void printActions(const QList<omnidrop::ActionDescriptor>& actions, QTextStream& out) {
  for (const auto& action : actions) {
    const auto state = action.available ? QStringLiteral("available")
                                        : (action.backend == "planned" ? QStringLiteral("planned")
                                                                        : QStringLiteral("unavailable"));
    out << action.id << '\t' << action.label << '\t' << state << '\t' << action.backend << '\n';
  }
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

  if (command == "diagnostics" && args.size() == 2) {
    omnidrop::DiagnosticsService diagnostics;
    out << QString::fromUtf8(diagnostics.collectJson(QJsonDocument::Indented));
    return 0;
  }

  if (command == "translate" && args.size() >= 9) {
    omnidrop::TranslationRequest request;
    request.path = args.at(2);
    request.sourceLanguage.clear();
    request.targetLanguage.clear();
    request.provider.clear();

    for (int index = 3; index < args.size(); ++index) {
      const auto flag = args.at(index);
      if (flag == "--allow-upload") {
        request.allowRemote = true;
        continue;
      }
      if (flag != "--from" && flag != "--to" &&
          flag != "--provider" && flag != "--endpoint") {
        err << "Unknown translation option: " << flag << '\n';
        return usage();
      }
      if (index + 1 >= args.size()) {
        err << "Missing value for " << flag << '\n';
        return usage();
      }
      const auto value = args.at(++index);
      if (flag == "--from") request.sourceLanguage = value;
      else if (flag == "--to") request.targetLanguage = value;
      else if (flag == "--provider") request.provider = value;
      else if (flag == "--endpoint") request.endpoint = value;
    }

    omnidrop::TranslationService service;
    const auto issue = service.validate(request);
    if (!issue.isEmpty()) {
      err << issue << '\n';
      return 4;
    }

    const auto translated = service.translateFile(request);
    if (!translated.ok) {
      err << translated.error << '\n';
      return 2;
    }
    out << translated.output << '\n';
    return 0;
  }

  if (command == "workflow" && args.size() >= 4) {
    const auto operation = args.at(2);
    if (operation != "validate" && operation != "plan" && operation != "run" &&
        operation != "run-stream") {
      return usage();
    }
    if ((operation == "validate" && args.size() != 4) ||
        (operation != "validate" && args.size() < 5)) {
      return usage();
    }

    omnidrop::WorkflowService workflows;
    const auto loaded = workflows.load(args.at(3));
    if (!loaded.ok) {
      err << loaded.error << '\n';
      return 3;
    }

    const QStringList inputPaths = args.mid(4);
    if (operation == "run-stream") {
      const auto response = workflows.runStreaming(
          loaded.document, inputPaths,
          [&out](const QJsonObject& event) {
            out << QJsonDocument(event).toJson(QJsonDocument::Compact) << '\n';
            out.flush();
          });
      QJsonParseError jsonError;
      const auto parsed = QJsonDocument::fromJson(response.output.toUtf8(), &jsonError);
      const QJsonObject finish{
          {"schema_version", 1},
          {"event", "workflow.finished"},
          {"result", parsed.isObject()
              ? QJsonValue(parsed.object())
              : QJsonValue(QJsonObject{
                  {"ok", false},
                  {"error", QJsonObject{
                      {"code", "transport_error"}, {"message", response.error}}},
                })},
      };
      out << QJsonDocument(finish).toJson(QJsonDocument::Compact) << '\n';
      out.flush();
      return response.ok ? 0 : 2;
    }
    const auto response = operation == "validate"
        ? workflows.validate(loaded.document)
        : (operation == "plan"
            ? workflows.plan(loaded.document, inputPaths)
            : workflows.run(loaded.document, inputPaths));

    if (!response.output.isEmpty()) {
      out << response.output << '\n';
    } else if (!response.error.isEmpty()) {
      err << response.error << '\n';
    }
    return response.ok ? 0 : 2;
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

    const auto inspection = service.inspect(args.at(2), parseCapabilities(capabilityResult));
    printActions(inspection.actions, out);
    return 0;
  }

  if (command == "batch-actions" && args.size() >= 4) {
    const QStringList paths = args.mid(2);
    if (!validateInputFiles(paths, err)) return 3;

    omnidrop::PythonWorkerClient worker;
    const auto capabilityResult = worker.capabilities();
    if (!capabilityResult.ok) {
      err << capabilityResult.error << '\n';
      return 2;
    }

    omnidrop::OmniDropService service;
    const auto batch = service.inspectMany(paths, parseCapabilities(capabilityResult));
    printActions(batch.actions, out);
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
    const auto* action = findAction(inspection.actions, actionId);
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

  if (command == "batch-run" && args.size() >= 5) {
    const auto actionId = args.at(2);
    const QStringList paths = args.mid(3);
    if (!validateInputFiles(paths, err)) return 3;

    omnidrop::PythonWorkerClient worker;
    const auto capabilityResult = worker.capabilities();
    if (!capabilityResult.ok) {
      err << capabilityResult.error << '\n';
      return 2;
    }

    omnidrop::OmniDropService service;
    const auto batch = service.inspectMany(paths, parseCapabilities(capabilityResult));
    const auto* action = findAction(batch.actions, actionId);
    if (action == nullptr) {
      err << "Action is not applicable to every selected file: " << actionId << '\n';
      return 4;
    }
    if (!action->available) {
      err << "Action is not currently available for every selected file: " << actionId << '\n';
      return 5;
    }

    QJsonArray results;
    bool allOk = true;

    if (action->scope == omnidrop::ActionScope::Batch) {
      const auto result = worker.runBatchAction(actionId, paths);
      if (result.ok) {
        const auto document = QJsonDocument::fromJson(result.output.toUtf8());
        if (document.isObject()) {
          results.append(document.object());
        } else {
          allOk = false;
          results.append(QJsonObject{
              {"ok", false},
              {"error", "Worker returned invalid JSON."},
          });
        }
      } else {
        allOk = false;
        results.append(QJsonObject{
            {"ok", false},
            {"error", result.error},
        });
      }
    } else {
      omnidrop::BatchCancellation cancellation;
      const auto progress = omnidrop::runSequentialBatch(
          paths,
          cancellation,
          [&](const QString& path) {
            const auto result = worker.runAction(actionId, path);
            if (result.ok) {
              const auto document = QJsonDocument::fromJson(result.output.toUtf8());
              if (document.isObject()) {
                results.append(document.object());
                return true;
              }
              results.append(QJsonObject{
                  {"ok", false},
                  {"path", path},
                  {"error", "Worker returned invalid JSON."},
              });
              return false;
            }

            results.append(QJsonObject{
                {"ok", false},
                {"path", path},
                {"error", result.error},
            });
            return false;
          },
          [](const omnidrop::BatchProgress&) {});

      allOk = progress.failed == 0 && !progress.stopped;
    }

    const QJsonObject payload{
        {"ok", allOk},
        {"action_id", actionId},
        {"count", paths.size()},
        {"results", results},
    };
    out << QJsonDocument(payload).toJson(QJsonDocument::Compact) << '\n';
    return allOk ? 0 : 2;
  }

  return usage();
}
