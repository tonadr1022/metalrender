#!/usr/bin/env python3
"""Plot git line insertions/deletions aggregated by ISO week \
   in the terminal."""

from __future__ import annotations

import argparse
import math
import shutil
import subprocess
import sys
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Iterator

SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parent

# Match common "first party" scope; override with positional pathspecs.
DEFAULT_PATHSPECS = (
    ".",
    ":(exclude)third_party",
    ":(exclude)build",
    ":(exclude).git",
)

# When restricting to C++ sources, match numstat paths case-insensitively.
_CPP_HPP_SUFFIXES = (".cpp", ".hpp")


def _numstat_effective_path(path_field: str) -> str:
    """Path shown in numstat (handles `old => new` rename form)."""
    if " => " in path_field:
        return path_field.split(" => ", 1)[-1].strip()
    return path_field.strip()


def _path_allowed(path_field: str, allowed_suffixes: tuple[str, ...]) -> bool:
    p = _numstat_effective_path(path_field).lower()
    return any(p.endswith(sfx) for sfx in allowed_suffixes)


@dataclass
class WeekBucket:
    year: int
    week: int
    added: int = 0
    deleted: int = 0

    @property
    def label(self) -> str:
        return f"{self.year}-W{self.week:02d}"

    @property
    def churn(self) -> int:
        return self.added + self.deleted

    @property
    def net(self) -> int:
        return self.added - self.deleted


def _run_git_numstat(
    repo: Path,
    pathspecs: tuple[str, ...],
    since: str | None,
    until: str | None,
    max_commits: int | None,
    no_merges: bool,
    first_parent: bool,
    extra_log_args: tuple[str, ...] = (),
    allowed_path_suffixes: tuple[str, ...] | None = None,
) -> list[tuple[int, int, int]]:
    """Return list of (unix_ts, added, deleted) per commit.

    If allowed_path_suffixes is set, only numstat rows whose path ends with one
    of those suffixes (case-insensitive) are counted — composes with any git
    pathspec without needing per-extension :(glob) pathspecs.
    """
    cmd: list[str] = [
        "git",
        "-C",
        str(repo),
        "log",
        *extra_log_args,
        "--numstat",
        "--pretty=format:%ct",
    ]
    if no_merges:
        cmd.append("--no-merges")
    if first_parent:
        cmd.append("--first-parent")
    if since:
        cmd.extend(["--since", since])
    if until:
        cmd.extend(["--until", until])
    if max_commits:
        cmd.extend(["-n", str(max_commits)])
    cmd.append("--")
    cmd.extend(pathspecs)

    proc = subprocess.run(
        cmd,
        capture_output=True,
        text=True,
        check=False,
    )
    if proc.returncode != 0:
        sys.stderr.write(proc.stderr or "")
        raise SystemExit(proc.returncode or 1)

    rows: list[tuple[int, int, int]] = []
    cur_ts: int | None = None
    commit_added = 0
    commit_deleted = 0

    def flush_commit() -> None:
        nonlocal cur_ts, commit_added, commit_deleted
        if cur_ts is not None and (commit_added or commit_deleted):
            rows.append((cur_ts, commit_added, commit_deleted))
        commit_added = 0
        commit_deleted = 0

    for raw in proc.stdout.splitlines():
        line = raw.rstrip("\n")
        if not line.strip():
            continue
        parts = line.split("\t")
        if len(parts) == 1 and parts[0].isdigit():
            flush_commit()
            cur_ts = int(parts[0])
            continue
        if "\t" not in line:
            continue
        a, rest = line.split("\t", 1)
        if "\t" not in rest:
            continue
        d, path_field = rest.split("\t", 1)
        if allowed_path_suffixes and not _path_allowed(
            path_field, allowed_path_suffixes
        ):
            continue
        if a == "-" or d == "-":
            continue
        try:
            commit_added += int(a)
            commit_deleted += int(d)
        except ValueError:
            continue

    flush_commit()
    return rows


def _iso_week_key(ts: int) -> tuple[int, int]:
    dt = datetime.fromtimestamp(ts, tz=timezone.utc)
    y, w, _ = dt.isocalendar()
    return y, w


def aggregate_by_week(
    commits: list[tuple[int, int, int]],
) -> dict[tuple[int, int], WeekBucket]:
    buckets: dict[tuple[int, int], WeekBucket] = {}
    for ts, added, deleted in commits:
        key = _iso_week_key(ts)
        if key not in buckets:
            buckets[key] = WeekBucket(year=key[0], week=key[1])
        b = buckets[key]
        b.added += added
        b.deleted += deleted
    return buckets


def _bar(
    value: float,
    max_value: float,
    width: int,
    fill: str = "█",
    empty: str = "░",
) -> str:
    if width <= 0:
        return ""
    if max_value <= 0:
        return empty * width
    frac = min(1.0, value / max_value)
    filled = int(math.floor(frac * width + 1e-9))
    if frac > 0 and filled == 0:
        filled = 1
    filled = min(filled, width)
    return fill * filled + empty * (width - filled)


def _fmt_int(n: int) -> str:
    return f"{n:,}"


