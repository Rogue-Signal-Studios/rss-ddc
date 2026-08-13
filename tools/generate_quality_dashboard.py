#!/usr/bin/env python3
"""Generate the data payload consumed by the dependency-free quality dashboard."""

import argparse
import json
from pathlib import Path


def coverage_summary(path: Path) -> dict:
    if not path.is_file():
        return {"status": "not-run"}
    document = json.loads(path.read_text(encoding="utf-8"))
    totals = document["data"][0]["totals"]

    def metric(name: str) -> dict:
        value = totals[name]
        return {"covered": value["covered"], "count": value["count"], "percent": value["percent"]}

    return {"status": "available", "lines": metric("lines"), "functions": metric("functions"),
            "regions": metric("regions"), "branches": metric("branches")}


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--metadata", required=True, type=Path)
    parser.add_argument("--coverage", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--commit", required=True)
    parser.add_argument("--timestamp", required=True)
    parser.add_argument("--test-executables", required=True, type=int)
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--status", choices=("local", "passing"), default="local")
    parser.add_argument("--security", choices=("not-run", "passing"), default="not-run")
    args = parser.parse_args()

    metadata = json.loads(args.metadata.read_text(encoding="utf-8"))
    check_status = "passing" if args.status == "passing" else "not-run"
    document = {
        "schemaVersion": 1,
        "commit": args.commit,
        "timestamp": args.timestamp,
        "version": metadata["version"],
        "overall": args.status,
        "checks": {
            "build": check_status,
            "tests": check_status,
            "consumerContract": check_status,
            "coverage": "passing" if args.status == "passing" and args.coverage.is_file() else "not-run",
            "staticAnalysis": check_status,
            "security": args.security,
        },
        "tests": {"executables": args.test_executables, "passed": args.test_executables if args.status == "passing" else 0,
                  "failed": 0},
        "compiler": args.compiler,
        "coverage": coverage_summary(args.coverage),
        "providers": metadata["providers"],
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(document, indent=2, sort_keys=True) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
