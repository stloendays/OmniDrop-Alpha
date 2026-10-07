#include "app/action_catalog.hpp"
#include "app/omnidrop_service.hpp"
#include "domain/file_kind.hpp"

#include <QCoreApplication>
#include <QSet>
#include <cassert>

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  using namespace omnidrop;

  assert(detectFileKind("sample.pdf") == FileKind::Pdf);
  assert(detectFileKind("photo.png") == FileKind::Image);
  assert(detectFileKind("movie.mp4") == FileKind::Video);
  assert(detectFileKind("notes.txt") == FileKind::Text);
  assert(detectFileKind("bundle.zip") == FileKind::Archive);

  ActionCatalog catalog;
  const auto text = catalog.recommendedActions(FileKind::Text);
  bool hasHash = false;
  bool hasNormalize = false;
  for (const auto& action : text) {
    hasHash |= action.id == "file.sha256" && action.available;
    hasNormalize |= action.id == "text.normalize" && action.available;
  }
  assert(hasHash);
  assert(hasNormalize);

  const auto pdf = catalog.recommendedActions(FileKind::Pdf);
  bool hasExtract = false;
  for (const auto& action : pdf) hasExtract |= action.id == "pdf.extract_text";
  assert(hasExtract);

  OmniDropService service;
  const QSet<QString> runtimeActions{
      "file.sha256",
      "text.normalize",
      "text.deduplicate",
      "image.compress",
      "image.convert_webp",
      "image.remove_metadata",
  };

  const auto twoText = service.inspectMany({"a.txt", "b.md"}, runtimeActions);
  bool batchHasNormalize = false;
  for (const auto& action : twoText.commonActions) {
    batchHasNormalize |= action.id == "text.normalize" && action.available;
  }
  assert(batchHasNormalize);

  const auto mixed = service.inspectMany({"a.txt", "b.png"}, runtimeActions);
  assert(mixed.commonActions.size() == 1);
  assert(mixed.commonActions.first().id == "file.sha256");
  assert(mixed.commonActions.first().available);

  return 0;
}
