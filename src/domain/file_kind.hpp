#pragma once

#include <QString>

namespace omnidrop {

enum class FileKind {
  Image,
  Pdf,
  Video,
  Audio,
  Text,
  Archive,
  Developer,
  Other
};

FileKind detectFileKind(const QString& path);
QString toString(FileKind kind);

}  // namespace omnidrop
