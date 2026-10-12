#ifdef NDEBUG
#undef NDEBUG
#endif

#include "app/settings_service.hpp"
#include "gui/settings_dialog.hpp"

#include <QApplication>
#include <QFile>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTemporaryDir>

#include <cassert>

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  QTemporaryDir temp;
  assert(temp.isValid());
  const QString path = temp.filePath("settings/config.json");
  qputenv("OMNIDROP_SETTINGS_PATH", QFile::encodeName(path));

  omnidrop::SettingsDialog dialog;
  auto* recent = dialog.findChild<QSpinBox*>("settingsRecentLimit");
  auto* activity = dialog.findChild<QSpinBox*>("settingsActivityLimit");
  auto* save = dialog.findChild<QPushButton*>("settingsSave");
  auto* status = dialog.findChild<QLabel*>("settingsStatus");
  auto* recover = dialog.findChild<QPushButton*>("settingsRecover");
  assert(recent && activity && save && status && recover);
  assert(recent->value() == 8 && activity->value() == 20);
  assert(save->isEnabled());

  recent->setValue(15);
  activity->setValue(55);
  save->click();
  const auto written = omnidrop::SettingsService(path).read();
  assert(written.ok);
  assert(written.preferences.recentFilesLimit == 15);
  assert(written.preferences.activityHistoryLimit == 55);
  assert(status->text().contains("saved"));
  assert(recover->isEnabled());

  // A second dialog loads the same persisted preferences as the CLI.
  omnidrop::SettingsDialog afterRestart;
  auto* restoredRecent = afterRestart.findChild<QSpinBox*>("settingsRecentLimit");
  auto* restoredActivity = afterRestart.findChild<QSpinBox*>("settingsActivityLimit");
  assert(restoredRecent && restoredActivity);
  assert(restoredRecent->value() == 15);
  assert(restoredActivity->value() == 55);
  return 0;
}
