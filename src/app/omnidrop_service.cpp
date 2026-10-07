#include "app/omnidrop_service.hpp"

#include <QFileInfo>
#include <QSet>

#include <utility>

namespace omnidrop {

FileInspection OmniDropService::inspectStatic(const QString& path) const {
  QFileInfo info(path);
  const auto kind = detectFileKind(path);
  auto actions = catalog_.recommendedActions(kind);

  if (kind == FileKind::Archive && info.suffix().compare("zip", Qt::CaseInsensitive) != 0) {
    for (auto& action : actions) {
      if (action.id.startsWith("archive.")) action.available = false;
    }
  }

  if (kind == FileKind::Image) {
    const auto suffix = info.suffix().toLower();
    const QSet<QString> compressible{"png", "jpg", "jpeg", "webp"};
    const QSet<QString> convertible{"png", "jpg", "jpeg", "bmp", "webp"};
    for (auto& action : actions) {
      if (action.id == "image.compress" || action.id == "image.remove_metadata") {
        action.available = action.available && compressible.contains(suffix);
      } else if (action.id == "image.convert_webp") {
        action.available = action.available && convertible.contains(suffix);
      }
    }
  }

  return {
      info.absoluteFilePath(),
      info.fileName(),
      info.exists() && info.isFile() ? info.size() : 0,
      kind,
      actions,
  };
}

FileInspection OmniDropService::inspect(const QString& path) const {
  return inspectStatic(path);
}

FileInspection OmniDropService::inspect(const QString& path,
                                        const QSet<QString>& availableActionIds) const {
  auto inspection = inspectStatic(path);
  for (auto& action : inspection.actions) {
    if (action.backend.startsWith("python")) {
      action.available = action.available && availableActionIds.contains(action.id);
    }
  }
  return inspection;
}

BatchInspection OmniDropService::inspectMany(
    const QStringList& paths,
    const QSet<QString>& availableActionIds) const {
  BatchInspection batch;
  batch.paths = paths;

  for (const auto& path : paths) {
    auto inspection = inspect(path, availableActionIds);
    batch.totalBytes += inspection.sizeBytes;
    batch.files.push_back(std::move(inspection));
  }

  if (batch.files.isEmpty()) return batch;

  for (const auto& firstAction : batch.files.first().actions) {
    auto common = firstAction;
    bool presentInEveryFile = true;

    for (int fileIndex = 1; fileIndex < batch.files.size(); ++fileIndex) {
      const auto& inspection = batch.files.at(fileIndex);
      const ActionDescriptor* match = nullptr;
      for (const auto& action : inspection.actions) {
        if (action.id == firstAction.id) {
          match = &action;
          break;
        }
      }

      if (match == nullptr) {
        presentInEveryFile = false;
        break;
      }
      common.available = common.available && match->available;
    }

    if (presentInEveryFile) batch.commonActions.push_back(common);
  }

  return batch;
}

}  // namespace omnidrop
