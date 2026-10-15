#!/usr/bin/env python3
"""Pin reviewed integration deltas after builds/tests, without altering sources."""
import hashlib
import json
from pathlib import Path
import test_preservation

ROOT = Path(__file__).resolve().parents[2]


def main():
    path = ROOT / "integration/import.json"
    data = json.loads(path.read_text())
    pinned = 0
    for item in data["files"]:
        if item["target"] in test_preservation.REVIEWED_MERGES:
            value = hashlib.sha256((ROOT / item["target"]).read_bytes()).hexdigest()
            if item.get("reviewed_target_sha256", value) != value:
                raise SystemExit("Previously reviewed delta changed: " + item["target"])
            item["reviewed_target_sha256"] = value
            pinned += 1
    data["status"] = "reviewed_source_deltas_pinned_build_evidence_in_release_manifest"
    data["reviewed_delta_count"] = pinned
    path.write_text(json.dumps(data, indent=2) + "\n")
    print("Pinned intentional integration deltas:", pinned)


if __name__ == "__main__":
    main()
