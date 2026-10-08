#ifdef NDEBUG
#undef NDEBUG
#endif

#include "app/workflow_service.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <cassert>

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);

  QTemporaryDir directory;
  assert(directory.isValid());

  const auto source = directory.filePath("notes.txt");
  {
    QFile file(source);
    assert(file.open(QIODevice::WriteOnly));
    const QByteArray contents("alpha  \nalpha  \nbeta \n");
    assert(file.write(contents) == contents.size());
  }

  const QJsonObject definition{
      {"schema_version", 1},
      {"name", "Clean text"},
      {"nodes", QJsonArray{
          QJsonObject{
              {"id", "normalize"},
              {"action_id", "text.normalize"},
              {"sources", QJsonArray{QStringLiteral("$input")}},
          },
          QJsonObject{
              {"id", "deduplicate"},
              {"action_id", "text.deduplicate"},
              {"sources", QJsonArray{QStringLiteral("normalize")}},
          },
      }},
  };

  omnidrop::WorkflowService service;
  const auto manifestPath = directory.filePath("clean.omniworkflow.json");
  assert(service.save(manifestPath, definition).isEmpty());
  const auto loaded = service.load(manifestPath);
  assert(loaded.ok);
  assert(loaded.document == definition);

  const auto validated = service.validate(loaded.document);
  assert(validated.ok);
  const auto validatedJson = QJsonDocument::fromJson(validated.output.toUtf8()).object();
  assert(validatedJson.value("node_count").toInt() == 2);

  const auto preview = service.plan(loaded.document, {source});
  assert(preview.ok);
  const auto previewJson = QJsonDocument::fromJson(preview.output.toUtf8()).object();
  assert(previewJson.value("operation_count").toInt() == 2);

  const auto output = service.run(loaded.document, {source});
  assert(output.ok);
  const auto outputJson = QJsonDocument::fromJson(output.output.toUtf8()).object();
  assert(outputJson.value("output_paths").toArray().size() == 1);
  const auto outputPath = outputJson.value("output_paths").toArray().first().toString();

  QFile cleaned(outputPath);
  assert(cleaned.open(QIODevice::ReadOnly));
  assert(cleaned.readAll() == "alpha\nbeta\n");

  QFile original(source);
  assert(original.open(QIODevice::ReadOnly));
  assert(original.readAll() == "alpha  \nalpha  \nbeta \n");

  const QJsonObject invalid{
      {"schema_version", 1},
      {"name", "unsafe"},
      {"nodes", QJsonArray{
          QJsonObject{
              {"id", "exec"},
              {"action_id", "shell.exec"},
              {"sources", QJsonArray{QStringLiteral("$input")}},
          },
      }},
  };
  assert(!service.validate(invalid).ok);

  const auto missing = service.load(directory.filePath("nope.json"));
  assert(!missing.ok);

  return 0;
}
