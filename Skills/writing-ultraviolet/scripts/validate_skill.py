#!/usr/bin/env python3
"""Validate the writing-ultraviolet primary-card scaffold."""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path


CARDS = ["Form", "Access", "Change", "Flow", "Envelope"]

CARD_FILES = [
    "references/form-card.md",
    "references/access-card.md",
    "references/change-card.md",
    "references/flow-card.md",
    "references/envelope-card.md",
]

REQUIRED_FILES = [
    "SKILL.md",
    "agents/openai.yaml",
    "references/SPECIFICATION.md",
    "references/mother-patterns.md",
    "references/uv-realization-index.md",
    "references/pattern-card-template.md",
    *CARD_FILES,
    "scripts/find_uv_topic.py",
    "scripts/spec_search.py",
    "scripts/validate_skill.py",
]

CLEARED_REFERENCE_PATTERNS = [
    "appendix-*.md",
    "chapter-*.md",
    "guide-*.md",
    "chapter-map.md",
    "evals.md",
    "idioms-common-fixes-and-examples.md",
    "language-surface-checklist.md",
]


def fail(message: str) -> None:
    print(f"[FAIL] {message}")
    raise SystemExit(1)


def check(condition: bool, message: str) -> None:
    if not condition:
        fail(message)


def parse_frontmatter(text: str) -> dict[str, str]:
    match = re.match(r"^---\n(.*?)\n---\n", text, re.DOTALL)
    check(match is not None, "SKILL.md is missing YAML frontmatter")
    fields: dict[str, str] = {}
    for raw_line in match.group(1).splitlines():
        if not raw_line.strip():
            continue
        key, sep, value = raw_line.partition(":")
        check(bool(sep), f"invalid frontmatter line: {raw_line}")
        fields[key.strip()] = value.strip()
    return fields


def run_json(command: list[str]) -> dict[str, object]:
    result = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    check(result.returncode == 0, f"{Path(command[1]).name} failed: {result.stderr}")
    try:
        return json.loads(result.stdout)
    except json.JSONDecodeError as exc:
        fail(f"{Path(command[1]).name} did not emit JSON: {exc}")


def assert_routes(skill_root: Path, query: str, expected_cards: set[str]) -> None:
    payload = run_json([sys.executable, str(skill_root / "scripts" / "find_uv_topic.py"), query, "--json"])
    actual = {str(match["card"]) for match in payload["matches"]}
    missing = expected_cards - actual
    check(not missing, f"routing for {query!r} missed cards: {', '.join(sorted(missing))}")


def assert_spec_search(skill_root: Path, query: str, expected_text: str) -> None:
    payload = run_json([sys.executable, str(skill_root / "scripts" / "spec_search.py"), query, "--json", "--limit", "5"])
    texts = "\n".join(str(match["text"]) for match in payload["matches"])
    check(expected_text in texts, f"spec search for {query!r} missed {expected_text!r}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("skill_root", nargs="?", default=".")
    parser.add_argument("--repo-root")
    args = parser.parse_args()

    skill_root = Path(args.skill_root).resolve()
    references = skill_root / "references"

    for relative in REQUIRED_FILES:
        check((skill_root / relative).exists(), f"missing required file: {relative}")

    skill_text = (skill_root / "SKILL.md").read_text(encoding="utf-8")
    fields = parse_frontmatter(skill_text)
    check(fields.get("name") == "writing-ultraviolet", "skill name must be writing-ultraviolet")
    check("primary pattern cards" in fields.get("description", ""), "description must mention primary pattern cards")
    for card_file in CARD_FILES:
        check(card_file in skill_text, f"SKILL.md must link {card_file}")
    check("scripts/spec_search.py" in skill_text, "SKILL.md must route to spec search")

    if args.repo_root:
        repo_spec = (Path(args.repo_root).resolve() / "SPECIFICATION.md").read_bytes()
        bundled_spec = (references / "SPECIFICATION.md").read_bytes()
        check(repo_spec == bundled_spec, "bundled SPECIFICATION.md is not byte-identical to repo root SPECIFICATION.md")

    for glob_pattern in CLEARED_REFERENCE_PATTERNS:
        matches = sorted(path.name for path in references.glob(glob_pattern))
        check(not matches, f"cleared reference files remain for {glob_pattern}: {', '.join(matches)}")

    check(not (skill_root / "assets").exists(), "assets directory remains after scaffold reset")
    check(not (skill_root / "scripts" / "collect_uv_examples.py").exists(), "old corpus collector remains")

    for card, relative in zip(CARDS, CARD_FILES):
        text = (skill_root / relative).read_text(encoding="utf-8")
        check(text.isascii(), f"{relative} contains non-ASCII text")
        check(text.startswith(f"# {card} Card"), f"{relative} has wrong title")
        for heading in ["## Meaning", "## Slots", "## UV Surfaces", "## Fill Order", "## Review Checks", "## Spec Search Terms"]:
            check(heading in text, f"{relative} missing {heading}")

    mother_text = (references / "mother-patterns.md").read_text(encoding="utf-8")
    for card_file in CARD_FILES:
        check(card_file in mother_text, f"mother-patterns.md missing card link {card_file}")
    for recipe in ["Transform", "Accumulate", "Remember", "Hide", "Effect", "Coordinate", "Parser", "Resource Lifecycle"]:
        check(f"## {recipe}" in mother_text, f"mother-patterns.md missing derived recipe {recipe}")

    realization_text = (references / "uv-realization-index.md").read_text(encoding="utf-8")
    for card in CARDS:
        check(card in realization_text, f"uv-realization-index.md missing {card} reference")

    required_realizations = [
        "Procedure Shell",
        "Local Binding",
        "Record Shape",
        "Enum Choice Shape",
        "Modal Lifecycle Shape",
        "Conditional Choice",
        "Pattern Choice",
        "Repeat Shape",
        "Accumulator Shape",
        "Method Call Shape",
        "String View Shape",
        "Bytes Inspection Shape",
        "Dynamic Verification Shape",
        "Effect Boundary Shape",
        "Unsafe Wrapper Shape",
    ]
    for section in required_realizations:
        check(f"## {section}" in realization_text, f"uv-realization-index.md missing {section}")

    assert_routes(skill_root, "scan text and count bytes", {"Flow", "Access", "Change"})
    assert_routes(skill_root, "choose enum variant by payload", {"Flow", "Access", "Form"})
    assert_routes(skill_root, "write output through context io capability", {"Envelope", "Form"})
    assert_routes(skill_root, "store retry state across calls", {"Change", "Flow", "Envelope"})
    assert_routes(skill_root, "modal state contract dynamic region allocation", {"Form", "Change", "Envelope"})

    assert_spec_search(skill_root, "loop_expr", "loop_expr")
    assert_spec_search(skill_root, "if_expr", "if_expr")
    assert_spec_search(skill_root, "string::length", "string::length")
    assert_spec_search(skill_root, "bytes::as_slice", "bytes::as_slice")

    print("[OK] writing-ultraviolet primary-card scaffold is valid")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
