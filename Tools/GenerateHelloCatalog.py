#!/usr/bin/env python3
"""Regenerate HelloUltraviolet's generated obligation catalog source."""

from __future__ import annotations

import csv
import pathlib
import re
import time
from collections import defaultdict
from dataclasses import dataclass


ROOT = pathlib.Path(__file__).resolve().parents[1]
CSV_PATH = ROOT / "Docs" / "Audit" / "UltravioletObligations.csv"
CATALOG_ROOT = ROOT / "HelloUltraviolet" / "Source" / "Audit" / "Catalog"
AUDIT_ROOT = ROOT / "HelloUltraviolet" / "Source" / "Audit"
SYMBOL_EXECUTION_ROOT = AUDIT_ROOT / "SymbolExecutions"
FIXTURE_CATALOG_ROOT = AUDIT_ROOT / "FixtureCatalog"
FIXTURE_CATALOG_MODULE = "HelloUltraviolet::Audit::FixtureCatalog"
AUDIT_DIGEST_MODULUS = 1_000_000_007
AUDIT_DIGEST_FACTOR = 257


def write_generated(path: pathlib.Path, text: str) -> None:
    if path.exists() and path.read_text(encoding="utf-8") == text:
        return

    for attempt in range(5):
        try:
            path.write_text(text, encoding="utf-8")
            return
        except OSError:
            if attempt == 4:
                raise
            time.sleep(0.25 * (attempt + 1))


def audit_digest(data: bytes) -> int:
    value = 0
    for byte in data:
        value = ((value * AUDIT_DIGEST_FACTOR) + byte + 1) % AUDIT_DIGEST_MODULUS
    return value


def text_digest(text: str) -> int:
    return audit_digest(text.encode("utf-8"))


def key_digest(keys: list[tuple[str, int]]) -> int:
    canonical = "".join(f"{obligation_id}\t{line}\n" for obligation_id, line in keys)
    return text_digest(canonical)


def target_digest(targets: list[tuple[str, str, str]]) -> int:
    canonical = "".join(
        f"{module_path}\t{symbol}\t{source_path}\n"
        for module_path, symbol, source_path in targets
    )
    return text_digest(canonical)


def pascal_identifier(text: str) -> str:
    pieces = re.findall(r"[A-Za-z0-9]+", text)
    return "".join(piece[:1].upper() + piece[1:] for piece in pieces)


def symbol_execution_group(source_path: str) -> str:
    parts = source_path.split("/")
    if len(parts) >= 3 and parts[0] == "Fixtures" and parts[1] == "RejectedSource":
        return f"FixturesRejectedSource{pascal_identifier(parts[2])}"
    if len(parts) >= 2 and parts[0] == "Fixtures":
        return f"Fixtures{pascal_identifier(parts[1])}"
    if len(parts) >= 3 and parts[0] == "Source" and parts[1] == "Reference":
        return f"Reference{pascal_identifier(parts[2])}"
    if len(parts) >= 2 and parts[0] == "Source" and parts[1] == "Audit":
        return "Audit"
    return pascal_identifier(source_path)


ENTRY_RE = re.compile(
    r'([A-Za-z][A-Za-z0-9]*ObligationEntryMatches)\(\s*"([^"]+)",\s*(\d+)usize,\s*'
    r'"([^"]+)",\s*"([^"]+)",\s*"([^"]+)"\s*\)',
    re.S,
)
COUNT_RE = re.compile(r"public procedure (count[A-Za-z0-9]+Obligations)\(\) -> usize")
VALIDATED_RE = re.compile(
    r"public procedure (validated[A-Za-z0-9]+Obligations)\(\) -> usize"
)
FIXTURE_EXPECTED_RE = re.compile(
    r'expectedDiagnostic(?:Absent)?\(\s*"[^"]*",\s*"([^"]+)"\s*\)',
    re.S,
)
REJECTED_SOURCE_SPECIMEN_EXPECTATION_RE = re.compile(
    r'rejectedSpecimen\(\s*"[^"]*",\s*"[^"]*",\s*"([^"]+)",\s*"[^"]+",\s*'
    r'expectedDiagnostic(?:Absent)?\(\s*"[^"]*",\s*"([^"]+)"\s*\)',
    re.S,
)
SPECIMEN_MATCHES_EXPECTATION_RE = re.compile(
    r'[A-Za-z0-9]+SpecimenMatches\(\s*"[^"]*",\s*"[^"]*",\s*"([^"]+)",\s*'
    r'"[^"]+",\s*"[^"]+",\s*"([^"]+)"\s*\)',
    re.S,
)
DIAGNOSTIC_SOURCE_SPECIMEN_EXPECTATION_RE = re.compile(
    r'diagnostic(?:Absence)?Specimen\(\s*"[^"]*",\s*"[^"]*",\s*"([^"]+)",\s*"[^"]+",\s*'
    r'expectedDiagnostic(?:Absent)?\(\s*"[^"]*",\s*"([^"]+)"\s*\)',
    re.S,
)
ARTIFACT_EXPECTATION_RE = re.compile(
    r'artifactExpectation\(\s*"[^"]*",\s*"([^"]+)"\s*\)',
    re.S,
)
ARTIFACT_PROJECT_SPECIMEN_EXPECTATION_RE = re.compile(
    r'artifactProjectSpecimen\(\s*"[^"]*",\s*"[^"]*",\s*"[^"]*",\s*'
    r'"([^"]+)",\s*"[^"]*",\s*"[^"]*",\s*'
    r'artifactExpectation\(\s*"[^"]*",\s*"([^"]+)"\s*\)',
    re.S,
)
ACCEPTED_PROJECT_EXPECTATION_RE = re.compile(
    r'acceptedProjectExpectation\(\s*"[^"]*",\s*"([^"]+)"\s*\)',
    re.S,
)
ACCEPTED_PROJECT_SPECIMEN_EXPECTATION_RE = re.compile(
    r'acceptedProjectSpecimen\(\s*"[^"]*",\s*"[^"]*",\s*"[^"]*",\s*'
    r'"([^"]+)",\s*"[^"]*",\s*"[^"]*",\s*'
    r'acceptedProjectExpectation\(\s*"[^"]*",\s*"([^"]+)"\s*\)',
    re.S,
)
ACCEPTED_PROJECT_HELPER_RE = re.compile(
    r"internal procedure ([A-Za-z0-9]+ExpectationMatches)\([^{}]*\) -> bool \{"
    r"(.*?)\n\}",
    re.S,
)
ACCEPTED_PROJECT_HELPER_CALL_RE = re.compile(
    r'([A-Za-z0-9]+ExpectationMatches)\(\s*acceptedProjectExpectation\('
    r'\s*"[^"]*",\s*"([^"]+)"\s*\)',
    re.S,
)
ACCEPTED_PROJECT_SPECIMEN_SOURCE_RE = re.compile(
    r'acceptedProjectSpecimen\(\s*"[^"]*",\s*"[^"]*",\s*"[^"]*",\s*"([^"]+)"',
    re.S,
)
FIXTURE_VALIDATED_RE = re.compile(
    r"public procedure (validated[A-Za-z0-9]+FixtureCount)\(\) -> usize"
)


ACCEPTED_HELPER = "acceptedObligationEntryMatches"
ACCEPTED_PROJECT_HELPER = "acceptedProjectObligationEntryMatches"
REJECTED_SOURCE_HELPER = "rejectedSourceObligationEntryMatches"
DIAGNOSTIC_SOURCE_HELPER = "diagnosticSourceObligationEntryMatches"
ARTIFACT_BEHAVIOR_HELPER = "artifactBehaviorObligationEntryMatches"
REFERENCE_MODEL_HELPER = "referenceModelObligationEntryMatches"


@dataclass(frozen=True)
class CsvRow:
    index: int
    obligation_id: str
    internal_spec_line: int


@dataclass(frozen=True)
class ReferenceTarget:
    path: pathlib.Path
    module_path: str
    symbol: str
    source_path: str


@dataclass(frozen=True)
class CatalogEntry:
    row: CsvRow
    target: ReferenceTarget
    helper: str


@dataclass(frozen=True)
class FixtureTarget:
    helper: str
    module_path: str
    symbol: str
    source_path: str


