#!/usr/bin/env python3
"""Search the bundled Ultraviolet specification."""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path


def spec_path() -> Path:
    return Path(__file__).resolve().parents[1] / "references" / "SPECIFICATION.md"


def line_matches(line: str, query: str, terms: list[str], regex: re.Pattern[str] | None) -> bool:
    if regex is not None:
        return bool(regex.search(line))
    lowered = line.lower()
    query_lower = query.lower()
    return query_lower in lowered or (bool(terms) and all(term in lowered for term in terms))


def build_rows(query: str, context: int, limit: int, use_regex: bool) -> list[dict[str, object]]:
    path = spec_path()
    lines = path.read_text(encoding="utf-8").splitlines()
    regex = re.compile(query) if use_regex else None
    terms = [term.lower() for term in re.findall(r"[A-Za-z0-9_:+~>\[\]-]+", query)]
    rows: list[dict[str, object]] = []
    for index, line in enumerate(lines):
        if not line_matches(line, query, terms, regex):
            continue
        start = max(0, index - context)
        end = min(len(lines), index + context + 1)
        rows.append(
            {
                "path": str(path),
                "line": index + 1,
                "text": line,
                "context": [
                    {"line": line_number + 1, "text": lines[line_number]}
                    for line_number in range(start, end)
                ],
            }
        )
        if len(rows) >= limit:
            break
    return rows


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("query", nargs="+")
    parser.add_argument("--context", type=int, default=2)
    parser.add_argument("--limit", type=int, default=20)
    parser.add_argument("--regex", action="store_true")
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()

    query = " ".join(args.query)
    rows = build_rows(query, max(0, args.context), max(1, args.limit), args.regex)
    if args.json:
        print(json.dumps({"query": query, "matches": rows}, indent=2))
        return 0

    for row in rows:
        print(f"SPECIFICATION.md:{row['line']}: {row['text']}")
        for item in row["context"]:
            marker = ">" if item["line"] == row["line"] else " "
            print(f"{marker} {item['line']}: {item['text']}")
        print()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
