#!/usr/bin/env python3
import json
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def main() -> None:
    with tempfile.TemporaryDirectory() as directory:
        temp = Path(directory)
        metadata = temp / "metadata.json"
        coverage = temp / "coverage.json"
        output = temp / "quality.json"
        metadata.write_text('{"version":"0.2.0","providers":[]}\n', encoding="utf-8")
        coverage.write_text(json.dumps({"data": [{"totals": {
            "lines": {"covered": 9, "count": 10, "percent": 90.0},
            "functions": {"covered": 2, "count": 2, "percent": 100.0},
            "regions": {"covered": 11, "count": 12, "percent": 91.67},
            "branches": {"covered": 1, "count": 2, "percent": 50.0},
        }}]}), encoding="utf-8")
        subprocess.run(["python3", "tools/generate_quality_dashboard.py", "--metadata", str(metadata),
                        "--coverage", str(coverage), "--output", str(output), "--commit", "abc1234",
                        "--timestamp", "2026-08-13T00:00:00Z", "--test-executables", "14",
                        "--compiler", "Apple clang", "--status", "passing", "--security", "passing"],
                       cwd=ROOT, check=True)
        document = json.loads(output.read_text(encoding="utf-8"))
        assert document["version"] == "0.2.0"
        assert document["overall"] == "passing"
        assert document["checks"]["coverage"] == "passing"
        assert document["coverage"]["lines"] == {"covered": 9, "count": 10, "percent": 90.0}
        assert document["tests"] == {"executables": 14, "passed": 14, "failed": 0}
    print("test_quality_dashboard: passed")


if __name__ == "__main__":
    main()