def normalized_existing_target(
    catalog_path: pathlib.Path,
    target: ReferenceTarget,
) -> ReferenceTarget:
    rel_path = catalog_path.relative_to(CATALOG_ROOT).as_posix()
    if rel_path == "ConcreteDataTypes/PrimitiveTypes.uv":
        return ReferenceTarget(
            path=catalog_path,
            module_path="HelloUltraviolet::Reference::DataTypes",
            symbol="runDataTypesPrimitivesReference",
            source_path="Source/Reference/DataTypes/Primitives.uv",
        )
    if rel_path == "ConcreteDataTypes/UnionTypes.uv":
        return ReferenceTarget(
            path=catalog_path,
            module_path="HelloUltraviolet::Reference::DataTypes",
            symbol="runDataTypesUnionsReference",
            source_path="Source/Reference/DataTypes/Unions.uv",
        )
    return target


def async_composition_target(existing_path: pathlib.Path, symbol: str) -> ReferenceTarget:
    return ReferenceTarget(
        path=existing_path,
        module_path="HelloUltraviolet::Reference::Async",
        symbol=symbol,
        source_path="Source/Reference/Async/CompositionForms.uv",
    )


def key_paths_target(existing_path: pathlib.Path) -> ReferenceTarget:
    return ReferenceTarget(
        path=existing_path,
        module_path="HelloUltraviolet::Reference::Keys",
        symbol="runKeysKeyPathsReference",
        source_path="Source/Reference/Keys/KeyPaths.uv",
    )


def generic_parameters_target(existing_path: pathlib.Path) -> ReferenceTarget:
    return ReferenceTarget(
        path=existing_path,
        module_path="HelloUltraviolet::Reference::Polymorphism",
        symbol="runPolymorphismGenericParametersReference",
        source_path="Source/Reference/Polymorphism/GenericParameters.uv",
    )


def data_types_target(topic_file: str, symbol: str, source_file: str) -> ReferenceTarget:
    return ReferenceTarget(
        path=CATALOG_ROOT / "ConcreteDataTypes" / topic_file,
        module_path="HelloUltraviolet::Reference::DataTypes",
        symbol=symbol,
        source_path=f"Source/Reference/DataTypes/{source_file}",
    )


def expressions_control_target(symbol: str) -> ReferenceTarget:
    return ReferenceTarget(
        path=CATALOG_ROOT / "Expressions" / "ControlExpressions.uv",
        module_path="HelloUltraviolet::Reference::Expressions",
        symbol=symbol,
        source_path="Source/Reference/Expressions/Control.uv",
    )


def parsing_reference_model_target(path: pathlib.Path) -> ReferenceTarget:
    return ReferenceTarget(
        path=path,
        module_path="HelloUltraviolet::Audit",
        symbol="runParsingReferenceModels",
        source_path="Source/Audit/ParsingReferenceModels.uv",
    )


def appendix_grammar_target(obligation_id: str, path: pathlib.Path) -> ReferenceTarget | None:
    targets = {
        "grammar.B.1.LexicalGrammar": (
            "HelloUltraviolet::Reference::SourceText",
            "runSourceTextLiteralsReference",
            "Source/Reference/SourceText/Literals.uv",
        ),
        "grammar.B.2.TypeGrammar": (
            "HelloUltraviolet::Reference::ModalTypes",
            "runModalTypesModalDeclarationsReference",
            "Source/Reference/ModalTypes/ModalDeclarations.uv",
        ),
        "req.B.2.ClosureTypeUnionParameterParentheses": (
            "HelloUltraviolet::Reference::ModalTypes",
            "runModalTypesClosuresReference",
            "Source/Reference/ModalTypes/Closures.uv",
        ),
        "grammar.B.2.GenericRefinementModalTypeGrammar": (
            "HelloUltraviolet::Reference::Polymorphism",
            "runPolymorphismGenericParametersReference",
            "Source/Reference/Polymorphism/GenericParameters.uv",
        ),
        "grammar.B.3.ExpressionGrammar": (
            "HelloUltraviolet::Reference::Expressions",
            "runExpressionsClosuresAndPipelinesReference",
            "Source/Reference/Expressions/ClosuresAndPipelines.uv",
        ),
        "req.B.3.ClosureExprUnionParameterParentheses": (
            "HelloUltraviolet::Reference::Expressions",
            "runExpressionsClosuresAndPipelinesReference",
            "Source/Reference/Expressions/ClosuresAndPipelines.uv",
        ),
        "grammar.B.3.ControlAndSpecialExpressionGrammar": (
            "HelloUltraviolet::Reference::Expressions",
            "runExpressionsControlSurfaceReference",
            "Source/Reference/Expressions/Control.uv",
        ),
        "grammar.B.4.PatternGrammar": (
            "HelloUltraviolet::Reference::Patterns",
            "runPatternsBasicPatternsReference",
            "Source/Reference/Patterns/BasicPatterns.uv",
        ),
        "grammar.B.5.StatementGrammar": (
            "HelloUltraviolet::Reference::Statements",
            "runStatementsControlTransferReference",
            "Source/Reference/Statements/ControlTransfer.uv",
        ),
        "grammar.B.6.DeclarationGrammar": (
            "HelloUltraviolet::Reference::Modules",
            "runModulesAggregationReference",
            "Source/Reference/Modules/Aggregation.uv",
        ),
        "grammar.B.7.ContractGrammar": (
            "HelloUltraviolet::Reference::Expressions",
            "runExpressionsLoopControlReference",
            "Source/Reference/Expressions/Control.uv",
        ),
        "grammar.B.8.AttributeGrammar": (
            "HelloUltraviolet::Reference::Attributes",
            "runAttributesSourceNativeTestsReference",
            "Source/Reference/Attributes/SourceNativeTests.uv",
        ),
        "grammar.B.9.KeySystemGrammar": (
            "HelloUltraviolet::Reference::Keys",
            "runKeysKeyPathsReference",
            "Source/Reference/Keys/KeyPaths.uv",
        ),
        "grammar.B.10.ConcurrencyGrammar": (
            "HelloUltraviolet::Reference::Parallelism",
            "runParallelismParallelBlocksReference",
            "Source/Reference/Parallelism/ParallelBlocks.uv",
        ),
        "grammar.B.11.AsyncGrammar": (
            "HelloUltraviolet::Reference::Async",
            "runAsyncCompositionFormsReference",
            "Source/Reference/Async/CompositionForms.uv",
        ),
        "grammar.B.12.MetaprogrammingGrammar": (
            "HelloUltraviolet::Reference::Comptime",
            "runComptimeCompileTimeFormsReference",
            "Source/Reference/Comptime/CompileTimeForms.uv",
        ),
        "grammar.B.13.FFIGrammar": (
            "HelloUltraviolet::Reference::FFI",
            "runFFIExternProceduresReference",
            "Source/Reference/FFI/ExternProcedures.uv",
        ),
        "grammar.B.14.RegionGrammar": (
            "HelloUltraviolet::Reference::Statements",
            "runStatementsRegionReference",
            "Source/Reference/Statements/Region.uv",
        ),
    }
    target = targets.get(obligation_id)
    if target is None:
        return None
    module_path, symbol, source_path = target
    return ReferenceTarget(
        path=path,
        module_path=module_path,
        symbol=symbol,
        source_path=source_path,
    )


def source_native_tests_target() -> ReferenceTarget:
    return ReferenceTarget(
        path=CATALOG_ROOT / "AttributesAndMetadata" / "SourceNativeTestAttributes.uv",
        module_path="HelloUltraviolet::Reference::Attributes",
        symbol="runAttributesSourceNativeTestsReference",
        source_path="Source/Reference/Attributes/SourceNativeTests.uv",
    )


