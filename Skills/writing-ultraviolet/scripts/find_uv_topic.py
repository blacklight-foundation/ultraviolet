#!/usr/bin/env python3
"""Map Ultraviolet topic text to skill reference files."""

from __future__ import annotations

import argparse
from collections import OrderedDict


TOPICS: "OrderedDict[str, list[str]]" = OrderedDict([
    ("move ownership responsibility partial drop lifetime", [
        "guide-memory-responsibility-permissions.md",
        "chapter-06-abstract-machine-responsibility-authority-memory.md",
        "chapter-10-permissions-and-binding-state.md",
        "chapter-24-lowering-lifecycle-and-backend.md",
    ]),
    ("context capability authority filesystem network effect stdout", [
        "guide-authority-capabilities-and-effects.md",
        "chapter-06-abstract-machine-responsibility-authority-memory.md",
        "chapter-16-expressions.md",
    ]),
    ("region frame provenance allocation arena caret", [
        "guide-regions-frames-and-provenance.md",
        "chapter-06-abstract-machine-responsibility-authority-memory.md",
        "chapter-18-statements-and-blocks.md",
    ]),
    ("shared key ordering fence speculative release", [
        "guide-keys-shared-access-and-memory-ordering.md",
        "chapter-19-key-system.md",
        "chapter-10-permissions-and-binding-state.md",
    ]),
    ("import using module static extern aggregation visibility", [
        "chapter-11-module-level-forms.md",
        "chapter-07-name-resolution-and-visibility.md",
    ]),
    ("record enum tuple array slice range union alias primitive", [
        "chapter-12-concrete-data-types.md",
        "appendix-b-complete-grammar.md",
    ]),
    ("generic class impl associated dynamic opaque refinement predicate", [
        "chapter-14-abstraction-and-polymorphism.md",
        "chapter-08-type-system.md",
    ]),
    ("procedure receiver method contract invariant test verification main", [
        "guide-procedures-contracts-tests-and-verification.md",
        "chapter-15-procedures-and-contracts.md",
        "chapter-09-attributes.md",
    ]),
    ("call field access method closure operator construction transmute widen", [
        "chapter-16-expressions.md",
        "appendix-b-complete-grammar.md",
    ]),
    ("pattern case exhaustive enum modal if is range destructure", [
        "chapter-17-patterns.md",
        "chapter-13-modal-and-special-types.md",
    ]),
    ("binding assignment defer statement block unsafe return break continue", [
        "chapter-18-statements-and-blocks.md",
        "guide-memory-responsibility-permissions.md",
    ]),
    ("parallel dispatch spawn domain capture cancellation determinism", [
        "guide-async-parallelism-and-cancellation.md",
        "chapter-20-structured-parallelism.md",
    ]),
    ("async yield sync race all suspension state machine", [
        "guide-async-parallelism-and-cancellation.md",
        "chapter-21-async.md",
    ]),
    ("comptime compile-time reflection quote splice derive emit", [
        "chapter-22-compile-time-execution.md",
    ]),
    ("ffi extern export abi foreign unsafe boundary unwind", [
        "guide-unsafe-ffi-runtime-and-lowering.md",
        "chapter-23-ffi.md",
        "chapter-24-lowering-lifecycle-and-backend.md",
    ]),
    ("diagnostic span error warning rendering code", [
        "chapter-02-diagnostics.md",
        "appendix-a-diagnostic-index.md",
    ]),
    ("lowering runtime backend llvm abi cleanup vtable symbol", [
        "chapter-24-lowering-lifecycle-and-backend.md",
        "appendix-d-layout-abi-runtime.md",
    ]),
])


def score(query: str, words: str) -> int:
    q = set(query.lower().replace("-", " ").split())
    return sum(1 for word in words.split() if word in q)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("query", nargs="+")
    args = parser.parse_args()
    query = " ".join(args.query)

    matches: list[tuple[int, str, list[str]]] = []
    for words, refs in TOPICS.items():
        value = score(query, words)
        if value:
            matches.append((value, words, refs))

    matches.sort(key=lambda item: item[0], reverse=True)
    if not matches:
        print("references/chapter-map.md")
        print("references/idioms-common-fixes-and-examples.md")
        return 0

    max_score = matches[0][0]
    selected = matches[:3] if max_score == 1 else [item for item in matches if item[0] == max_score]

    seen: set[str] = set()
    for _, _, refs in selected:
        for ref in refs:
            if ref not in seen:
                seen.add(ref)
                print(f"references/{ref}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
