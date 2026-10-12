#include "app/local_update_preflight_service.hpp"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>

namespace omnidrop {
namespace {

constexpr qint64 kMaxZipBytes = 1073741824LL;      // 1 GiB
constexpr quint64 kMaxExpandedBytes = 4294967296ULL;  // 4 GiB
constexpr int kMaxEntries = 20000;
constexpr quint32 kLocalHeader = 0x04034b50U;
constexpr quint32 kCentralHeader = 0x02014b50U;
constexpr quint32 kEndOfCentral = 0x06054b50U;

quint16 le16(const QByteArray& bytes, int offset) {
  const auto* p = reinterpret_cast<const unsigned char*>(
      bytes.constData() + offset);
  return quint16(p[0]) | (quint16(p[1]) << 8);
}

quint32 le32(const QByteArray& bytes, int offset) {
  return quint32(le16(bytes, offset)) |
         (quint32(le16(bytes, offset + 2)) << 16);
}

bool readExact(QFile& file, qint64 count, QByteArray* bytes) {
  if (count < 0 || count > 262144) return false;
  *bytes = file.read(count);
  return bytes->size() == count;
}

bool safeWindowsComponent(const QString& component) {
  if (component.isEmpty() || component == "." || component == ".." ||
      component.endsWith(' ') || component.endsWith('.') ||
      component.contains(':') || component.contains('\\'))
    return false;
  for (const QChar ch : component) {
    if (ch.unicode() < 32 || ch == '<' || ch == '>' || ch == '"' ||
        ch == '|' || ch == '?' || ch == '*')
      return false;
  }
  const QString base = component.section('.', 0, 0).toUpper();
  if (base == "CON" || base == "PRN" || base == "AUX" ||
      base == "NUL" || base == "CONIN$" || base == "CONOUT$")
    return false;
  if (base.size() == 4 && (base.startsWith("COM") ||
                            base.startsWith("LPT")) &&
      base.at(3) >= '1' && base.at(3) <= '9') return false;
  return true;
}

QString validateZipPath(const QByteArray& raw, quint16 flags, bool* directory) {
  if (raw.isEmpty() || raw.size() > 4096 || raw.contains('\0')) return {};
  const QString text = QString::fromUtf8(raw);
  // Reject undecodable names, ambiguous legacy codepages and Windows aliases.
  if (text.toUtf8() != raw || (! (flags & 0x0800) &&
      raw.contains(char(0x80)))) return {};
  if (text.startsWith('/') || text.startsWith('\\') ||
      text.contains('\\')) return {};
  *directory = text.endsWith('/');
  const QString stripped = *directory ? text.left(text.size() - 1) : text;
  if (stripped.isEmpty()) return {};
  const auto components = stripped.split('/', Qt::KeepEmptyParts);
  for (const auto& component : components) {
    if (!safeWindowsComponent(component) ||
        component.compare(".git", Qt::CaseInsensitive) == 0) return {};
  }
  return stripped;
}

QString inspectZip(const QString& path, int* entries) {
  QFile archive(path);
  if (!archive.open(QIODevice::ReadOnly)) return "Cannot open ZIP for inspection.";
  const qint64 bytes = archive.size();
  if (bytes < 22 || bytes > kMaxZipBytes)
    return "ZIP is empty, truncated or exceeds the 1 GiB limit.";

  const qint64 tailStart = qMax<qint64>(0, bytes - (65535 + 22));
  if (!archive.seek(tailStart)) return "Cannot seek to ZIP directory.";
  const QByteArray tail = archive.read(bytes - tailStart);
  qint64 endOffset = -1;
  int endIndex = -1;
  for (int i = tail.size() - 22; i >= 0; --i) {
    if (le32(tail, i) == kEndOfCentral &&
        i + 22 + le16(tail, i + 20) == tail.size()) {
      endOffset = tailStart + i;
      endIndex = i;
      break;
    }
  }
  if (endIndex < 0) return "ZIP central directory footer not found.";
  const quint16 count = le16(tail, endIndex + 10);
  const quint16 countOnDisk = le16(tail, endIndex + 8);
  const quint32 centralSize = le32(tail, endIndex + 12);
  const quint32 centralOffset = le32(tail, endIndex + 16);
  if (le16(tail, endIndex + 4) != 0 ||
      le16(tail, endIndex + 6) != 0 ||
      count == 0 || count != countOnDisk || count > kMaxEntries ||
      count == 0xffff || centralSize == 0xffffffffU ||
      centralOffset == 0xffffffffU ||
      quint64(centralOffset) + centralSize != quint64(endOffset) ||
      quint64(centralSize) < quint64(count) * 46) {
    return "Unsupported, split, ZIP64 or inconsistent ZIP central directory.";
  }

  QSet<QString> allNames;
  QSet<QString> regularFiles;
  bool hasGui = false;
  bool hasCli = false;
  quint64 totalExpanded = 0;
  if (!archive.seek(centralOffset)) return "Cannot read ZIP central directory.";

  for (int index = 0; index < count; ++index) {
    const qint64 centralPosition = archive.pos();
    QByteArray header;
    if (!readExact(archive, 46, &header) ||
        le32(header, 0) != kCentralHeader)
      return "Truncated or malformed ZIP directory entry.";
    const quint16 madeBy = le16(header, 4);
    const quint16 flags = le16(header, 8);
    const quint16 method = le16(header, 10);
    const quint32 compressed = le32(header, 20);
    const quint32 uncompressed = le32(header, 24);
    const quint16 nameLength = le16(header, 28);
    const quint16 extraLength = le16(header, 30);
    const quint16 commentLength = le16(header, 32);
    const quint16 startDisk = le16(header, 34);
    const quint32 unixAttributes = le32(header, 38) >> 16;
    const quint32 localOffset = le32(header, 42);

    if (flags & (0x0001 | 0x0040 | 0x2000))
      return "Encrypted ZIP entries are not supported.";
    if (method != 0 && method != 8)
      return "ZIP entry uses an unsupported compression method.";
    if (!nameLength || nameLength > 4096 || compressed == 0xffffffffU ||
        uncompressed == 0xffffffffU || localOffset == 0xffffffffU ||
        startDisk != 0 || quint64(uncompressed) > 1073741824ULL)
      return "ZIP entry exceeds supported limits or requires ZIP64.";
    if (quint64(archive.pos()) + nameLength + extraLength + commentLength >
        quint64(centralOffset) + centralSize)
      return "ZIP entry overflows the central directory.";

    QByteArray name, extra;
    if (!readExact(archive, nameLength, &name) ||
        !readExact(archive, extraLength, &extra) ||
        !archive.seek(archive.pos() + commentLength))
      return "ZIP entry metadata is truncated.";
    for (int n = 0; n + 4 <= extra.size();) {
      const quint16 key = le16(extra, n);
      const quint16 size = le16(extra, n + 2);
      if (n + 4 + size > extra.size())
        return "Invalid ZIP extra-field length.";
      if (key == 1) return "ZIP64 extra fields are not supported.";
      n += 4 + size;
    }

    bool directory = false;
    const QString filename = validateZipPath(name, flags, &directory);
    if (filename.isEmpty())
      return "ZIP contains unsafe, ambiguous, or invalid Windows paths.";
    const auto key = filename.toCaseFolded();
    if (allNames.contains(key))
      return "ZIP contains duplicate or case-colliding Windows paths.";
    allNames.insert(key);

    // ZIP's Unix host mode must not introduce links or special files.
    if ((madeBy >> 8) == 3) {
      const quint32 kind = unixAttributes & 0170000U;
      if (kind != 0 && kind != (directory ? 0040000U : 0100000U))
        return "ZIP contains a symlink or special filesystem entry.";
    }
    if (directory && (compressed != 0 || uncompressed != 0))
      return "ZIP directory declares unexpected file content.";
    if (!directory) {
      regularFiles.insert(key);
      if (filename == "OmniDrop.exe") hasGui = true;
      if (filename == "omnidrop-cli.exe") hasCli = true;
    }
    totalExpanded += uncompressed;
    if (totalExpanded > kMaxExpandedBytes)
      return "ZIP declares more than 4 GiB of expanded file data.";
    if (method == 0 && compressed != uncompressed)
      return "Uncompressed ZIP entry size is inconsistent.";

    const qint64 nextCentral = archive.pos();
    if (localOffset >= quint64(centralOffset) ||
        !archive.seek(localOffset))
      return "ZIP local entry offset is invalid.";
    QByteArray local, localName;
    if (!readExact(archive, 30, &local) ||
        le32(local, 0) != kLocalHeader)
      return "ZIP local header is missing or malformed.";
    const quint16 localNameLength = le16(local, 26);
    const quint16 localExtraLength = le16(local, 28);
    if (le16(local, 6) != flags || le16(local, 8) != method ||
        localNameLength != nameLength ||
        !readExact(archive, localNameLength, &localName) ||
        localName != name ||
        quint64(localOffset) + 30 + localNameLength +
            localExtraLength + compressed > quint64(centralOffset))
      return "ZIP local header disagrees with the directory.";
    if (!archive.seek(nextCentral))
      return "Cannot resume ZIP central directory.";
    Q_UNUSED(centralPosition);
  }

  if (quint64(archive.pos()) !=
      quint64(centralOffset) + centralSize)
    return "ZIP directory contains unexpected trailing records.";
  for (const auto& name : allNames) {
    const auto components = name.split('/');
    QString parent;
    for (int i = 0; i < components.size() - 1; ++i) {
      if (!parent.isEmpty()) parent += '/';
      parent += components.at(i);
      if (regularFiles.contains(parent))
        return "ZIP contains a file/directory prefix collision.";
    }
  }
  if (!hasGui || !hasCli)
    return "ZIP lacks the expected root OmniDrop.exe and omnidrop-cli.exe.";
  *entries = count;
  return {};
}

}  // namespace

QJsonObject LocalUpdatePreflightResult::toJson() const {
  return QJsonObject{
      {"schema_version", 1},
      {"operation", "update.verify-local"},
      {"ok", ok},
      {"state", state},
      {"package_version", packageVersion},
      {"archive_sha256", archiveSha256},
      {"archive_bytes", static_cast<double>(archiveBytes)},
      {"entry_count", entryCount},
      {"target_safe", targetSafe},
      {"error", error},
      {"checksum_matches", ok},
      {"zip_paths_checked", ok},
      {"publisher_authenticated", false},
      {"auto_install_supported", false},
  };
}

QString LocalUpdatePreflightService::validateInstallDirectory(
    const QString& installDirectory) {
  if (!QDir::isAbsolutePath(installDirectory))
    return "Installation directory must be an absolute path.";
  QDir current(QDir::cleanPath(installDirectory));
  if (!current.exists()) return "Installation directory does not exist.";
  while (true) {
    const QString path = current.absolutePath();
    const QFileInfo info(path);
    if (info.isSymLink() || !info.isDir())
      return "Installation path contains a symlink or is not a directory.";
    const QFileInfo git(current.filePath(".git"));
    if (git.exists() || git.isSymLink())
      return "Development checkout detected (.git). Automatic replacement is forbidden.";
    if (!current.cdUp()) break;
  }
  return {};
}

LocalUpdatePreflightResult LocalUpdatePreflightService::verify(
    const QString& zipPath, const QString& checksumPath,
    const QString& installDirectory) {
  LocalUpdatePreflightResult result;
  const QString target = installDirectory.isEmpty()
      ? QCoreApplication::applicationDirPath() : installDirectory;
  const QString targetError = validateInstallDirectory(target);
  if (!targetError.isEmpty()) {
    result.error = targetError;
    return result;
  }
  result.targetSafe = true;

  if (!QDir::isAbsolutePath(zipPath) ||
      !QDir::isAbsolutePath(checksumPath)) {
    result.error = "ZIP and checksum paths must be absolute.";
    return result;
  }

  const QFileInfo zipInfo(zipPath);
  const QFileInfo checksumInfo(checksumPath);
  if (!zipInfo.isFile() || zipInfo.isSymLink() ||
      !checksumInfo.isFile() || checksumInfo.isSymLink()) {
    result.error = "ZIP and checksum must be regular, non-symlink files.";
    return result;
  }
  const QString filename = zipInfo.fileName();
  static const QRegularExpression expected(
      R"(^OmniDrop-(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)-windows-dev\.zip$)");
  const auto match = expected.match(filename);
  if (!match.hasMatch()) {
    result.error = "ZIP filename does not match the canonical OmniDrop release name.";
    return result;
  }
  for (int i = 1; i <= 3; ++i) {
    bool valid = false;
    const int number = match.captured(i).toInt(&valid);
    if (!valid || number > 999999) {
      result.error = "ZIP version is not a supported numeric release.";
      return result;
    }
  }
  result.packageVersion = QString("%1.%2.%3").arg(
      match.captured(1), match.captured(2), match.captured(3));
  if (checksumInfo.fileName() != filename + ".sha256" ||
      zipInfo.size() < 22 || zipInfo.size() > kMaxZipBytes ||
      checksumInfo.size() > 256) {
    result.error = "Incorrect checksum filename or file exceeds permitted size.";
    return result;
  }

  QFile checksums(checksumPath);
  if (!checksums.open(QIODevice::ReadOnly)) {
    result.error = "Cannot read checksum file.";
    return result;
  }
  const QByteArray line = checksums.readAll();
  const QByteArray prefix = line.left(64).toLower();
  static const QRegularExpression hex(R"(^[0-9a-f]{64}$)");
  if (line.size() < 67 || line.size() > 256 ||
      !hex.match(QString::fromLatin1(prefix)).hasMatch() ||
      line.mid(64, 2) != "  " ||
      (line.mid(66) != filename.toUtf8() + "\n" &&
       line.mid(66) != filename.toUtf8() + "\r\n" &&
       line.mid(66) != filename.toUtf8())) {
    result.error = "Checksum must contain one SHA-256 and the exact ZIP filename.";
    return result;
  }

  QFile archive(zipPath);
  if (!archive.open(QIODevice::ReadOnly)) {
    result.error = "Cannot read ZIP file.";
    return result;
  }
  QCryptographicHash digest(QCryptographicHash::Sha256);
  QByteArray buffer(1024 * 1024, '\0');
  qint64 consumed = 0;
  while (!archive.atEnd()) {
    const qint64 length = archive.read(buffer.data(), buffer.size());
    if (length < 0) {
      result.error = "Failed while hashing ZIP.";
      return result;
    }
    if (length == 0) break;
    digest.addData(buffer.constData(), length);
    consumed += length;
  }
  if (consumed != zipInfo.size()) {
    result.error = "ZIP size changed during verification.";
    return result;
  }
  result.archiveBytes = consumed;
  result.archiveSha256 = QString::fromLatin1(digest.result().toHex());
  if (result.archiveSha256 != QString::fromLatin1(prefix)) {
    result.error = "SHA-256 does not match the supplied checksum file.";
    return result;
  }
  const QString zipError = inspectZip(zipPath, &result.entryCount);
  if (!zipError.isEmpty()) {
    result.error = zipError;
    return result;
  }
  result.ok = true;
  result.state = "passed_local_preflight";
  return result;
}

}  // namespace omnidrop