def normalized_row_target(row: CsvRow, target: ReferenceTarget) -> ReferenceTarget:
    map_rows = {
        "rule.21.T-Async-Map",
        "rule.21.EvalSigma-Map-Create",
        "rule.21.EvalSigma-Map-Resume-Yield",
        "rule.21.EvalSigma-Map-Resume-Complete",
        "rule.21.EvalSigma-Map-Resume-Failed",
        "rule.21.Lower-Async-Map",
    }
    filter_rows = {
        "rule.21.T-Async-Filter",
        "rule.21.EvalSigma-Filter-Create",
        "rule.21.EvalSigma-Filter-Resume-Pass",
        "rule.21.EvalSigma-Filter-Resume-Skip",
        "rule.21.EvalSigma-Filter-Resume-Complete",
        "rule.21.Lower-Async-Filter",
    }
    take_rows = {
        "rule.21.T-Async-Take",
        "rule.21.EvalSigma-Take-Create",
        "rule.21.EvalSigma-Take-Resume-Yield",
        "rule.21.EvalSigma-Take-Resume-Done",
        "rule.21.EvalSigma-Take-Resume-Source-Complete",
        "rule.21.Lower-Async-Take",
    }
    fold_rows = {
        "rule.21.T-Async-Fold",
        "rule.21.EvalSigma-Fold-Create",
        "rule.21.EvalSigma-Fold-Resume-Accumulate",
        "rule.21.EvalSigma-Fold-Resume-Complete",
        "rule.21.EvalSigma-Fold-Resume-Failed",
        "rule.21.Lower-Async-Fold",
    }
    chain_rows = {
        "rule.21.T-Async-Chain",
        "rule.21.EvalSigma-Chain-Create",
        "rule.21.EvalSigma-Chain-Resume-Source-Complete",
        "rule.21.EvalSigma-Chain-Resume-Chained",
        "rule.21.EvalSigma-Chain-Resume-Source-Failed",
        "rule.21.Lower-Async-Chain",
    }
    until_rows = {
        "requirement.21.UntilMethodCallSurface",
        "def.21.UntilType",
        "requirement.21.UntilRuntimeSemantics",
    }
    combinator_rows = {
        "ast.21.AsyncCombinatorMembers",
        "def.21.AsyncCombinatorTypes",
        "requirement.21.AsyncCombinatorMemberLookup",
        "def.21.AsyncCombinatorRuntimeWrappers",
        "requirement.21.AsyncCombinatorWrapperLowering",
        "requirement.21.AsyncWrapperLoweringSemantics",
    }
    control_surface_rows = {
        "grammar.16.ControlExpressions",
        "req.16.ControlExpressionOwnership",
        "rule.16.ControlExpressionParsingRemainderFamily",
        "def.16.ControlExprAst",
        "def.16.ControlAstHelpers",
        "req.16.ControlExpressionLoweringOwnership",
    }
    control_if_rows = {
        "rule.16.Parse-If-Expr",
        "rule.16.T-If-No-Else",
        "rule.16.CheckIfFamily",
        "rule.16.EvalSigma-If-False-None",
        "rule.16.EvalSigma-If-False-Some",
        "rule.16.EvalSigma-If-Ctrl",
    }
    control_if_is_rows = {
        "rule.16.T-If-Is-No-Else",
        "rule.16.EvalSigma-If-Is-Ctrl",
    }
    control_if_case_rows = {
        "req.16.PatternTypingOwnershipForControlExpressions",
        "rule.16.IfCaseTypingFamily",
        "rule.16.CheckIfIsAndIfCaseFamily",
        "rule.16.EvalSigma-If-Cases-Ctrl",
        "rule.16.EvalIfCasesFamily",
    }
    control_block_rows = {
        "rule.16.Parse-Block-Expr",
        "req.16.BlockTypingOwnershipForControlExpressions",
        "rule.16.EvalSigma-Block",
        "rule.16.Lower-Expr-Block",
    }
    control_loop_rows = {
        "rule.16.Parse-Loop-Expr",
        "def.16.LoopTypeInference",
        "req.16.LoopInvariantTypingOwnership",
        "rule.16.T-Loop-Infinite",
        "rule.16.EvalSigma-Loop-Infinite-Step",
        "rule.16.EvalSigma-Loop-Infinite-Continue",
        "rule.16.EvalSigma-Loop-Infinite-Break",
        "rule.16.EvalSigma-Loop-Infinite-Ctrl",
        "rule.16.EvalSigma-Loop-Cond-False",
        "rule.16.EvalSigma-Loop-Cond-True-Step",
        "rule.16.EvalSigma-Loop-Cond-Continue",
        "rule.16.EvalSigma-Loop-Cond-Ctrl",
        "rule.16.EvalSigma-Loop-Cond-Body-Ctrl",
        "rule.16.LowerLoopExpressionFamily",
    }
    control_iter_rows = {
        "rule.16.T-Loop-Iter",
        "def.16.LoopIterableTypePredicates",
        "def.16.LoopIteratorRuntime",
        "def.16.LoopIterJudgement",
        "rule.16.EvalSigma-Loop-Iter",
        "rule.16.EvalSigma-Loop-Iter-Ctrl",
        "rule.16.LoopIter-Done",
        "rule.16.LoopIter-Step-Val",
        "rule.16.LoopIter-Step-Continue",
        "rule.16.LoopIter-Step-Break",
        "rule.16.LoopIter-Step-Ctrl",
    }
    parsing_metadata_symbols = {
        "runParsingItemSequencingReference",
        "runParsingAttributeParsingReference",
        "runParsingRecoverySpecimensReference",
        "runParsingTerminatorsReference",
    }

    appendix_target = appendix_grammar_target(row.obligation_id, target.path)
    if appendix_target is not None:
        return appendix_target

    if target.symbol in parsing_metadata_symbols:
        return parsing_reference_model_target(target.path)

    if row.obligation_id in map_rows:
        return async_composition_target(target.path, "runAsyncCompositionMapReference")
    if row.obligation_id in filter_rows:
        return async_composition_target(target.path, "runAsyncCompositionFilterReference")
    if row.obligation_id in take_rows:
        return async_composition_target(target.path, "runAsyncCompositionTakeReference")
    if row.obligation_id in fold_rows:
        return async_composition_target(target.path, "runAsyncCompositionFoldReference")
    if row.obligation_id in chain_rows:
        return async_composition_target(target.path, "runAsyncCompositionChainReference")
    if row.obligation_id in until_rows:
        return async_composition_target(target.path, "runAsyncCompositionUntilReference")
    if row.obligation_id in combinator_rows:
        return async_composition_target(target.path, "runAsyncCompositionCombinatorsReference")
    if row.obligation_id in control_surface_rows:
        return expressions_control_target("runExpressionsControlSurfaceReference")
    if row.obligation_id in control_if_rows:
        return expressions_control_target("runExpressionsIfControlReference")
    if row.obligation_id in control_if_is_rows:
        return expressions_control_target("runExpressionsIfIsControlReference")
    if row.obligation_id in control_if_case_rows:
        return expressions_control_target("runExpressionsIfCaseControlReference")
    if row.obligation_id in control_block_rows:
        return expressions_control_target("runExpressionsBlockControlReference")
    if row.obligation_id in control_loop_rows:
        return expressions_control_target("runExpressionsLoopControlReference")
    if row.obligation_id in control_iter_rows:
        return expressions_control_target("runExpressionsIteratorLoopControlReference")
    if row.obligation_id in {
        "Parse-KeyBoundaryOpt-Yes",
        "Parse-KeyBoundaryOpt-No",
        "requirement.19.FieldKeyBoundary",
    }:
        return key_paths_target(target.path)
    if row.obligation_id in {
        "req.GenericParamsNominalOwnerChapters",
        "rule.14.WF-Apply",
        "req.GenericNominalSizeAlignSubstitutedBody",
    }:
        return generic_parameters_target(target.path)
    if row.obligation_id == "grammar.RecordSyntax":
        return data_types_target(
            "Records.uv",
            "runDataTypesRecordsReference",
            "Records.uv",
        )
    if row.obligation_id == "grammar.EnumSyntax":
        return data_types_target(
            "Enums.uv",
            "runDataTypesEnumsReference",
            "Enums.uv",
        )
    if row.obligation_id == "grammar.UnionTypeSyntax":
        return data_types_target(
            "UnionTypes.uv",
            "runDataTypesUnionsReference",
            "Unions.uv",
        )
    if row.obligation_id == "grammar.TypeAliasSyntax":
        return data_types_target(
            "TypeAliases.uv",
            "runDataTypesTypeAliasesReference",
            "TypeAliases.uv",
        )
    if row.obligation_id in {
        "grammar.TestAttribute",
        "parse.TestAttributeByOrdinaryAttributeParser",
        "ast.TestProcedureClassification",
        "def.TestName",
        "def.TestCoverage",
        "req.TestAttributeProcedureTarget",
        "def.TestAttributeArgsOk",
        "req.TestProcedureShape",
        "req.TestAuthority",
        "conformance.TestAttributeDynamicSemantics",
        "lowering.TestHarnessGeneration",
        "def.TestDiscoveryOrder",
    }:
        return source_native_tests_target()
    return target


def load_csv_rows() -> list[CsvRow]:
    with CSV_PATH.open(newline="", encoding="utf-8") as handle:
        return [
            CsvRow(
                index=int(row["index"]),
                obligation_id=row["id"],
                internal_spec_line=int(row["internal_spec_line"]),
            )
            for row in csv.DictReader(handle)
        ]


def current_catalog_entries() -> list[tuple[str, int, ReferenceTarget]]:
    entries: list[tuple[str, int, ReferenceTarget]] = []
    for path in sorted(CATALOG_ROOT.rglob("*.uv")):
        if path.name == "Imports.uv":
            continue
        text = path.read_text(encoding="utf-8")
        for match in ENTRY_RE.finditer(text):
            target = ReferenceTarget(
                path=path,
                module_path=match.group(4),
                symbol=match.group(5),
                source_path=match.group(6),
            )
            entries.append(
                (
                    match.group(2),
                    int(match.group(3)),
                    normalized_existing_target(path, target),
                )
            )
    return entries


