#!/usr/bin/env python3

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

from resolve_spec_path import resolve_spec_path


DIAG_CODE_PATTERN = re.compile(r"^[EWIP]-[A-Z]{3,4}-[0-9]{4}$")
DIAG_ID_REF_PATTERN = re.compile(r"Code\((?P<id>[A-Za-z][A-Za-z0-9\-]*)\)")
EXISTING_MAP_ENTRY_PATTERN = re.compile(r'\{"(?P<diag>[^"]+)",\s*"(?P<code>[EWIP]-[A-Z]{3,4}-[0-9]{4})"\}')


def normalize_cell(value: str) -> str:
    normalized = value.strip()
    if (
        normalized.startswith("`")
        and normalized.endswith("`")
        and len(normalized) >= 2
        and "`" not in normalized[1:-1]
    ):
        normalized = normalized[1:-1]
    return normalized.strip()


def escape_cpp_string(value: str) -> str:
    return value.replace("\\", "\\\\").replace('"', '\\"')


def read_existing_diag_map(paths: list[Path]) -> dict[str, str]:
    result: dict[str, str] = {}
    for path in paths:
        if not path.exists():
            continue
        text = path.read_text(encoding="utf-8")
        for match in EXISTING_MAP_ENTRY_PATTERN.finditer(text):
            diag = match.group("diag")
            code = match.group("code")
            result.setdefault(diag, code)
    return result


