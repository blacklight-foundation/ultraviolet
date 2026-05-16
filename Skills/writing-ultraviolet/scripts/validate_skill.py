#!/usr/bin/env python3
"""Validate the writing-ultraviolet skill structure."""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path


REQUIRED_REFERENCES = [
    "authoring-workflow.md",
    "chapter-map.md",
    *[f"chapter-{index:02d}-{slug}.md" for index, slug in [
        (0, "front-matter"),
        (1, "conformance-and-notation"),
        (2, "diagnostics"),
        (3, "project-and-compilation"),
        (4, "source-text-and-lexical-structure"),
        (5, "parsing-and-ast"),
        (6, "abstract-machine-responsibility-authority-memory"),
        (7, "name-resolution-and-visibility"),
        (8, "type-system"),
        (9, "attributes"),
        (10, "permissions-and-binding-state"),
        (11, "module-level-forms"),
        (12, "concrete-data-types"),
        (13, "modal-and-special-types"),
        (14, "abstraction-and-polymorphism"),
        (15, "procedures-and-contracts"),
        (16, "expressions"),
        (17, "patterns"),
        (18, "statements-and-blocks"),
        (19, "key-system"),
        (20, "structured-parallelism"),
        (21, "async"),
        (22, "compile-time-execution"),
        (23, "ffi"),
        (24, "lowering-lifecycle-and-backend"),
    ]],
    "appendix-a-diagnostic-index.md",
    "appendix-b-complete-grammar.md",
    "appendix-c-ast-form-index.md",
    "appendix-d-layout-abi-runtime.md",
    "guide-memory-responsibility-permissions.md",
    "guide-authority-capabilities-and-effects.md",
    "guide-regions-frames-and-provenance.md",
    "guide-keys-shared-access-and-memory-ordering.md",
    "guide-procedures-contracts-tests-and-verification.md",
    "guide-modal-resource-lifecycle.md",
    "guide-async-parallelism-and-cancellation.md",
    "guide-unsafe-ffi-runtime-and-lowering.md",
    "idioms-common-fixes-and-examples.md",
    "compiler-mismatch-workflow.md",
    "evals.md",
]

CHAPTER_HEADINGS = [
    "## Load When",
    "## Authoring Rules",
    "## Syntax Forms",
    "## Static Semantics",
    "## Runtime and Lowering Notes",
    "## Diagnostics",
    "## Reference Corpus",
    "## Spec Fallback",
]

TOPIC_WORDS = [
    "memory",
    "responsibility",
    "permissions",
    "authority",
    "capabilities",
    "keys",
    "regions",
    "frames",
    "contracts",
    "procedures",
    "modules",
    "expressions",
    "patterns",
    "statements",
    "async",
    "parallelism",
    "compile-time",
    "ffi",
    "diagnostics",
    "lowering",
    "runtime",
]

