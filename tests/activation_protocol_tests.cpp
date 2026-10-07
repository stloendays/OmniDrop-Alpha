#include "app/activation_protocol.hpp"

#include <QCoreApplication>
#include <cassert>

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  using namespace omnidrop;

  const QStringList expected{
      "C:/data/one file.pdf",
      QString::fromUtf8("C:/数据/二.txt"),
  };

  QString error;
  const auto decoded = decodeActivationRequest(encodeActivationRequest(expected), &error);
  assert(decoded.has_value());
  assert(error.isEmpty());
  assert(*decoded == expected);

  assert(!decodeActivationRequest(
              R"({"schema_version":2,"command":"app.activate","paths":[]})",
              &error)
              .has_value());
  assert(!error.isEmpty());

  assert(!decodeActivationRequest(
              R"({"schema_version":1,"command":"unknown","paths":[]})",
              &error)
              .has_value());

  assert(!decodeActivationRequest(
              R"({"schema_version":1,"command":"app.activate","paths":[1]})",
              &error)
              .has_value());

  return 0;
}
