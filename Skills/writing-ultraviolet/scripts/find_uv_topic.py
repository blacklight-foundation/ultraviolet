#!/usr/bin/env python3
"""Route prompt text to Ultraviolet primary cards and spec anchors."""

from __future__ import annotations

import argparse
import json
import re
from dataclasses import dataclass


SYNONYMS = {
    "alloc": "allocation",
    "allocates": "allocation",
    "bytestring": "bytes",
    "characters": "text",
    "chars": "text",
    "conditions": "condition",
    "contracts": "contract",
    "fields": "field",
    "indexes": "index",
    "indices": "index",
    "invariants": "invariant",
    "iterates": "loop",
    "iterating": "loop",
    "loops": "loop",
    "matches": "match",
    "matching": "match",
    "permissions": "permission",
    "records": "record",
    "regions": "region",
    "retries": "retry",
    "slices": "slice",
    "states": "state",
    "strings": "string",
    "transitions": "transition",
    "variants": "variant",
}


@dataclass(frozen=True)
class CardRoute:
    card: str
    reference: str
    keywords: tuple[str, ...]
    realization_sections: tuple[str, ...]
    spec_terms: tuple[str, ...]


CARDS = [
    CardRoute(
        "Form",
        "references/form-card.md",
        (
            "form", "create", "construct", "declare", "record", "enum", "modal", "class",
            "type", "literal", "procedure", "attribute", "project", "manifest", "quote",
            "splice", "derive", "extern", "variant", "expression", "statement", "call",
            "invoke", "operation", "write", "output",
        ),
        (
            "Procedure Shell", "Record Shape", "Enum Choice Shape", "Modal Lifecycle Shape",
            "String View Shape", "Bytes Inspection Shape",
        ),
        (
            "top_level_item", "primary_expr", "record_decl", "enum_decl", "modal_decl",
            "attribute", "quote_expr",
        ),
    ),
    CardRoute(
        "Access",
        "references/access-card.md",
        (
            "access", "read", "observe", "inspect", "project", "field", "index", "slice",
            "match", "pattern", "payload", "receiver", "method", "import", "using", "path",
            "bytes", "string", "view", "borrow", "shared",
        ),
        (
            "Pattern Choice", "Method Call Shape", "String View Shape", "Bytes Inspection Shape",
            "Record Shape",
        ),
        (
            "postfix_expr", "postfix_suffix", "if_case_pattern", "pattern",
            "bytes::as_slice", "string::slice", "receiver",
        ),
    ),
    CardRoute(
        "Change",
        "references/change-card.md",
        (
            "change", "update", "assign", "assignment", "mutate", "var", "transition",
            "state", "lifecycle", "accumulate", "count", "append", "move", "consume",
            "release", "allocation", "heap", "region", "responsibility", "key", "sync",
            "store", "retry",
        ),
        (
            "Accumulator Shape", "Modal Lifecycle Shape", "Dynamic Verification Shape",
            "Effect Boundary Shape",
        ),
        (
            "assignment", "transition", "modal_decl", "permission", "responsibility",
            "HeapAllocator", "region", "key",
        ),
    ),
    CardRoute(
        "Flow",
        "references/flow-card.md",
        (
            "flow", "sequence", "block", "branch", "choose", "if", "else", "case",
            "loop", "repeat", "scan", "traverse", "return", "break", "yield", "wait",
            "join", "async", "parallel", "dispatch", "cancel", "retry", "until",
        ),
        (
            "Conditional Choice", "Pattern Choice", "Repeat Shape", "Accumulator Shape",
            "Procedure Shell",
        ),
        (
            "block_expr", "statement", "if_expr", "loop_expr", "return", "break",
            "async", "parallel", "dispatch",
        ),
    ),
    CardRoute(
        "Envelope",
        "references/envelope-card.md",
        (
            "envelope", "scope", "visibility", "public", "private", "internal",
            "permission", "authority", "capability", "context", "heap", "io", "unsafe",
            "ffi", "lifetime", "region", "frame", "provenance", "contract", "invariant",
            "refinement", "dynamic", "static", "[[dynamic]]", "attribute", "gpu", "cpu",
            "domain", "key", "memory", "abi", "layout", "lowering", "modal", "state",
        ),
        (
            "Dynamic Verification Shape", "Effect Boundary Shape", "Unsafe Wrapper Shape",
            "Modal Lifecycle Shape",
        ),
        (
            "visibility", "permission", "authority", "capability", "region", "frame",
            "contract", "invariant", "refinement", "[[dynamic]]", "unsafe", "ffi",
            "execution domain", "key",
        ),
    ),
]


