#include "adapters/github_release_client.hpp"

#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

namespace omnidrop {

UpdateCheckResult GitHubReleaseClient::check(
    const QString& installedVersion, const QString& channel) {
  // Reject invalid parameters before opening a network connection.
  const auto validation = UpdateCheckService::evaluate(
      QByteArray("[]"), installedVersion, channel);
  if (!validation.ok) return validation;

  static constexpr qsizetype kResponseLimit = 1048576;
  const QUrl endpoint(
      "https://api.github.com/repos/stloendays/OmniDrop-Alpha/releases?per_page=30");
  QNetworkAccessManager manager;
  QNetworkRequest request(endpoint);
  request.setRawHeader("User-Agent", "OmniDrop-readonly-update-check");
  request.setRawHeader("Accept", "application/vnd.github+json");
  request.setAttribute(
      QNetworkRequest::RedirectPolicyAttribute,
      QNetworkRequest::ManualRedirectPolicy);
  request.setTransferTimeout(12000);

  QEventLoop loop;
  QTimer deadline;
  deadline.setSingleShot(true);
  bool timedOut = false;
  bool oversized = false;
  QByteArray bytes;
  auto* reply = manager.get(request);

  QObject::connect(reply, &QIODevice::readyRead, &loop, [&] {
    const qsizetype available = kResponseLimit + 1 - bytes.size();
    if (available <= 0) {
      oversized = true;
      reply->abort();
      return;
    }
    bytes += reply->read(available);
    if (bytes.size() > kResponseLimit) {
      oversized = true;
      reply->abort();
    }
  });
  QObject::connect(&deadline, &QTimer::timeout, &loop, [&] {
    timedOut = true;
    reply->abort();
  });
  QObject::connect(reply, &QNetworkReply::finished,
                   &loop, &QEventLoop::quit);
  deadline.start(15000);
  loop.exec();
  deadline.stop();

  // The final readyRead may be delivered together with finished.
  if (!oversized && reply->bytesAvailable() > 0) {
    const qsizetype available = kResponseLimit + 1 - bytes.size();
    bytes += reply->read(available);
    oversized = bytes.size() > kResponseLimit ||
                reply->bytesAvailable() > 0;
  }
  const auto error = reply->error();
  const int status = reply->attribute(
      QNetworkRequest::HttpStatusCodeAttribute).toInt();
  delete reply;

  UpdateCheckResult result;
  result.installedVersion = installedVersion;
  result.channel = channel;

  if (oversized) {
    result.error = "GitHub release response exceeds the 1 MiB limit.";
  } else if (timedOut) {
    result.error = "GitHub release request timed out (15 seconds).";
  } else if (status != 200) {
    result.error = status > 0
        ? QString("GitHub returned HTTP %1.").arg(status)
        : "Cannot connect to the official GitHub release API.";
  } else if (error != QNetworkReply::NoError) {
    result.error = "GitHub release request failed; check connectivity or TLS.";
  } else {
    return UpdateCheckService::evaluate(bytes, installedVersion, channel);
  }
  return result;
}

}  // namespace omnidrop
