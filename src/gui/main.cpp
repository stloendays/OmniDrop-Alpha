#include "gui/main_window.hpp"

#include <QApplication>

int main(int argc, char* argv[]) {
  QApplication app(argc, argv);
  QApplication::setApplicationName("OmniDrop");
  QApplication::setOrganizationName("OmniDrop");
  QApplication::setApplicationVersion(OMNIDROP_VERSION);

  omnidrop::MainWindow window;
  window.show();
  return app.exec();
}
