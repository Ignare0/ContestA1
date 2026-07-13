#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import subprocess
import tempfile
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--probe", type=Path, required=True)
    args = parser.parse_args()
    probe = args.probe.resolve()

    with tempfile.TemporaryDirectory(prefix="a1-probe-smoke-") as raw:
        work = Path(raw)
        source = work / "tiny.v"
        source.write_text(
            "module tb2;\n"
            "  reg a;\n"
            "  wire y;\n"
            "  assign y = ~a;\n"
            "  initial begin a = 0; #1; $display(\"%b\", y); $finish; end\n"
            "endmodule\n",
            encoding="utf-8",
        )
        filelist = work / "filelist.txt"
        filelist.write_text(str(source.resolve()) + "\n", encoding="utf-8")
        output = work / "report.json"

        subprocess.run(
            [
                str(probe),
                "-f",
                str(filelist),
                "--top",
                "tb2",
                "--probe-output",
                str(output),
            ],
            cwd=work,
            check=True,
        )
        report = json.loads(output.read_text(encoding="utf-8"))
        assert report["top_instances"] == ["tb2"]
        assert report["symbols"]["ContinuousAssign"] == 1
        assert report["statements"]["Timed"] == 1
        assert report["timing_controls"]["Delay"] == 1
        assert report["system_calls"]["$display"] == 1
        assert report["system_calls"]["$finish"] == 1

        bad = subprocess.run(
            [
                str(probe),
                "-f",
                str(filelist),
                "--top",
                "missing_top",
                "--probe-output",
                str(work / "bad.json"),
            ],
            cwd=work,
            check=False,
        )
        assert bad.returncode != 0
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