def function_names_by_file() -> dict[pathlib.Path, tuple[str, str]]:
    names: dict[pathlib.Path, tuple[str, str]] = {}
    for path in sorted(CATALOG_ROOT.rglob("*.uv")):
        if path.name == "Imports.uv":
            continue
        text = path.read_text(encoding="utf-8")
        count_match = COUNT_RE.search(text)
        validated_match = VALIDATED_RE.search(text)
        if count_match and validated_match:
            names[path] = (count_match.group(1), validated_match.group(1))
    return names


def match_existing_targets(
    rows: list[CsvRow],
    existing_entries: list[tuple[str, int, ReferenceTarget]],
) -> dict[tuple[str, int, int], ReferenceTarget]:
    rows_by_id: dict[str, list[CsvRow]] = defaultdict(list)
    for row in rows:
        rows_by_id[row.obligation_id].append(row)

    assigned: dict[tuple[str, int, int], ReferenceTarget] = {}
    used: set[tuple[str, int, int]] = set()
    for obligation_id, old_line, target in existing_entries:
        candidates = rows_by_id.get(obligation_id, [])
        if not candidates:
            continue
        candidates = sorted(
            candidates,
            key=lambda row: (
                (row.obligation_id, row.internal_spec_line, row.index) in used,
                abs(row.internal_spec_line - old_line),
                row.index,
            ),
        )
        row = candidates[0]
        key = (row.obligation_id, row.internal_spec_line, row.index)
        assigned[key] = target
        used.add(key)
    return assigned


def add_fixture_obligations_from_file(
    targets: dict[str, FixtureTarget],
    path: pathlib.Path,
    helper: str,
    module_path: str,
    source_path: str,
    source_paths_by_obligation: dict[str, str] | None = None,
) -> None:
    text = path.read_text(encoding="utf-8")
    validated = FIXTURE_VALIDATED_RE.search(text)
    if validated is None:
        return
    symbol = validated.group(1)
    obligation_ids = set(FIXTURE_EXPECTED_RE.findall(text))
    if source_paths_by_obligation is not None:
        obligation_ids.update(source_paths_by_obligation.keys())
    for obligation_id in sorted(obligation_ids):
        target_source_path = source_path
        if source_paths_by_obligation is not None:
            target_source_path = source_paths_by_obligation.get(obligation_id, "")
            if target_source_path == "":
                raise RuntimeError(f"missing fixture source for {obligation_id}")
        targets.setdefault(
            obligation_id,
            FixtureTarget(
                helper=helper,
                module_path=module_path,
                symbol=symbol,
                source_path=target_source_path,
            ),
        )


def specimen_obligation_source_paths(
    text: str,
    pattern: re.Pattern[str],
) -> dict[str, str]:
    source_paths = {
        obligation_id: source_path
        for source_path, obligation_id in pattern.findall(text)
    }
    for source_path, obligation_id in SPECIMEN_MATCHES_EXPECTATION_RE.findall(text):
        source_paths.setdefault(obligation_id, source_path)
    return source_paths


def accepted_project_obligation_source_paths(text: str) -> dict[str, str]:
    source_paths: dict[str, str] = {}

    for source_path, obligation_id in ACCEPTED_PROJECT_SPECIMEN_EXPECTATION_RE.findall(text):
        source_paths.setdefault(obligation_id, source_path)

    helper_sources: dict[str, str] = {}
    for helper_name, body in ACCEPTED_PROJECT_HELPER_RE.findall(text):
        source_match = ACCEPTED_PROJECT_SPECIMEN_SOURCE_RE.search(body)
        if source_match is not None:
            helper_sources[helper_name] = source_match.group(1)

    for helper_name, obligation_id in ACCEPTED_PROJECT_HELPER_CALL_RE.findall(text):
        source_path = helper_sources.get(helper_name)
        if source_path is not None:
            source_paths.setdefault(obligation_id, source_path)

    return source_paths


def artifact_project_obligation_source_paths(text: str) -> dict[str, str]:
    return {
        obligation_id: source_path
        for source_path, obligation_id in ARTIFACT_PROJECT_SPECIMEN_EXPECTATION_RE.findall(
            text
        )
    }


def physical_expected_obligation_source_paths(
    fixture_root: pathlib.Path,
    explicit_source_paths: dict[str, str] | None = None,
) -> dict[str, str]:
    source_paths: dict[str, str] = {}
    explicit_source_paths = explicit_source_paths or {}
    hello_root = ROOT / "HelloUltraviolet"
    if not fixture_root.exists():
        return source_paths

    for expected_path in sorted(fixture_root.rglob("Expected.uv")):
        text = expected_path.read_text(encoding="utf-8")
        obligation_ids = FIXTURE_EXPECTED_RE.findall(text)
        unresolved_ids = [
            obligation_id
            for obligation_id in obligation_ids
            if obligation_id not in explicit_source_paths
        ]
        source_rel = ""
        if unresolved_ids:
            source_root = expected_path.parent / "Source"
            source_path = source_root / "Main.uv"
            if not source_path.exists():
                source_files = sorted(source_root.glob("*.uv"))
                if len(source_files) != 1:
                    raise RuntimeError(f"ambiguous fixture source for {expected_path}")
                source_path = source_files[0]

            source_rel = source_path.relative_to(hello_root).as_posix()

        for obligation_id in obligation_ids:
            explicit_source_path = explicit_source_paths.get(obligation_id)
            if explicit_source_path is not None:
                source_paths.setdefault(obligation_id, explicit_source_path)
                continue
            source_paths.setdefault(obligation_id, source_rel)
    return source_paths


