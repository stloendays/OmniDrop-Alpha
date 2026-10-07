#include "gui/main_window.hpp"

#include <QApplication>
#include <QIcon>

int main(int argc, char* argv[]) {
  QApplication app(argc, argv);
  QApplication::setApplicationName("OmniDrop");
  QApplication::setOrganizationName("OmniDrop");
  QApplication::setApplicationVersion(OMNIDROP_VERSION);
  QApplication::setApplicationDisplayName("OmniDrop");
  QApplication::setWindowIcon(QIcon(":/icons/omnidrop.ico"));

  omnidrop::MainWindow window;
  window.show();
  return app.exec();
}
