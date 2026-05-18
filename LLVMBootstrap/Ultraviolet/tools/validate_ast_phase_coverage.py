#!/usr/bin/env python3
"""Validate AST expression coverage in compiler phase dispatchers."""

from __future__ import annotations

import argparse
import pathlib
import re
import sys


TYPE_CONTEXTUAL_ONLY = {
    # Splice nodes are validated inside quote/comptime-specific walkers, not as
    # ordinary runtime expression typing cases.
    "SpliceExprNode",
    "SpliceIdentNode",
}

LOWERING_PHASE_ONLY = {
    # Compile-time and metaprogramming forms must be resolved before runtime IR
    # lowering. If one becomes lowerable, move it into LowerExprImpl explicitly.
    "TypeLiteralExpr",
    "QuoteExpr",
    "SpliceExprNode",
    "SpliceIdentNode",
    "CtIfExpr",
    "CtLoopIterExpr",
    # Type-argument call syntax is a surface form consumed by typing/resolution.
    "CallTypeArgsExpr",
}

VARIANT_RE = re.compile(
    r"using\s+ExprNode\s*=\s*std::variant\s*<(?P<body>.*?)>\s*;",
    re.DOTALL,
)
AST_CASE_RE = re.compile(
    r"std::is_same_v\s*<\s*T\s*,\s*ast::([A-Za-z_][A-Za-z0-9_]*)\s*>"
)


def read_utf8(path: pathlib.Path) -> str:
    return path.read_text(encoding="utf-8")


def strip_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)
    text = re.sub(r"//.*", "", text)
    return text


def expr_variants(header: pathlib.Path) -> list[str]:
    text = strip_comments(read_utf8(header))
    match = VARIANT_RE.search(text)
    if not match:
        raise ValueError(f"{header}: unable to find ExprNode variant")
    body = match.group("body")
    variants = re.findall(r"\b[A-Za-z_][A-Za-z0-9_]*\b", body)
    return variants


def extract_lower_dispatch(text: str) -> str:
    match = re.search(
        r"LowerResult\s+LowerExprImpl\s*\([^)]*\)\s*\{",
        text,
    )
    if not match:
        raise ValueError("unable to find LowerExprImpl")
    end = text.find("expr.node);", match.end())
    if end == -1:
        raise ValueError("unable to find LowerExprImpl visit terminator")
    return text[match.start() : end + len("expr.node);")]


def extract_type_dispatch(text: str) -> str:
    match = re.search(
        r"result\s*=\s*std::visit\(\s*\n\s*"
        r"\[&\]\(const auto& node\) -> ExprTypeResult",
        text,
    )
    if not match:
        raise ValueError("unable to find main TypeExpr std::visit dispatcher")
    end = text.find("e->node);", match.end())
    if end == -1:
        raise ValueError("unable to find main TypeExpr visit terminator")
    return text[match.start() : end + len("e->node);")]


def ast_cases(dispatch_text: str) -> set[str]:
    return set(AST_CASE_RE.findall(dispatch_text))


def format_set(values: set[str]) -> str:
    return ", ".join(sorted(values))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-root", type=pathlib.Path, required=True)
    args = parser.parse_args()

    root = args.source_root.resolve()
    expr_header = root / "include" / "02_source" / "ast" / "nodes" / "ast_exprs.h"
    type_expr = root / "src" / "04_analysis" / "typing" / "type_expr.cpp"
    lower_expr = root / "src" / "05_codegen" / "lower" / "expr" / "expr_common.cpp"

    violations: list[str] = []
    try:
        variants = expr_variants(expr_header)
        variant_set = set(variants)
        if len(variants) != len(variant_set):
            duplicates = {name for name in variants if variants.count(name) > 1}
            violations.append(
                f"{expr_header.relative_to(root)}: duplicate ExprNode variants: "
                f"{format_set(duplicates)}"
            )

        type_dispatch = extract_type_dispatch(read_utf8(type_expr))
        lower_dispatch = extract_lower_dispatch(read_utf8(lower_expr))
    except (OSError, UnicodeDecodeError, ValueError) as exc:
        print(f"AST phase coverage validation failed: {exc}", file=sys.stderr)
        return 1

    type_covered = ast_cases(type_dispatch)
    lower_covered = ast_cases(lower_dispatch)

    stale_type_context = TYPE_CONTEXTUAL_ONLY - variant_set
    if stale_type_context:
        violations.append(
            "TYPE_CONTEXTUAL_ONLY contains names that are no longer ExprNode "
            f"variants: {format_set(stale_type_context)}"
        )

    stale_lower_phase_only = LOWERING_PHASE_ONLY - variant_set
    if stale_lower_phase_only:
        violations.append(
            "LOWERING_PHASE_ONLY contains names that are no longer ExprNode "
            f"variants: {format_set(stale_lower_phase_only)}"
        )

    missing_type = variant_set - type_covered - TYPE_CONTEXTUAL_ONLY
    if missing_type:
        violations.append(
            f"{type_expr.relative_to(root)}: main TypeExpr dispatcher is missing "
            f"ExprNode cases: {format_set(missing_type)}"
        )

    missing_lower = variant_set - lower_covered - LOWERING_PHASE_ONLY
    if missing_lower:
        violations.append(
            f"{lower_expr.relative_to(root)}: LowerExprImpl is missing ExprNode "
            f"cases: {format_set(missing_lower)}"
        )

    if '"unknown_expr"' in lower_dispatch:
        violations.append(
            f"{lower_expr.relative_to(root)}: LowerExprImpl must not silently "
            "produce an unknown_expr placeholder"
        )

    fallback = lower_dispatch.rsplit("} else {", 1)
    if len(fallback) != 2 or "ctx.ReportCodegenFailure()" not in fallback[1]:
        violations.append(
            f"{lower_expr.relative_to(root)}: LowerExprImpl final fallback must "
            "report a codegen failure"
        )

    if violations:
        print("AST phase coverage validation failed:", file=sys.stderr)
        for violation in violations:
            print(f"  {violation}", file=sys.stderr)
        return 1

    type_contextual = len((variant_set - type_covered) & TYPE_CONTEXTUAL_ONLY)
    lower_phase_only = len((variant_set - lower_covered) & LOWERING_PHASE_ONLY)
    print(
        "[ast-coverage] PASS: "
        f"expr_variants={len(variant_set)} "
        f"type_covered={len(type_covered & variant_set)} "
        f"type_contextual={type_contextual} "
        f"lower_covered={len(lower_covered & variant_set)} "
        f"lower_phase_only={lower_phase_only}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
