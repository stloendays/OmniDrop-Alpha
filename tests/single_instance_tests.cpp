#include "platform/single_instance_coordinator.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QLocalServer>
#include <QThread>

#include <cassert>

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  using namespace omnidrop;

  const auto serverName =
      QString("com.omnidrop.OmniDrop.test.%1").arg(QCoreApplication::applicationPid());
  QLocalServer::removeServer(serverName);

  SingleInstanceCoordinator primary(serverName);
  QString error;
  assert(primary.start({}, &error) == SingleInstanceCoordinator::StartResult::Primary);
  assert(primary.isPrimary());
  assert(error.isEmpty());

  QStringList received;
  QObject::connect(
      &primary,
      &SingleInstanceCoordinator::activationRequested,
      [&](const QStringList& paths) { received = paths; });

  const QStringList expected{
      "C:/files/a.txt",
      QString::fromUtf8("C:/文件/b.pdf"),
  };

  SingleInstanceCoordinator secondary(serverName);
  assert(secondary.start(expected, &error) ==
         SingleInstanceCoordinator::StartResult::Forwarded);
  assert(!secondary.isPrimary());
  assert(error.isEmpty());

  QElapsedTimer timer;
  timer.start();
  while (received.isEmpty() && timer.elapsed() < 1500) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    QThread::msleep(10);
  }

  assert(received == expected);
  return 0;
}
