#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

class QLocalServer;
class QLocalSocket;

namespace omnidrop {

class SingleInstanceCoordinator final : public QObject {
  Q_OBJECT

 public:
  enum class StartResult {
    Primary,
    Forwarded,
    Error,
  };

  explicit SingleInstanceCoordinator(
      QString serverName = QStringLiteral("com.omnidrop.OmniDrop.activation.v1"),
      QObject* parent = nullptr);
  ~SingleInstanceCoordinator() override;

  StartResult start(const QStringList& activationPaths, QString* error = nullptr);
  bool isPrimary() const noexcept;

 signals:
  void activationRequested(const QStringList& paths);

 private slots:
  void acceptPendingConnections();

 private:
  bool tryForward(const QStringList& activationPaths, int timeoutMs);
  void processSocket(QLocalSocket* socket);

  QString serverName_;
  QLocalServer* server_{nullptr};
  bool primary_{false};
};

}  // namespace omnidrop
