#include "platform/single_instance_coordinator.hpp"

#include "app/activation_protocol.hpp"

#include <QLocalServer>
#include <QLocalSocket>

#include <utility>

namespace omnidrop {

SingleInstanceCoordinator::SingleInstanceCoordinator(QString serverName, QObject* parent)
    : QObject(parent),
      serverName_(std::move(serverName)),
      server_(new QLocalServer(this)) {
  server_->setSocketOptions(QLocalServer::UserAccessOption);
  connect(server_, &QLocalServer::newConnection,
          this, &SingleInstanceCoordinator::acceptPendingConnections);
}

SingleInstanceCoordinator::~SingleInstanceCoordinator() {
  if (!primary_) return;
  server_->close();
  QLocalServer::removeServer(serverName_);
}

bool SingleInstanceCoordinator::isPrimary() const noexcept {
  return primary_;
}

bool SingleInstanceCoordinator::tryForward(const QStringList& activationPaths, int timeoutMs) {
  QLocalSocket socket;
  socket.connectToServer(serverName_, QIODevice::WriteOnly);
  if (!socket.waitForConnected(timeoutMs)) return false;

  const auto payload = encodeActivationRequest(activationPaths);
  if (socket.write(payload) != payload.size()) return false;
  if (!socket.waitForBytesWritten(timeoutMs)) return false;

  socket.disconnectFromServer();
  return true;
}

SingleInstanceCoordinator::StartResult SingleInstanceCoordinator::start(
    const QStringList& activationPaths,
    QString* error) {
  if (primary_) return StartResult::Primary;

  if (tryForward(activationPaths, 200)) {
    if (error) error->clear();
    return StartResult::Forwarded;
  }

  if (server_->listen(serverName_)) {
    primary_ = true;
    if (error) error->clear();
    return StartResult::Primary;
  }

  // A concurrently starting primary may have won the race between our first
  // connection attempt and listen(). Retry before treating the endpoint as stale.
  if (tryForward(activationPaths, 600)) {
    if (error) error->clear();
    return StartResult::Forwarded;
  }

  // Unix-domain socket files can survive an unclean exit. Only remove the
  // endpoint after both connection attempts failed.
  QLocalServer::removeServer(serverName_);
  if (server_->listen(serverName_)) {
    primary_ = true;
    if (error) error->clear();
    return StartResult::Primary;
  }

  if (error) {
    *error = QString("Unable to create or contact the OmniDrop activation endpoint: %1")
                 .arg(server_->errorString());
  }
  return StartResult::Error;
}

void SingleInstanceCoordinator::acceptPendingConnections() {
  while (server_->hasPendingConnections()) {
    auto* socket = server_->nextPendingConnection();
    if (!socket) continue;

    connect(socket, &QLocalSocket::readyRead, this, [this, socket] {
      processSocket(socket);
    });
    connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);

    processSocket(socket);
  }
}

void SingleInstanceCoordinator::processSocket(QLocalSocket* socket) {
  while (socket->canReadLine()) {
    const auto line = socket->readLine();
    QString error;
    const auto paths = decodeActivationRequest(line, &error);
    if (!paths) continue;
    emit activationRequested(*paths);
  }
}

}  // namespace omnidrop
