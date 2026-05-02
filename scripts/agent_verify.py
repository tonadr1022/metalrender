#!/usr/bin/env python3
"""Agent-oriented verification driver for metalrender."""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parent
DEFAULT_TARGETS = [
    "metalrender",
    "teng-shaderc",
    "teng_core_tests",
    "engine_scene_smoke",
    "teng-scene-tool",
]
SHADER_ROOT = Path("resources/shaders/hlsl")
SHADER_ENTRY_SUFFIXES = (
    ".vert.hlsl",
    ".frag.hlsl",
    ".comp.hlsl",
    ".mesh.hlsl",
    ".task.hlsl",
)
FIRST_PARTY_DIRS = ("apps/", "src/", "tests/")
FORMAT_DIRS = ("apps/", "src/", "tests/", "cmake/")
TIDY_EXTS = (".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".inl")
FORMAT_EXTS = (".cpp", ".h", ".hpp", ".cc", ".cxx", ".inl")


@dataclass(frozen=True)
class GateResult:
    name: str
    command: list[str]
    returncode: int
    output: str


@dataclass
class Summary:
    passed: list[str]
    skipped: list[str]


def rel(path: Path) -> str:
    return str(path.relative_to(REPO_ROOT))


def print_phase(name: str) -> None:
    print(f"\n==> {name}", flush=True)


def command_text(command: Iterable[str]) -> str:
    return " ".join(str(part) for part in command)


def run_checked(name: str, command: list[str], *, quiet: bool = False) -> None:
    print_phase(name)
    print(f"$ {command_text(command)}", flush=True)
    stdout = subprocess.DEVNULL if quiet else None
    subprocess.run(command, cwd=REPO_ROOT, stdout=stdout, check=True)


def run_capture(name: str, command: list[str]) -> GateResult:
    completed = subprocess.run(
        command,
        cwd=REPO_ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    return GateResult(
        name=name,
        command=command,
        returncode=completed.returncode,
        output=completed.stdout,
    )


def git_lines(args: list[str]) -> list[str]:
    completed = subprocess.run(
        ["git", "-C", str(REPO_ROOT), *args],
        text=True,
        stdout=subprocess.PIPE,
        check=True,
    )
    return [line for line in completed.stdout.splitlines() if line]


def changed_paths() -> list[Path]:
    paths = set(git_lines(["diff", "--name-only", "--diff-filter=d"]))
    paths.update(git_lines(["diff", "--cached", "--name-only", "--diff-filter=d"]))
    paths.update(git_lines(["ls-files", "-o", "--exclude-standard"]))
    return [Path(path) for path in sorted(paths)]


def existing_first_party_files(
    paths: Iterable[Path], exts: tuple[str, ...], dirs: tuple[str, ...]
) -> list[Path]:
    result = []
    for path in paths:
        path_text = path.as_posix()
        if not path_text.startswith(dirs):
            continue
        if path.suffix not in exts and not any(path_text.endswith(ext) for ext in exts):
            continue
        absolute = REPO_ROOT / path
        if absolute.is_file():
            result.append(absolute)
    return result


def shader_command(
    bin_dir: Path, paths: list[Path], mode: str
) -> tuple[list[str] | None, str]:
    shaderc = bin_dir / "teng-shaderc"
    if mode == "all":
        return [str(shaderc), "--all"], "shaderc all"

    if not paths:
        if mode == "changed":
            return None, "shaderc skipped: no changed files"
        return [str(shaderc), "--all"], "shaderc all"

    need_all = False
    entries: list[str] = []
    for path in paths:
        path_text = path.as_posix()
        if not path_text.startswith(SHADER_ROOT.as_posix() + "/"):
            continue
        if not (REPO_ROOT / path).is_file():
            need_all = True
            continue
        if path_text.endswith(SHADER_ENTRY_SUFFIXES):
            entries.append(path_text)
        else:
            need_all = True

    if need_all:
        return [str(shaderc), "--all"], "shaderc all"
    if entries:
        return [str(shaderc), *entries], f"shaderc changed ({len(entries)})"
    return None, "shaderc skipped: no changed shaders"


def clang_tidy_command(
    build_dir: Path, paths: list[Path]
) -> tuple[list[str] | None, str]:
    files = existing_first_party_files(paths, TIDY_EXTS, FIRST_PARTY_DIRS)
    if not files:
        return None, "clang-tidy skipped: no changed first-party C/C++ files"

    compile_commands = build_dir / "compile_commands.json"
    if not compile_commands.is_file():
        raise RuntimeError(
            f"missing {rel(compile_commands)}; re-run without --skip-configure"
        )

    run_clang_tidy = os.environ.get("RUN_CLANG_TIDY", "run-clang-tidy-21")
    clang_tidy = os.environ.get("CLANG_TIDY", "clang-tidy-21")
    config_args = []
    if (REPO_ROOT / ".clang-tidy").is_file():
        config_args = ["-config-file", str(REPO_ROOT / ".clang-tidy")]

    if shutil.which(run_clang_tidy):
        return [
            run_clang_tidy,
            "-p",
            str(build_dir),
            *config_args,
            *map(str, files),
        ], f"clang-tidy ({len(files)})"
    if shutil.which(clang_tidy):
        joined = " && ".join(
            command_text(
                [
                    clang_tidy,
                    f"--config-file={REPO_ROOT / '.clang-tidy'}",
                    "-p",
                    str(build_dir),
                    str(file),
                ]
            )
            for file in files
        )
        return ["bash", "-lc", joined], f"clang-tidy ({len(files)})"
    return None, f"clang-tidy skipped: {clang_tidy} not found"


def run_format(paths: list[Path], summary: Summary) -> None:
    clang_format = os.environ.get("CLANG_FORMAT", "clang-format")
    if not shutil.which(clang_format):
        raise RuntimeError(
            f"{clang_format} not found; install clang-format or set CLANG_FORMAT"
        )

    files = existing_first_party_files(paths, FORMAT_EXTS, FORMAT_DIRS)
    if not files:
        summary.skipped.append("clang-format: no changed first-party C/C++ files")
        return

    print_phase("clang-format")
    needs_format: list[Path] = []
    for file in files:
        result = subprocess.run(
            [clang_format, "--dry-run", "--Werror", str(file)],
            cwd=REPO_ROOT,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        if result.returncode != 0:
            needs_format.append(file)

    if needs_format:
        print(f"clang-format fixing {len(needs_format)} file(s)", flush=True)
        subprocess.run(
            [clang_format, "-i", *map(str, needs_format)], cwd=REPO_ROOT, check=True
        )
        subprocess.run(
            [clang_format, "--dry-run", "--Werror", *map(str, needs_format)],
            cwd=REPO_ROOT,
            check=True,
        )
    else:
        print("clang-format: already clean", flush=True)
    summary.passed.append("clang-format")


def app_smoke_commands(bin_dir: Path) -> list[tuple[str, list[str]]]:
    metalrender = bin_dir / "metalrender"
    return [
        ("app smoke: startup scene", [str(metalrender), "--quit-after-frames", "30"]),
        (
            "app smoke: demo_cube",
            [
                str(metalrender),
                "--scene",
                "resources/scenes/demo_cube.tscene.json",
                "--quit-after-frames",
                "30",
            ],
        ),
    ]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Configure, build, and run the repo's agent verification gates.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""\
Modes:
  default  Build, smoke test, changed-file clang-tidy, and shader checks.
  quick    Build, smoke test, and shader checks; skips clang-tidy.
  full     Default gates plus full shader compile and bounded metalrender app smokes.

Environment:
  CMAKE_PRESET      Same as --preset.
  CLANG_FORMAT      clang-format binary (default: clang-format).
  CLANG_TIDY        clang-tidy binary (default: clang-tidy).
  RUN_CLANG_TIDY    run-clang-tidy binary (default: run-clang-tidy).
  VERIFY_TARGETS    Space-separated extra cmake --target names after defaults.
""",
    )
    parser.add_argument(
        "--format",
        action="store_true",
        help="Run clang-format -i on changed first-party C/C++ files.",
    )
    parser.add_argument("--skip-tidy", action="store_true", help="Skip clang-tidy.")
    parser.add_argument(
        "--require-tidy", action="store_true", help="Fail if clang-tidy cannot run."
    )
    parser.add_argument(
        "--preset",
        default=os.environ.get("CMAKE_PRESET", "Debug"),
        help="CMake preset.",
    )
    parser.add_argument(
        "--skip-configure",
        action="store_true",
        help="Use an already-configured build directory.",
    )
    parser.add_argument(
        "--quick", action="store_true", help="Skip slower optional analysis gates."
    )
    parser.add_argument(
        "--full",
        action="store_true",
        help="Run full shader compile and bounded app smokes.",
    )
    parser.add_argument(
        "--app-smoke", action="store_true", help="Run bounded metalrender app smokes."
    )
    parser.add_argument(
        "--shader-mode",
        choices=("auto", "changed", "all", "skip"),
        default="auto",
        help="Shader validation policy. auto runs all shaders on clean trees or shared shader changes.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.quick and args.full:
        print(
            "agent_verify.py: --quick and --full are mutually exclusive",
            file=sys.stderr,
        )
        return 2

    build_dir = REPO_ROOT / "build" / args.preset
    bin_dir = build_dir / "bin"
    summary = Summary(passed=[], skipped=[])
    paths = changed_paths()

    try:
        if args.format:
            run_format(paths, summary)

        if not args.skip_configure:
            run_checked("configure", ["cmake", "--preset", args.preset], quiet=True)
            summary.passed.append("configure")
        else:
            summary.skipped.append("configure: --skip-configure")

        extra_targets = os.environ.get("VERIFY_TARGETS", "").split()
        run_checked(
            "build",
            [
                "cmake",
                "--build",
                str(build_dir),
                "--target",
                *DEFAULT_TARGETS,
                *extra_targets,
            ],
        )
        summary.passed.append("build")

        gates: list[tuple[str, list[str]]] = [
            ("ctest", ["ctest", "--test-dir", str(build_dir), "--output-on-failure"]),
        ]

        shader_mode = args.shader_mode
        if args.full:
            shader_mode = "all"
        if shader_mode != "skip":
            command, label = shader_command(bin_dir, paths, shader_mode)
            if command is None:
                summary.skipped.append(label)
            else:
                gates.append((label, command))
        else:
            summary.skipped.append("shaderc: --shader-mode skip")

        do_tidy = not args.skip_tidy and not args.quick
        if do_tidy:
            command, label = clang_tidy_command(build_dir, paths)
            if command is None:
                if args.require_tidy:
                    raise RuntimeError(label)
                summary.skipped.append(label)
            else:
                gates.append((label, command))
        else:
            summary.skipped.append("clang-tidy: skipped")

        print_phase("post-build gates")
        with ThreadPoolExecutor(max_workers=min(4, len(gates))) as executor:
            futures = [
                executor.submit(run_capture, name, command) for name, command in gates
            ]
            for future in as_completed(futures):
                result = future.result()
                print(f"\n--- {result.name}", flush=True)
                print(f"$ {command_text(result.command)}", flush=True)
                if result.output:
                    print(
                        result.output, end="" if result.output.endswith("\n") else "\n"
                    )
                if result.returncode != 0:
                    print(
                        f"agent_verify.py: {result.name} failed with exit code {result.returncode}",
                        file=sys.stderr,
                    )
                    return result.returncode
                summary.passed.append(result.name)

        if args.full or args.app_smoke:
            for name, command in app_smoke_commands(bin_dir):
                result = run_capture(name, command)
                print(f"\n--- {result.name}", flush=True)
                print(f"$ {command_text(result.command)}", flush=True)
                if result.output:
                    print(
                        result.output, end="" if result.output.endswith("\n") else "\n"
                    )
                if result.returncode != 0:
                    print(
                        f"agent_verify.py: {result.name} failed with exit code {result.returncode}",
                        file=sys.stderr,
                    )
                    return result.returncode
                summary.passed.append(result.name)

    except (RuntimeError, subprocess.CalledProcessError) as error:
        print(f"agent_verify.py: {error}", file=sys.stderr)
        return 1

    print_phase("summary")
    print("Passed: " + ", ".join(summary.passed))
    if summary.skipped:
        print("Skipped: " + "; ".join(summary.skipped))
    print("agent_verify.py: OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