def fixture_obligation_targets() -> dict[str, FixtureTarget]:
    targets: dict[str, FixtureTarget] = {}

    for obligation_id in {
        "rule.18.BlockInfo-Res-Err",
        "rule.14.Impl-Orphan-Err",
        "WF-Union-TooFew",
        "TupleIndex-NonConst",
        "Enum-Disc-NotInt",
        "Enum-Disc-Negative",
        "rule.24.LowerIR-Err",
        "rule.24.EmitObj-Err",
    }:
        targets.setdefault(
            obligation_id,
            FixtureTarget(
                helper=REFERENCE_MODEL_HELPER,
                module_path="HelloUltraviolet::Audit",
                symbol="runSpecClarificationReferenceModels",
                source_path="Source/Audit/SpecClarificationReferenceModels.uv",
            ),
        )

    targets.setdefault(
        "Union-DirectAccess-Err",
        FixtureTarget(
            helper=REJECTED_SOURCE_HELPER,
            module_path=f"{FIXTURE_CATALOG_MODULE}::RejectedSource",
            symbol="validatedExpressionsRejectedSourceFixtureCount",
            source_path="Fixtures/RejectedSource/Expressions/UnionDirectAccess/Source/Main.uv",
        ),
    )
    targets.setdefault(
        "TupleAccess-NotTuple",
        FixtureTarget(
            helper=REJECTED_SOURCE_HELPER,
            module_path=f"{FIXTURE_CATALOG_MODULE}::RejectedSource",
            symbol="validatedExpressionsRejectedSourceFixtureCount",
            source_path="Fixtures/RejectedSource/Expressions/TupleAccessNonTuple/Source/Main.uv",
        ),
    )
    targets.setdefault(
        "diagnostics.Records",
        FixtureTarget(
            helper=REJECTED_SOURCE_HELPER,
            module_path=f"{FIXTURE_CATALOG_MODULE}::RejectedSource",
            symbol="validatedExpressionsRejectedSourceFixtureCount",
            source_path="Fixtures/RejectedSource/Expressions/RecordDuplicateField/Source/Main.uv",
        ),
    )
    targets.setdefault(
        "diagnostics.Enums",
        FixtureTarget(
            helper=REJECTED_SOURCE_HELPER,
            module_path=f"{FIXTURE_CATALOG_MODULE}::RejectedSource",
            symbol="validatedExpressionsRejectedSourceFixtureCount",
            source_path="Fixtures/RejectedSource/Expressions/EnumEmpty/Source/Main.uv",
        ),
    )

    rejected_root = FIXTURE_CATALOG_ROOT / "RejectedSource"
    for path in sorted(rejected_root.glob("*.uv")):
        source_path = f"Source/Audit/FixtureCatalog/RejectedSource/{path.name}"
        text = path.read_text(encoding="utf-8")
        source_paths = specimen_obligation_source_paths(
            text,
            REJECTED_SOURCE_SPECIMEN_EXPECTATION_RE,
        )
        physical_source_paths = physical_expected_obligation_source_paths(
            ROOT / "HelloUltraviolet" / "Fixtures" / "RejectedSource" / path.stem,
            source_paths,
        )
        physical_source_paths.update(source_paths)
        source_paths = physical_source_paths
        add_fixture_obligations_from_file(
            targets,
            path,
            REJECTED_SOURCE_HELPER,
            f"{FIXTURE_CATALOG_MODULE}::RejectedSource",
            source_path,
            source_paths,
        )

    diagnostic_root = FIXTURE_CATALOG_ROOT / "DiagnosticSource"
    for path in sorted(diagnostic_root.glob("*.uv")):
        source_path = f"Source/Audit/FixtureCatalog/DiagnosticSource/{path.name}"
        text = path.read_text(encoding="utf-8")
        source_paths = specimen_obligation_source_paths(
            text,
            DIAGNOSTIC_SOURCE_SPECIMEN_EXPECTATION_RE,
        )
        physical_source_paths = physical_expected_obligation_source_paths(
            ROOT / "HelloUltraviolet" / "Fixtures" / "DiagnosticSource" / path.stem,
            source_paths,
        )
        physical_source_paths.update(source_paths)
        source_paths = physical_source_paths
        add_fixture_obligations_from_file(
            targets,
            path,
            DIAGNOSTIC_SOURCE_HELPER,
            f"{FIXTURE_CATALOG_MODULE}::DiagnosticSource",
            source_path,
            source_paths,
        )

    accepted_project_coverage = FIXTURE_CATALOG_ROOT / "AcceptedProjects" / "Coverage.uv"
    accepted_project_text = accepted_project_coverage.read_text(encoding="utf-8")
    accepted_project_sources = accepted_project_obligation_source_paths(
        accepted_project_text
    )
    for obligation_id in ACCEPTED_PROJECT_EXPECTATION_RE.findall(accepted_project_text):
        source_path = accepted_project_sources.get(obligation_id)
        if source_path is None:
            raise RuntimeError(f"missing accepted-project source for {obligation_id}")
        targets.setdefault(
            obligation_id,
            FixtureTarget(
                helper=ACCEPTED_PROJECT_HELPER,
                module_path=f"{FIXTURE_CATALOG_MODULE}::AcceptedProjects",
                symbol="validatedAcceptedProjectFixtureCount",
                source_path=source_path,
            ),
        )

    output_diagnostic_sources = [
        FIXTURE_CATALOG_ROOT / "OutputDiagnostics" / "Lowering.uv",
        FIXTURE_CATALOG_ROOT / "OutputDiagnostics" / "Projects.uv",
        FIXTURE_CATALOG_ROOT / "OutputDiagnostics" / "CommandLine.uv",
    ]
    for output_diagnostic_source in output_diagnostic_sources:
        output_text = output_diagnostic_source.read_text(encoding="utf-8")
        output_sources = specimen_obligation_source_paths(
            output_text,
            DIAGNOSTIC_SOURCE_SPECIMEN_EXPECTATION_RE,
        )
        add_fixture_obligations_from_file(
            targets,
            output_diagnostic_source,
            ARTIFACT_BEHAVIOR_HELPER,
            f"{FIXTURE_CATALOG_MODULE}::OutputDiagnostics",
            f"Source/Audit/FixtureCatalog/OutputDiagnostics/{output_diagnostic_source.name}",
            output_sources,
        )

    artifact_coverage = FIXTURE_CATALOG_ROOT / "ArtifactProjects" / "Coverage.uv"
    artifact_text = artifact_coverage.read_text(encoding="utf-8")
    artifact_sources = artifact_project_obligation_source_paths(artifact_text)
    artifact_sources.update(
        physical_expected_obligation_source_paths(
            ROOT / "HelloUltraviolet" / "Fixtures" / "ArtifactProjects"
        )
    )
    artifact_obligation_ids = set(ARTIFACT_EXPECTATION_RE.findall(artifact_text))
    artifact_obligation_ids.update(artifact_sources.keys())
    for obligation_id in sorted(artifact_obligation_ids):
        source_path = artifact_sources.get(obligation_id)
        if source_path is None:
            raise RuntimeError(f"missing artifact-project source for {obligation_id}")
        targets.setdefault(
            obligation_id,
            FixtureTarget(
                helper=ARTIFACT_BEHAVIOR_HELPER,
                module_path=f"{FIXTURE_CATALOG_MODULE}::ArtifactProjects",
                symbol="validatedArtifactProjectFixtureCount",
                source_path=source_path,
            ),
        )

    return targets


def apply_fixture_target(
    row: CsvRow,
    target: ReferenceTarget,
    fixture_targets: dict[str, FixtureTarget],
) -> tuple[ReferenceTarget, str]:
    fixture = fixture_targets.get(row.obligation_id)
    if fixture is None:
        return target, ACCEPTED_HELPER
    return (
        ReferenceTarget(
            path=target.path,
            module_path=fixture.module_path,
            symbol=fixture.symbol,
            source_path=fixture.source_path,
        ),
        fixture.helper,
    )


