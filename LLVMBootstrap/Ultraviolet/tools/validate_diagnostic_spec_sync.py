#!/usr/bin/env python3

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

from resolve_spec_path import resolve_spec_path


CODE_PATTERN = re.compile(r"[EWIP]-[A-Z]{3}-[0-9]{4}")
REGISTRY_ROW_PATTERN = re.compile(r'\{"[EWIP]-[A-Z]{3}-[0-9]{4}",')
SOURCE_PATTERNS = [
    re.compile(pattern)
    for pattern in (
        r'MakeDiagnostic\("(?P<code>[EWIP]-[A-Z]{3}-[0-9]{4})"',
        r'MakeDiagnosticById\("(?P<code>[EWIP]-[A-Z]{3}-[0-9]{4})"',
        r'\.code\s*=\s*"(?P<code>[EWIP]-[A-Z]{3}-[0-9]{4})"',
        r'error_code\.empty\(\)\s*\?\s*"(?P<code>[EWIP]-[A-Z]{3}-[0-9]{4})"',
        r'code\.empty\(\)\s*\?\s*"(?P<code>[EWIP]-[A-Z]{3}-[0-9]{4})"',
    )
]


def bootstrap_source_root(repo_root: Path) -> Path:
    direct = repo_root / "Ultraviolet" / "src"
    if direct.exists():
        return direct
    return repo_root / "LLVMBootstrap" / "Ultraviolet" / "src"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo-root", default="")
    parser.add_argument("--source-root", default="")
    parser.add_argument("--spec-path", default="")
    parser.add_argument("--registry-path", default="")
    return parser.parse_args()


def main() -> int:
    args = parse_args()

    repo_root = Path(args.repo_root).resolve() if args.repo_root else Path(__file__).resolve().parents[3]
    default_source_root = bootstrap_source_root(repo_root)
    spec_path = Path(args.spec_path).resolve() if args.spec_path else resolve_spec_path(repo_root)
    registry_path = (
        Path(args.registry_path).resolve()
        if args.registry_path
        else default_source_root / "00_core" / "generated" / "diag_registry.inc"
    )

    if not spec_path.exists():
        print(f"Spec file not found: {spec_path}", file=sys.stderr)
        return 1
    if not registry_path.exists():
        print(f"Generated registry not found: {registry_path}", file=sys.stderr)
        return 1

    spec_text = spec_path.read_text(encoding="utf-8-sig")
    spec_codes = {match.group(0) for match in CODE_PATTERN.finditer(spec_text)}

    source_codes: set[str] = set()
    src_root = Path(args.source_root).resolve() if args.source_root else default_source_root
    source_files = sorted(
        path
        for path in src_root.rglob("*")
        if path.is_file() and path.suffix in {".cpp", ".h", ".hpp", ".inc"}
    )
    for source_file in source_files:
        text = source_file.read_text(encoding="utf-8")
        for pattern in SOURCE_PATTERNS:
            for match in pattern.finditer(text):
                source_codes.add(match.group("code"))

    missing_in_spec = sorted(code for code in source_codes if code not in spec_codes)
    if missing_in_spec:
        print("[diag-sync] FAIL: active compiler diagnostics missing from canonical language spec:")
        for code in missing_in_spec:
            print(f"  - {code}")
        print(
            "Diagnostic spec sync validation failed. Update the canonical language spec diagnostic tables.",
            file=sys.stderr,
        )
        return 1

    registry_text = registry_path.read_text(encoding="utf-8")
    registry_rows = len(REGISTRY_ROW_PATTERN.findall(registry_text))
    if registry_rows == 0:
        print(f"Generated registry appears empty: {registry_path}", file=sys.stderr)
        return 1

    print(f"[diag-sync] PASS: source_codes={len(source_codes)} spec_codes={len(spec_codes)} registry_rows={registry_rows}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
