#pragma once

#include "adapters/python_worker_client.hpp"

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace omnidrop {

class DiagnosticsService {
 public:
  explicit DiagnosticsService(PythonWorkerClient worker = {});

  QJsonObject collect() const;
  QByteArray collectJson(
      QJsonDocument::JsonFormat format = QJsonDocument::Indented) const;

 private:
  PythonWorkerClient worker_;
};

}  // namespace omnidrop