def normalize(token: str) -> str:
    token = token.lower().strip("_:-")
    if token in SYNONYMS:
        return SYNONYMS[token]
    if token.endswith("ies") and len(token) > 4:
        return token[:-3] + "y"
    if token.endswith("ing") and len(token) > 5:
        return token[:-3]
    if token.endswith("ed") and len(token) > 4:
        return token[:-2]
    if token.endswith("s") and len(token) > 3 and token not in {"bytes"}:
        return token[:-1]
    return token


def words(query: str) -> set[str]:
    raw = re.findall(r"[A-Za-z][A-Za-z0-9_:-]*|\[\[dynamic\]\]|~>|::|\[[^\]]+\]", query)
    result: set[str] = set()
    for token in raw:
        lower = token.lower()
        result.add(lower)
        result.add(normalize(lower))

    lowered = query.lower()
    if "csv" in lowered or "json" in lowered:
        result.update({"access", "string", "bytes", "loop", "change", "form"})
    if "retry" in lowered:
        result.update({"loop", "flow", "state", "change", "envelope"})
    if "[[dynamic]]" in lowered:
        result.update({"dynamic", "runtime", "verify", "envelope"})
    if "~>" in query:
        result.update({"method", "receiver", "access", "form"})
    if "::" in query:
        result.update({"path", "access", "form"})
    return result


def score(query_words: set[str], route: CardRoute) -> int:
    value = 0
    for keyword in route.keywords:
        normalized = normalize(keyword)
        if keyword in query_words or normalized in query_words:
            value += 2
    for term in route.spec_terms:
        lowered = term.lower()
        if lowered in query_words or normalize(lowered) in query_words:
            value += 1

    if route.card == "Envelope" and {"authority", "capability", "permission", "contract", "dynamic", "region", "ffi"} & query_words:
        value += 3
    if route.card == "Flow" and {"loop", "if", "return", "break", "async", "parallel"} & query_words:
        value += 3
    if route.card == "Change" and {"state", "transition", "assign", "append", "move", "allocation"} & query_words:
        value += 3
    if route.card == "Access" and {"field", "index", "slice", "match", "bytes", "receiver"} & query_words:
        value += 3
    if route.card == "Form" and {"record", "enum", "modal", "procedure", "type", "attribute"} & query_words:
        value += 3
    return value


def build_matches(query: str, include_all: bool) -> list[dict[str, object]]:
    query_words = words(query)
    matches: list[dict[str, object]] = []
    for route in CARDS:
        value = score(query_words, route)
        if value:
            matches.append(
                {
                    "score": value,
                    "card": route.card,
                    "references": [route.reference, "references/uv-realization-index.md"],
                    "realization_sections": list(route.realization_sections),
                    "spec_search_terms": list(route.spec_terms),
                }
            )
    if not matches:
        return [
            {
                "score": 0,
                "card": "Form",
                "references": ["references/form-card.md", "references/envelope-card.md", "references/uv-realization-index.md"],
                "realization_sections": ["Procedure Shell", "Composition Checklist"],
                "spec_search_terms": ["top_level_item", "expression", "statement"],
            }
        ]
    matches.sort(key=lambda item: int(item["score"]), reverse=True)
    return matches if include_all else matches[:5]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("query", nargs="+")
    parser.add_argument("--all", action="store_true", help="print all positive card matches")
    parser.add_argument("--json", action="store_true", help="print machine-readable matches")
    args = parser.parse_args()

    query = " ".join(args.query)
    matches = build_matches(query, args.all)

    if args.json:
        print(json.dumps({"query": query, "query_words": sorted(words(query)), "matches": matches}, indent=2))
    else:
        for match in matches:
            print(f"{match['card']} (score {match['score']})")
            print("  references: " + ", ".join(match["references"]))
            print("  realization sections: " + ", ".join(match["realization_sections"]))
            print("  spec search terms: " + ", ".join(match["spec_search_terms"]))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
