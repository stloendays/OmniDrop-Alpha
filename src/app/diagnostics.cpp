#include "app/diagnostics.hpp"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSysInfo>
#include <QtGlobal>

namespace omnidrop {

QJsonObject buildDiagnosticsSnapshot(const WorkerDiagnosticsInput& worker) {
  QJsonObject workerObject{
      {"ok", worker.ok},
  };

  if (!worker.error.isEmpty()) {
    workerObject.insert("error", worker.error);
  }

  if (!worker.output.trimmed().isEmpty()) {
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(worker.output, &parseError);
    if (parseError.error == QJsonParseError::NoError && document.isObject()) {
      workerObject.insert("capabilities", document.object());
    } else {
      workerObject.insert("response_valid_json", false);
      workerObject.insert("response_parse_error", parseError.errorString());
    }
  }

  const QJsonObject appObject{
      {"name", "OmniDrop"},
      {"version", QCoreApplication::applicationVersion().isEmpty()
                      ? QString::fromLatin1(OMNIDROP_VERSION)
                      : QCoreApplication::applicationVersion()},
  };

  const QJsonObject runtimeObject{
      {"qt_version", QString::fromLatin1(qVersion())},
      {"os", QSysInfo::prettyProductName()},
      {"kernel_type", QSysInfo::kernelType()},
      {"kernel_version", QSysInfo::kernelVersion()},
      {"cpu_architecture", QSysInfo::currentCpuArchitecture()},
      {"build_cpu_architecture", QSysInfo::buildCpuArchitecture()},
      {"build_abi", QSysInfo::buildAbi()},
  };

  return QJsonObject{
      {"schema_version", kDiagnosticsSchemaVersion},
      {"app", appObject},
      {"runtime", runtimeObject},
      {"worker", workerObject},
  };
}

QByteArray diagnosticsJson(const WorkerDiagnosticsInput& worker,
                           QJsonDocument::JsonFormat format) {
  return QJsonDocument(buildDiagnosticsSnapshot(worker)).toJson(format);
}

}  // namespace omnidrop