def missing_target(row: CsvRow) -> ReferenceTarget:
    host_primitives = (
        CATALOG_ROOT
        / "AbstractMachineObjectsResponsibilityAndAuthority"
        / "HostPrimitives.uv"
    )
    capability_classes = (
        CATALOG_ROOT / "AbstractionAndPolymorphism" / "CapabilityClasses.uv"
    )
    top_level_names = (
        CATALOG_ROOT / "NameResolutionAndVisibility" / "TopLevelNameCollection.uv"
    )
    module_aggregation = (
        CATALOG_ROOT / "ModuleLevelForms" / "ModuleAndFileAggregation.uv"
    )
    control_expressions = CATALOG_ROOT / "Expressions" / "ControlExpressions.uv"
    basic_patterns = CATALOG_ROOT / "Patterns" / "BasicPatterns.uv"
    case_clauses = CATALOG_ROOT / "Patterns" / "CaseClauses.uv"

    time_target = ReferenceTarget(
        path=host_primitives,
        module_path="HelloUltraviolet::Reference::Authority",
        symbol="runAuthorityTimeReference",
        source_path="Source/Reference/Authority/Time.uv",
    )
    builtin_type_names_target = ReferenceTarget(
        path=capability_classes,
        module_path="HelloUltraviolet::Reference::Authority",
        symbol="runAuthorityBuiltinTypeNamesReference",
        source_path="Source/Reference/Authority/BuiltinTypeNames.uv",
    )
    pattern_target = ReferenceTarget(
        path=basic_patterns,
        module_path="HelloUltraviolet::Reference::Patterns",
        symbol="runPatternsBasicPatternsReference",
        source_path="Source/Reference/Patterns/BasicPatterns.uv",
    )

    if row.obligation_id in {
        "def.TimePrimitiveJudgments",
        "def.TimePrimitiveValueConstructors",
        "req.TimePrimitiveAttenuationSemantics",
        "req.MonotonicTimePrimitiveSemantics",
        "req.WallTimePrimitiveSemantics",
        "Prim-Time-Monotonic",
        "Prim-Time-Wall",
        "Prim-MonotonicTime-Now",
        "Prim-MonotonicTime-Resolution",
        "Prim-MonotonicTime-Elapsed",
        "Prim-MonotonicTime-Coarsen",
        "Prim-WallTime-NowUtc",
        "Prim-WallTime-Resolution",
        "Prim-WallTime-Coarsen",
        "rule.24.BuiltinSym-Time-Monotonic",
        "rule.24.BuiltinSym-Time-Wall",
        "rule.24.BuiltinSym-MonotonicTime-Now",
        "rule.24.BuiltinSym-MonotonicTime-Resolution",
        "rule.24.BuiltinSym-MonotonicTime-Elapsed",
        "rule.24.BuiltinSym-MonotonicTime-Coarsen",
        "rule.24.BuiltinSym-WallTime-NowUtc",
        "rule.24.BuiltinSym-WallTime-Resolution",
        "rule.24.BuiltinSym-WallTime-Coarsen",
        "req.24.TimeHostPrimitivesDefinedInAuthorityModel",
        "rule.24.Prim-Time-Monotonic-Runtime",
        "rule.24.Prim-Time-Wall-Runtime",
        "rule.24.Prim-MonotonicTime-Now-Runtime",
        "rule.24.Prim-MonotonicTime-Resolution-Runtime",
        "rule.24.Prim-MonotonicTime-Elapsed-Runtime",
        "rule.24.Prim-MonotonicTime-Coarsen-Runtime",
        "rule.24.Prim-WallTime-NowUtc-Runtime",
        "rule.24.Prim-WallTime-Resolution-Runtime",
        "rule.24.Prim-WallTime-Coarsen-Runtime",
    }:
        return time_target

    if row.obligation_id in {
        "def.14.TimeInterface",
        "def.14.MonotonicTimeInterface",
        "def.14.WallTimeInterface",
        "def.14.TimeErrorDecl",
        "def.14.DurationDecl",
        "def.14.MonotonicInstantDecl",
        "def.14.UtcInstantDecl",
    }:
        return ReferenceTarget(
            path=capability_classes,
            module_path=time_target.module_path,
            symbol=time_target.symbol,
            source_path=time_target.source_path,
        )

    if row.obligation_id == "def.14.BuiltinTypesIO":
        return builtin_type_names_target

    if row.obligation_id in {
        "PatNames-TypedPattern-Discard",
        "PatNames-TypedPattern-Identifier",
    }:
        return ReferenceTarget(
            path=top_level_names,
            module_path=pattern_target.module_path,
            symbol=pattern_target.symbol,
            source_path=pattern_target.source_path,
        )

    if row.obligation_id == "TypeRef-TypedPattern":
        return ReferenceTarget(
            path=module_aggregation,
            module_path=pattern_target.module_path,
            symbol=pattern_target.symbol,
            source_path=pattern_target.source_path,
        )

    if row.obligation_id == "rule.16.Parse-If-Is-TypeTest":
        return ReferenceTarget(
            path=control_expressions,
            module_path="HelloUltraviolet::Reference::Expressions",
            symbol="runExpressionsControlReference",
            source_path="Source/Reference/Expressions/Control.uv",
        )

    if row.obligation_id == "req.ElseContinuationAcrossNewline":
        return ReferenceTarget(
            path=control_expressions,
            module_path="HelloUltraviolet::Reference::Expressions",
            symbol="runExpressionsIfControlReference",
            source_path="Source/Reference/Expressions/Control.uv",
        )

    if row.obligation_id in {
        "rule.16.Parse-Unary-Copy",
        "rule.16.T-Copy",
        "rule.16.EvalSigma-Copy",
        "rule.16.Lower-Expr-Copy",
    }:
        return ReferenceTarget(
            path=ROOT / "HelloUltraviolet" / "Source" / "Audit" /
            "Catalog" / "Expressions" / "EffectfulCoreExpressions.uv",
            module_path="HelloUltraviolet::Reference::Expressions",
            symbol="runExpressionsEffectfulCoreReference",
            source_path="Source/Reference/Expressions/EffectfulCore.uv",
        )

    if row.obligation_id == "def.16.ArgumentPassExpressions":
        return ReferenceTarget(
            path=ROOT / "HelloUltraviolet" / "Source" / "Audit" /
            "Catalog" / "Expressions" / "CallExpressions.uv",
            module_path="HelloUltraviolet::Reference::Expressions",
            symbol="runExpressionsCallsReference",
            source_path="Source/Reference/Expressions/Calls.uv",
        )

    if row.obligation_id == "def.IndexUsizeExpr":
        return data_types_target(
            "Arrays.uv",
            "runDataTypesArraysReference",
            "Arrays.uv",
        )

    if row.obligation_id == "def.RangeIndexExpr":
        return data_types_target(
            "Slices.uv",
            "runDataTypesSlicesReference",
            "Slices.uv",
        )

    if row.obligation_id in {
        "rule.17.Parse-Pattern-Typed",
        "rule.17.Pat-Typed-Discard",
        "rule.17.Pat-Typed-Ident",
        "rule.17.Pat-Typed-Exact-R",
        "rule.17.Pat-Typed-Union-R",
        "rule.17.Match-Typed-Discard",
        "rule.17.Match-Typed-Ident",
    }:
        return pattern_target

    if row.obligation_id in {
        "rule.17.Parse-IfCase-Pattern",
        "rule.17.PatternNarrow-Typed",
        "rule.17.PatternRejectNarrow-Union",
        "rule.17.ElseScope-Narrow",
        "rule.17.ElseScope-Original",
        "rule.17.CasesElseScope-Empty",
        "rule.17.CasesElseScope-Cons-Narrow",
        "rule.17.CasesElseScope-Cons-Original",
    }:
        return ReferenceTarget(
            path=case_clauses,
            module_path=pattern_target.module_path,
            symbol=pattern_target.symbol,
            source_path=pattern_target.source_path,
        )

    raise RuntimeError(f"no catalog target for new obligation {row.obligation_id}")


def build_catalog_entries(rows: list[CsvRow]) -> list[CatalogEntry]:
    assigned = match_existing_targets(rows, current_catalog_entries())
    fixture_targets = fixture_obligation_targets()
    entries: list[CatalogEntry] = []
    for row in rows:
        key = (row.obligation_id, row.internal_spec_line, row.index)
        target = assigned.get(key)
        if target is None:
            target = missing_target(row)
        target = normalized_row_target(row, target)
        target, helper = apply_fixture_target(row, target, fixture_targets)
        if target.symbol == "runParsingReferenceModels":
            helper = REFERENCE_MODEL_HELPER
        entries.append(CatalogEntry(row=row, target=target, helper=helper))
    return entries


def write_catalog_root(total: int) -> None:
    write_generated(
        AUDIT_ROOT / "Catalog.uv",
        "//! Root catalog accounting for generated obligation entries.\n\n"
        f"public let EXPECTED_OBLIGATION_COUNT: usize = {total}\n\n"
        "public procedure catalogObligationCount() -> usize {\n"
        "    return EXPECTED_OBLIGATION_COUNT\n"
        "}\n",
    )


def write_catalog_imports(entries: list[CatalogEntry]) -> None:
    helpers_by_directory: dict[pathlib.Path, set[str]] = defaultdict(set)
    for entry in entries:
        helpers_by_directory[entry.target.path.parent].add(entry.helper)

    for directory, helpers in sorted(helpers_by_directory.items()):
        lines = [
            "//! Shared catalog imports for generated obligation entries.",
            "",
        ]
        for helper in sorted(helpers):
            lines.append(f"using HelloUltraviolet::Audit::{{ {helper} }}")
        lines.append("")
        write_generated(directory / "Imports.uv", "\n".join(lines))


def write_topic_files(entries: list[CatalogEntry]) -> None:
    names = function_names_by_file()
    by_file: dict[pathlib.Path, list[CatalogEntry]] = defaultdict(list)
    for entry in entries:
        by_file[entry.target.path].append(entry)

    for path, path_entries in sorted(by_file.items()):
        if path not in names:
            raise RuntimeError(f"missing generated function names for {path}")
        count_name, validated_name = names[path]
        path_entries.sort(key=lambda entry: entry.row.index)
        old_text = path.read_text(encoding="utf-8")
        header = old_text.splitlines()[0]
        lines: list[str] = [
            header,
            "",
            "",
            f"public procedure {count_name}() -> usize {{",
            f"    return {len(path_entries)}usize",
            "}",
            "",
            f"public procedure {validated_name}() -> usize {{",
            "    var count: usize = 0usize",
        ]
        for entry in path_entries:
            lines.extend(
                [
                    f"    if {entry.helper}(",
                    f'        "{entry.row.obligation_id}",',
                    f"        {entry.row.internal_spec_line}usize,",
                    f'        "{entry.target.module_path}",',
                    f'        "{entry.target.symbol}",',
                    f'        "{entry.target.source_path}"',
                    "    ) {",
                    "        count = count + 1usize",
                    "    }",
                ]
            )
        lines.extend(["    return count", "}", ""])
        write_generated(path, "\n".join(lines))


