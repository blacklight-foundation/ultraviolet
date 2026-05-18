#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import re
import stat
import sys
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path

from resolve_spec_path import resolve_spec_path


RULE_PATTERNS = [
    re.compile(r'SPEC_RULE(?:_AT)?\(\s*"([^"]+)"'),
    re.compile(r'\bRecord[A-Za-z0-9_]*Rule\(\s*"([^"]+)"'),
]
DIAG_CODE_PATTERN = re.compile(r"^[EWI]-[A-Z]{3}-[0-9]{4}$")
RULE_HEADER_PATTERN = re.compile(r"^\*\*\(([^)]+)\)\*\*$")
RULE_BAR_PATTERN = re.compile(r"^[─-]{3,}$")
PREMISE_SPLIT_PATTERN = re.compile(r"\s{4,}")


@dataclass
class StaticRuleEntry:
    rule_id: str
    conclusion_family: str
    diag_id: str | None
    source_path: str
    premises_text: str | None
    has_bottom_premise: bool


def normalize_rel_path(base_path: Path, path: Path) -> str:
    return path.resolve().relative_to(base_path.resolve()).as_posix()


def escape_cpp_string(value: str) -> str:
    return (
        value.replace("\\", "\\\\")
        .replace('"', '\\"')
        .replace("\n", "\\n")
        .replace("\r", "\\r")
    )


def write_text_if_changed(path: Path, content: str) -> bool:
    if path.exists() and path.read_text(encoding="utf-8") == content:
        return False
    try:
        path.write_text(content, encoding="utf-8", newline="\n")
    except PermissionError:
        if not path.exists():
            raise
        # Windows source checkouts can leave tracked generated files read-only.
        path.chmod(path.stat().st_mode | stat.S_IWRITE)
        path.write_text(content, encoding="utf-8", newline="\n")
    return True


def check_text_matches(path: Path, content: str) -> bool:
    if not path.exists():
        print(f"Generated output is missing: {path}", file=sys.stderr)
        return False
    if path.read_text(encoding="utf-8") == content:
        return True
    print(f"Generated output is stale: {path}", file=sys.stderr)
    return False


def bootstrap_source_root(repo_root: Path) -> Path:
    direct = repo_root / "Ultraviolet" / "src"
    if direct.exists():
        return direct
    return repo_root / "LLVMBootstrap" / "Ultraviolet" / "src"


def cpp_bool(value: bool) -> str:
    return "true" if value else "false"


def has_bottom_premise(premises: list[str] | None) -> bool:
    if premises is None:
        return False
    return any(premise.strip() == "⊥" for premise in premises)


def extract_rule_ids(content: str) -> list[tuple[str, int]]:
    matches: list[tuple[str, int]] = []
    for pattern in RULE_PATTERNS:
        matches.extend((match.group(1), match.start()) for match in pattern.finditer(content))
    matches.sort(key=lambda entry: entry[1])
    return matches


def parse_static_judgment_families(spec_path: Path) -> list[str]:
    text = spec_path.read_text(encoding="utf-8-sig")
    match = re.search(r"StaticJudgSet\s*=\s*(?P<body>[^\n]+)", text)
    if match is None:
        return []
    return [part.strip() for part in match.group("body").split("∪") if part.strip()]


def run_self_test() -> int:
    cases = [
        (["⊥"], True),
        (["mode = ⊥"], False),
        (["Code(DiagIdOf(J)) = ⊥"], False),
        (["Γ ⊢ T ⇓ τ", "⊥"], True),
        ([], False),
        (None, False),
    ]
    for premises, expected in cases:
        actual = has_bottom_premise(premises)
        if actual != expected:
            print(
                f"Self-test failed for premises={premises!r}: expected {expected}, got {actual}",
                file=sys.stderr,
            )
            return 1
    rule_cases = [
        ('SPEC_RULE("Rule-A")', ["Rule-A"]),
        ('SPEC_RULE_AT("Rule-B", span)', ["Rule-B"]),
        ('SPEC_RULE_AT( "Rule-C", TokSpan(parser))', ["Rule-C"]),
        ('RecordGenericArgsRule("Rule-D", span, "some", 1)', ["Rule-D"]),
    ]
    for text, expected in rule_cases:
        actual = [rule_id for rule_id, _ in extract_rule_ids(text)]
        if actual != expected:
            print(
                f"Self-test failed for rule trace {text!r}: "
                f"expected {expected}, got {actual}",
                file=sys.stderr,
            )
            return 1
    print("[static-rule-registry-self-test] PASS")
    return 0


