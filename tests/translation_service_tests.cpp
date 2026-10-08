#ifdef NDEBUG
#undef NDEBUG
#endif

#include "app/translation_service.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <cassert>

int main(int argc, char* argv[]) {
  QCoreApplication app(argc, argv);
  QTemporaryDir temp;
  assert(temp.isValid());

  const auto path = temp.filePath("notes.txt");
  {
    QFile file(path);
    assert(file.open(QIODevice::WriteOnly));
    assert(file.write("Hello.\n") == 7);
  }

  omnidrop::TranslationService service;
  omnidrop::TranslationRequest request;
  request.path = path;
  request.provider = "argos";
  assert(service.validate(request).isEmpty());

  request.provider = "mymemory";
  assert(!service.validate(request).isEmpty());
  request.allowRemote = true;
  assert(service.validate(request).isEmpty());

  request.sourceLanguage = "auto";
  assert(!service.validate(request).isEmpty());
  request.provider = "deepl-free";
  assert(service.validate(request).isEmpty());

  request.sourceLanguage = "zh";
  request.targetLanguage = "zh";
  assert(!service.validate(request).isEmpty());
  request.targetLanguage = "en";

  request.endpoint = "http://localhost:5000/translate";
  assert(!service.validate(request).isEmpty());
  request.provider = "libretranslate";
  assert(service.validate(request).isEmpty());

  request.path = temp.filePath("missing.txt");
  assert(!service.validate(request).isEmpty());

  request.path = temp.filePath("unsupported.pdf");
  {
    QFile file(request.path);
    assert(file.open(QIODevice::WriteOnly));
    assert(file.write("%PDF") == 4);
  }
  assert(!service.validate(request).isEmpty());

  return 0;
}
