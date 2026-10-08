#pragma once

#include <QString>

namespace omnidrop {

// Public DTO for translation from GUI, CLI, and future automation clients.
// Credentials must remain ephemeral; never log or persist apiKey.
struct TranslationRequest {
  QString path;
  QString sourceLanguage{"en"};
  QString targetLanguage{"zh"};
  QString provider{"mymemory"};
  bool allowRemote{false};
  QString endpoint;
  QString apiKey;
};

}  // namespace omnidrop
