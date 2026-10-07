#include "adapters/action_history_store.hpp"

#include <QDateTime>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>

namespace omnidrop {
namespace {
constexpr auto kHistoryKey = "history/actions";
}

ActionHistoryStore::ActionHistoryStore(int maxEntries) : maxEntries_(maxEntries) {}

QStringList ActionHistoryStore::loadDisplayEntries() const {
  QSettings settings;
  const auto rows = settings.value(kHistoryKey).toStringList();
  QStringList display;
  for (const auto& row : rows) {
    const auto doc = QJsonDocument::fromJson(row.toUtf8());
    if (!doc.isObject()) continue;
    const auto object = doc.object();
    const auto timestamp = QDateTime::fromString(object.value("timestamp").toString(), Qt::ISODate);
    const auto timeText = timestamp.isValid() ? timestamp.toLocalTime().toString("MM-dd HH:mm") : QStringLiteral("--");
    const auto state = object.value("success").toBool() ? QStringLiteral("Done") : QStringLiteral("Failed");
    const auto action = object.value("action").toString();
    const auto file = QFileInfo(object.value("path").toString()).fileName();
    display.push_back(QString("%1  %2  %3  -  %4").arg(timeText, state, action, file));
  }
  return display;
}

void ActionHistoryStore::record(const QString& actionLabel, const QString& sourcePath, bool success,
                                const QString& detail) const {
  QSettings settings;
  auto rows = settings.value(kHistoryKey).toStringList();
  const QJsonObject object{
      {"timestamp", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
      {"success", success},
      {"action", actionLabel},
      {"path", sourcePath},
      {"detail", detail},
  };
  rows.prepend(QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact)));
  while (rows.size() > maxEntries_) rows.removeLast();
  settings.setValue(kHistoryKey, rows);
}

void ActionHistoryStore::clear() const {
  QSettings settings;
  settings.remove(kHistoryKey);
}

}  // namespace omnidrop
