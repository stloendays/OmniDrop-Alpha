#pragma once

#include "adapters/python_worker_client.hpp"
#include "app/translation_request.hpp"

#include <QString>

namespace omnidrop {

class TranslationService {
 public:
  QString validate(const TranslationRequest& request) const;
  WorkerResult translateFile(const TranslationRequest& request) const;
  static bool isRemoteProvider(const QString& provider);

 private:
  PythonWorkerClient worker_;
};

}  // namespace omnidrop