def parse_spec_rule_premises(spec_path: Path) -> dict[str, list[str]]:
    premises_by_rule: dict[str, list[str]] = {}
    lines = spec_path.read_text(encoding="utf-8-sig").splitlines()
    index = 0

    while index < len(lines):
        match = RULE_HEADER_PATTERN.match(lines[index].strip())
        if match is None:
            index += 1
            continue

        rule_id = match.group(1).strip()
        index += 1
        premise_lines: list[str] = []

        while index < len(lines):
            stripped = lines[index].strip()
            if not stripped:
                index += 1
                continue
            if RULE_BAR_PATTERN.match(stripped):
                break
            premise_lines.append(stripped)
            index += 1

        premises: list[str] = []
        for premise_line in premise_lines:
            premises.extend(
                part.strip()
                for part in PREMISE_SPLIT_PATTERN.split(premise_line)
                if part.strip()
            )
        premises_by_rule[rule_id] = premises

    return premises_by_rule


def resolve_rule_family(
    rule_id: str,
    source_rel: str,
    default_family: str,
    path_family_defaults: list[tuple[re.Pattern[str], str]],
    family_overrides: dict[str, str],
) -> str:
    if rule_id in family_overrides:
        return family_overrides[rule_id]
    for pattern, family in path_family_defaults:
        if pattern.search(source_rel):
            return family
    return default_family


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo-root", default="")
    parser.add_argument("--source-root", default="")
    parser.add_argument("--spec-path", default="")
    parser.add_argument("--mapping-path", default="")
    parser.add_argument("--output-path", default="")
    parser.add_argument("--report-path", default="")
    parser.add_argument("--strict", action="store_true")
    parser.add_argument(
        "--check",
        action="store_true",
        help="validate that output-path is up to date without rewriting it",
    )
    parser.add_argument("--self-test", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.self_test:
        return run_self_test()

    if not args.repo_root:
        print("--repo-root is required unless --self-test is used", file=sys.stderr)
        return 1
    if not args.mapping_path:
        print("--mapping-path is required unless --self-test is used", file=sys.stderr)
        return 1
    if not args.output_path:
        print("--output-path is required unless --self-test is used", file=sys.stderr)
        return 1

    repo_root = Path(args.repo_root).resolve()
    spec_path = Path(args.spec_path).resolve() if args.spec_path else resolve_spec_path(repo_root)
    mapping_path = Path(args.mapping_path).resolve()
    output_path = Path(args.output_path).resolve()
    report_path = Path(args.report_path).resolve() if args.report_path else None

    if not repo_root.exists():
        print(f"RepoRoot not found: {repo_root}", file=sys.stderr)
        return 1
    if not spec_path.exists():
        print(f"SpecPath not found: {spec_path}", file=sys.stderr)
        return 1
    if not mapping_path.exists():
        print(f"MappingPath not found: {mapping_path}", file=sys.stderr)
        return 1

    mapping = json.loads(mapping_path.read_text(encoding="utf-8"))
    premises_by_rule = parse_spec_rule_premises(spec_path)
    static_judgment_families = parse_static_judgment_families(spec_path)
    if not static_judgment_families:
        print("Unable to parse StaticJudgSet from canonical specification", file=sys.stderr)
        return 1

    default_family = str(mapping.get("default_family", "")).strip()
    if not default_family:
        print("Mapping file missing default_family", file=sys.stderr)
        return 1

    path_family_defaults = [
        (re.compile(str(entry["regex"])), str(entry["family"]))
        for entry in mapping.get("path_family_defaults", [])
    ]
    family_overrides = {str(key): str(value) for key, value in mapping.get("rule_family_overrides", {}).items()}
    diag_overrides = {str(key): str(value) for key, value in mapping.get("rule_diag_overrides", {}).items()}
    rule_source_overrides = {str(key): str(value) for key, value in mapping.get("rule_source_overrides", {}).items()}

    source_root = Path(args.source_root).resolve() if args.source_root else bootstrap_source_root(repo_root)
    if not source_root.exists():
        print(f"Source root not found: {source_root}", file=sys.stderr)
        return 1

    files = sorted(path for path in source_root.rglob("*") if path.suffix in {".cpp", ".h"} and path.is_file())

    rule_to_entry: dict[str, StaticRuleEntry] = {}
    rule_to_sources: dict[str, list[str]] = {}
    family_conflicts: list[object] = []
    unmapped_rules: list[str] = []
    unknown_families: list[object] = []

    for file_path in files:
        content = file_path.read_text(encoding="utf-8")
        matches = extract_rule_ids(content)
        if not matches:
            continue

        source_rel = normalize_rel_path(source_root, file_path)

        for rule_id, _ in matches:
            family = resolve_rule_family(rule_id, source_rel, default_family, path_family_defaults, family_overrides)
            if not family.strip():
                unmapped_rules.append(rule_id)
                continue

            diag_id = None
            if rule_id in diag_overrides:
                diag_id = diag_overrides[rule_id]
            elif DIAG_CODE_PATTERN.match(rule_id):
                diag_id = rule_id

            existing_sources = rule_to_sources.setdefault(rule_id, [])
            if source_rel not in existing_sources:
                existing_sources.append(source_rel)

            if rule_id in rule_to_entry:
                continue

            premises = premises_by_rule.get(rule_id)
            rule_to_entry[rule_id] = StaticRuleEntry(
                rule_id=rule_id,
                conclusion_family=family,
                diag_id=diag_id,
                source_path=source_rel,
                premises_text=None if premises is None else "\n".join(premises),
                has_bottom_premise=has_bottom_premise(premises),
            )

    invalid_source_overrides: list[str] = []
    applied_source_overrides: dict[str, str] = {}

    for rule_id, preferred_source in rule_source_overrides.items():
        if rule_id not in rule_to_entry:
            invalid_source_overrides.append(f"{rule_id}:missing-rule")
            continue
        if preferred_source not in rule_to_sources.get(rule_id, []):
            invalid_source_overrides.append(f"{rule_id}:missing-source:{preferred_source}")
            continue

        entry = rule_to_entry[rule_id]
        if entry.source_path != preferred_source:
            rule_to_entry[rule_id] = StaticRuleEntry(
                rule_id=entry.rule_id,
                conclusion_family=resolve_rule_family(
                    rule_id,
                    preferred_source,
                    default_family,
                    path_family_defaults,
                    family_overrides,
                ),
                diag_id=entry.diag_id,
                source_path=preferred_source,
                premises_text=entry.premises_text,
                has_bottom_premise=entry.has_bottom_premise,
            )
        applied_source_overrides[rule_id] = preferred_source

    sorted_rule_ids = sorted(rule_to_entry)
    static_judgment_family_set = set(static_judgment_families)
    for rule_id in sorted_rule_ids:
        entry = rule_to_entry[rule_id]
        if entry.conclusion_family not in static_judgment_family_set:
            unknown_families.append(
                {
                    "rule_id": rule_id,
                    "family": entry.conclusion_family,
                    "source_path": entry.source_path,
                }
            )

    lines = [
        "// Auto-generated by ultraviolet/tools/generate_static_rule_registry.py",
        "// DO NOT EDIT MANUALLY.",
        "static constexpr std::string_view kStaticJudgmentFamilies[] = {",
    ]
    for family in static_judgment_families:
        lines.append(f'    "{escape_cpp_string(family)}",')
    lines.extend([
        "};",
        "",
        "static const StaticRuleMeta kStaticRules[] = {",
    ])
    for rule_id in sorted_rule_ids:
        entry = rule_to_entry[rule_id]
        diag_field = "std::nullopt"
        if entry.diag_id:
            diag_field = f'std::string_view("{escape_cpp_string(entry.diag_id)}")'
        premises_field = "std::nullopt"
        if entry.premises_text is not None:
            premises_field = f'std::string_view("{escape_cpp_string(entry.premises_text)}")'
        lines.append(
            '    {{"{0}", "{1}", {2}, "{3}", {4}, {5}}},'.format(
                escape_cpp_string(entry.rule_id),
                escape_cpp_string(entry.conclusion_family),
                diag_field,
                escape_cpp_string(entry.source_path),
                premises_field,
                cpp_bool(entry.has_bottom_premise),
            )
        )
    lines.append("};")
    output_content = "\n".join(lines) + "\n"

    if args.check:
        output_current = check_text_matches(output_path, output_content)
    else:
        output_path.parent.mkdir(parents=True, exist_ok=True)
        write_text_if_changed(output_path, output_content)
        output_current = True

    duplicate_rule_ids = []
    for rule_id in sorted_rule_ids:
        sources = rule_to_sources[rule_id]
        if len(sources) > 1 and rule_id not in applied_source_overrides:
            duplicate_rule_ids.append(
                {
                    "rule_id": rule_id,
                    "source_count": len(sources),
                    "sources": sources,
                }
            )

    report = {
        "generated_at": datetime.now(timezone.utc).astimezone().isoformat(),
        "source_root": str(source_root),
        "static_judgment_family_count": len(static_judgment_families),
        "static_judgment_families": static_judgment_families,
        "rule_count": len(sorted_rule_ids),
        "unique_rule_count": len(sorted_rule_ids),
        "rules_missing_premises": [
            rule_id
            for rule_id in sorted_rule_ids
            if rule_to_entry[rule_id].premises_text is None
        ],
        "rules_with_bottom_premise": [
            rule_id
            for rule_id in sorted_rule_ids
            if rule_to_entry[rule_id].has_bottom_premise
        ],
        "duplicate_rule_ids": duplicate_rule_ids,
        "applied_source_overrides": applied_source_overrides,
        "invalid_source_overrides": invalid_source_overrides,
        "family_conflicts": family_conflicts,
        "unmapped_rules": unmapped_rules,
        "unknown_static_judgment_families": unknown_families,
        "output_path": str(output_path),
        "check_mode": args.check,
        "output_current": output_current,
    }

    if report_path is not None:
        report_path.parent.mkdir(parents=True, exist_ok=True)
        report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8", newline="\n")

    if args.strict:
        if unmapped_rules:
            print(f"Strict mode failed: unmapped rules detected ({len(unmapped_rules)}).", file=sys.stderr)
            return 1
        if invalid_source_overrides:
            joined = ", ".join(invalid_source_overrides)
            print(f"Strict mode failed: invalid source overrides detected ({joined}).", file=sys.stderr)
            return 1
        if unknown_families:
            print(
                f"Strict mode failed: unknown static judgment families detected ({len(unknown_families)}).",
                file=sys.stderr,
            )
            return 1

    if args.check and not output_current:
        return 1

    print(
        "[static-rule-registry] "
        f"rules={len(sorted_rule_ids)} "
        f"families={len(static_judgment_families)} "
        f"bottom_rules={len(report['rules_with_bottom_premise'])} "
        f"unmapped={len(unmapped_rules)} "
        f"conflicts={len(family_conflicts)}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
