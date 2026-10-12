#pragma once

#include "app/update_check_service.hpp"

#include <QString>

namespace omnidrop {

// Opt-in read-only HTTPS fetch of GitHub's official OmniDrop releases.
// It never downloads ZIP files, stages updates, modifies local files or
// launches a process. This is safe to call from a worker thread.
class GitHubReleaseClient {
 public:
  static UpdateCheckResult check(const QString& installedVersion,
                                 const QString& channel = "alpha");
};

}  // namespace omnidrop
