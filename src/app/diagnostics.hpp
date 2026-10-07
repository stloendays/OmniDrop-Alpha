#pragma once

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

namespace omnidrop {

inline constexpr int kDiagnosticsSchemaVersion = 1;

struct WorkerDiagnosticsInput {
  bool ok{false};
  QByteArray output;
  QString error;
};

QJsonObject buildDiagnosticsSnapshot(const WorkerDiagnosticsInput& worker);
QByteArray diagnosticsJson(const WorkerDiagnosticsInput& worker,
                           QJsonDocument::JsonFormat format = QJsonDocument::Indented);

}  // namespace omnidrop
