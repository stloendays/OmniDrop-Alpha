#ifdef NDEBUG
#undef NDEBUG
#endif

#include "app/update_check_service.hpp"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <cassert>

namespace {

QJsonObject makeRelease(const QString& version,
                        bool prerelease,
                        bool draft = false) {
  const QString tag = "v" + version;
  const QString base =
      "https://github.com/stloendays/OmniDrop-Alpha/releases/download/" + tag + "/";
  const QString zip = "OmniDrop-" + version + "-windows-dev.zip";
  const QJsonArray assets{
      QJsonObject{
          {"name", zip},
          {"size", 43048236},
          {"browser_download_url", base + zip},
      },
      QJsonObject{
          {"name", zip + ".sha256"},
          {"size", 98},
          {"browser_download_url", base + zip + ".sha256"},
      },
  };
  return QJsonObject{
      {"tag_name", tag},
      {"draft", draft},
      {"prerelease", prerelease},
      {"assets", assets},
      {"body", "Untrusted text is never rendered as HTML"},
  };
}

QByteArray encode(const QJsonArray& releases) {
  return QJsonDocument(releases).toJson(QJsonDocument::Compact);
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  using omnidrop::UpdateCheckService;
  assert(UpdateCheckService::validChannel("alpha"));
  assert(UpdateCheckService::validChannel("stable"));
  assert(!UpdateCheckService::validChannel("nightly"));

  QJsonArray feed{
      makeRelease("0.3.0", true),
      makeRelease("0.9.0", true),
      makeRelease("0.10.0", true),
      makeRelease("1.0.0", false),
      makeRelease("9.0.0", false, true),  // draft is never announced
  };
  auto result = UpdateCheckService::evaluate(
      encode(feed), "0.4.0", "alpha");
  assert(result.ok && result.state == "update_available");
  assert(result.availableVersion == "1.0.0");
  assert(!result.prerelease);
  assert(result.releaseUrl ==
      "https://github.com/stloendays/OmniDrop-Alpha/releases/tag/v1.0.0");
  assert(!result.toJson().value("auto_install_supported").toBool(true));

  result = UpdateCheckService::evaluate(encode(feed), "1.0.0", "alpha");
  assert(result.ok && result.state == "up_to_date");
  result = UpdateCheckService::evaluate(encode(feed), "0.4.0", "stable");
  assert(result.ok && result.availableVersion == "1.0.0");
  assert(!result.prerelease);
  result = UpdateCheckService::evaluate(
      encode(QJsonArray{makeRelease("0.10.0", true),
                         makeRelease("0.9.0", true)}),
      "0.4.0", "stable");
  assert(result.ok && result.state == "no_release");

  result = UpdateCheckService::evaluate(
      encode(QJsonArray{makeRelease("0.10.0", true),
                         makeRelease("0.9.0", true)}),
      "0.4.0", "alpha");
  assert(result.ok && result.availableVersion == "0.10.0");
  assert(result.prerelease);

  auto malformed = makeRelease("2.0.0", false);
  malformed.insert("tag_name", "v2.0.0/../../attack");
  feed.append(malformed);
  result = UpdateCheckService::evaluate(
      encode(QJsonArray{malformed}), "0.4.0", "alpha");
  assert(result.ok && result.state == "no_release");

  auto forged = makeRelease("2.0.0", false);
  auto assets = forged.value("assets").toArray();
  auto zip = assets.at(0).toObject();
  zip.insert("browser_download_url", "https://attacker.invalid/tool.exe");
  assets.replace(0, zip);
  forged.insert("assets", assets);
  result = UpdateCheckService::evaluate(
      encode(QJsonArray{forged}), "0.4.0", "alpha");
  assert(result.ok && result.state == "no_release");

  auto missingChecksum = makeRelease("2.0.0", false);
  QJsonArray onlyZip{missingChecksum.value("assets").toArray().at(0)};
  missingChecksum.insert("assets", onlyZip);
  result = UpdateCheckService::evaluate(
      encode(QJsonArray{missingChecksum}), "0.4.0", "alpha");
  assert(result.ok && result.state == "no_release");

  auto duplicate = makeRelease("2.0.0", false);
  auto duplicatedAssets = duplicate.value("assets").toArray();
  duplicatedAssets.append(duplicatedAssets.first());
  duplicate.insert("assets", duplicatedAssets);
  result = UpdateCheckService::evaluate(
      encode(QJsonArray{duplicate}), "0.4.0", "alpha");
  assert(result.ok && result.state == "no_release");

  assert(!UpdateCheckService::evaluate(
      QByteArray("not-json"), "0.4.0", "alpha").ok);
  assert(!UpdateCheckService::evaluate(
      QByteArray("[]"), "0.4.0-rc1", "alpha").ok);
  assert(!UpdateCheckService::evaluate(
      QByteArray("[]"), "0.4.0", "nightly").ok);
  assert(!UpdateCheckService::evaluate(
      QByteArray(1048577, 'a'), "0.4.0", "alpha").ok);
  assert(UpdateCheckService::evaluate(
      QByteArray("[]"), "0.4.0", "alpha").state == "no_release");
  return 0;
}
