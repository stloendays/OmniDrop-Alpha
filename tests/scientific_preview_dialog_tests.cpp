#ifdef NDEBUG
#undef NDEBUG
#endif

#include "gui/scientific_preview_dialog.hpp"

#include <QApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QPlainTextEdit>
#include <QTemporaryDir>

#include <cassert>

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  QTemporaryDir directory;
  assert(directory.isValid());

  const QString path = directory.filePath("preview.md");
  QFile preview(path);
  assert(preview.open(QIODevice::WriteOnly));
  preview.write("# Example research paper\nE = mc^2\n\n<script>not executable</script>");
  preview.close();

  const QJsonObject result{
      {"output_path", directory.filePath("paper.md")},
      {"output_dir", directory.path()},
      {"preview_path", path},
      {"preview_images", QJsonArray()},
  };
  omnidrop::ScientificPreviewDialog dialog(result);
  auto* text = dialog.findChild<QPlainTextEdit*>("scientificPreviewText");
  assert(text != nullptr);
  assert(text->isReadOnly());
  assert(text->toPlainText().contains("Example research paper"));
  assert(text->toPlainText().contains("script"));
  assert(dialog.findChild<QWidget*>("scientificFigureGallery") != nullptr);
  return 0;
}
