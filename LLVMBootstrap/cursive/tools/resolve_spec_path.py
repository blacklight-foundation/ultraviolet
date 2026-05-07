#!/usr/bin/env python3

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path


SPEC_HEADER_PATTERN = re.compile(r"(?m)^# Cursive Language Specification\s*$")
SPEC_MARKER = "This file is the canonical normative language specification."
EXCLUDED_DIRS = {".archive", ".git", ".vs", "build", "extern", "node_modules"}
PREFERRED_SPEC_CANDIDATES = (
    Path("docs") / "CursiveSpecification.md",
)


def iter_markdown_files(repo_root: Path):
    for path in repo_root.rglob("*.md"):
        if any(part in EXCLUDED_DIRS for part in path.parts):
            continue
        yield path


def is_canonical_spec(path: Path) -> bool:
    text = path.read_text(encoding="utf-8-sig")
    return bool(SPEC_HEADER_PATTERN.search(text)) and SPEC_MARKER in text


def resolve_spec_path(repo_root: Path) -> Path:
    for relative_path in PREFERRED_SPEC_CANDIDATES:
        candidate = (repo_root / relative_path).resolve()
        if candidate.exists() and is_canonical_spec(candidate):
            return candidate

    candidates = sorted(path.resolve() for path in iter_markdown_files(repo_root) if is_canonical_spec(path))
    if not candidates:
        raise RuntimeError(f"Unable to resolve canonical language spec under repo root: {repo_root}")
    if len(candidates) > 1:
        joined = "; ".join(str(path) for path in candidates)
        raise RuntimeError(f"Multiple canonical language spec candidates found: {joined}")
    return candidates[0]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo-root", default="")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    repo_root = Path(args.repo_root).resolve() if args.repo_root else Path(__file__).resolve().parents[2]
    try:
        spec_path = resolve_spec_path(repo_root)
    except Exception as exc:
        print(str(exc), file=sys.stderr)
        return 1

    print(spec_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
