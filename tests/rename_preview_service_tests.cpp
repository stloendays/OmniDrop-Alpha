#ifdef NDEBUG
#undef NDEBUG
#endif

#include "app/rename_preview_service.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>

#include <cassert>

namespace {

void writeFile(const QString& path, const QByteArray& data) {
  QFile file(path);
  assert(file.open(QIODevice::WriteOnly));
  assert(file.write(data) == data.size());
  file.close();
}

QByteArray contents(const QString& path) {
  QFile file(path);
  assert(file.open(QIODevice::ReadOnly));
  return file.readAll();
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QTemporaryDir temp;
  assert(temp.isValid());
  const QString root = temp.path() + "/";
  const QString first = root + "photos-alpha.txt";
  const QString second = root + "photos-beta.txt";
  const QString alpha = root + "alpha.txt";
  const QString beta = root + "beta.txt";
  const QString existing = root + "exists.txt";
  const QString note = root + "note-19.txt";
  writeFile(first, "one");
  writeFile(second, "two");
  writeFile(alpha, "alpha");
  writeFile(beta, "beta");
  writeFile(existing, "existing");
  writeFile(note, "note");

  const omnidrop::RenamePreviewService service;
  omnidrop::RenamePreviewOptions options;
  options.find = "photos-";
  options.replacement = "archive-";
  auto preview = service.preview({first, second}, options);
  assert(preview.ok);
  assert(preview.rows.size() == 2);
  assert(preview.proposedChangeCount == 2);
  assert(preview.readyCount == 2);
  assert(preview.conflictCount == 0);
  assert(preview.rows[0].proposedName == "archive-alpha.txt");
  assert(preview.rows[1].proposedName == "archive-beta.txt");
  assert(preview.rows[0].status == "ready");
  assert(!QFile::exists(root + "archive-alpha.txt"));
  assert(!QFile::exists(root + "archive-beta.txt"));
  assert(contents(first) == "one");
  assert(contents(second) == "two");

  const auto json = preview.toJson();
  assert(json.value("schema_version").toInt() == 1);
  assert(json.value("operation").toString() == "rename.preview");
  assert(json.value("preview_only").toBool());
  assert(!json.value("can_apply").toBool(true));
  assert(json.value("rows").toArray().size() == 2);

  // Case-insensitive matching affects only the filename stem, not suffix.
  options.find = "PHOTOS-";
  options.replacement = "image-";
  options.caseSensitive = false;
  preview = service.preview({first}, options);
  assert(preview.ok);
  assert(preview.rows[0].proposedName == "image-alpha.txt");

  // Regex capture groups use Qt replacement semantics.
  options = {};
  options.find = R"(note-(\d+))";
  options.replacement = QStringLiteral("draft-\\1");
  options.useRegex = true;
  preview = service.preview({note}, options);
  assert(preview.ok);
  assert(preview.rows[0].proposedName == "draft-19.txt");
  assert(preview.rows[0].status == "ready");

  // Preserve extensions by default, but allow an explicit full-name preview.
  options = {};
  options.find = ".txt";
  options.replacement = ".md";
  preview = service.preview({alpha}, options);
  assert(preview.ok);
  assert(preview.rows[0].status == "unchanged");
  options.includeExtension = true;
  preview = service.preview({alpha}, options);
  assert(preview.ok);
  assert(preview.rows[0].proposedName == "alpha.md");

  // Two selections may not propose the same target, even on case-sensitive Linux.
  options = {};
  options.find = "^(alpha|beta)";
  options.replacement = "shared";
  options.useRegex = true;
  preview = service.preview({alpha, beta}, options);
  assert(preview.ok);
  assert(preview.conflictCount == 2);
  assert(preview.readyCount == 0);
  assert(preview.rows[0].status == "conflict");
  assert(preview.rows[1].status == "conflict");
  assert(!QFile::exists(root + "shared.txt"));

  // An existing target cannot be silently replaced.
  options = {};
  options.find = "alpha";
  options.replacement = "exists";
  preview = service.preview({alpha}, options);
  assert(preview.ok);
  assert(preview.rows[0].status == "conflict");
  assert(preview.conflictCount == 1);
  assert(contents(existing) == "existing");

  // Windows rejects traversal, reserved names, trailing periods and
  // case-only renames; consistent preview behavior on Linux and Windows.
  options.replacement = "../hidden";
  preview = service.preview({alpha}, options);
  assert(preview.ok && preview.conflictCount == 1);
  assert(preview.rows[0].targetPath.isEmpty());

  options.replacement = "CON";
  preview = service.preview({alpha}, options);
  assert(preview.ok && preview.conflictCount == 1);

  options.replacement = "ALPHA";
  options.caseSensitive = false;
  preview = service.preview({alpha}, options);
  assert(preview.ok && preview.conflictCount == 1);
  assert(preview.rows[0].reason.contains("Case-only"));

  options = {};
  options.find = "alpha.txt";
  options.replacement = "alpha.";
  options.includeExtension = true;
  preview = service.preview({alpha}, options);
  assert(preview.ok && preview.conflictCount == 1);

  // Malformed patterns/inputs must fail without touching disk.
  options = {};
  options.useRegex = true;
  options.find = "(";
  preview = service.preview({alpha}, options);
  assert(!preview.ok);
  assert(preview.error.contains("Invalid regular expression"));

  options = {};
  options.find = "alpha";
  options.replacement = "beta";
  preview = service.preview({alpha, alpha}, options);
  assert(!preview.ok);
  preview = service.preview({root + "missing.txt"}, options);
  assert(!preview.ok);
  QStringList tooMany;
  for (int i = 0; i < 129; ++i) tooMany.append(alpha);
  preview = service.preview(tooMany, options);
  assert(!preview.ok);

  assert(contents(alpha) == "alpha");
  assert(contents(beta) == "beta");
  assert(contents(existing) == "existing");
  assert(contents(note) == "note");
  return 0;
}
