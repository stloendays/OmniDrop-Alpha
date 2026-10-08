#include "adapters/python_runtime.hpp"

#include <QDir>
#include <QFileInfo>
#include <QtGlobal>

namespace omnidrop {

QString resolvePythonExecutable(const QString& applicationDirectory,
                                const QProcessEnvironment& environment) {
  const QString configured = environment.value("OMNIDROP_PYTHON").trimmed();
  if (!configured.isEmpty()) return configured;

#ifdef Q_OS_WIN
  // Resolve against the host executable, never the process working directory.
  // The embedded distribution keeps its own isolated python313._pth.
  const QFileInfo bundled(
      QDir(applicationDirectory).filePath("runtime/python/python.exe"));
  if (bundled.isFile()) return bundled.absoluteFilePath();
  // Source-tree builds remain usable with a developer-provided interpreter.
  return QStringLiteral("python");
#else
  Q_UNUSED(applicationDirectory);
  return QStringLiteral("python3");
#endif
}

}  // namespace omnidrop
