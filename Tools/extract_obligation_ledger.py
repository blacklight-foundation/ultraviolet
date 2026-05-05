#!/usr/bin/env python3
from __future__ import annotations

import argparse
import csv
import re
import sys
from dataclasses import dataclass
from pathlib import Path


OPEN_MARKER = "<!-- ULTRAVIOLET-SPEC-UNIT"
CLOSE_MARKER = "<!-- /ULTRAVIOLET-SPEC-UNIT -->"
LEGACY_PATTERNS = (
    "Cursive",
    "CURSIVE",
    ".cursive",
    "Cursive.toml",
    "source.cursive",
    "```cursive",
    "cursive::",
    "cursive0",
)
LEGACY_WORD_PATTERN = re.compile(r"(?<![A-Za-z])cursive(?![A-Za-z])")
OPEN_BLOCK_PATTERN = re.compile(r"(?ms)^<!-- ULTRAVIOLET-SPEC-UNIT\n(?P<body>.*?)^-->\n?")


@dataclass(frozen=True)
class Obligation:
    index: int
    obligation_id: str
    kind: str
    phase: str
    strength: str
    owner: str
    applies_to: str
    summary: str
    marker_line: int


def fail(message: str) -> int:
    print(f"[obligation-ledger] FAIL: {message}", file=sys.stderr)
    return 1


def parse_metadata(block_body: str) -> dict[str, str]:
    metadata: dict[str, str] = {}
    for raw_line in block_body.splitlines():
        if not raw_line.strip():
            continue
        if ":" not in raw_line:
            continue
        key, value = raw_line.split(":", 1)
        metadata[key.strip()] = value.strip()
    return metadata


def marker_line_for_offset(text: str, offset: int) -> int:
    return text.count("\n", 0, offset) + 1


def extract_obligations(text: str) -> list[Obligation]:
    obligations: list[Obligation] = []
    required_keys = ("id", "kind", "phase", "strength", "owner", "applies-to", "summary")
    for index, match in enumerate(OPEN_BLOCK_PATTERN.finditer(text), start=1):
        metadata = parse_metadata(match.group("body"))
        missing = [key for key in required_keys if key not in metadata]
        if missing:
            raise ValueError(
                f"marker at line {marker_line_for_offset(text, match.start())} "
                f"is missing metadata keys: {', '.join(missing)}"
            )
        obligations.append(
            Obligation(
                index=index,
                obligation_id=metadata["id"],
                kind=metadata["kind"],
                phase=metadata["phase"],
                strength=metadata["strength"],
                owner=metadata["owner"],
                applies_to=metadata["applies-to"],
                summary=metadata["summary"],
                marker_line=marker_line_for_offset(text, match.start()),
            )
        )
    return obligations


def validate_no_legacy_names(path: Path, text: str) -> list[str]:
    errors: list[str] = []
    for pattern in LEGACY_PATTERNS:
        if pattern in text:
            errors.append(f"{path}: contains legacy token {pattern!r}")
    if LEGACY_WORD_PATTERN.search(text):
        errors.append(f"{path}: contains standalone lowercase legacy token 'cursive'")
    return errors


def validate_public_spec(path: Path) -> list[str]:
    text = path.read_text(encoding="utf-8-sig")
    errors: list[str] = []
    if OPEN_MARKER in text or CLOSE_MARKER in text:
        errors.append(f"{path}: public spec must not contain internal obligation markers")
    if "CURSIVE-SPEC-UNIT" in text:
        errors.append(f"{path}: public spec must not contain legacy obligation markers")
    return errors


def write_ledger(path: Path, obligations: list[Obligation]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle, lineterminator="\n")
        writer.writerow(
            [
                "index",
                "id",
                "kind",
                "phase",
                "strength",
                "owner",
                "applies_to",
                "summary",
                "internal_spec_line",
            ]
        )
        for obligation in obligations:
            writer.writerow(
                [
                    obligation.index,
                    obligation.obligation_id,
                    obligation.kind,
                    obligation.phase,
                    obligation.strength,
                    obligation.owner,
                    obligation.applies_to,
                    obligation.summary,
                    obligation.marker_line,
                ]
            )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Extract the internal Ultraviolet spec obligation metadata into "
            "the compiler conformance ledger."
        )
    )
    parser.add_argument("--public-spec", default="SPECIFICATION.md")
    parser.add_argument(
        "--internal-spec",
        default="docs/internal/UltravioletSpecification.obligations.md",
    )
    parser.add_argument(
        "--ledger",
        default="docs/audit/ULTRAVIOLET_OBLIGATIONS.csv",
        help="CSV obligation ledger path to write",
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="check that the existing ledger matches the internal spec without rewriting it",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    public_spec = Path(args.public_spec)
    internal_spec = Path(args.internal_spec)
    ledger = Path(args.ledger)

    if not public_spec.exists():
        return fail(f"public specification not found: {public_spec}")
    if not internal_spec.exists():
        return fail(f"internal obligation specification not found: {internal_spec}")

    errors = validate_public_spec(public_spec)
    text = internal_spec.read_text(encoding="utf-8-sig")
    errors.extend(validate_no_legacy_names(internal_spec, text))

    open_count = text.count(OPEN_MARKER)
    close_count = text.count(CLOSE_MARKER)
    if open_count == 0:
        errors.append(f"{internal_spec}: no internal obligation markers found")
    if open_count != close_count:
        errors.append(
            f"{internal_spec}: unbalanced markers open={open_count} close={close_count}"
        )
    if "CURSIVE-SPEC-UNIT" in text:
        errors.append(f"{internal_spec}: contains legacy CURSIVE-SPEC-UNIT marker")

    try:
        obligations = extract_obligations(text)
    except ValueError as exc:
        errors.append(str(exc))
        obligations = []

    if obligations and len(obligations) != open_count:
        errors.append(
            f"{internal_spec}: parsed {len(obligations)} obligations from {open_count} markers"
        )

    if errors:
        for error in errors:
            print(f"[obligation-ledger] {error}", file=sys.stderr)
        return 1

    if args.check:
        if not ledger.exists():
            return fail(f"ledger not found: {ledger}")
        before = ledger.read_text(encoding="utf-8")
        import io

        buffer = io.StringIO()
        writer = csv.writer(buffer, lineterminator="\n")
        writer.writerow(
            [
                "index",
                "id",
                "kind",
                "phase",
                "strength",
                "owner",
                "applies_to",
                "summary",
                "internal_spec_line",
            ]
        )
        for obligation in obligations:
            writer.writerow(
                [
                    obligation.index,
                    obligation.obligation_id,
                    obligation.kind,
                    obligation.phase,
                    obligation.strength,
                    obligation.owner,
                    obligation.applies_to,
                    obligation.summary,
                    obligation.marker_line,
                ]
            )
        if before != buffer.getvalue():
            return fail(f"ledger is stale: {ledger}")
    else:
        write_ledger(ledger, obligations)

    print(
        f"[obligation-ledger] PASS obligations={len(obligations)} "
        f"public_spec={public_spec} internal_spec={internal_spec}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
