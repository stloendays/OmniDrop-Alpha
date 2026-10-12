#ifdef NDEBUG
#undef NDEBUG
#endif

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>

#include <cassert>

namespace {

struct CommandResult {
  int exitCode{-1};
  QJsonObject body;
};

CommandResult command(const QString& binary,
                      const QStringList& arguments,
                      const QProcessEnvironment& environment) {
  QProcess process;
  process.setProgram(binary);
  process.setArguments(arguments);
  process.setProcessEnvironment(environment);
  process.start();
  assert(process.waitForStarted(8000));
  assert(process.waitForFinished(12000));
  CommandResult result;
  result.exitCode = process.exitCode();
  const auto bytes = process.readAllStandardOutput();
  const auto doc = QJsonDocument::fromJson(bytes);
  if (doc.isObject()) result.body = doc.object();
  return result;
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  assert(argc >= 2);
  QTemporaryDir temp;
  assert(temp.isValid());

  auto environment = QProcessEnvironment::systemEnvironment();
  const QString config = temp.filePath("config/settings.json");
  const QString backup = temp.filePath("export.json");
  environment.insert("OMNIDROP_SETTINGS_PATH", config);
  const QString binary = QString::fromLocal8Bit(argv[1]);

  auto result = command(binary, {"settings", "get"}, environment);
  assert(result.exitCode == 0);
  assert(result.body.value("ok").toBool());
  assert(result.body.value("settings").toObject()
             .value("recent_files_limit").toInt() == 8);

  result = command(binary,
                   {"settings", "set", "recent_files_limit", "14"},
                   environment);
  assert(result.exitCode == 0);
  assert(result.body.value("settings").toObject()
             .value("recent_files_limit").toInt() == 14);

  result = command(binary, {"settings", "export", backup}, environment);
  assert(result.exitCode == 0);
  assert(QFile::exists(backup));

  result = command(binary,
                   {"settings", "set", "recent_files_limit", "3"},
                   environment);
  assert(result.exitCode == 0);
  result = command(binary, {"settings", "import", backup}, environment);
  assert(result.exitCode != 0);
  result = command(binary,
                   {"settings", "import", backup, "--confirm"}, environment);
  assert(result.exitCode == 0);
  assert(result.body.value("settings").toObject()
             .value("recent_files_limit").toInt() == 14);

  result = command(binary, {"settings", "reset"}, environment);
  assert(result.exitCode != 0);
  result = command(binary, {"settings", "reset", "--confirm"}, environment);
  assert(result.exitCode == 0);
  assert(result.body.value("settings").toObject()
             .value("recent_files_limit").toInt() == 8);

  // A damaged primary cannot be reset silently, but the last valid snapshot
  // can be restored after explicit approval, using the same service as Qt.
  QFile corrupt(config);
  assert(corrupt.open(QIODevice::WriteOnly | QIODevice::Truncate));
  assert(corrupt.write("{damaged") == 8);
  corrupt.close();

  result = command(binary, {"settings", "get"}, environment);
  assert(result.exitCode != 0);
  assert(!result.body.value("ok").toBool());
  result = command(binary, {"settings", "recover"}, environment);
  assert(result.exitCode != 0);
  result = command(binary, {"settings", "recover", "--confirm"}, environment);
  assert(result.exitCode == 0);
  assert(result.body.value("settings").toObject()
             .value("recent_files_limit").toInt() == 14);
  return 0;
}
