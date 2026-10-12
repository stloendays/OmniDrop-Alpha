#ifdef NDEBUG
#undef NDEBUG
#endif

#include "app/local_update_preflight_service.hpp"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QList>
#include <QTemporaryDir>

#include <cassert>

namespace {

struct Entry {
  QByteArray name;
  QByteArray data;
  bool symlink{false};
  bool encrypted{false};
};

void append16(QByteArray* bytes, quint16 value) {
  bytes->append(char(value & 0xff));
  bytes->append(char((value >> 8) & 0xff));
}
void append32(QByteArray* bytes, quint32 value) {
  append16(bytes, quint16(value & 0xffff));
  append16(bytes, quint16((value >> 16) & 0xffff));
}

quint32 crc32(const QByteArray& data) {
  quint32 result = 0xffffffffU;
  for (const unsigned char byte : data) {
    result ^= byte;
    for (int bit = 0; bit < 8; ++bit) {
      result = (result >> 1) ^
          ((result & 1U) ? 0xedb88320U : 0U);
    }
  }
  return ~result;
}

QByteArray zipBytes(const QList<Entry>& specs) {
  QByteArray output;
  QByteArray central;
  for (const auto& spec : specs) {
    const quint32 localOffset = output.size();
    const quint32 checksum = crc32(spec.data);
    const quint16 flags = spec.encrypted ? 1 : 0;
    append32(&output, 0x04034b50U);
    append16(&output, 20);
    append16(&output, flags);
    append16(&output, 0);  // Stored
    append16(&output, 0);
    append16(&output, 0);
    append32(&output, checksum);
    append32(&output, spec.data.size());
    append32(&output, spec.data.size());
    append16(&output, spec.name.size());
    append16(&output, 0);
    output += spec.name;
    output += spec.data;

    append32(&central, 0x02014b50U);
    append16(&central, spec.symlink ? ((3 << 8) | 20) : 20);
    append16(&central, 20);
    append16(&central, flags);
    append16(&central, 0);
    append16(&central, 0);
    append16(&central, 0);
    append32(&central, checksum);
    append32(&central, spec.data.size());
    append32(&central, spec.data.size());
    append16(&central, spec.name.size());
    append16(&central, 0);
    append16(&central, 0);
    append16(&central, 0);
    append16(&central, 0);
    append32(&central, spec.symlink ? (0120777U << 16) : 0);
    append32(&central, localOffset);
    central += spec.name;
  }
  const quint32 centralOffset = output.size();
  output += central;
  append32(&output, 0x06054b50U);
  append16(&output, 0);
  append16(&output, 0);
  append16(&output, specs.size());
  append16(&output, specs.size());
  append32(&output, central.size());
  append32(&output, centralOffset);
  append16(&output, 0);
  return output;
}

void writeBytes(const QString& path, const QByteArray& bytes) {
  QFile file(path);
  assert(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
  assert(file.write(bytes) == bytes.size());
  file.close();
}

void writePackage(const QString& zipPath,
                  const QString& checksumPath,
                  const QByteArray& zip) {
  writeBytes(zipPath, zip);
  const auto hash = QCryptographicHash::hash(
      zip, QCryptographicHash::Sha256).toHex();
  writeBytes(checksumPath, hash + "  " +
      QFileInfo(zipPath).fileName().toUtf8() + "\n");
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QTemporaryDir temp;
  assert(temp.isValid());
  const QString root = temp.path();
  const QString install = root + "/installed";
  assert(QDir().mkpath(install));
  const QString zipPath = root + "/OmniDrop-0.5.0-windows-dev.zip";
  const QString checksum = zipPath + ".sha256";
  const QList<Entry> safe{
      {"OmniDrop.exe", "application"},
      {"omnidrop-cli.exe", "command-line"},
      {"python/runner.py", "print('hi')"},
      {"assets/icon.png", "image"},
  };
  auto zip = zipBytes(safe);
  writePackage(zipPath, checksum, zip);

  using omnidrop::LocalUpdatePreflightService;
  auto r = LocalUpdatePreflightService::verify(zipPath, checksum, install);
  assert(r.ok && r.targetSafe);
  assert(r.packageVersion == "0.5.0");
  assert(r.entryCount == safe.size());
  assert(r.archiveBytes == zip.size());
  assert(r.archiveSha256.size() == 64);
  assert(r.toJson().value("state").toString() == "passed_local_preflight");
  assert(!r.toJson().value("publisher_authenticated").toBool(true));
  assert(!r.toJson().value("auto_install_supported").toBool(true));

  // Even if the ZIP is edited after the checksum is published, no success.
  QFile archive(zipPath);
  assert(archive.open(QIODevice::ReadWrite));
  assert(archive.seek(40));
  const auto original = archive.read(1);
  assert(original.size() == 1);
  assert(archive.seek(40));
  assert(archive.write(QByteArray(1, char(original[0] ^ 1))) == 1);
  archive.close();
  r = LocalUpdatePreflightService::verify(zipPath, checksum, install);
  assert(!r.ok && r.error.contains("SHA-256"));
  writePackage(zipPath, checksum, zip);

  // Matching the SHA alone cannot make an unsafe ZIP acceptable.
  for (const QByteArray& bad : {
           QByteArray("../escaped.dll"), QByteArray("/absolute.dll"),
           QByteArray("C:/danger.dll"), QByteArray("sub\\evil.dll"),
           QByteArray("a/../evil.dll"), QByteArray("CON.txt"),
           QByteArray("folder/.git/config"),
           QByteArray("double//slash.txt"),
           QByteArray("folder/trailing. /x")
       }) {
    auto entries = safe;
    entries.append(Entry{bad, "malicious"});
    writePackage(zipPath, checksum, zipBytes(entries));
    r = LocalUpdatePreflightService::verify(zipPath, checksum, install);
    assert(!r.ok);
    assert(!r.error.isEmpty());
  }

  auto duplicate = safe;
  duplicate.append(Entry{"OMNIDROP.EXE", "replacement"});
  writePackage(zipPath, checksum, zipBytes(duplicate));
  assert(!LocalUpdatePreflightService::verify(
      zipPath, checksum, install).ok);

  auto link = safe;
  link.append(Entry{"python/link", "elsewhere", true});
  writePackage(zipPath, checksum, zipBytes(link));
  r = LocalUpdatePreflightService::verify(zipPath, checksum, install);
  assert(!r.ok && r.error.contains("symlink"));

  auto encrypted = safe;
  encrypted.append(Entry{"password.bin", "secret", false, true});
  writePackage(zipPath, checksum, zipBytes(encrypted));
  assert(!LocalUpdatePreflightService::verify(
      zipPath, checksum, install).ok);

  auto missingExe = safe;
  missingExe.removeFirst();
  writePackage(zipPath, checksum, zipBytes(missingExe));
  assert(!LocalUpdatePreflightService::verify(
      zipPath, checksum, install).ok);

  auto collision = safe;
  collision.append(Entry{"file", "one"});
  collision.append(Entry{"file/child.txt", "two"});
  writePackage(zipPath, checksum, zipBytes(collision));
  assert(!LocalUpdatePreflightService::verify(
      zipPath, checksum, install).ok);

  // An invalid ZIP footer must be rejected even with a matching checksum.
  writePackage(zipPath, checksum, QByteArray("not a zip file"));
  assert(!LocalUpdatePreflightService::verify(
      zipPath, checksum, install).ok);
  writePackage(zipPath, checksum, zip);

  // The SHA filename must match the ZIP exactly and contain only one record.
  const QString wrongName = root + "/unrelated.sha256";
  writeBytes(wrongName, QFileInfo(zipPath).fileName().toUtf8());
  assert(!LocalUpdatePreflightService::verify(
      zipPath, wrongName, install).ok);
  QFile sha(checksum);
  assert(sha.open(QIODevice::WriteOnly | QIODevice::Append));
  assert(sha.write("garbage") == 7);
  sha.close();
  assert(!LocalUpdatePreflightService::verify(
      zipPath, checksum, install).ok);
  writePackage(zipPath, checksum, zip);

  // .git can be a directory or a worktree pointer file, and either blocks
  // using the directory as an installation replacement target.
  const QString checkout = root + "/checkout";
  const QString nested = checkout + "/bin/release";
  assert(QDir().mkpath(nested));
  assert(QDir().mkpath(checkout + "/.git"));
  r = LocalUpdatePreflightService::verify(zipPath, checksum, nested);
  assert(!r.ok && !r.targetSafe);
  assert(r.error.contains(".git"));

  const QString worktree = root + "/worktree";
  assert(QDir().mkpath(worktree + "/bin"));
  writeBytes(worktree + "/.git", "gitdir: elsewhere");
  r = LocalUpdatePreflightService::verify(zipPath, checksum, worktree + "/bin");
  assert(!r.ok && r.error.contains(".git"));

  // Input paths must be explicit and absolute; no directory is mutated.
  r = LocalUpdatePreflightService::verify("release.zip", checksum, install);
  assert(!r.ok);
  assert(LocalUpdatePreflightService::validateInstallDirectory(
      root + "/missing").contains("does not exist"));
  assert(LocalUpdatePreflightService::verify(zipPath, checksum, install).ok);
  return 0;
}
