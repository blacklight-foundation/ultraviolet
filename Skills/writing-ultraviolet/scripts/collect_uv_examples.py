#!/usr/bin/env python3
"""Report HelloUltraviolet reference corpus coverage for this skill."""

from __future__ import annotations

from pathlib import Path


CORPUS_TO_REFERENCES = {
    "Async": ["chapter-21-async.md", "guide-async-parallelism-and-cancellation.md"],
    "Attributes": ["chapter-09-attributes.md"],
    "Authority": ["chapter-06-abstract-machine-responsibility-authority-memory.md", "guide-authority-capabilities-and-effects.md"],
    "Comptime": ["chapter-22-compile-time-execution.md"],
    "Conformance": ["chapter-01-conformance-and-notation.md"],
    "DataTypes": ["chapter-12-concrete-data-types.md"],
    "Diagnostics": ["chapter-02-diagnostics.md", "appendix-a-diagnostic-index.md"],
    "Expressions": ["chapter-16-expressions.md"],
    "FFI": ["chapter-23-ffi.md", "guide-unsafe-ffi-runtime-and-lowering.md"],
    "Keys": ["chapter-19-key-system.md", "guide-keys-shared-access-and-memory-ordering.md"],
    "Lowering": ["chapter-24-lowering-lifecycle-and-backend.md", "appendix-d-layout-abi-runtime.md"],
    "ModalTypes": ["chapter-13-modal-and-special-types.md", "guide-modal-resource-lifecycle.md"],
    "Modules": ["chapter-11-module-level-forms.md"],
    "Names": ["chapter-07-name-resolution-and-visibility.md"],
    "Parallelism": ["chapter-20-structured-parallelism.md", "guide-async-parallelism-and-cancellation.md"],
    "Parsing": ["chapter-05-parsing-and-ast.md", "appendix-b-complete-grammar.md", "appendix-c-ast-form-index.md"],
    "Patterns": ["chapter-17-patterns.md"],
    "Permissions": ["chapter-10-permissions-and-binding-state.md", "guide-memory-responsibility-permissions.md"],
    "Polymorphism": ["chapter-14-abstraction-and-polymorphism.md"],
    "Procedures": ["chapter-15-procedures-and-contracts.md", "guide-procedures-contracts-tests-and-verification.md"],
    "Projects": ["chapter-03-project-and-compilation.md"],
    "SourceText": ["chapter-04-source-text-and-lexical-structure.md"],
    "Statements": ["chapter-18-statements-and-blocks.md", "guide-regions-frames-and-provenance.md"],
    "Types": ["chapter-08-type-system.md"],
}


def repo_root() -> Path:
    return Path(__file__).resolve().parents[3]


def main() -> int:
    root = repo_root()
    corpus = root / "HelloUltraviolet" / "Source" / "Reference"
    if not corpus.exists():
        print(f"[WARN] reference corpus not found: {corpus}")
        return 0

    for folder in sorted(CORPUS_TO_REFERENCES):
        path = corpus / folder
        files = sorted(path.rglob("*.uv")) if path.exists() else []
        refs = ", ".join(CORPUS_TO_REFERENCES[folder])
        status = "OK" if files else "MISSING"
        print(f"{status} {folder}: {len(files)} files -> {refs}")
        for file_path in files[:5]:
            print(f"  {file_path.relative_to(root)}")
        if len(files) > 5:
            print(f"  ... {len(files) - 5} more")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
