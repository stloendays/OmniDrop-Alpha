#ifdef NDEBUG
#undef NDEBUG
#endif

#include "gui/update_check_dialog.hpp"

#include <QApplication>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>

#include <cassert>

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  omnidrop::UpdateCheckDialog dialog;
  dialog.show();
  QApplication::processEvents();

  auto* status = dialog.findChild<QLabel*>("updateStatus");
  auto* installed = dialog.findChild<QLabel*>("updateCurrentVersion");
  auto* check = dialog.findChild<QPushButton*>("updateCheckNow");
  auto* browse = dialog.findChild<QPushButton*>("updateOpenRelease");
  auto* verify = dialog.findChild<QPushButton*>("updateVerifyLocalZip");
  auto* channel = dialog.findChild<QComboBox*>("updateChannel");
  assert(status && installed && check && browse && verify && channel);
  assert(installed->text().contains(OMNIDROP_VERSION));
  assert(status->text() == "No network request has been sent.");
  assert(check->isEnabled() && verify->isEnabled() && !browse->isEnabled());
  assert(channel->count() == 2);
  assert(channel->itemData(0).toString() == "alpha");
  assert(channel->itemData(1).toString() == "stable");

  // No network call should occur merely by opening or switching channels.
  channel->setCurrentIndex(1);
  QApplication::processEvents();
  assert(status->text() == "No network request has been sent.");
  assert(!browse->isEnabled());
  assert(verify->toolTip().contains("read-only", Qt::CaseInsensitive) ||
         verify->toolTip().contains("Read-only"));
  return 0;
}
