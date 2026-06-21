#!/usr/bin/env python3
"""Rebuild the consolidated Ultraviolet Developer Handbook from its chapter parts.

The handbook is authored as one Markdown file per chapter in this directory,
named ``NN-slug.md`` (for example ``08-data-types.md``). Each part file begins
with a single level-2 heading ``## N. Title`` and uses ``###``/``####`` below it.

This script concatenates the parts, in numeric order, into a single document:

    Docs/ULTRAVIOLET_HANDBOOK.md   (one directory up from this script)

It prepends the handbook title, the provenance and reading-guide notes, and a
table of contents that links to per-chapter anchors (``#chNN``). The anchors are
explicit ``<a id="chNN">`` tags rather than slugified headings, so the links stay
stable regardless of punctuation in chapter titles.

Edit the part files, never the consolidated output, then re-run this script:

    py -3 build_handbook.py            # Windows
    python3 build_handbook.py          # Linux/macOS

The build is deterministic: the same parts always produce byte-identical output.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

# Part files are named NN-slug.md, where NN is a zero-padded chapter number.
PART_GLOB = "[0-9][0-9]-*.md"

TITLE = "Ultraviolet Developer Handbook"

SUBTITLE = (
    "The complete reference and engineering guide for writing correct, "
    "idiomatic Ultraviolet."
)

PROVENANCE = """\
> **Provenance.** Every chapter in this handbook is derived solely from the two authoritative
> Ultraviolet sources — `Docs/SPECIFICATION.md` (the language specification, the single source of
> truth for all syntax and semantics) and `AGENTS.md` (the official style guide) — in the
> `blacklight-foundation/ultraviolet` repository. Each chapter was authored from the specification
> and then independently re-verified against it, token by token. The `HelloUltraviolet` example
> corpus was deliberately excluded as a source because it contains known inaccuracies. Where any
> statement here conflicts with the specification, the specification governs."""

READING_GUIDE = """\
> **How to read this.** Chapters 1–7 establish the model (philosophy, conformance, projects,
> lexicon, names, modules, the type core). Chapters 8–19 are the day-to-day language (types,
> modal typestate, strings/bytes, generics/classes/contracts, expressions, patterns, statements).
> Chapters 20–28 cover authority, concurrency, async, metaprogramming, FFI, and lifecycle.
> Chapter 29 is the complete grammar quick-reference; chapter 30 is the engineering standard."""

GENERATED_NOTICE = (
    "<!-- GENERATED FILE — do not edit directly.\n"
    "     Source chapters live in Docs/UltravioletHandbook.parts/ (one per file).\n"
    "     Edit the part files, then regenerate with: py -3 build_handbook.py -->"
)


class Chapter:
    """One handbook chapter: its zero-padded number, title, and source path."""

    def __init__(self, number: str, title: str, path: Path) -> None:
        self.number = number
        self.title = title
        self.path = path


def first_heading(path: Path) -> str | None:
    """Return the text of the first ``## `` heading in *path*, or None."""
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.startswith("## "):
            return line[len("## "):].strip()
    return None


def discover_chapters(parts_dir: Path) -> list[Chapter]:
    """Find and order every ``NN-slug.md`` chapter part in *parts_dir*."""
    chapters: list[Chapter] = []
    for path in sorted(parts_dir.glob(PART_GLOB), key=lambda p: p.name):
        number = path.name.split("-", 1)[0]
        title = first_heading(path)
        if title is None:
            print(
                f"warning: {path.name} has no '## ' heading; "
                f"falling back to file name for its title",
                file=sys.stderr,
            )
            title = path.stem
        chapters.append(Chapter(number, title, path))
    return chapters


def render(chapters: list[Chapter]) -> str:
    """Render the full consolidated handbook as a single Markdown string."""
    lines: list[str] = []

    lines.append(GENERATED_NOTICE)
    lines.append("")
    lines.append(f"# {TITLE}")
    lines.append("")
    lines.append(SUBTITLE)
    lines.append("")
    lines.extend(PROVENANCE.splitlines())
    lines.append("")
    lines.extend(READING_GUIDE.splitlines())
    lines.append("")

    lines.append("## Table of Contents")
    lines.append("")
    for chapter in chapters:
        lines.append(f"- [{chapter.title}](#ch{chapter.number})")
    lines.append("")
    lines.append("---")
    lines.append("")

    for chapter in chapters:
        lines.append(f'<a id="ch{chapter.number}"></a>')
        lines.append("")
        body = chapter.path.read_text(encoding="utf-8").rstrip("\n")
        lines.extend(body.splitlines())
        lines.append("")
        lines.append("---")
        lines.append("")

    return "\n".join(lines).rstrip("\n") + "\n"


def main() -> int:
    script_dir = Path(__file__).resolve().parent
    parser = argparse.ArgumentParser(
        description="Rebuild the consolidated Ultraviolet Developer Handbook.",
    )
    parser.add_argument(
        "--parts-dir",
        type=Path,
        default=script_dir,
        help="directory containing the NN-slug.md chapter parts "
        "(default: this script's directory)",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=script_dir.parent / "ULTRAVIOLET_HANDBOOK.md",
        help="path to write the consolidated handbook "
        "(default: ../ULTRAVIOLET_HANDBOOK.md)",
    )
    args = parser.parse_args()

    parts_dir: Path = args.parts_dir
    if not parts_dir.is_dir():
        print(f"error: parts directory not found: {parts_dir}", file=sys.stderr)
        return 1

    chapters = discover_chapters(parts_dir)
    if not chapters:
        print(
            f"error: no chapter parts matching '{PART_GLOB}' in {parts_dir}",
            file=sys.stderr,
        )
        return 1

    document = render(chapters)
    output: Path = args.output
    output.write_text(document, encoding="utf-8", newline="\n")

    line_count = document.count("\n")
    print(f"Rebuilt {output}")
    print(f"  chapters: {len(chapters)}")
    print(f"  lines:    {line_count}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