BAD_UV_PATTERNS = [
    (re.compile(r"context\.execution_domain"), "use corpus-backed execution-domain calls such as context~>inline()"),
    (re.compile(r"\bResult::"), "avoid fictional Result examples; use Outcome or corpus-defined enums"),
    (re.compile(r"\bModule::Submodule\b"), "avoid schematic module paths in executable examples"),
    (re.compile(r"\bTools::Uv::Driver\b"), "avoid non-corpus module paths in executable examples"),
    (re.compile(r"\bRuntime::Filesystem\b"), "avoid non-corpus module paths in executable examples"),
    (re.compile(r"\b(public|internal|private)\s+static\b"), "static declarations use let or var, not a static keyword"),
    (re.compile(r"\bpublic\s+enum\s+Outcome\b"), "do not shadow the builtin Outcome modal in examples"),
    (re.compile(r"transition\s+\w+\([^)]*~"), "modal transitions do not declare receiver shorthand"),
    (re.compile(r"transition[^\n]*->\s+[A-Za-z_][A-Za-z0-9_]*@"), "transition return syntax is -> @State"),
    (re.compile(r"^\s*yield\s*$", re.MULTILINE), "yield requires an output expression"),
    (re.compile(r"^\s*yield\s+release\s*$", re.MULTILINE), "yield release requires an output expression"),
    (re.compile(r"race\s*\{[^}\n;]*;"), "race arms use comma-separated arm syntax with -> handlers"),
    (re.compile(r"all\s*\{[^}\n;]*;"), "all operands are comma-separated expressions"),
    (re.compile(r"\bsplice\s+\$"), "splicing occurs inside quote forms as $(expr) or identifier splices"),
    (re.compile(r"Type::<[^>]+>~>name"), "reflection uses introspect~>type_name(Type::<T>)"),
    (re.compile(r'string@Managed\s*=\s*"'), "string literals produce string@View unless converted explicitly"),
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


def ultraviolet_fences(path: Path, text: str) -> list[tuple[int, str]]:
    fences: list[tuple[int, str]] = []
    in_fence = False
    fence_language = ""
    fence_start = 0
    body: list[str] = []
    for line_number, line in enumerate(text.splitlines(), 1):
        if line.startswith("```"):
            if not in_fence:
                in_fence = True
                fence_language = line[3:].strip()
                fence_start = line_number
                body = []
            else:
                if fence_language == "ultraviolet":
                    fences.append((fence_start, "\n".join(body)))
                in_fence = False
                fence_language = ""
                fence_start = 0
                body = []
            continue
        if in_fence:
            body.append(line)
    check(not in_fence, f"{path.name} has an unterminated fenced code block")
    return fences


def strip_uv_strings(source: str) -> str:
    return re.sub(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', '""', source)


def validate_uv_fence(path: Path, line_number: int, source: str) -> None:
    for pattern, message in BAD_UV_PATTERNS:
        check(not pattern.search(source), f"{path.name}:{line_number} has invalid UV example: {message}")

    stripped = strip_uv_strings(source)
    for open_char, close_char, name in [("{", "}", "brace"), ("(", ")", "paren"), ("[", "]", "bracket")]:
        check(
            stripped.count(open_char) == stripped.count(close_char),
            f"{path.name}:{line_number} has unbalanced {name} delimiters",
        )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("skill_root", nargs="?", default=".")
    args = parser.parse_args()

    skill_root = Path(args.skill_root).resolve()
    references = skill_root / "references"
    scripts = skill_root / "scripts"
    assets = skill_root / "assets"
    skill_md = skill_root / "SKILL.md"

    check(skill_md.exists(), "SKILL.md is missing")
    text = skill_md.read_text(encoding="utf-8")
    fields = parse_frontmatter(text)
    check(set(fields) == {"name", "description"}, "frontmatter must contain only name and description")
    check(fields["name"] == "writing-ultraviolet", "skill name must be writing-ultraviolet")
    placeholder_marker = "TO" + "DO"
    check(placeholder_marker not in text, "SKILL.md contains placeholder marker")

    for ref_name in REQUIRED_REFERENCES:
        ref_path = references / ref_name
        check(ref_path.exists(), f"missing reference: {ref_name}")
        check(ref_name in text or ref_name in (references / "chapter-map.md").read_text(encoding="utf-8"),
              f"reference is not discoverable: {ref_name}")

    for ref_path in references.glob("*.md"):
        ref_text = ref_path.read_text(encoding="utf-8")
        check(placeholder_marker not in ref_text, f"{ref_path.name} contains placeholder marker")
        check(ref_text.isascii(), f"{ref_path.name} contains non-ASCII text")
        if re.match(r"chapter-\d\d-", ref_path.name) or ref_path.name.startswith("appendix-"):
            for heading in CHAPTER_HEADINGS:
                check(heading in ref_text, f"{ref_path.name} missing heading {heading}")
        for fence_start, source in ultraviolet_fences(ref_path, ref_text):
            validate_uv_fence(ref_path, fence_start, source)

    chapter_map = (references / "chapter-map.md").read_text(encoding="utf-8").lower()
    for word in TOPIC_WORDS:
        check(word in chapter_map, f"chapter-map.md missing topic word: {word}")

    for script_name in ["validate_skill.py", "find_uv_topic.py", "collect_uv_examples.py"]:
        check((scripts / script_name).exists(), f"missing script: {script_name}")

    minimal = assets / "minimal-project"
    check((minimal / "Ultraviolet.toml").exists(), "minimal project manifest missing")
    check((minimal / "Source" / "Main.uv").exists(), "minimal project Main.uv missing")

    main_uv = (minimal / "Source" / "Main.uv").read_text(encoding="utf-8")
    check("public procedure main" in main_uv, "minimal Main.uv missing public main")
    check("-> i32" in main_uv, "minimal Main.uv missing i32 return")
    check("return" in main_uv, "minimal Main.uv missing explicit return")

    print("[OK] writing-ultraviolet skill structure is valid")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