def write_membership(rows: list[CsvRow], entries: list[CatalogEntry]) -> None:
    csv_text = CSV_PATH.read_text(encoding="utf-8")
    csv_keys = sorted((row.obligation_id, row.internal_spec_line) for row in rows)
    catalog_keys = sorted((entry.row.obligation_id, entry.row.internal_spec_line) for entry in entries)
    for old_group in AUDIT_ROOT.glob("CatalogCsvMembershipGroup*.uv"):
        old_group.unlink()

    lines: list[str] = [
        "//! CSV-to-catalog membership checks for generated obligation references.",
        "",
        f"internal let CATALOG_AUDIT_DIGEST_MODULUS: usize = {AUDIT_DIGEST_MODULUS}usize",
        "",
        "internal procedure catalogAuditTextDigest(text: string@View) -> usize {",
        "    let text_length: usize = string::length(text)",
        "    var value: usize = 0usize",
        "    var index: usize = 0usize",
        "    loop index < text_length {",
        "        let byte_value: usize = auditTextByte(text, index) as usize",
        "        value = ((value * 257usize) + byte_value + 1usize) %",
        "            CATALOG_AUDIT_DIGEST_MODULUS",
        "        index = index + 1usize",
        "    }",
        "    return value",
        "}",
        "",
        "internal procedure catalogAuditLineBreakCount(text: string@View) -> usize {",
        "    let text_length: usize = string::length(text)",
        "    var count: usize = 0usize",
        "    var index: usize = 0usize",
        "    loop index < text_length {",
        "        if auditTextByte(text, index) == 10u8 {",
        "            count = count + 1usize",
        "        }",
        "        index = index + 1usize",
        "    }",
        "    return count",
        "}",
        "",
        "public procedure expectedCsvObligationKeyCount() -> usize {",
        f"    return {len(csv_keys)}usize",
        "}",
        "",
        "public procedure catalogGeneratedObligationKeyCount() -> usize {",
        f"    return {len(catalog_keys)}usize",
        "}",
        "",
        "public procedure expectedCsvObligationKeyDigest() -> usize {",
        f"    return {key_digest(csv_keys)}usize",
        "}",
        "",
        "public procedure catalogGeneratedObligationKeyDigest() -> usize {",
        f"    return {key_digest(catalog_keys)}usize",
        "}",
        "",
        "public procedure expectedCsvFileByteCount() -> usize {",
        f"    return {len(csv_text.encode('utf-8'))}usize",
        "}",
        "",
        "public procedure expectedCsvFileLineBreakCount() -> usize {",
        f"    return {csv_text.count(chr(10))}usize",
        "}",
        "",
        "public procedure expectedCsvFileDigest() -> usize {",
        f"    return {text_digest(csv_text)}usize",
        "}",
        "",
        "internal procedure csvFileShapeMatches(csv_text: string@View) -> bool {",
        "    return string::length(csv_text) == expectedCsvFileByteCount() &&",
        "        catalogAuditLineBreakCount(csv_text) == expectedCsvFileLineBreakCount() &&",
        "        catalogAuditTextDigest(csv_text) == expectedCsvFileDigest()",
        "}",
        "",
        "internal procedure generatedCatalogKeysMatchCsv() -> bool {",
        "    return catalogGeneratedObligationKeyCount() == expectedCsvObligationKeyCount() &&",
        "        catalogGeneratedObligationKeyDigest() == expectedCsvObligationKeyDigest()",
        "}",
        "",
        "public procedure catalogMatchesCsvObligations(context: Context) -> bool {",
        '    let read_result: Outcome<unique string@Managed, IoError> = context.io~>read_file(',
        '        "Docs/Audit/UltravioletObligations.csv"',
        "    )",
        "    return if move read_result is {",
        "        @Value { value } {",
        "            let csv_text: string@View = string::as_view(value)",
        "            csvFileShapeMatches(csv_text) && generatedCatalogKeysMatchCsv()",
        "        }",
        "        @Error {",
        "            false",
        "        }",
        "    }",
        "}",
        "",
    ]
    write_generated(AUDIT_ROOT / "CatalogCsvMembership.uv", "\n".join(lines))


def write_primary_references(entries: list[CatalogEntry]) -> None:
    keys = sorted((entry.row.obligation_id, entry.row.internal_spec_line) for entry in entries)
    unique_keys = sorted(set(keys))
    for old_group in AUDIT_ROOT.glob("CatalogPrimaryReferenceOrderGroup*.uv"):
        old_group.unlink()

    lines: list[str] = [
        "//! Uniqueness checks for generated catalog primary obligation references.",
        "",
        "public procedure expectedCatalogPrimaryReferenceCount() -> usize {",
        f"    return {len(keys)}usize",
        "}",
        "",
        "public procedure uniqueCatalogPrimaryReferenceCount() -> usize {",
        f"    return {len(unique_keys)}usize",
        "}",
        "",
        "public procedure expectedCatalogPrimaryReferenceDigest() -> usize {",
        f"    return {key_digest(keys)}usize",
        "}",
        "",
        "public procedure uniqueCatalogPrimaryReferenceDigest() -> usize {",
        f"    return {key_digest(unique_keys)}usize",
        "}",
        "",
        "public procedure catalogPrimaryReferencesAreUnique() -> bool {",
        "    return uniqueCatalogPrimaryReferenceCount() ==",
        "        expectedCatalogPrimaryReferenceCount() &&",
        "        uniqueCatalogPrimaryReferenceDigest() ==",
        "            expectedCatalogPrimaryReferenceDigest()",
        "}",
        "",
    ]
    write_generated(AUDIT_ROOT / "CatalogPrimaryReferences.uv", "\n".join(lines))


def write_source_paths(entries: list[CatalogEntry]) -> None:
    paths = [f"HelloUltraviolet/{source_path}" for source_path in sorted(
        {entry.target.source_path for entry in entries}
    )]
    manifest_text = "".join(f"{source_path}\n" for source_path in paths)
    write_generated(ROOT / "HelloUltraviolet" / "Audit" / "CatalogSourcePaths.txt", manifest_text)

    lines = [
        "//! Runtime path checks for source files referenced by generated catalog rows.",
        "",
        f"internal let SOURCE_PATH_DIGEST_MODULUS: usize = {AUDIT_DIGEST_MODULUS}usize",
        "",
        "internal procedure sourcePathManifestDigest(text: string@View) -> usize {",
        "    let text_length: usize = string::length(text)",
        "    var value: usize = 0usize",
        "    var index: usize = 0usize",
        "    loop index < text_length {",
        "        let byte_value: usize = auditTextByte(text, index) as usize",
        "        value = ((value * 257usize) + byte_value + 1usize) %",
        "            SOURCE_PATH_DIGEST_MODULUS",
        "        index = index + 1usize",
        "    }",
        "    return value",
        "}",
        "",
        "internal procedure sourcePathManifestLineBreakCount(text: string@View) -> usize {",
        "    let text_length: usize = string::length(text)",
        "    var count: usize = 0usize",
        "    var index: usize = 0usize",
        "    loop index < text_length {",
        "        if auditTextByte(text, index) == 10u8 {",
        "            count = count + 1usize",
        "        }",
        "        index = index + 1usize",
        "    }",
        "    return count",
        "}",
        "",
        "public procedure expectedCatalogSourcePathCount() -> usize {",
        f"    return {len(paths)}usize",
        "}",
        "",
        "public procedure catalogSourcePathCount() -> usize {",
        f"    return {len(paths)}usize",
        "}",
        "",
        "public procedure expectedCatalogSourcePathManifestByteCount() -> usize {",
        f"    return {len(manifest_text.encode('utf-8'))}usize",
        "}",
        "",
        "public procedure expectedCatalogSourcePathManifestLineBreakCount() -> usize {",
        f"    return {manifest_text.count(chr(10))}usize",
        "}",
        "",
        "public procedure expectedCatalogSourcePathManifestDigest() -> usize {",
        f"    return {text_digest(manifest_text)}usize",
        "}",
        "",
        "internal procedure sourcePathManifestShapeMatches(text: string@View) -> bool {",
        "    return string::length(text) == expectedCatalogSourcePathManifestByteCount() &&",
        "        sourcePathManifestLineBreakCount(text) ==",
        "            expectedCatalogSourcePathManifestLineBreakCount() &&",
        "        sourcePathManifestDigest(text) == expectedCatalogSourcePathManifestDigest()",
        "}",
        "",
        "internal procedure catalogSourcePathExists(context: Context, source_path: string@View) -> bool {",
        "    return context.io~>exists(source_path)",
        "}",
        "",
        "internal procedure writeCatalogSourcePathMessage(context: Context, text: string@View) -> bool {",
        "    let output: Outcome<(), IoError> = context.io~>write_stderr(text)",
        "    return if output is {",
        "        @Value {",
        "            true",
        "        }",
        "        @Error {",
        "            false",
        "        }",
        "    }",
        "}",
        "",
        "internal procedure recordCatalogSourcePath(",
        "    context: Context,",
        "    source_path: string@View",
        ") -> bool {",
        "    if catalogSourcePathExists(context, source_path) {",
        "        return true",
        "    }",
        "",
        "    if (!writeCatalogSourcePathMessage(context, \"catalog source path missing: \")) {",
        "        return false",
        "    }",
        "    if (!writeCatalogSourcePathMessage(context, source_path)) {",
        "        return false",
        "    }",
        "    if (!writeCatalogSourcePathMessage(context, \"\\n\")) {",
        "        return false",
        "    }",
        "    return false",
        "}",
        "",
        "internal procedure recordCatalogSourcePathManifestLine(",
        "    context: Context,",
        "    manifest_text: string@View,",
        "    start: usize,",
        "    length: usize",
        ") -> bool {",
        "    if length == 0usize {",
        "        return true",
        "    }",
        "",
        "    let end: usize = start + length",
        "    let source_path: string@View = string::slice(manifest_text, start, end)",
        "    return recordCatalogSourcePath(context, source_path)",
        "}",
        "",
        "internal procedure sourcePathsInManifestExist(",
        "    context: Context,",
        "    manifest_text: string@View",
        ") -> bool {",
        "    let text_length: usize = string::length(manifest_text)",
        "    var passed: bool = true",
        "    var path_count: usize = 0usize",
        "    var start: usize = 0usize",
        "    var index: usize = 0usize",
        "    loop index < text_length {",
        "        if auditTextByte(manifest_text, index) == 10u8 {",
        "            let length: usize = index - start",
        "            if length > 0usize {",
        "                path_count = path_count + 1usize",
        "            }",
        "            passed = recordCatalogSourcePathManifestLine(",
        "                context,",
        "                manifest_text,",
        "                start,",
        "                length",
        "            ) && passed",
        "            start = index + 1usize",
        "        }",
        "        index = index + 1usize",
        "    }",
        "",
        "    if start < text_length {",
        "        let length: usize = text_length - start",
        "        path_count = path_count + 1usize",
        "        passed = recordCatalogSourcePathManifestLine(",
        "            context,",
        "            manifest_text,",
        "            start,",
        "            length",
        "        ) && passed",
        "    }",
        "",
        "    return path_count == expectedCatalogSourcePathCount() && passed",
        "}",
        "",
        "internal procedure sourcePathManifestAuditPasses(",
        "    context: Context,",
        "    manifest_text: string@View",
        ") -> bool {",
        "    return sourcePathManifestShapeMatches(manifest_text) &&",
        "        sourcePathsInManifestExist(context, manifest_text)",
        "}",
        "",
        "public procedure catalogSourcePathsExist(context: Context) -> bool {",
        "    if catalogSourcePathCount() != expectedCatalogSourcePathCount() {",
        "        return false",
        "    }",
        "",
        '    let read_result: Outcome<unique string@Managed, IoError> = context.io~>read_file(',
        '        "HelloUltraviolet/Audit/CatalogSourcePaths.txt"',
        "    )",
        "    return if move read_result is {",
        "        @Value { value } {",
        "            let manifest_text: string@View = string::as_view(value)",
        "            sourcePathManifestAuditPasses(context, manifest_text)",
        "        }",
        "        @Error {",
        "            false",
        "        }",
        "    }",
    ]
    lines.extend(["}", ""])
    write_generated(AUDIT_ROOT / "CatalogSourcePaths.uv", "\n".join(lines))


