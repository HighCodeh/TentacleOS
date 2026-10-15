#!/usr/bin/env python3
"""Pre-commit formatter for first-party sources, with local import-format evidence."""
import hashlib
import json
from pathlib import Path
import subprocess


def is_first_party(path):
    if not path.startswith(("firmware_p4/components/", "firmware_p4/main/",
                            "firmware_c5/components/", "firmware_c5/main/")):
        return False
    if not path.endswith((".c", ".h")):
        return False
    return not any(part in path for part in (
        "/Applications/doom/", "/Service/highboy_muse/sdk/",
        "/Service/noise_core/", "/Service/minimp3/", "/Service/fatfs/",
    )) and not path.endswith("/fly_highboy_mesh.h")


def main():
    names = subprocess.check_output(["git", "diff", "--cached", "--name-only", "-z",
                                     "--diff-filter=ACM"]).decode().split("\0")
    names = [name for name in names if is_first_party(name)]
    if not names:
        return
    # Do not accidentally stage unrelated unstaged edits during automatic formatting.
    for name in names:
        staged = subprocess.check_output(["git", "show", ":" + name])
        if staged != Path(name).read_bytes():
            raise SystemExit("Stage the complete file before formatting: " + name)
    version = subprocess.check_output(["clang-format", "--version"], text=True).strip()
    before = {name: hashlib.sha256(Path(name).read_bytes()).hexdigest() for name in names}
    for start in range(0, len(names), 100):
        subprocess.run(["clang-format", "-i", *names[start:start + 100]], check=True)
    journal_path = Path(subprocess.check_output(
        ["git", "rev-parse", "--git-path", "highboy-format-journal.json"], text=True).strip())
    journal = json.loads(journal_path.read_text()) if journal_path.exists() else {}
    for name in names:
        after = hashlib.sha256(Path(name).read_bytes()).hexdigest()
        if after != before[name]:
            previous = journal.get(name)
            if previous and previous["after_sha256"] != before[name]:
                raise SystemExit("Source changed after earlier formatting; review: " + name)
            journal[name] = {"before_sha256": previous["before_sha256"] if previous else before[name],
                             "after_sha256": after, "formatter": version}
    journal_path.write_text(json.dumps(journal, indent=2, sort_keys=True) + "\n")
    subprocess.run(["git", "add", "--", *names], check=True)
    print(f"Pre-commit: formatted {len(names)} first-party files; vendor imports unchanged.")


if __name__ == "__main__":
    main()
