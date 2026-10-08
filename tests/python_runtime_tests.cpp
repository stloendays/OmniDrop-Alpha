#include "adapters/python_runtime.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QTextStream>
#include <QtGlobal>

int main(int argc, char* argv[]) {
  QCoreApplication app(argc, argv);
  QTemporaryDir temp;
  if (!temp.isValid()) return 1;

  QProcessEnvironment environment;
  const auto fallback = omnidrop::resolvePythonExecutable(temp.path(), environment);
#ifdef Q_OS_WIN
  if (fallback != QStringLiteral("python")) return 2;
#else
  if (fallback != QStringLiteral("python3")) return 2;
#endif

  environment.insert("OMNIDROP_PYTHON", "external-python-for-test");
  if (omnidrop::resolvePythonExecutable(temp.path(), environment) !=
      QStringLiteral("external-python-for-test")) return 3;
  environment.remove("OMNIDROP_PYTHON");

  const auto runtimeDirectory = QDir(temp.path()).filePath("runtime/python");
  if (!QDir().mkpath(runtimeDirectory)) return 4;
  const QString bundled = QDir(runtimeDirectory).filePath("python.exe");
  QFile executable(bundled);
  if (!executable.open(QIODevice::WriteOnly)) return 5;
  executable.write("test placeholder");
  executable.close();

#ifdef Q_OS_WIN
  if (omnidrop::resolvePythonExecutable(temp.path(), environment) !=
      QFileInfo(bundled).absoluteFilePath()) return 6;
#else
  if (omnidrop::resolvePythonExecutable(temp.path(), environment) !=
      QStringLiteral("python3")) return 6;
#endif
  environment.insert("OMNIDROP_PYTHON", "priority-override");
  if (omnidrop::resolvePythonExecutable(temp.path(), environment) !=
      QStringLiteral("priority-override")) return 7;
  return 0;
}
