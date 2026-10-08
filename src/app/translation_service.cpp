#include "app/translation_service.hpp"

#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>

namespace omnidrop {

bool TranslationService::isRemoteProvider(const QString& provider) {
  return provider == "mymemory" || provider == "libretranslate" ||
         provider == "deepl-free";
}

QString TranslationService::validate(const TranslationRequest& request) const {
  static const QSet<QString> providers{"mymemory", "libretranslate", "deepl-free", "argos"};
  static const QSet<QString> extensions{"txt", "md", "markdown", "srt", "vtt"};
  static const QRegularExpression langPattern("^[a-z]{2}(-[a-z]{2})?$");

  if (!providers.contains(request.provider)) {
    return "Select MyMemory, LibreTranslate, DeepL API Free, or offline Argos.";
  }
  if (request.sourceLanguage != "auto" &&
      !langPattern.match(request.sourceLanguage).hasMatch()) {
    return "Invalid source language code.";
  }
  if (!langPattern.match(request.targetLanguage).hasMatch() ||
      request.sourceLanguage == request.targetLanguage) {
    return "Choose a different valid target language.";
  }
  if (request.sourceLanguage == "auto" &&
      (request.provider == "mymemory" || request.provider == "argos")) {
    return "Automatic language detection is unavailable with this provider.";
  }
  if (isRemoteProvider(request.provider) && !request.allowRemote) {
    return "Please explicitly approve sending document text to a third-party provider.";
  }

  const QFileInfo info(request.path);
  if (!info.exists() || !info.isFile() || !info.isReadable()) {
    return "Choose a readable input file.";
  }
  if (!extensions.contains(info.suffix().toLower())) {
    return "Translation currently supports TXT, Markdown, SRT, and VTT files.";
  }
  if (info.size() > 100000) {
    return "Files larger than 100 KB are not yet supported for translation.";
  }
  if (request.provider != "libretranslate" && !request.endpoint.isEmpty()) {
    return "A custom endpoint is only supported for LibreTranslate.";
  }
  return {};
}

WorkerResult TranslationService::translateFile(const TranslationRequest& request) const {
  const auto error = validate(request);
  if (!error.isEmpty()) return {false, {}, error};
  return worker_.translateFile(request);
}

}  // namespace omnidrop
