#pragma once

#include "app/rename_preview_service.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace omnidrop {

// A local transactional rename with a durable operation journal. The default
// workflow is still preview-only: prepare() writes a plan, never renames files.
// apply()/undo()/recover() require a separate explicit confirmation argument.
struct RenameTransactionResult {
  bool ok{false};
  QString transactionId;
  QString state;
  QString error;
  QJsonArray rows;
  QJsonObject toJson() const;
};

class RenameTransactionService {
 public:
  explicit RenameTransactionService(QString journalDirectory = {});

  RenameTransactionResult prepare(
      const QStringList& selectedPaths, const RenamePreviewOptions& options) const;
  RenameTransactionResult status(const QString& transactionId) const;
  // List IDs/states without returning private file paths or hashes.
  QJsonObject listTransactions() const;
  RenameTransactionResult apply(const QString& transactionId,
                                bool explicitlyApproved) const;
  RenameTransactionResult undo(const QString& transactionId,
                               bool explicitlyApproved) const;
  // Reconcile interrupted "committing" / "undoing" journals by conservatively
  // restoring verified original paths. Never overwrite another file.
  RenameTransactionResult recover(const QString& transactionId,
                                  bool explicitlyApproved) const;

  QString journalDirectory() const { return journalDirectory_; }

 private:
  QString journalDirectory_;
};

}  // namespace omnidrop
