#pragma once

#include <QProcessEnvironment>
#include <QString>

namespace omnidrop {

// Keep runtime selection independent of any UI and reusable by GUI/CLI worker clients.
// An explicit environment override has priority for developer/enterprise integrations.
QString resolvePythonExecutable(const QString& applicationDirectory,
                                const QProcessEnvironment& environment);

}  // namespace omnidrop
