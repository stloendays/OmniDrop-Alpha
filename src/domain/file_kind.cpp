#include "domain/file_kind.hpp"

#include <QFileInfo>
#include <QSet>

namespace omnidrop {

FileKind detectFileKind(const QString& path) {
  const auto suffix = QFileInfo(path).suffix().toLower();

  static const QSet<QString> images{"png", "jpg", "jpeg", "webp", "bmp", "gif", "tif", "tiff", "heic", "svg"};
  static const QSet<QString> videos{"mp4", "mov", "mkv", "avi", "webm", "m4v"};
  static const QSet<QString> audio{"mp3", "wav", "flac", "aac", "m4a", "ogg", "opus"};
  static const QSet<QString> text{"txt", "md", "markdown", "srt", "vtt", "rtf", "csv", "tsv", "log"};
  static const QSet<QString> archives{"zip", "7z", "rar", "tar", "gz", "bz2", "xz"};
  static const QSet<QString> developer{"json", "yaml", "yml", "xml", "toml", "ini", "cpp", "c", "h", "hpp", "py", "js", "ts", "html", "css", "sql"};

  if (suffix == "pdf") return FileKind::Pdf;
  if (images.contains(suffix)) return FileKind::Image;
  if (videos.contains(suffix)) return FileKind::Video;
  if (audio.contains(suffix)) return FileKind::Audio;
  if (text.contains(suffix)) return FileKind::Text;
  if (archives.contains(suffix)) return FileKind::Archive;
  if (developer.contains(suffix)) return FileKind::Developer;
  return FileKind::Other;
}

QString toString(FileKind kind) {
  switch (kind) {
    case FileKind::Image: return "image";
    case FileKind::Pdf: return "pdf";
    case FileKind::Video: return "video";
    case FileKind::Audio: return "audio";
    case FileKind::Text: return "text";
    case FileKind::Archive: return "archive";
    case FileKind::Developer: return "developer";
    case FileKind::Other: return "other";
  }
  return "other";
}

}  // namespace omnidrop
