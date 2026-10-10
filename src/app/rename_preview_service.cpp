#include "app/rename_preview_service.hpp"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>

namespace omnidrop {
namespace {

constexpr int kMaximumFiles = 128;
constexpr int kMaximumPatternLength = 256;
constexpr int kMaximumNameLength = 255;

QString invalidWindowsFilenameReason(const QString& name) {
  if (name.isEmpty() || name == "." || name == "..") {
    return "The proposed filename is empty or reserved.";
  }
  if (name.size() > kMaximumNameLength) {
    return "The proposed filename exceeds 255 UTF-16 code units.";
  }
  if (name.endsWith(' ') || name.endsWith('.')) {
    return "A filename cannot end with a space or period on Windows.";
  }
  const QString reservedCharacters = QStringLiteral("<>:\"/\\|?*");
  for (QChar ch : name) {
    if (ch.unicode() < 32 || reservedCharacters.contains(ch)) {
      return "The proposed filename contains an invalid or path-separator character.";
    }
  }
  const QString prefix = name.section('.', 0, 0).trimmed().toUpper();
  static const QRegularExpression reserved(
      QStringLiteral("^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])$"));
  if (reserved.match(prefix).hasMatch()) {
    return "The proposed filename uses a reserved Windows device name.";
  }
  return {};
}

void reject(RenamePreviewRow& row, const QString& reason) {
  row.status = "conflict";
  row.reason = reason;
}

}  // namespace

QJsonObject RenamePreviewResult::toJson() const {
  QJsonArray items;
  for (const auto& row : rows) {
    items.append(QJsonObject{
        {"source_path", row.sourcePath},
        {"current_name", row.currentName},
        {"proposed_name", row.proposedName},
        {"target_path", row.targetPath},
        {"status", row.status},
        {"reason", row.reason},
    });
  }
  return QJsonObject{
      {"schema_version", 1},
      {"operation", "rename.preview"},
      {"ok", ok},
      {"preview_only", true},
      {"can_apply", false},
      {"error", error},
      {"input_count", rows.size()},
      {"proposed_change_count", proposedChangeCount},
      {"ready_count", readyCount},
      {"conflict_count", conflictCount},
      {"rows", items},
  };
}

RenamePreviewResult RenamePreviewService::preview(
    const QStringList& selectedPaths, const RenamePreviewOptions& options) const {
  RenamePreviewResult result;
  if (selectedPaths.isEmpty() || selectedPaths.size() > kMaximumFiles) {
    result.error = "Select between 1 and 128 local files.";
    return result;
  }
  if (options.find.isEmpty() || options.find.size() > kMaximumPatternLength) {
    result.error = "Find pattern must contain between 1 and 256 characters.";
    return result;
  }
  if (options.replacement.size() > kMaximumNameLength) {
    result.error = "Replacement must not exceed 255 UTF-16 code units.";
    return result;
  }

  QRegularExpression regular;
  if (options.useRegex) {
    auto flags = QRegularExpression::UseUnicodePropertiesOption;
    if (!options.caseSensitive) flags |= QRegularExpression::CaseInsensitiveOption;
    regular = QRegularExpression(options.find, flags);
    if (!regular.isValid()) {
      result.error = "Invalid regular expression: " + regular.errorString();
      return result;
    }
  }

  QSet<QString> selected;
  QHash<QString, QList<int>> destinations;
  for (const auto& input : selectedPaths) {
    const QFileInfo info(input);
    if (!info.exists() || !info.isFile() || info.isSymLink()) {
      result.error = "Every selected path must be an existing regular non-symlink file.";
      return result;
    }
    const QString canonical = info.canonicalFilePath();
    if (canonical.isEmpty()) {
      result.error = "Could not resolve a selected file path.";
      return result;
    }
    const QString key = canonical.toCaseFolded();
    if (selected.contains(key)) {
      result.error = "The selection refers to the same file more than once.";
      return result;
    }
    selected.insert(key);

    RenamePreviewRow row;
    row.sourcePath = canonical;
    row.currentName = QFileInfo(canonical).fileName();

    // Only the last filename extension is preserved. Hidden dotfiles such as
    // .gitignore are treated as a whole name rather than a blank stem.
    const int dot = row.currentName.lastIndexOf('.');
    const int split = !options.includeExtension && dot > 0 ? dot : row.currentName.size();
    const QString stem = row.currentName.left(split);
    const QString extension = row.currentName.mid(split);
    QString updated = stem;
    if (options.useRegex) {
      updated.replace(regular, options.replacement);
    } else {
      updated.replace(options.find, options.replacement,
                      options.caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive);
    }

    row.proposedName = updated + extension;
    row.status = "unchanged";
    if (row.proposedName != row.currentName) {
      ++result.proposedChangeCount;
      row.status = "ready";
      const QString invalidReason = invalidWindowsFilenameReason(row.proposedName);
      if (!invalidReason.isEmpty()) {
        reject(row, invalidReason);
      } else if (row.proposedName.compare(
                     row.currentName, Qt::CaseInsensitive) == 0) {
        reject(row, "Case-only renames require a separate safe rename workflow.");
      } else {
        row.targetPath = QDir::cleanPath(
            QFileInfo(canonical).absolutePath() + QDir::separator() + row.proposedName);
        const QFileInfo targetInfo(row.targetPath);
        if (targetInfo.exists() || targetInfo.isSymLink()) {
          reject(row, "A file or directory already occupies the proposed target path.");
        } else {
          // Detect collisions independently of filesystem case-sensitivity,
          // since the primary packaged platform is Windows.
          destinations[row.targetPath.toCaseFolded()].append(result.rows.size());
        }
      }
    }
    result.rows.append(row);
  }

  for (auto it = destinations.cbegin(); it != destinations.cend(); ++it) {
    if (it.value().size() < 2) continue;
    for (int index : it.value()) {
      reject(result.rows[index], "Multiple selected files would share this target name.");
    }
  }
  for (const auto& row : result.rows) {
    if (row.status == "ready") ++result.readyCount;
    if (row.status == "conflict") ++result.conflictCount;
  }
  result.ok = true;
  return result;
}

}  // namespace omnidrop
