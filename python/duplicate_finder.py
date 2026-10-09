"""Read-only exact-duplicate report for explicitly selected local files.

Only compares SHA-256 digests after grouping equal file sizes. No deletion,
renaming, deduplication or implicit directory recursion is performed.
"""
from __future__ import annotations

import hashlib
import json
import os
from collections import defaultdict
from pathlib import Path
from typing import Callable

MAX_FILES = 128
MAX_TOTAL_BYTES = 4 * 1024 ** 3


def create_report(
    paths: list[Path], allocate_output: Callable[[Path, str, str], Path]
) -> tuple[Path, dict]:
    if not 2 <= len(paths) <= MAX_FILES:
        raise ValueError("Select 2 to 128 files for duplicate analysis.")

    canonical: list[Path] = []
    by_size: dict[int, list[tuple[Path, int]]] = defaultdict(list)
    seen: set[str] = set()
    total_size = 0

    for candidate in paths:
        if not candidate.is_file():
            raise ValueError("A selected input is not a regular file.")
        path = candidate.resolve()
        key = os.path.normcase(str(path))
        if key in seen:
            raise ValueError("Selection contains the same file more than once.")
        seen.add(key)
        information = path.stat()
        if not path.is_file():
            raise ValueError("Selected input is no longer a regular file.")
        total_size += information.st_size
        if total_size > MAX_TOTAL_BYTES:
            raise ValueError("Selection exceeds the 4 GiB scan limit.")
        canonical.append(path)
        by_size[information.st_size].append((path, information.st_mtime_ns))

    groups: list[dict] = []
    reclaimed = 0
    for size, entries in by_size.items():
        if len(entries) < 2:
            continue
        digests: dict[str, list[str]] = defaultdict(list)
        for path, previous_mtime in entries:
            sha = hashlib.sha256()
            with path.open("rb") as handle:
                for block in iter(lambda: handle.read(1024 * 1024), b""):
                    sha.update(block)
            new_stat = path.stat()
            if new_stat.st_size != size or new_stat.st_mtime_ns != previous_mtime:
                raise ValueError("A selected file changed while scanning; try again after saving.")
            digests[sha.hexdigest()].append(str(path))

        for hexdigest, filenames in digests.items():
            if len(filenames) > 1:
                groups.append({
                    "sha256": hexdigest,
                    "size_bytes": size,
                    "files": filenames,
                })
                reclaimed += size * (len(filenames) - 1)

    payload = {
        "schema_version": 1,
        "method": "size+sha256",
        "input_count": len(canonical),
        "duplicate_group_count": len(groups),
        "duplicate_file_count": sum(len(group["files"]) - 1 for group in groups),
        "potential_reclaimable_bytes": reclaimed,
        "groups": groups,
    }
    output = allocate_output(canonical[0], "duplicates", ".json")
    # The caller receives an advisory report, not a destructive cleaning step.
    with output.open("x", encoding="utf-8") as handle:
        json.dump(payload, handle, ensure_ascii=False, indent=2)
        handle.write("\n")
    return output, payload
