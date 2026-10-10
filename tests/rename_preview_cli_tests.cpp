#ifdef NDEBUG
#undef NDEBUG
#endif

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>

#include <cassert>

namespace {

void writeFile(const QString& path, const QByteArray& content) {
  QFile file(path);
  assert(file.open(QIODevice::WriteOnly));
  assert(file.write(content) == content.size());
}

QJsonObject invoke(const QString& exe, const QStringList& arguments,
                   int expectedExit) {
  QProcess process;
  process.setProgram(exe);
  process.setArguments(arguments);
  process.start();
  assert(process.waitForStarted(5000));
  assert(process.waitForFinished(15000));
  assert(process.exitStatus() == QProcess::NormalExit);
  assert(process.exitCode() == expectedExit);
  const auto output = QJsonDocument::fromJson(process.readAllStandardOutput());
  assert(output.isObject());
  return output.object();
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  assert(argc == 2);
  const QString exe = QString::fromLocal8Bit(argv[1]);

  QTemporaryDir temp;
  assert(temp.isValid());
  const QString a = temp.filePath("notes A.txt");
  const QString b = temp.filePath("notes B.txt");
  writeFile(a, "alpha");
  writeFile(b, "beta");
  const QStringList arguments{
      "rename-preview", "--find", "notes ", "--replace", "archived-", a, b,
  };
  const auto clean = invoke(exe, arguments, 0);
  assert(clean.value("ok").toBool());
  assert(clean.value("preview_only").toBool());
  assert(!clean.value("can_apply").toBool(true));
  assert(clean.value("ready_count").toInt() == 2);
  assert(clean.value("rows").toArray().size() == 2);
  assert(clean.value("rows").toArray().at(0).toObject()
             .value("proposed_name").toString() == "archived-A.txt");
  assert(!QFile::exists(temp.filePath("archived-A.txt")));

  writeFile(temp.filePath("archived-A.txt"), "taken");
  const auto blocked = invoke(exe, arguments, 3);
  assert(blocked.value("ok").toBool());
  assert(blocked.value("conflict_count").toInt() == 1);
  assert(blocked.value("ready_count").toInt() == 1);

  const auto invalid = invoke(exe, {
      "rename-preview", "--regex", "--find", "(", "--replace", "x", a,
  }, 2);
  assert(!invalid.value("ok").toBool());
  assert(invalid.value("error").toString().contains("Invalid regular expression"));

  QFile originalA(a);
  QFile originalB(b);
  assert(originalA.open(QIODevice::ReadOnly));
  assert(originalB.open(QIODevice::ReadOnly));
  assert(originalA.readAll() == "alpha");
  assert(originalB.readAll() == "beta");
  return 0;
}
