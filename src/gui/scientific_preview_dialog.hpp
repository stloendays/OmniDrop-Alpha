#pragma once

#include <QDialog>
#include <QJsonObject>

// Read-only local preview of an optional scientific PDF parse.
// No network requests, untrusted HTML execution, or re-parsing is performed.
namespace omnidrop {

class ScientificPreviewDialog final : public QDialog {
 public:
  explicit ScientificPreviewDialog(const QJsonObject& result, QWidget* parent = nullptr);
};

}  // namespace omnidrop
