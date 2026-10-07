#include "adapters/diagnostics_service.hpp"

#include "app/diagnostics.hpp"

#include <utility>

namespace omnidrop {

DiagnosticsService::DiagnosticsService() = default;

DiagnosticsService::DiagnosticsService(PythonWorkerClient worker)
    : worker_(std::move(worker)) {}

QJsonObject DiagnosticsService::collect() const {
  const auto result = worker_.capabilities();
  return buildDiagnosticsSnapshot(
      WorkerDiagnosticsInput{
          result.ok,
          result.output.toUtf8(),
          result.error,
      });
}

QByteArray DiagnosticsService::collectJson(QJsonDocument::JsonFormat format) const {
  return QJsonDocument(collect()).toJson(format);
}

}  // namespace omnidrop
