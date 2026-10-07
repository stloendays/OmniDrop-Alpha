#pragma once

#include <QString>
#include <QStringList>

#include <atomic>

namespace omnidrop {

struct BatchProgress {
  int total{0};
  int processed{0};
  int succeeded{0};
  int failed{0};
  bool stopped{false};
  QString currentPath;
};

class BatchCancellation {
 public:
  void requestStop() noexcept { stopRequested_.store(true, std::memory_order_relaxed); }
  bool stopRequested() const noexcept { return stopRequested_.load(std::memory_order_relaxed); }

 private:
  std::atomic_bool stopRequested_{false};
};

template <typename Operation, typename ProgressCallback>
BatchProgress runSequentialBatch(const QStringList& paths,
                                 BatchCancellation& cancellation,
                                 Operation&& operation,
                                 ProgressCallback&& onProgress) {
  BatchProgress progress;
  progress.total = paths.size();

  for (const auto& path : paths) {
    if (cancellation.stopRequested()) {
      progress.stopped = true;
      break;
    }

    const bool ok = operation(path);
    ++progress.processed;
    if (ok) {
      ++progress.succeeded;
    } else {
      ++progress.failed;
    }
    progress.currentPath = path;
    onProgress(progress);
  }

  if (cancellation.stopRequested() && progress.processed < progress.total) {
    progress.stopped = true;
  }

  return progress;
}

}  // namespace omnidrop
