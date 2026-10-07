#include "gui/main_window.hpp"
#include "platform/single_instance_coordinator.hpp"

#include <QApplication>
#include <QIcon>
#include <QMessageBox>

namespace {

QStringList activationPathsFromArguments() {
  QStringList paths;
  const auto arguments = QCoreApplication::arguments();
  for (int index = 1; index < arguments.size(); ++index) {
    const auto& argument = arguments.at(index);
    if (argument.startsWith('-')) continue;
    paths.push_back(argument);
  }
  return paths;
}

}  // namespace

int main(int argc, char* argv[]) {
  QApplication app(argc, argv);
  QApplication::setApplicationName("OmniDrop");
  QApplication::setOrganizationName("OmniDrop");
  QApplication::setApplicationVersion(OMNIDROP_VERSION);
  QApplication::setApplicationDisplayName("OmniDrop");
  QApplication::setWindowIcon(QIcon(":/icons/omnidrop.ico"));

  const auto activationPaths = activationPathsFromArguments();
  omnidrop::SingleInstanceCoordinator singleInstance;

  QString activationError;
  const auto startResult = singleInstance.start(activationPaths, &activationError);
  if (startResult == omnidrop::SingleInstanceCoordinator::StartResult::Forwarded) {
    return 0;
  }
  if (startResult == omnidrop::SingleInstanceCoordinator::StartResult::Error) {
    QMessageBox::critical(
        nullptr,
        "OmniDrop could not start",
        activationError.isEmpty()
            ? "OmniDrop could not create or contact its local activation endpoint."
            : activationError);
    return 2;
  }

  omnidrop::MainWindow window;
  QObject::connect(
      &singleInstance,
      &omnidrop::SingleInstanceCoordinator::activationRequested,
      &window,
      [&window](const QStringList& paths) {
        if (!paths.isEmpty()) window.openPaths(paths);
        window.showNormal();
        window.raise();
        window.activateWindow();
      });

  if (!activationPaths.isEmpty()) {
    window.openPaths(activationPaths);
  }

  window.show();
  return app.exec();
}
