#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <optional>

namespace omnidrop {

inline constexpr int kActivationProtocolVersion = 1;

QByteArray encodeActivationRequest(const QStringList& paths);
std::optional<QStringList> decodeActivationRequest(const QByteArray& payload,
                                                   QString* error = nullptr);

}  // namespace omnidrop