def check_text_matches(path: Path, content: str) -> bool:
    if not path.exists():
        print(f"Generated output is missing: {path}", file=sys.stderr)
        return False
    if path.read_text(encoding="utf-8") == content:
        return True
    print(f"Generated output is stale: {path}", file=sys.stderr)
    return False


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo-root", default="")
    parser.add_argument("--source-root", default="")
    parser.add_argument("--spec-path", default="")
    parser.add_argument("--output-registry-path", default="")
    parser.add_argument("--output-typecheck-map-path", default="")
    parser.add_argument(
        "--check",
        action="store_true",
        help="validate generated outputs are up to date without rewriting them",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()

    repo_root = Path(args.repo_root).resolve() if args.repo_root else Path(__file__).resolve().parents[2]
    spec_path = Path(args.spec_path).resolve() if args.spec_path else resolve_spec_path(repo_root)
    output_registry_path = (
        Path(args.output_registry_path).resolve()
        if args.output_registry_path
        else repo_root / "cursive" / "src" / "00_core" / "generated" / "diag_registry.inc"
    )
    output_typecheck_map_path = (
        Path(args.output_typecheck_map_path).resolve()
        if args.output_typecheck_map_path
        else repo_root / "cursive" / "src" / "04_analysis" / "typing" / "item" / "typecheck_diag_map.inc"
    )

    if not spec_path.exists():
        print(f"Spec file not found: {spec_path}", file=sys.stderr)
        return 1

    rows_by_code: dict[str, dict[str, str]] = {}
    lines = spec_path.read_text(encoding="utf-8-sig").splitlines()
    inside_diag_table = False
    skip_next_separator = False

    for line in lines:
        if not inside_diag_table:
            if re.match(r"^\|\s*Code\s*\|\s*Severity\s*\|\s*Detection\s*\|\s*Condition\s*\|\s*$", line):
                inside_diag_table = True
                skip_next_separator = True
            continue

        if skip_next_separator:
            skip_next_separator = False
            continue

        if not re.match(r"^\s*\|", line):
            inside_diag_table = False
            continue

        parts = line.split("|")
        if len(parts) < 5:
            continue

        code = normalize_cell(parts[1])
        if not DIAG_CODE_PATTERN.match(code):
            continue

        severity = normalize_cell(parts[2])
        condition = normalize_cell(parts[4])
        rows_by_code[code] = {"code": code, "severity": severity, "condition": condition}

    if not rows_by_code:
        print(f"No diagnostic rows were parsed from {spec_path}.", file=sys.stderr)
        return 1

    map_by_diag_id = {code: code for code in rows_by_code}

    existing_map = read_existing_diag_map([output_registry_path, output_typecheck_map_path])
    for diag_id, code in existing_map.items():
        if code in rows_by_code and diag_id not in map_by_diag_id:
            map_by_diag_id[diag_id] = code

    explicit_map = {
        "Index-Array-NonConst-Err": "E-UNS-0102",
        "Index-Array-NonUsize": "E-TYP-1812",
        "Index-Array-OOB-Err": "E-UNS-0103",
        "Index-NonIndexable": "E-SEM-2527",
        "Index-Slice-NonUsize": "E-TYP-1820",
        "If-Branch-Mismatch": "E-MOD-2402",
        "IfCase-Branch-Mismatch": "E-MOD-2402",
        "IfCase-Enum-NonExhaustive": "E-SEM-2741",
        "IfCase-Modal-NonExhaustive": "E-TYP-2060",
        "IfCase-NonExhaustive": "E-SEM-2741",
        "IfCase-Unreachable": "E-SEM-2751",
        "TupleAccess-NotTuple": "E-SEM-2524",
        "TupleIndex-OOB": "E-TYP-1801",
        "T-Cast-Invalid": "E-SEM-2528",
        "IfCase-Union-NonExhaustive": "E-SEM-2705",
        "ValueUse-NonBitcopyPlace": "E-UNS-0107",
    }
    removed_diag_ids: set[str] = set()

    spec_text = spec_path.read_text(encoding="utf-8-sig")
    spec_diag_ids = {match.group("id") for match in DIAG_ID_REF_PATTERN.finditer(spec_text)}

    for diag_id, code in explicit_map.items():
        if code not in rows_by_code:
            print(f"Explicit diagnostic mapping references unknown code: {diag_id} -> {code}", file=sys.stderr)
            return 1
        if (
            diag_id in spec_diag_ids
            or diag_id.startswith("If")
            or diag_id.startswith("Index-")
            or diag_id.startswith("Tuple")
            or diag_id == "T-Cast-Invalid"
        ):
            map_by_diag_id[diag_id] = code

    for removed_diag_id in removed_diag_ids:
        map_by_diag_id.pop(removed_diag_id, None)

    ordered_rows = [rows_by_code[code] for code in sorted(rows_by_code)]
    ordered_map = sorted(map_by_diag_id.items(), key=lambda item: item[0])

    registry_lines = [
        "// ===========================================================================",
        "// diag_registry.inc - Diagnostic Registry (generated from canonical language spec)",
        "// ===========================================================================",
        "// AUTO-GENERATED. Do not edit manually.",
        "// Source: canonical language specification",
        "// ===========================================================================",
        "",
        "struct DiagRegistryRow {",
        "  const char* code;",
        "  const char* severity;",
        "  const char* condition;",
        "};",
        "",
        "struct DiagIdCodeEntry {",
        "  const char* diag_id;",
        "  const char* code;",
        "};",
        "",
        "static constexpr DiagRegistryRow kDiagRegistryRows[] = {",
    ]
    for row in ordered_rows:
        registry_lines.append(
            '  {{"{0}", "{1}", "{2}"}},'.format(
                escape_cpp_string(row["code"]),
                escape_cpp_string(row["severity"]),
                escape_cpp_string(row["condition"]),
            )
        )
    registry_lines.extend(["};", "", "static constexpr DiagIdCodeEntry kDiagIdCodeMapEntries[] = {"])
    for diag_id, code in ordered_map:
        registry_lines.append(
            '  {{"{0}", "{1}"}},'.format(
                escape_cpp_string(diag_id),
                escape_cpp_string(code),
            )
        )
    registry_lines.append("};")
    registry_content = "\n".join(registry_lines) + "\n"

    typecheck_lines = [
        "// ===========================================================================",
        "// typecheck_diag_map.inc - Typecheck Diagnostic ID to Code Mapping",
        "// ===========================================================================",
        "// AUTO-GENERATED from the canonical language specification. Do not edit manually.",
        "// ===========================================================================",
        "",
        "static const DiagMapEntry kTypecheckDiagMap[] = {",
    ]
    for diag_id, code in ordered_map:
        typecheck_lines.append(
            '  {{"{0}", "{1}"}},'.format(
                escape_cpp_string(diag_id),
                escape_cpp_string(code),
            )
        )
    typecheck_lines.extend(["  {nullptr, nullptr},", "};"])
    typecheck_content = "\n".join(typecheck_lines) + "\n"

    if args.check:
        registry_current = check_text_matches(output_registry_path, registry_content)
        typecheck_current = check_text_matches(output_typecheck_map_path, typecheck_content)
        if not registry_current or not typecheck_current:
            return 1
    else:
        output_registry_path.parent.mkdir(parents=True, exist_ok=True)
        output_typecheck_map_path.parent.mkdir(parents=True, exist_ok=True)
        output_registry_path.write_text(registry_content, encoding="utf-8", newline="\n")
        output_typecheck_map_path.write_text(typecheck_content, encoding="utf-8", newline="\n")

    print(f"[diag-registry] rows={len(ordered_rows)} map_entries={len(ordered_map)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
