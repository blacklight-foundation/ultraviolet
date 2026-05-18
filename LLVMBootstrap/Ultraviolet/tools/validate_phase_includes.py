#!/usr/bin/env python3
"""Validate compiler phase include direction.

The source tree is organized by numbered phase directories. Earlier phases may
include earlier or same-phase headers, but must not include later phases.
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys


PHASES = {
    "00_core": 0,
    "01_project": 1,
    "02_source": 2,
    "03_comptime": 3,
    "04_analysis": 4,
    "05_codegen": 5,
    "06_driver": 6,
}

INCLUDE_RE = re.compile(r'^\s*#\s*include\s+"([^"]+)"')


def phase_of_path(path: pathlib.Path) -> tuple[str, int] | None:
    parts = path.parts
    for marker in ("src", "include"):
        if marker not in parts:
            continue
        index = parts.index(marker)
        if index + 1 >= len(parts):
            continue
        phase = parts[index + 1]
        if phase in PHASES:
            return phase, PHASES[phase]
    return None


def phase_of_include(include: str) -> tuple[str, int] | None:
    head = include.split("/", 1)[0]
    if head not in PHASES:
        return None
    return head, PHASES[head]


def source_files(root: pathlib.Path):
    for base in (root / "src", root / "include"):
        if not base.exists():
            continue
        for path in base.rglob("*"):
            if path.suffix in {".cpp", ".h", ".hpp", ".inc"}:
                yield path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-root", type=pathlib.Path, required=True)
    args = parser.parse_args()

    root = args.source_root.resolve()
    violations: list[str] = []
    for path in source_files(root):
        owner = phase_of_path(path)
        if owner is None:
            continue
        owner_name, owner_rank = owner
        try:
            lines = path.read_text(encoding="utf-8").splitlines()
        except UnicodeDecodeError as exc:
            violations.append(f"{path}: invalid UTF-8 while scanning includes: {exc}")
            continue
        for lineno, line in enumerate(lines, start=1):
            match = INCLUDE_RE.match(line)
            if not match:
                continue
            target = phase_of_include(match.group(1))
            if target is None:
                continue
            target_name, target_rank = target
            if target_rank > owner_rank:
                rel = path.relative_to(root)
                violations.append(
                    f"{rel}:{lineno}: {owner_name} must not include later "
                    f"phase {target_name}: {match.group(1)}"
                )

    if violations:
        print("Phase include validation failed:", file=sys.stderr)
        for violation in violations:
            print(f"  {violation}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
