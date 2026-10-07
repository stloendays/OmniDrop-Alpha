#include "app/action_catalog.hpp"
#include "app/batch_job.hpp"
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

  const auto imageActions = catalog.recommendedActions(FileKind::Image);
  bool hasRotateClockwise = false;
  bool hasRotateCounterclockwise = false;
  for (const auto& action : imageActions) {
    hasRotateClockwise |= action.id == "image.rotate_clockwise";
    hasRotateCounterclockwise |= action.id == "image.rotate_counterclockwise";
  }
  assert(hasRotateClockwise);
  assert(hasRotateCounterclockwise);

  const auto pdf = catalog.recommendedActions(FileKind::Pdf);
  bool hasExtract = false;
  for (const auto& action : pdf) hasExtract |= action.id == "pdf.extract_text";
  assert(hasExtract);

  OmniDropService service;
  const QSet<QString> runtimeActions{
      "file.sha256",
      "text.normalize",
      "text.deduplicate",
      "text.format_json",
      "text.format_xml",
      "image.compress",
      "image.convert_webp",
      "image.remove_metadata",
      "pdf.extract_text",
      "pdf.split",
      "pdf.merge",
  };

  const auto twoText = service.inspectMany({"a.txt", "b.md"}, runtimeActions);
  bool batchHasNormalize = false;
  for (const auto& action : twoText.actions) {
    batchHasNormalize |= action.id == "text.normalize" && action.available;
  }
  assert(batchHasNormalize);

  const auto mixed = service.inspectMany({"a.txt", "b.png"}, runtimeActions);
  assert(mixed.actions.size() == 1);
  assert(mixed.actions.first().id == "file.sha256");
  assert(mixed.actions.first().available);

  const auto twoPdf = service.inspectMany({"a.pdf", "b.pdf"}, runtimeActions);
  bool hasMerge = false;
  for (const auto& action : twoPdf.actions) {
    if (action.id == "pdf.merge") {
      hasMerge = action.available && action.scope == ActionScope::Batch;
    }
  }
  assert(hasMerge);

  const auto onePdf = service.inspectMany({"a.pdf"}, runtimeActions);
  for (const auto& action : onePdf.actions) {
    assert(action.id != "pdf.merge");
  }

  {
    BatchCancellation cancellation;
    int progressCallbacks = 0;
    const auto progress = runSequentialBatch(
        {"a.txt", "b.txt", "c.txt"},
        cancellation,
        [](const QString& path) { return path != "b.txt"; },
        [&](const BatchProgress&) { ++progressCallbacks; });

    assert(progress.total == 3);
    assert(progress.processed == 3);
    assert(progress.succeeded == 2);
    assert(progress.failed == 1);
    assert(!progress.stopped);
    assert(progressCallbacks == 3);
  }

  {
    BatchCancellation cancellation;
    const auto progress = runSequentialBatch(
        {"a.txt", "b.txt", "c.txt"},
        cancellation,
        [](const QString&) { return true; },
        [&](const BatchProgress& state) {
          if (state.processed == 1) cancellation.requestStop();
        });

    assert(progress.total == 3);
    assert(progress.processed == 1);
    assert(progress.succeeded == 1);
    assert(progress.failed == 0);
    assert(progress.stopped);
  }

  return 0;
}
