#include "app/activation_protocol.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace omnidrop {

QByteArray encodeActivationRequest(const QStringList& paths) {
  QJsonArray values;
  for (const auto& path : paths) values.append(path);

  const QJsonObject object{
      {"schema_version", kActivationProtocolVersion},
      {"command", "app.activate"},
      {"paths", values},
  };

  auto payload = QJsonDocument(object).toJson(QJsonDocument::Compact);
  payload.append('\n');
  return payload;
}

std::optional<QStringList> decodeActivationRequest(const QByteArray& payload, QString* error) {
  const auto fail = [&](const QString& message) -> std::optional<QStringList> {
    if (error) *error = message;
    return std::nullopt;
  };

  QJsonParseError parseError;
  const auto document = QJsonDocument::fromJson(payload.trimmed(), &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
    return fail("Activation request is not a JSON object.");
  }

  const auto object = document.object();
  if (object.value("schema_version").toInt(-1) != kActivationProtocolVersion) {
    return fail("Unsupported activation protocol version.");
  }
  if (object.value("command").toString() != "app.activate") {
    return fail("Unsupported activation command.");
  }

  const auto pathsValue = object.value("paths");
  if (!pathsValue.isArray()) {
    return fail("Activation paths must be an array.");
  }

  QStringList paths;
  for (const auto& value : pathsValue.toArray()) {
    if (!value.isString()) {
      return fail("Activation paths must contain only strings.");
    }
    paths.push_back(value.toString());
  }

  if (error) error->clear();
  return paths;
}

}  // namespace omnidrop