def render_chart(
    ordered: list[WeekBucket],
    metric: str,
    *,
    bar_width: int,
    show_net_column: bool,
) -> Iterator[str]:
    if not ordered:
        yield "No commits matched the filters."
        return

    def metric_value(b: WeekBucket) -> int:
        if metric == "added":
            return b.added
        if metric == "deleted":
            return b.deleted
        if metric == "net":
            return abs(b.net)
        return b.churn

    values = [metric_value(b) for b in ordered]
    peak = max(values) if values else 0
    label_w = max(len(b.label) for b in ordered)

    yield ""
    title = {
        "added": "Insertions",
        "deleted": "Deletions",
        "net": "|Net change|",
        "churn": "Churn (insertions + deletions)",
    }[metric]
    yield f"  {title} per ISO week (UTC) — max {_fmt_int(peak)}"
    sep_w = label_w + bar_width + 34
    yield f"  {'─' * sep_w}"

    for b in ordered:
        v = metric_value(b)
        bar = _bar(float(v), float(peak), bar_width)
        extra = ""
        if show_net_column and metric != "net":
            extra = f"  net {b.net:+,}"
        if metric == "net":
            count_disp = f"{b.net:+,}"
        else:
            count_disp = _fmt_int(v)
        line = (
            f"  {b.label:>{label_w}}  │{bar}│ {count_disp:>12}  "
            f"+{_fmt_int(b.added)} -{_fmt_int(b.deleted)}{extra}"
        )
        yield line

    total_added = sum(b.added for b in ordered)
    total_deleted = sum(b.deleted for b in ordered)
    yield f"  {'─' * sep_w}"
    net_total = total_added - total_deleted
    yield (
        f"  Totals: +{_fmt_int(total_added)} -{_fmt_int(total_deleted)} "
        f"(net {net_total:+,})"
    )
    yield ""


def main() -> None:
    p = argparse.ArgumentParser(
        description=(
            "Show git line insertions/deletions grouped by ISO week "
            "(from git log --numstat)."
        )
    )
    p.add_argument(
        "pathspec",
        nargs="*",
        help=(
            "Optional git pathspecs after '--'. "
            "Default: repo root excluding third_party/ and build/."
        ),
    )
    p.add_argument(
        "--repo",
        type=Path,
        default=REPO_ROOT,
        help=f"Git repository root (default: {REPO_ROOT})",
    )
    p.add_argument(
        "--metric",
        choices=("added", "deleted", "net", "churn"),
        default="added",
        help="Which number drives bar length (default: added).",
    )
    p.add_argument("--since", help="Passed to git log --since")
    p.add_argument("--until", help="Passed to git log --until")
    p.add_argument(
        "-n",
        "--max-commits",
        type=int,
        default=None,
        help="Limit number of commits (git -n).",
    )
    p.add_argument(
        "--merges",
        action="store_true",
        help="Include merge commits (default: exclude).",
    )
    p.add_argument(
        "--no-first-parent",
        action="store_true",
        help="Do not pass --first-parent (default: first-parent on).",
    )
    p.add_argument(
        "--all-branches",
        action="store_true",
        help="Use git log --all (default: current HEAD history only).",
    )
    p.add_argument(
        "--no-net-column",
        action="store_true",
        help="Hide the net column when metric is not net.",
    )
    p.add_argument(
        "--cpp-hpp-only",
        action="store_true",
        help="Only count paths ending in .cpp or .hpp (case-insensitive).",
    )
    args = p.parse_args()

    pathspecs = tuple(args.pathspec) if args.pathspec else DEFAULT_PATHSPECS

    cmd_prefix_check = [
        "git",
        "-C",
        str(args.repo),
        "rev-parse",
        "--is-inside-work-tree",
    ]
    chk = subprocess.run(cmd_prefix_check, capture_output=True, text=True)
    if chk.returncode != 0 or chk.stdout.strip() != "true":
        print(f"Not a git repository: {args.repo}", file=sys.stderr)
        raise SystemExit(1)

    extra = ("--all",) if args.all_branches else ()

    suffix_filter: tuple[str, ...] | None = (
        _CPP_HPP_SUFFIXES if args.cpp_hpp_only else None
    )

    rows = _run_git_numstat(
        args.repo,
        pathspecs,
        since=args.since,
        until=args.until,
        max_commits=args.max_commits,
        no_merges=not args.merges,
        first_parent=not args.no_first_parent,
        extra_log_args=extra,
        allowed_path_suffixes=suffix_filter,
    )

    buckets = aggregate_by_week(rows)
    ordered = sorted(buckets.values(), key=lambda b: (b.year, b.week))

    term_w = max(40, shutil.get_terminal_size(fallback=(100, 24)).columns)
    label_w = max((len(b.label) for b in ordered), default=10)
    # Reserve: padding + label + "  │" + "│ NNNNNNNNNN  +N -N  net ..."
    reserved = label_w + 48 + (0 if args.no_net_column else 14)
    bar_width = max(10, min(72, term_w - reserved))

    if args.cpp_hpp_only:
        print("  Paths: .cpp / .hpp only (case-insensitive)")

    for line in render_chart(
        ordered,
        args.metric,
        bar_width=bar_width,
        show_net_column=not args.no_net_column,
    ):
        print(line)


if __name__ == "__main__":
    main()