def write_symbols(entries: list[CatalogEntry]) -> None:
    targets = sorted(
        {
            (entry.target.module_path, entry.target.symbol, entry.target.source_path)
            for entry in entries
        }
    )
    context_symbols = {
        "runAuthorityTimeReference",
        "runAuthorityBuiltinTypeNamesReference",
        "runAsyncSuspensionFormsReference",
        "runParallelismCancellationReference",
        "runParallelismCaptureSemanticsReference",
        "runParallelismDeterminismReference",
        "runParallelismDispatchReference",
        "runParallelismExecutionDomainsReference",
        "runParallelismPanicHandlingReference",
        "runParallelismParallelBlocksReference",
        "runParallelismSpawnReference",
        "runPolymorphismCapabilityClassesReference",
        "runKeysDynamicVerificationReference",
    }
    count_symbols = {
        symbol
        for _module_path, symbol, _source_path in targets
        if symbol.startswith("validated") and symbol.endswith("FixtureCount")
    }

    SYMBOL_EXECUTION_ROOT.mkdir(parents=True, exist_ok=True)
    for old_file in SYMBOL_EXECUTION_ROOT.glob("*.uv"):
        old_file.unlink()

    support_lines = [
        "//! Shared support for generated compiled-symbol execution checks.",
        "",
        "internal procedure writeCatalogSymbolMessage(context: Context, text: string@View) -> bool {",
        "    let output: Outcome<(), IoError> = context.io~>write_stderr(text)",
        "    return if output is {",
        "        @Value {",
        "            true",
        "        }",
        "        @Error {",
        "            false",
        "        }",
        "    }",
        "}",
        "",
        "internal procedure recordCatalogSymbolExecution(",
        "    context: Context,",
        "    symbol: string@View,",
        "    passed: bool",
        ") -> bool {",
        "    if passed {",
        "        return true",
        "    }",
        "",
        "    if (!writeCatalogSymbolMessage(context, \"catalog compiled symbol failed: \")) {",
        "        return false",
        "    }",
        "    if (!writeCatalogSymbolMessage(context, symbol)) {",
        "        return false",
        "    }",
        "    if (!writeCatalogSymbolMessage(context, \"\\n\")) {",
        "        return false",
        "    }",
        "    return false",
        "}",
        "",
    ]
    write_generated(SYMBOL_EXECUTION_ROOT / "Support.uv", "\n".join(support_lines))

    targets_by_group: dict[str, list[tuple[str, str, str]]] = defaultdict(list)
    for target in targets:
        targets_by_group[symbol_execution_group(target[2])].append(target)

    group_functions: list[tuple[str, str]] = []
    for group_name, group_targets in sorted(targets_by_group.items()):
        function_name = f"catalogCompiledSymbolsExecute{group_name}"
        group_functions.append((group_name, function_name))
        group_lines = [
            f"//! Compiled symbol execution checks for {group_name}.",
            "",
        ]
        for module_path, symbol in sorted(
            {(module_path, symbol) for module_path, symbol, _source_path in group_targets}
        ):
            group_lines.append(f"using {module_path}::{{ {symbol} }}")
        group_lines.extend(["", f"public procedure {function_name}(context: Context) -> bool {{"])
        group_lines.append("    var passed: bool = true")
        for _module_path, symbol in sorted(
            {(module_path, symbol) for module_path, symbol, _source_path in group_targets}
        ):
            if symbol in context_symbols:
                call = f"{symbol}(context)"
            elif symbol in count_symbols:
                call = f"{symbol}() > 0usize"
            else:
                call = f"{symbol}()"
            group_lines.extend(
                [
                    "    passed = recordCatalogSymbolExecution(",
                    "        context,",
                    f'        "{symbol}",',
                    f"        {call}",
                    "    ) && passed",
                ]
            )
        group_lines.append("    return passed")
        group_lines.extend(["}", ""])
        write_generated(SYMBOL_EXECUTION_ROOT / f"{group_name}.uv", "\n".join(group_lines))

    lines = ["//! Compiled symbol checks for source references named by catalog rows.", ""]
    for _group_name, function_name in group_functions:
        lines.append(
            f"using HelloUltraviolet::Audit::SymbolExecutions::{{ {function_name} }}"
        )
    lines.extend(
        [
            "",
            "public procedure expectedCatalogCompiledSymbolCount() -> usize {",
            f"    return {len(targets)}usize",
            "}",
            "",
            "public procedure catalogCompiledSymbolCount() -> usize {",
            f"    return {len(targets)}usize",
            "}",
            "",
            "public procedure expectedCatalogCompiledSymbolTargetDigest() -> usize {",
            f"    return {target_digest(targets)}usize",
            "}",
            "",
            "public procedure catalogCompiledSymbolTargetDigest() -> usize {",
            f"    return {target_digest(targets)}usize",
            "}",
            "",
            "public procedure catalogCompiledSymbolTargetsAreIndexed() -> bool {",
            "    return catalogCompiledSymbolCount() == expectedCatalogCompiledSymbolCount() &&",
            "        catalogCompiledSymbolTargetDigest() ==",
            "            expectedCatalogCompiledSymbolTargetDigest()",
            "}",
            "",
            "public procedure catalogCompiledSymbolsExecute(context: Context) -> bool {",
            "    var passed: bool = true",
        ]
    )
    for _group_name, function_name in group_functions:
        lines.append(f"    passed = {function_name}(context) && passed")
    lines.append("    return passed")
    lines.extend(["}", ""])
    write_generated(AUDIT_ROOT / "CatalogSymbols.uv", "\n".join(lines))


def main() -> int:
    rows = load_csv_rows()
    entries = build_catalog_entries(rows)
    write_catalog_root(len(entries))
    write_catalog_imports(entries)
    write_topic_files(entries)
    write_membership(rows, entries)
    write_primary_references(entries)
    write_source_paths(entries)
    write_symbols(entries)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
