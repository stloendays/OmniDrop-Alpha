#include "app/update_check_service.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QVersionNumber>

#include <cmath>

namespace omnidrop {
namespace {

const QString kReleaseBase =
    QStringLiteral("https://github.com/stloendays/OmniDrop-Alpha/releases/");

bool parseVersion(const QString& text, QVersionNumber* version) {
  static const QRegularExpression pattern(
      QStringLiteral(R"(^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$)"));
  const auto match = pattern.match(text);
  if (!match.hasMatch()) return false;
  int parts[3];
  for (int i = 0; i != 3; ++i) {
    bool converted = false;
    parts[i] = match.captured(i + 1).toInt(&converted);
    if (!converted || parts[i] > 999999) return false;
  }
  *version = QVersionNumber(parts[0], parts[1], parts[2]);
  return true;
}

bool eligibleAsset(const QJsonObject& asset, const QString& tag,
                   const QString& expectedName, bool checksum) {
  const auto name = asset.value("name");
  const auto url = asset.value("browser_download_url");
  const auto size = asset.value("size");
  if (!name.isString() || name.toString() != expectedName ||
      !url.isString() ||
      url.toString() != kReleaseBase + "download/" + tag + "/" + expectedName ||
      !size.isDouble()) return false;
  const double bytes = size.toDouble();
  if (!std::isfinite(bytes) || std::floor(bytes) != bytes) return false;
  return checksum ? bytes >= 64 && bytes <= 4096
                  : bytes >= 1048576 && bytes <= 1073741824;
}

bool completePortableAssets(const QJsonArray& assets,
                            const QString& tag, const QString& version) {
  const QString zip = "OmniDrop-" + version + "-windows-dev.zip";
  const QString sha = zip + ".sha256";
  int zipMatches = 0;
  int shaMatches = 0;
  for (const auto& value : assets) {
    if (!value.isObject()) continue;
    const auto obj = value.toObject();
    if (obj.value("name").toString() == zip &&
        eligibleAsset(obj, tag, zip, false)) ++zipMatches;
    if (obj.value("name").toString() == sha &&
        eligibleAsset(obj, tag, sha, true)) ++shaMatches;
  }
  return zipMatches == 1 && shaMatches == 1;
}

}  // namespace

QJsonObject UpdateCheckResult::toJson() const {
  return QJsonObject{
      {"schema_version", 1},
      {"operation", "update.check"},
      {"ok", ok},
      {"state", state},
      {"channel", channel},
      {"installed_version", installedVersion},
      {"available_version", availableVersion},
      {"release_url", releaseUrl},
      {"prerelease", prerelease},
      {"error", error},
      {"auto_install_supported", false},
  };
}

bool UpdateCheckService::validChannel(const QString& channel) {
  return channel == "alpha" || channel == "stable";
}

UpdateCheckResult UpdateCheckService::evaluate(
    const QByteArray& releasesJson,
    const QString& installedVersion,
    const QString& channel) {
  UpdateCheckResult result;
  result.installedVersion = installedVersion;
  result.channel = channel;

  QVersionNumber local;
  if (!validChannel(channel)) {
    result.error = "Channel must be alpha or stable.";
    return result;
  }
  if (!parseVersion(installedVersion, &local)) {
    result.error = "Installed version is not canonical Major.Minor.Patch.";
    return result;
  }
  if (releasesJson.isEmpty() || releasesJson.size() > 1048576) {
    result.error = "Release metadata is missing or exceeds the size limit.";
    return result;
  }

  QJsonParseError parseError;
  const QJsonDocument doc = QJsonDocument::fromJson(releasesJson, &parseError);
  if (parseError.error != QJsonParseError::NoError || !doc.isArray()) {
    result.error = "GitHub releases response is not a valid JSON array.";
    return result;
  }

  QVersionNumber latest;
  QString chosenTag;
  bool chosenPrerelease = false;
  for (const auto& entry : doc.array()) {
    if (!entry.isObject()) continue;
    const auto obj = entry.toObject();
    if (!obj.value("draft").isBool() ||
        obj.value("draft").toBool() ||
        !obj.value("prerelease").isBool() ||
        !obj.value("tag_name").isString() ||
        !obj.value("assets").isArray()) continue;
    const bool prerelease = obj.value("prerelease").toBool();
    if (channel == "stable" && prerelease) continue;

    const QString tag = obj.value("tag_name").toString();
    if (!tag.startsWith("v")) continue;
    QVersionNumber releaseVersion;
    const auto versionText = tag.mid(1);
    if (!parseVersion(versionText, &releaseVersion)) continue;
    if (!completePortableAssets(obj.value("assets").toArray(),
                                tag, versionText)) continue;
    if (chosenTag.isEmpty() ||
        QVersionNumber::compare(releaseVersion, latest) > 0) {
      latest = releaseVersion;
      chosenTag = tag;
      chosenPrerelease = prerelease;
    }
  }

  result.ok = true;
  if (chosenTag.isEmpty()) {
    result.state = "no_release";
    return result;
  }
  result.availableVersion = chosenTag.mid(1);
  result.releaseUrl = kReleaseBase + "tag/" + chosenTag;
  result.prerelease = chosenPrerelease;
  result.state = QVersionNumber::compare(latest, local) > 0
      ? "update_available" : "up_to_date";
  return result;
}

}  // namespace omnidrop
