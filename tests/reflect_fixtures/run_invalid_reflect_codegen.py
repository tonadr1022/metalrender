#!/usr/bin/env python3
from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(
        description="Run teng_reflect_codegen.py expecting failure."
    )
    parser.add_argument("--python", required=True, help="Python executable.")
    parser.add_argument("--generator", required=True, help="Path to generator script.")
    parser.add_argument("--out-dir", required=True, help="Output directory for generator.")
    parser.add_argument("--module-name", required=True, help="Module name label.")
    parser.add_argument(
        "--header", required=True, help="Single header to pass to --headers."
    )
    args = parser.parse_args(argv)

    python = Path(args.python)
    generator = Path(args.generator)
    out_dir = Path(args.out_dir)
    header = Path(args.header)

    out_dir.mkdir(parents=True, exist_ok=True)

    result = subprocess.run(
        [
            str(python),
            str(generator),
            "--out-dir",
            str(out_dir),
            "--module-name",
            str(args.module_name),
            "--headers",
            str(header),
        ],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )

    if result.returncode == 0:
        print("expected generator failure, but it succeeded", file=sys.stderr)
        if result.stdout:
            print(result.stdout, file=sys.stderr, end="" if result.stdout.endswith("\n") else "\n")
        return 1

    # Success: failure observed.
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))

