#include "app/action_catalog.hpp"

#include <algorithm>
#include <utility>

namespace omnidrop {
namespace {

ActionDescriptor action(QString id, QString label, QString description, int priority,
                        bool available = false, QString backend = "planned",
                        ActionScope scope = ActionScope::PerFile) {
  return {
      std::move(id),
      std::move(label),
      std::move(description),
      priority,
      available,
      std::move(backend),
      scope,
  };
}

QList<ActionDescriptor> commonActions() {
  return {
      action("file.sha256", "Calculate SHA-256", "Create a content fingerprint for this file.", 90, true, "python"),
  };
}

}  // namespace

QList<ActionDescriptor> ActionCatalog::recommendedActions(FileKind kind) const {
  QList<ActionDescriptor> actions;
  switch (kind) {
    case FileKind::Image:
      actions = {
          action("image.compress", "Compress image", "Reduce file size while preserving useful quality.", 10, true, "python-pillow"),
          action("image.convert_webp", "Convert to WebP", "Create a web-friendly WebP copy.", 20, true, "python-pillow"),
          action("image.rotate_clockwise", "Rotate 90° clockwise", "Create a copy rotated 90 degrees clockwise.", 30, true, "python-pillow"),
          action("image.rotate_counterclockwise", "Rotate 90° counterclockwise", "Create a copy rotated 90 degrees counterclockwise.", 31, true, "python-pillow"),
          action("image.remove_metadata", "Remove metadata", "Strip EXIF and other embedded metadata.", 40, true, "python-pillow"),
          action("image.ocr", "Extract text (OCR)", "Recognize text contained in the image.", 50),
      };
      break;
    case FileKind::Pdf:
      actions = {
          action("pdf.compress", "Compress PDF", "Reduce PDF file size.", 10),
          action("pdf.extract_text", "Extract text", "Export readable text from the document.", 20, true, "python-pypdf"),
          action("pdf.split", "Split pages", "Create one PDF file per page in a new folder.", 30, true, "python-pypdf"),
          action("pdf.rotate_clockwise", "Rotate pages 90° clockwise", "Create a copy with every page rotated 90 degrees clockwise.", 35, true, "python-pypdf"),
          action("pdf.rotate_counterclockwise", "Rotate pages 90° counterclockwise", "Create a copy with every page rotated 90 degrees counterclockwise.", 36, true, "python-pypdf"),
          action("pdf.extract_images", "Extract images", "Export embedded images.", 40),
      };
      break;
    case FileKind::Video:
      actions = {
          action("video.compress", "Compress video", "Reduce video size with a modern codec.", 10),
          action("video.extract_audio", "Extract audio", "Save the audio stream separately.", 20),
          action("video.convert", "Convert format", "Transcode to another container or codec.", 30),
      };
      break;
    case FileKind::Audio:
      actions = {
          action("audio.convert", "Convert audio", "Convert to another audio format.", 10),
          action("audio.compress", "Compress audio", "Reduce audio file size.", 20),
      };
      break;
    case FileKind::Text:
    case FileKind::Developer:
      actions = {
          action("text.normalize", "Normalize text", "Normalize line endings and trailing whitespace.", 10, true, "python"),
          action("text.deduplicate", "Remove duplicate lines", "Create a copy with duplicate lines removed.", 20, true, "python"),
          action("text.format_json", "Format JSON", "Validate and pretty-print JSON into a new sibling file.", 30, true, "python"),
          action("text.format_xml", "Format XML", "Parse and pretty-print XML into a new sibling file.", 40, true, "python"),
      };
      break;
    case FileKind::Archive:
      actions = {
          action("archive.inspect", "Inspect archive", "Write an inventory of ZIP contents without extracting.", 10, true, "python"),
          action("archive.extract", "Extract archive", "Safely extract ZIP contents into a new folder.", 20, true, "python"),
      };
      break;
    case FileKind::Other:
      break;
  }

  actions.append(commonActions());
  std::sort(actions.begin(), actions.end(), [](const auto& a, const auto& b) { return a.priority < b.priority; });
  return actions;
}

QList<ActionDescriptor> ActionCatalog::recommendedBatchActions(const QList<FileKind>& kinds) const {
  if (kinds.size() < 2) return {};

  const bool allPdf = std::all_of(kinds.begin(), kinds.end(), [](FileKind kind) {
    return kind == FileKind::Pdf;
  });
  if (!allPdf) return {};

  return {
      action(
          "pdf.merge",
          "Merge PDFs",
          "Combine selected PDFs into one document in the current selection order.",
          5,
          true,
          "python-pypdf",
          ActionScope::Batch),
  };
}

}  // namespace omnidrop
