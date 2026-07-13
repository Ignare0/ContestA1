#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import subprocess
import tempfile
from collections import Counter
from pathlib import Path


EXPECTED_CASES = {
    "alu",
    "axis_fifo",
    "basic01",
    "basic02",
    "basic03",
    "basic04",
    "basic05",
    "GEMM",
    "i2c",
    "ip",
    "priority_encoder",
    "sha256",
}
CATEGORIES = (
    "symbols",
    "statements",
    "expressions",
    "timing_controls",
    "data_types",
    "system_calls",
)


def stage_filelist(case_dir: Path, work_dir: Path) -> Path:
    source = case_dir / "filelist.txt"
    output = work_dir / "filelist.txt"
    lines = [f"-I{(case_dir / 'rtl').resolve()}", ""]
    for raw in source.read_text(encoding="utf-8").splitlines():
        stripped = raw.strip()
        if not stripped or stripped.startswith(("#", "//", "+", "-")):
            lines.append(raw)
        elif Path(stripped).is_absolute():
            lines.append(stripped)
        else:
            lines.append(str((source.parent / stripped).resolve()))
    output.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return output


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--probe", type=Path, required=True)
    parser.add_argument("--cases-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--top", default="tb2")
    args = parser.parse_args()

    probe = args.probe.resolve()
    cases_root = args.cases_root.resolve()
    output = args.output.resolve()
    case_dirs = {path.name: path for path in cases_root.iterdir() if path.is_dir()}
    if set(case_dirs) != EXPECTED_CASES:
        missing = sorted(EXPECTED_CASES - set(case_dirs))
        extra = sorted(set(case_dirs) - EXPECTED_CASES)
        raise SystemExit(f"unexpected public cases: missing={missing} extra={extra}")

    reports: dict[str, object] = {}
    aggregate = {category: Counter() for category in CATEGORIES}
    with tempfile.TemporaryDirectory(prefix="a1-public-probe-") as raw_temp:
        temp_root = Path(raw_temp)
        for case_name in sorted(case_dirs):
            work_dir = temp_root / case_name
            work_dir.mkdir(parents=True)
            filelist = stage_filelist(case_dirs[case_name], work_dir)
            report_path = work_dir / "report.json"
            subprocess.run(
                [
                    str(probe),
                    "-f",
                    str(filelist),
                    "--top",
                    args.top,
                    "--timescale",
                    "1ns/1ps",
                    "--probe-output",
                    str(report_path),
                ],
                cwd=work_dir,
                check=True,
            )
            report = json.loads(report_path.read_text(encoding="utf-8"))
            if report["top_instances"] != [args.top]:
                raise SystemExit(
                    f"{case_name}: top instances are {report['top_instances']}, "
                    f"expected [{args.top!r}]"
                )
            reports[case_name] = report
            for category in CATEGORIES:
                aggregate[category].update(report[category])

    if "$fscanf" not in reports["basic01"]["system_calls"]:
        raise SystemExit("basic01 inventory did not contain $fscanf")

    document = {
        "top": args.top,
        "cases": reports,
        "aggregate": {
            category: dict(sorted(counts.items()))
            for category, counts in aggregate.items()
        },
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(
        json.dumps(document, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(f"wrote {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
