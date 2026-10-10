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

QByteArray run(const QString& program, const QStringList& args,
               const QString& directory, int exit) {
  QProcess process;
  process.setProgram(program);
  process.setArguments(args);
  auto environment = QProcessEnvironment::systemEnvironment();
  environment.insert("OMNIDROP_RENAME_JOURNAL_DIR", directory);
  process.setProcessEnvironment(environment);
  process.start();
  assert(process.waitForStarted(5000));
  assert(process.waitForFinished(25000));
  assert(process.exitStatus() == QProcess::NormalExit);
  assert(process.exitCode() == exit);
  return process.readAllStandardOutput();
}

QJsonObject decode(const QByteArray& bytes) {
  const auto doc = QJsonDocument::fromJson(bytes);
  assert(doc.isObject());
  return doc.object();
}

void write(const QString& path, const QByteArray& data) {
  QFile file(path);
  assert(file.open(QIODevice::WriteOnly));
  assert(file.write(data) == data.size());
  file.close();
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  assert(argc == 2);
  const auto executable = QString::fromLocal8Bit(argv[1]);
  QTemporaryDir temp;
  assert(temp.isValid());

  const QString a = temp.filePath("notes A.txt");
  const QString b = temp.filePath("notes B.txt");
  const QString journalDirectory = temp.filePath("journals");
  write(a, "private-a\n");
  write(b, "private-b\n");

  const QStringList selection = {
      "rename-prepare", "--find", "notes ", "--replace", "archive-", a, b,
  };
  const auto prepared = decode(run(executable, selection, journalDirectory, 0));
  assert(prepared.value("ok").toBool());
  assert(prepared.value("state").toString() == "prepared");
  assert(prepared.value("row_count").toInt() == 2);
  const QString id = prepared.value("transaction_id").toString();
  assert(id.size() == 36);
  assert(QFile::exists(a) && QFile::exists(b));
  assert(!QFile::exists(temp.filePath("archive-A.txt")));

  const auto status = decode(run(
      executable, {"rename-status", id}, journalDirectory, 0));
  assert(status.value("state").toString() == "prepared");

  // Unsafe/implicit execution cannot proceed.
  assert(run(executable, {"rename-apply", id, "--confirm"},
             journalDirectory, 0).contains("\"committed\""));
  assert(QFile::exists(temp.filePath("archive-A.txt")));
  assert(!QFile::exists(a) && !QFile::exists(b));

  const auto reversed = decode(run(
      executable, {"rename-undo", id, "--confirm"}, journalDirectory, 0));
  assert(reversed.value("state").toString() == "undone");
  assert(QFile::exists(a) && QFile::exists(b));
  assert(!QFile::exists(temp.filePath("archive-A.txt")));
  assert(!QFile::exists(temp.filePath("archive-B.txt")));

  QFile original(a);
  assert(original.open(QIODevice::ReadOnly));
  assert(original.readAll() == "private-a\n");
  original.close();

  const auto refused = decode(run(executable,
      {"rename-apply", "00000000-0000-0000-0000-000000000000", "--confirm"},
      journalDirectory, 2));
  assert(!refused.value("ok").toBool());

  return 0;
}
