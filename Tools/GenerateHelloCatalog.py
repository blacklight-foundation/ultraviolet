#!/usr/bin/env python3
"""Regenerate HelloUltraviolet's generated obligation catalog source."""

from __future__ import annotations

import argparse
import csv
import io
import pathlib
import re
import sys
import time
from collections import defaultdict, deque
from dataclasses import dataclass


ROOT = pathlib.Path(__file__).resolve().parents[1]
CSV_PATH = ROOT / "Docs" / "Internal" / "UltravioletObligations.csv"
CATALOG_ROOT = ROOT / "HelloUltraviolet" / "Source" / "Audit" / "Catalog"
AUDIT_ROOT = ROOT / "HelloUltraviolet" / "Source" / "Audit"
SOURCE_ROOT = ROOT / "HelloUltraviolet" / "Source"
SYMBOL_EXECUTION_ROOT = AUDIT_ROOT / "SymbolExecutions"
FIXTURE_CATALOG_ROOT = AUDIT_ROOT / "FixtureCatalog"
FIXTURE_CATALOG_MODULE = "HelloUltraviolet::Audit::FixtureCatalog"
EXERCISE_QUALITY_MANIFEST = ROOT / "HelloUltraviolet" / "Audit" / "ExerciseQualityManifest.csv"
AUDIT_DIGEST_MODULUS = 1_000_000_007
AUDIT_DIGEST_FACTOR = 257
EXERCISE_QUALITY_GROUP_SIZE = 200
CHECK_MODE = False
CHECK_FAILURES: list[str] = []
EXPECTED_GENERATED_PATHS: set[pathlib.Path] = set()
BLOCKED_OBLIGATIONS: dict[str, str] = {
    "Reject-IllFormed": (
        "conformance reference does not exercise project rejection"
    ),
    "def.RuleDiagnosticIdentity": (
        "conformance reference does not exercise diagnostic identity mapping"
    ),
    "def.DiagnosticCodeComponents": (
        "conformance reference does not exercise diagnostic code components"
    ),
    "def.DiagnosticCodeFormat": (
        "conformance reference does not exercise diagnostic code format"
    ),
    "def.DiagnosticCodeDigitParts": (
        "conformance reference does not exercise diagnostic code digit partitions"
    ),
    "def.Conforming": "conformance reference does not exercise project conformance",
    "def.WF": "conformance reference does not exercise well-formed projects",
    "def.ReqJudgments": (
        "conformance reference does not exercise required judgment sequencing"
    ),
    "def.TypeAndStatementNodes": (
        "conformance reference does not exercise AST node collection"
    ),
    "def.ItemKind": "conformance reference does not exercise item classification",
    "def.TopDeclConstructs": (
        "conformance reference does not exercise top-level declaration constructs"
    ),
    "def.TypeCtor": "conformance reference does not exercise type constructors",
    "def.TypeConstructs": (
        "conformance reference does not exercise type construct aggregation"
    ),
    "def.PermissionConstructs": (
        "conformance reference does not exercise permission construct aggregation"
    ),
    "def.ExprKind": "conformance reference does not exercise expression classification",
    "def.StmtKind": "conformance reference does not exercise statement classification",
    "def.ExprStmtConstructs": (
        "conformance reference does not exercise expression/statement constructs"
    ),
    "def.CapConstructs": (
        "conformance reference does not exercise capability construct aggregation"
    ),
    "def.Constructs": "conformance reference does not exercise construct aggregation",
    "def.StaticJudgmentSet": (
        "conformance reference does not exercise static judgment set"
    ),
    "def.StaticRuleSet": "conformance reference does not exercise static rule set",
    "def.RuleShape": "conformance reference does not exercise formal rule shape",
    "def.RuleSubstitutions": (
        "conformance reference does not exercise formal rule substitutions"
    ),
    "def.RuleApplies": (
        "conformance reference does not exercise formal rule applicability"
    ),
    "def.PremisesHold": (
        "conformance reference does not exercise premise satisfaction"
    ),
    "def.IllFormed": "conformance reference does not exercise ill-formedness",
    "def.StaticUndefined": (
        "conformance reference does not exercise static undefinedness"
    ),
    "def.RuleSectionIndex": (
        "conformance reference does not exercise formal rule section index"
    ),
    "Static-Undefined": (
        "conformance reference does not exercise static undefined diagnostic emission"
    ),
    "Static-Undefined-NoCode": (
        "conformance reference does not exercise uncoded static undefined emission"
    ),
    "def.OutsideConformance": (
        "conformance reference does not exercise outside-conformance boundary"
    ),
    "def.CheckKind": "conformance reference does not exercise check kind set",
    "def.StaticCheck": "conformance reference does not exercise static check set",
    "req.ResourceExhaustionOutsideConformance": (
        "conformance reference does not exercise resource exhaustion boundary"
    ),
    "def.LexRecovery": "conformance reference does not exercise lexical recovery",
    "def.ParseRecovery": "conformance reference does not exercise parse recovery",
    "def.TypeRecovery": "conformance reference does not exercise type recovery",
    "def.MaxErrorCount": (
        "conformance reference does not exercise maximum error count"
    ),
    "def.SuggestedMaxErrorCount": (
        "conformance reference does not exercise suggested maximum error count"
    ),
    "def.AbortOnErrorCount": (
        "conformance reference does not exercise abort-on-error threshold"
    ),
    "def.NormativeKeywords": (
        "conformance reference does not exercise normative keyword set"
    ),
    "conformance.RFC2119": (
        "conformance reference does not exercise RFC 2119 interpretation"
    ),
    "refs.NormativeRefs": (
        "conformance reference does not exercise normative references"
    ),
    "refs.ReferenceDetails": (
        "conformance reference does not exercise normative reference details"
    ),
    "refs.Conformance": (
        "conformance reference does not exercise reference conformance precedence"
    ),
    "def.DiagnosticStream": (
        "diagnostic records reference does not exercise diagnostic streams"
    ),
    "Emit-Append": "diagnostic records reference does not exercise stream append",
    "def.DiagnosticTableColumns": (
        "diagnostic records reference does not exercise diagnostic table columns"
    ),
    "def.EmitImplicit": (
        "diagnostic records reference does not exercise implicit diagnostic emission"
    ),
    "def.EmitList": (
        "diagnostic records reference does not exercise diagnostic list emission"
    ),
    "def.DiagnosticMessageLookup": (
        "diagnostic records reference does not exercise message lookup"
    ),
    "def.CompileStatus": (
        "diagnostic records reference does not exercise compile status"
    ),
    "def.SpecCode": (
        "diagnostic records reference does not exercise spec-code lookup"
    ),
    "conformance.AppendixADiagnosticIndex": (
        "diagnostic records reference does not exercise Appendix A index"
    ),
    "DiagId-Code": (
        "diagnostic records reference does not exercise diagnostic ID to code mapping"
    ),
    "def.DiagIdCodeMapping": (
        "diagnostic records reference does not exercise diagnostic ID/code mapping"
    ),
    "def.DiagnosticRenderRich": (
        "diagnostic rendering reference does not exercise rich rendering"
    ),
    "def.DiagnosticRenderRichHeader": (
        "diagnostic rendering reference does not exercise rich headers"
    ),
    "def.DiagnosticSourceRendering": (
        "diagnostic rendering reference does not exercise source excerpts"
    ),
    "def.TypeRenderLexemes": (
        "diagnostic rendering reference does not exercise type lexemes"
    ),
    "def.TypeRenderStateSuffixes": (
        "diagnostic rendering reference does not exercise state suffixes"
    ),
    "def.ParamRender": (
        "diagnostic rendering reference does not exercise parameter rendering"
    ),
    "def.TypeRender": (
        "diagnostic rendering reference does not exercise type rendering"
    ),
    "def.ModalRefRender": (
        "diagnostic rendering reference does not exercise modal reference rendering"
    ),
    "rule.17.PatternRejectNarrow-Union": (
        "pattern reference does not exercise rejected union narrowing"
    ),
    "rule.18.T-ErrorStmt": (
        "statement reference does not exercise error statement typing"
    ),
    "rule.18.ExecSigma-Error": (
        "statement reference does not exercise error statement execution"
    ),
    "rule.19.K-Dynamic-Index-Conflict": (
        "key conflict reference does not exercise dynamic index conflict"
    ),
    "rule.19.K-Read-Write-Reject": (
        "key conflict reference does not exercise read/write rejection"
    ),
    "rule.19.Spec-Commit-Fail-Retry": (
        "speculative execution reference does not exercise commit-failure retry"
    ),
    "rule.19.Spec-Commit-Fail-Fallback": (
        "speculative execution reference does not exercise commit-failure fallback"
    ),
    "rule.19.Spec-Retry": (
        "speculative execution reference does not exercise retry semantics"
    ),
    "rule.19.Spec-Fallback": (
        "speculative execution reference does not exercise fallback semantics"
    ),
    "requirement.19.SpeculativeAbstractSemanticsAndFallback": (
        "speculative execution reference does not exercise fallback semantics"
    ),
    "rule.21.EvalSigma-YieldFrom-Completed": (
        "async artifact evidence does not exercise yield-from completed source"
    ),
    "rule.21.EvalYieldFromContinue-Suspended": (
        "async artifact evidence does not exercise yield-from continuation suspension"
    ),
    "rule.21.EvalYieldFromContinue-Failed": (
        "async artifact evidence does not exercise yield-from continuation failure"
    ),
    "requirement.21.YieldFromEnterLoweringLoop": (
        "async artifact evidence does not exercise yield-from completed loop entry"
    ),
    "rule.22.CtBuiltin-Read-InvalidPath": (
        "compile-time capability reference does not exercise read invalid path"
    ),
    "rule.22.CtBuiltin-ReadBytes-InvalidPath": (
        "compile-time capability reference does not exercise read_bytes invalid path"
    ),
    "rule.22.CtBuiltin-Exists-InvalidPath": (
        "compile-time capability reference does not exercise exists invalid path"
    ),
    "def.23.ProhibitedFfiType": (
        "FFI safety reference does not exercise prohibited FFI type"
    ),
    "rule.23.Parse-EnsuresPredicate-Error": (
        "foreign contract reference does not exercise invalid ensures predicate"
    ),
    "def.HostPrimitiveMappingAndFailure": (
        "host primitive reference does not exercise primitive mapping failure"
    ),
    "req.UnmappedHostPrimitiveFailureIllFormed": (
        "host primitive reference does not exercise unmapped primitive rejection"
    ),
    "IOPath-OtherFailure": (
        "IO reference does not exercise operation returning IoFailure"
    ),
    "req.NetRestrictHostEqualityEnforcement": (
        "network reference does not exercise rejected host mismatch"
    ),
    "req.NetRestrictRejectBeforeObservableEffect": (
        "network reference does not exercise pre-effect rejection"
    ),
    "Trans-Moved-NoAccess": (
        "binding state reference does not exercise moved-value access diagnostic"
    ),
    "Trans-Partial-NoAccess": (
        "binding state reference does not exercise partial-move access diagnostic"
    ),
    "Trans-Let-NoReassign": (
        "binding state reference does not exercise let reassignment diagnostic"
    ),
    "P-Closure-Escape-Err": (
        "region reference does not exercise escaping closure diagnostic"
    ),
    "req.MalformedAttributeSyntaxIllFormed": (
        "attribute reference does not exercise malformed attribute rejection"
    ),
    "AttrList-Target-Err": (
        "attribute reference does not exercise invalid attribute target"
    ),
    "req.DynamicTargetRestrictions": (
        "diagnostic attribute reference does not exercise dynamic target rejection"
    ),
    "req.EmptyDynamicScopeWarning": (
        "diagnostic attribute reference does not exercise empty dynamic scope warning"
    ),
    "req.15.PreconditionCheckInsertionOwnedByVerificationLogic": (
        "precondition reference does not exercise inserted precondition check"
    ),
    "rule.15.Contract-Dynamic-Check": (
        "verification reference does not exercise dynamic contract check"
    ),
    "rule.15.Check-False": (
        "verification reference does not exercise false check"
    ),
    "rule.15.Check-Fail": (
        "verification reference does not exercise failed check"
    ),
    "rule.15.Insert-Precondition-Check": (
        "verification reference does not exercise precondition check insertion"
    ),
    "rule.15.Insert-Postcondition-Check": (
        "verification reference does not exercise postcondition check insertion"
    ),
    "rule.15.Insert-TypeInv-Construction-Check": (
        "verification reference does not exercise construction invariant insertion"
    ),
    "rule.15.Insert-TypeInv-PreCall-Check": (
        "verification reference does not exercise pre-call invariant insertion"
    ),
    "rule.15.Insert-TypeInv-PostCall-Check": (
        "verification reference does not exercise post-call invariant insertion"
    ),
    "rule.15.Insert-LoopInv-Init-Check": (
        "verification reference does not exercise loop invariant init insertion"
    ),
    "rule.15.Insert-LoopInv-Maintenance-Check": (
        "verification reference does not exercise loop invariant maintenance insertion"
    ),
    "rule.15.Insert-Refinement-Check": (
        "verification reference does not exercise refinement check insertion"
    ),
    "WF-Assembly-Name-Dup-Err": (
        "manifest reference asserts duplicate-name code, not duplicate-name error branch"
    ),
    "Step-Validate-Err": (
        "manifest reference does not exercise validation loader error transition"
    ),
    "FileOrder-Rel-Fail": (
        "module discovery reference does not exercise file-order relativization failure"
    ),
    "WF-Source-Root-Err": (
        "module discovery reference does not exercise invalid source root"
    ),
    "CompilationUnit-Rel-Fail": (
        "module discovery reference does not exercise compilation-unit relativization failure"
    ),
    "req.ImportDeclarationDiagnosticOwnership": (
        "imports reference does not exercise import diagnostics"
    ),
    "Using-Item-Public-Err": (
        "usings reference does not exercise public using item error"
    ),
    "Using-List-Public-Err": (
        "usings reference does not exercise public using list error"
    ),
    "req.UsingDeclarationDiagnosticOwnership": (
        "usings reference does not exercise using declaration diagnostics"
    ),
    "StaticVisOk-Err": (
        "statics reference does not exercise invalid static visibility"
    ),
    "req.StaticDeclarationDiagnosticOwnership": (
        "statics reference does not exercise static diagnostics"
    ),
    "req.ExternBlockDiagnosticOwnership": (
        "extern blocks reference does not exercise extern diagnostics"
    ),
    "grammar.TestAttribute": "source-native test marker is not obligation evidence",
    "parse.TestAttributeByOrdinaryAttributeParser": (
        "source-native test marker is not obligation evidence"
    ),
    "ast.TestProcedureClassification": (
        "source-native test marker is not obligation evidence"
    ),
    "def.TestName": "source-native test marker is not obligation evidence",
    "def.TestCoverage": "source-native test marker is not obligation evidence",
    "def.TestAttributeArgsOk": (
        "source-native test marker is not obligation evidence"
    ),
    "def.15.ReturnAnnOk": "rejected fixture exercises missing return annotation",
    "def.16.PtrNullExpected": "rejected fixture exercises incompatible expected type",
    "def.16.CastValidity": "rejected fixture exercises invalid cast",
    "Unify-Occurs-Fail": "inference reference does not exercise failure path",
    "Unify-Tuple-Fail": "inference reference does not exercise failure path",
    "Unify-Array": "inference reference does not exercise array unification",
    "Unify-Array-Len-Fail": "inference reference does not exercise failure path",
    "Unify-Slice": "inference reference does not exercise slice unification",
    "Unify-Perm": "inference reference does not exercise permission unification",
    "Unify-Perm-Fail": "inference reference does not exercise failure path",
    "Unify-Func": "inference reference does not exercise function type unification",
    "Unify-Func-Fail": "inference reference does not exercise failure path",
    "Unify-Closure": "inference reference does not exercise closure type unification",
    "Unify-Closure-Fail": "inference reference does not exercise failure path",
    "Unify-Ptr": "inference reference does not exercise pointer unification",
    "Unify-Ptr-State-Fail": "inference reference does not exercise failure path",
    "Unify-RawPtr": "inference reference does not exercise raw pointer unification",
    "Unify-RawPtr-Qual-Fail": "inference reference does not exercise failure path",
    "Unify-Apply": "inference reference does not exercise generic type application",
    "Unify-Apply-Fail": "inference reference does not exercise failure path",
    "Unify-Range": "inference reference does not exercise range unification",
    "Unify-RangeInclusive": "inference reference does not exercise inclusive range unification",
    "Unify-RangeFrom": "inference reference does not exercise range-from unification",
    "Unify-RangeTo": "inference reference does not exercise range-to unification",
    "Unify-RangeToInclusive": (
        "inference reference does not exercise range-to-inclusive unification"
    ),
    "Unify-Refine": "inference reference does not exercise refinement unification",
    "Unify-Refine-Pred-Fail": "inference reference does not exercise failure path",
    "Unify-Rigid-Fail": "inference reference does not exercise failure path",
    "Unify-Ctor-Mismatch": (
        "inference reference does not exercise constructor mismatch path"
    ),
    "Unify-Err": "inference reference does not exercise error path",
    "Solve-Fail": "inference reference does not exercise failure path",
    "Syn-Call-Err": "inference reference does not exercise error path",
    "Syn-Unit": "inference reference does not exercise unit synthesis",
    "Chk-Subsumption-Modal-NonNiche": (
        "inference reference does not exercise modal subsumption error path"
    ),
    "Chk-Null-Ptr": "inference reference does not exercise null pointer checking",
    "def.PtrNullExpectedType": (
        "inference reference does not exercise pointer-null expected type"
    ),
    "Syn-PtrNull-Err": "inference reference does not exercise null synthesis error",
    "Chk-PtrNull-Err": "inference reference does not exercise null checking error",
    "Decode-Err": "unicode reference does not exercise invalid UTF-8 input",
    "Step-Decode-Err": "unicode reference does not exercise invalid UTF-8 input",
    "Step-EmbeddedBOM-Err": "unicode reference does not exercise embedded BOM failure",
    "Step-Prohibited-Err": "unicode reference does not exercise prohibited code point",
    "LoadSource-Err": "unicode reference does not exercise source load failure",
    "def.SourceLoadDiagnosticOffsets": (
        "unicode reference does not exercise diagnostic offset mapping"
    ),
    "def.NonCharacter": "literal reference does not exercise non-character input",
    "def.CharacterLiteralInvalid": (
        "literal reference does not exercise invalid character literal"
    ),
    "Lex-Char-Invalid": "literal reference does not exercise invalid character literal",
    "Max-Munch-Err": "literal reference does not exercise token candidate failure",
    "LexSecure-Err": "literal reference does not exercise lexical security error",
    "Confusable-Err": "literal reference does not exercise confusable identifier error",
    "MixedScript-Err": "literal reference does not exercise mixed-script identifier error",
    "def.LexicalSecurityDiagnosticSpans": (
        "literal reference does not exercise lexical security diagnostics"
    ),
    "def.SensitiveCharacters": "literal reference does not exercise sensitive characters",
    "def.LineFeedOrEOFBeforeClose": (
        "literal reference does not exercise unterminated literal"
    ),
    "def.BadEscapeAt": "literal reference does not exercise invalid escape",
    "Lex-Char-Unterminated": (
        "literal reference does not exercise unterminated character literal"
    ),
    "def.LexicalSecurityInputs": (
        "literal reference does not exercise lexical security inputs"
    ),
    "def.LiteralAndCommentRanges": (
        "literal reference does not exercise lexical security range exclusion"
    ),
    "def.SensitivePositionsInSpan": (
        "literal reference does not exercise sensitive Unicode positions"
    ),
    "def.UnsafeTokenSpans": "literal reference does not exercise unsafe token spans",
    "def.LexicalSecurityCheck": (
        "literal reference does not exercise lexical security check"
    ),
    "LexSecure-Warn": "literal reference does not exercise lexical security warning",
    "def.ConfusableIdentifierChecks": (
        "literal reference does not exercise confusable identifier checks"
    ),
    "Lex-String-Unterminated-Recover": (
        "literal reference does not exercise unterminated string recovery"
    ),
    "Lex-Char-Unterminated-Recover": (
        "literal reference does not exercise unterminated character recovery"
    ),
    "Lex-Sensitive": "literal reference does not exercise sensitive tokenization",
    "def.SensitiveTokenPositions": (
        "literal reference does not exercise sensitive token positions"
    ),
    "Lex-Token-Err": "literal reference does not exercise tokenization error",
    "Tokenize-Secure-Err": "literal reference does not exercise lexical security error",
    "Tokenize-Err": "literal reference does not exercise tokenization error",
    "Module-Path-Rel-Fail": "module aggregation reference does not exercise path failure",
    "WF-Module-Path-Ident-Err": (
        "module aggregation reference does not exercise invalid module path"
    ),
    "ReadBytes-Err": "module aggregation reference does not exercise read failure",
    "ParseModule-Err-Read": "module aggregation reference does not exercise read failure",
    "ParseModule-Err-Load": "module aggregation reference does not exercise load failure",
    "ParseModule-Err-Unit": "module aggregation reference does not exercise unit failure",
    "ParseModule-Err-Parse": "module aggregation reference does not exercise parse failure",
    "def.HasErrorDiagnostics": (
        "module aggregation reference does not exercise diagnostic-bearing parse result"
    ),
    "Mod-Start-Err-Unit": "module aggregation reference does not exercise unit failure",
    "Mod-Scan-Err-Read": "module aggregation reference does not exercise read failure",
    "Mod-Scan-Err-Load": "module aggregation reference does not exercise load failure",
    "Mod-Scan-Err-Parse": "module aggregation reference does not exercise parse failure",
    "ParseModules-Err": "module aggregation reference does not exercise module parse failure",
    "WF-Module-Path-Collision": (
        "module aggregation reference does not exercise module path collision"
    ),
    "Disc-Collision": "module aggregation reference does not exercise discovery collision",
    "Disc-Rel-Fail": "module aggregation reference does not exercise discovery failure",
    "ModulePrefix-None": (
        "module aggregation reference does not exercise unresolved module prefix"
    ),
    "ResolveModulePath-Err": (
        "module aggregation reference does not exercise module resolution failure"
    ),
    "ItemOfPath-None": "module aggregation reference does not exercise missing item lookup",
    "Import-Ok-Err": "module aggregation reference does not exercise import failure",
    "Resolve-Using-Err": "module aggregation reference does not exercise using failure",
    "WF-Assembly-Name-Err": "manifest reference does not exercise invalid assembly name",
    "WF-Assembly-Kind-Err": "manifest reference does not exercise invalid assembly kind",
    "WF-Assembly-Root-Path-Err": (
        "manifest reference does not exercise invalid assembly root path"
    ),
    "WF-Assembly-Table-Err": "manifest reference does not exercise invalid assembly table",
    "WF-Assembly-Count-Err": "manifest reference does not exercise zero assembly count",
    "WF-Assembly-Keys-Err": "manifest reference does not exercise invalid assembly keys",
    "WF-Build-Err": "manifest reference does not exercise invalid build table",
    "Resolve-Canonical-Err": "manifest reference does not exercise canonicalization failure",
    "WF-RelPath-Err": "manifest reference does not exercise invalid relative path",
    "Step-Asm-Err": "manifest reference does not exercise assembly load failure",
    "Step-Asm-Own-Err": "manifest reference does not exercise assembly ownership failure",
    "Step-Asm-Done-Err": (
        "manifest reference does not exercise final assembly selection failure"
    ),
    "Select-Err": "assembly model reference does not exercise assembly selection error",
    "BuildAssembly-Err-Resolve": (
        "assembly model reference does not exercise assembly resolve failure"
    ),
    "BuildAssembly-Err-Root": (
        "assembly model reference does not exercise assembly root failure"
    ),
    "WF-Assembly-Root-Owner-Ambiguous": (
        "assembly model reference does not exercise ambiguous root owner"
    ),
    "Intro-Dup": "qualified resolution reference does not exercise duplicate introduction",
    "Intro-Outer-Err": (
        "qualified resolution reference does not exercise outer-scope introduction error"
    ),
    "Intro-Reserved-Gen-Err": (
        "qualified resolution reference does not exercise reserved-name error"
    ),
    "Intro-Reserved-Ultraviolet-Err": (
        "qualified resolution reference does not exercise reserved-name error"
    ),
    "Validate-Module-Keyword-Err": (
        "qualified resolution reference does not exercise keyword module path error"
    ),
    "Lookup-Unqualified-None": (
        "qualified resolution reference does not exercise missing unqualified lookup"
    ),
    "Bind-ErrorItem": "qualified resolution reference does not exercise error item binding",
    "Collect-Using-Import-Dup": (
        "qualified resolution reference does not exercise using/import duplication"
    ),
    "Collect-Err": "qualified resolution reference does not exercise collection error",
    "def.UsingImportConflict": (
        "qualified resolution reference does not exercise using/import conflict"
    ),
    "Names-Step-Using-Import-Dup": (
        "qualified resolution reference does not exercise using/import duplication"
    ),
    "Names-Step-Dup": "qualified resolution reference does not exercise duplicate name step",
    "Names-Step-Err": "qualified resolution reference does not exercise name step error",
    "ResolveExpr-Ident-Err": (
        "qualified resolution reference does not exercise identifier resolution error"
    ),
    "ResolveKeyPathExpr-Err": (
        "qualified resolution reference does not exercise key path resolution error"
    ),
    "Validate-ModulePath-Reserved-Err": (
        "qualified resolution reference does not exercise reserved module path error"
    ),
    "ResolveModules-Err-Parse": (
        "qualified resolution reference does not exercise module parse error"
    ),
    "ResolveModules-Err-Resolve": (
        "qualified resolution reference does not exercise module resolution error"
    ),
    "rule.13.WF-ModalState-ArgCount-Err": (
        "modal declaration reference does not exercise invalid state argument count"
    ),
    "rule.13.StateMemberVisOk-Err": (
        "modal declaration reference does not exercise state member visibility error"
    ),
    "rule.13.Modal-NoStates-Err": (
        "modal declaration reference does not exercise modal with no states"
    ),
    "rule.13.Modal-DupState-Err": (
        "modal declaration reference does not exercise duplicate modal state"
    ),
    "rule.13.Modal-StateName-Err": (
        "modal declaration reference does not exercise invalid modal state name"
    ),
    "rule.13.Region-Unchecked-Unsafe-Err": (
        "modal declaration reference does not exercise unsafe region error"
    ),
    "rule.13.StateMember-Name-Conflict": (
        "modal transition reference does not exercise member name conflict"
    ),
    "rule.13.Transition-Target-Err": (
        "modal transition reference does not exercise invalid transition target"
    ),
    "rule.13.Transition-Source-Err": (
        "modal transition reference does not exercise invalid transition source"
    ),
    "rule.13.Transition-Body-Err": (
        "modal transition reference does not exercise invalid transition body"
    ),
    "rule.13.BytesWithCapacity-Err": (
        "bytes reference does not exercise allocation failure"
    ),
    "rule.13.BytesFromSlice-Err": (
        "bytes reference does not exercise allocation failure"
    ),
    "rule.13.BytesToManaged-Err": (
        "bytes reference does not exercise allocation failure"
    ),
    "rule.13.BytesAppend-Err": "bytes reference does not exercise append failure",
    "rule.13.StringFrom-Err": (
        "string reference does not exercise allocation failure"
    ),
    "rule.13.StringToManaged-Err": (
        "string reference does not exercise allocation failure"
    ),
    "rule.13.StringCloneWith-Err": (
        "string reference does not exercise allocation failure"
    ),
    "rule.13.StringAppend-Err": "string reference does not exercise append failure",
    "rule.24.Lower-ReadVarIR-Err": (
        "backend reference does not exercise variable read lowering error"
    ),
    "rule.24.Lower-ReadPathIR-Static-User": (
        "backend reference does not exercise user static path lowering"
    ),
    "rule.24.Lower-ReadPathIR-Static-Gen": (
        "backend reference does not exercise generated static path lowering"
    ),
    "rule.24.Lower-ReadPathIR-Proc-Gen": (
        "backend reference does not exercise generated procedure path lowering"
    ),
    "rule.24.Lower-ReadPathIR-Runtime": (
        "backend reference does not exercise runtime path lowering"
    ),
    "rule.24.Lower-MoveStateIR": (
        "backend reference does not exercise modal state movement"
    ),
    "rule.24.Lower-StoreGlobal": (
        "backend reference does not exercise global storage lowering"
    ),
    "def.24.PtrType": "backend reference does not exercise pointer types",
    "rule.24.Lower-ReadPtrIR": "backend reference does not exercise pointer reads",
    "rule.24.Lower-ReadPtrIR-Raw": (
        "backend reference does not exercise raw pointer reads"
    ),
    "rule.24.Lower-ReadPtrIR-Null": (
        "backend reference does not exercise null pointer reads"
    ),
    "rule.24.Lower-ReadPtrIR-Expired": (
        "backend reference does not exercise expired pointer reads"
    ),
    "rule.24.Lower-WritePtrIR": "backend reference does not exercise pointer writes",
    "rule.24.Lower-WritePtrIR-Null": (
        "backend reference does not exercise null pointer writes"
    ),
    "rule.24.Lower-WritePtrIR-Expired": (
        "backend reference does not exercise expired pointer writes"
    ),
    "rule.24.Lower-WritePtrIR-Raw": (
        "backend reference does not exercise raw pointer writes"
    ),
    "rule.24.Lower-WritePtrIR-Raw-Err": (
        "backend reference does not exercise raw pointer write error"
    ),
    "rule.24.Lower-AddrOfIR": (
        "backend reference does not exercise address-of lowering"
    ),
    "def.24.DynamicDispatchHelpers": (
        "backend reference does not exercise dynamic dispatch helpers"
    ),
    "rule.24.Lower-CallVTable": (
        "backend reference does not exercise vtable call lowering"
    ),
    "rule.24.LowerIRInstr-ClearPanic": (
        "backend reference does not exercise panic flag clearing"
    ),
    "rule.24.LowerIRInstr-PanicCheck": (
        "backend reference does not exercise panic checking"
    ),
    "rule.24.LowerIRInstr-CheckPoison": (
        "backend reference does not exercise poison checking"
    ),
    "rule.24.LowerIRInstr-LowerPanic": (
        "backend reference does not exercise panic lowering"
    ),
    "rule.24.LowerIRDecl-Err": (
        "backend reference does not exercise declaration lowering error"
    ),
    "rule.24.LowerIRInstr-Err": (
        "backend reference does not exercise instruction lowering error"
    ),
    "def.24.BindRegionTarget": (
        "backend reference does not exercise binding region targets"
    ),
    "req.24.ResolveTargetNearestLiveAlias": (
        "backend reference does not exercise live-alias target resolution"
    ),
    "rule.24.BindSlot-Param-ByRef": (
        "backend reference does not exercise by-reference parameter binding"
    ),
    "rule.24.BindSlot-Region": (
        "backend reference does not exercise region binding"
    ),
    "rule.24.BindSlot-Static": (
        "backend reference does not exercise static binding"
    ),
    "rule.24.UpdateValid-PartialMove-Init": (
        "backend reference does not exercise partial move initialization"
    ),
    "rule.24.UpdateValid-PartialMove-Step": (
        "backend reference does not exercise partial move validity update"
    ),
    "def.24.DropOnAssignHelpers": (
        "backend reference does not exercise drop-on-assign helpers"
    ),
    "rule.24.DropOnAssign-Record-Valid": (
        "backend reference does not exercise valid-record drop-on-assign"
    ),
    "rule.24.DropOnAssign-Record-Partial": (
        "backend reference does not exercise partial-record drop-on-assign"
    ),
    "rule.24.DropOnAssign-Record-Moved": (
        "backend reference does not exercise moved-record drop-on-assign"
    ),
    "rule.24.DropOnAssign-Aggregate-Ok": (
        "backend reference does not exercise aggregate drop-on-assign"
    ),
    "rule.24.DropOnAssign-Aggregate-Moved": (
        "backend reference does not exercise moved-aggregate drop-on-assign"
    ),
    "rule.24.BindSlot-Err": (
        "backend reference does not exercise binding slot error"
    ),
    "rule.24.BindValid-Err": (
        "backend reference does not exercise binding validity error"
    ),
    "rule.24.UpdateValid-Err": (
        "backend reference does not exercise validity update error"
    ),
    "rule.24.DropOnAssign-Err": (
        "backend reference does not exercise drop-on-assign error"
    ),
    "rule.24.EmitLiteral-Err": (
        "backend reference does not exercise literal emission error"
    ),
    "rule.24.PoisonFlag-Err": (
        "backend reference does not exercise poison flag error"
    ),
    "rule.24.CheckPoison-Err": (
        "backend reference does not exercise poison check error"
    ),
    "rule.24.SetPoison-Err": (
        "backend reference does not exercise poison set error"
    ),
    "rule.24.Topo-Cycle": (
        "emit-LL artifact evidence does not exercise init graph cycle"
    ),
    "rule.24.EmitInitPlan-Err": (
        "emit-LL artifact evidence does not exercise init plan error"
    ),
    "rule.24.EmitDeinitPlan-Err": (
        "emit-LL artifact evidence does not exercise deinit plan error"
    ),
    "rule.24.Init-Panic": (
        "emit-LL artifact evidence does not exercise static initializer panic"
    ),
    "rule.24.Init-Fail": (
        "emit-LL artifact evidence does not exercise static initializer failure"
    ),
    "rule.24.Deinit-Panic": (
        "emit-LL artifact evidence does not exercise static deinitializer panic"
    ),
    "rule.24.EntrySym-Err": (
        "emit-LL artifact evidence does not exercise entry symbol error"
    ),
    "rule.24.EntryStub-Err": (
        "emit-LL artifact evidence does not exercise entry stub error"
    ),
    "rule.24.Interpret-Project-Init-Panic": (
        "emit-LL artifact evidence does not exercise project init panic interpretation"
    ),
    "rule.24.Interpret-Project-Deinit-Panic": (
        "emit-LL artifact evidence does not exercise project deinit panic interpretation"
    ),
    "rule.24.DropValueOut-DropPanic": (
        "emit-LL artifact evidence does not exercise direct drop panic"
    ),
    "rule.24.DropValueOut-ChildPanic": (
        "emit-LL artifact evidence does not exercise child drop panic"
    ),
    "rule.24.Cleanup-Step-Drop-Panic": (
        "emit-LL artifact evidence does not exercise drop cleanup panic"
    ),
    "rule.24.Cleanup-Step-DropStatic-Panic": (
        "emit-LL artifact evidence does not exercise static drop cleanup panic"
    ),
    "rule.24.Cleanup-Step-Defer-Panic": (
        "emit-LL artifact evidence does not exercise defer cleanup panic"
    ),
    "rule.24.Cleanup-Cons-Drop-Panic": (
        "emit-LL artifact evidence does not exercise drop cleanup panic construction"
    ),
    "rule.24.Cleanup-Cons-DropStatic-Panic": (
        "emit-LL artifact evidence does not exercise static cleanup panic construction"
    ),
    "rule.24.Cleanup-Cons-Defer-Panic": (
        "emit-LL artifact evidence does not exercise defer cleanup panic construction"
    ),
    "def.24.LLVMToolchain": (
        "emit-LL artifact evidence only checks logged LLVM payload"
    ),
    "req.24.HostedCompilerLLVMVersion": (
        "emit-LL artifact evidence only checks logged LLVM version payload"
    ),
    "rule.24.LLVMArgLower-Err": (
        "emit-LL artifact evidence does not exercise invalid argument lowering"
    ),
    "rule.24.LLVMRetLower-Err": (
        "emit-LL artifact evidence does not exercise invalid return lowering"
    ),
    "rule.24.EmitVTable-Err": (
        "emit-LL artifact evidence does not exercise invalid vtable emission"
    ),
    "rule.24.SetPoison-OnInitFail": (
        "emit-LL artifact evidence does not exercise poison set on init failure"
    ),
    "req.TestProcedureShape": "source-native test marker is not obligation evidence",
    "req.TestAuthority": "source-native test marker is not obligation evidence",
    "conformance.TestAttributeDynamicSemantics": (
        "source-native test marker is not obligation evidence"
    ),
    "def.TestDiscoveryOrder": "source-native test marker is not obligation evidence",
    "diagnostics.TestAttributes": (
        "source-native test marker is not obligation evidence"
    ),
}
STALE_FIXTURE_TARGET_OBLIGATIONS: set[str] = {
    "Sub-Generic-Contravariant-Err",
    "Sub-Generic-Covariant-Err",
    "Sub-Generic-Invariant-Err",
    "conformance.UnsafeForeignAuthority",
    "diag-table.14.RefinementPolymorphismDiagnostics",
    "diag-table.15.ProcedureContractEntryDiagnostics",
    "diag.14.RefinementPolymorphismDiagnosticsOwnership",
    "diag.15.ProcedureContractEntryDiagnosticsOwnership",
    "diag.16.ExpressionDiagnosticsSupplement",
    "diag.17.PatternDiagnosticsSupplement",
    "diag.18.StatementDiagnosticsSupplement",
    "diagnostics.20.StructuredParallelismSupplement",
    "diagnostics.21.AsyncDiagnosticsSupplement",
    "diagnostics.22.CompileTimeDiagnosticsSupplement",
    "diagnostics.23.FfiDiagnosticsSupplement",
    "diagnostics.CommandLineDiagnostics",
    "diagnostics.CoreTypeDiagnostics",
    "diagnostics.NameResolutionAndReservedNames",
    "diagnostics.ParsingDiagnostics",
    "diagnostics.RuntimeStateAndMemoryDiagnostics",
    "diagnostics.SourceLexicalDiagnostics",
    "lowering.TestHarnessGeneration",
    "req.18.AssignmentConstPermissionDiagnostic",
    "req.TestAttributeProcedureTarget",
    "requirement.20.StructuredParallelismRuntimePanicOwnership",
    "requirement.22.UserDiagnosticBuiltinEmission",
    "rule.13.P-Deref-Raw-Imm",
    "rule.13.P-Deref-Raw-Mut",
    "rule.13.T-Deref-Raw",
    "rule.16.DerefPlaceTypingFamily",
    "rule.16.RangeTypingFamily",
    "rule.16.T-Arith",
    "rule.16.T-Bitwise",
    "rule.16.T-Compare-Eq",
    "rule.16.T-Compare-Ord",
    "rule.16.T-Deref-Raw",
    "rule.16.T-Logical",
    "rule.16.T-Neg",
    "rule.16.T-Not-Bool",
    "rule.16.T-Not-Int",
    "rule.16.T-Range-Lift",
    "rule.16.T-Shift",
}


def display_path(path: pathlib.Path) -> str:
    return path.relative_to(ROOT).as_posix()


def write_generated(path: pathlib.Path, text: str) -> None:
    EXPECTED_GENERATED_PATHS.add(path)
    expected_bytes = text.encode("utf-8")
    if CHECK_MODE:
        if not path.exists():
            CHECK_FAILURES.append(f"missing generated file: {display_path(path)}")
            return
        if path.read_bytes() != expected_bytes:
            CHECK_FAILURES.append(f"out-of-date generated file: {display_path(path)}")
        return

    if path.exists() and path.read_bytes() == expected_bytes:
        return

    for attempt in range(5):
        try:
            path.write_bytes(expected_bytes)
            return
        except OSError:
            if attempt == 4:
                raise
            time.sleep(0.25 * (attempt + 1))


def remove_generated(path: pathlib.Path) -> None:
    if CHECK_MODE:
        return
    path.unlink()


def check_stale_generated(patterns: list[tuple[pathlib.Path, str]]) -> None:
    if not CHECK_MODE:
        return
    for root, pattern in patterns:
        for path in root.glob(pattern):
            if path not in EXPECTED_GENERATED_PATHS:
                CHECK_FAILURES.append(f"stale generated file: {display_path(path)}")


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


def is_blocked_obligation(row: "CsvRow", blocked_ids: set[str]) -> bool:
    return row.obligation_id in blocked_ids


def blocked_obligation_rows(
    rows: list["CsvRow"],
    blocked_ids: set[str],
) -> list["CsvRow"]:
    return [row for row in rows if is_blocked_obligation(row, blocked_ids)]


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


def is_compiled_symbol_execution_target(symbol: str) -> bool:
    is_run_reference = (
        re.match(r"^run[A-Z][A-Za-z0-9]*(Reference|ReferenceModels)$", symbol)
        is not None
    )
    is_fixture_count = (
        symbol.startswith("validated") and symbol.endswith("FixtureCount")
    )
    return is_run_reference or is_fixture_count


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
FIXTURE_ENTRY_OBLIGATION_RE = re.compile(
    r'\b[A-Za-z0-9]+(?:EntryCount|EntryMatches|SpecimenMatches)\('
    r'\s*(?:"[^"]*",\s*)+"([^"]+)"\s*\)',
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
PROCEDURE_RE = re.compile(
    r"(?m)^(?P<visibility>public|internal|private)\s+procedure\s+"
    r"(?P<name>[A-Za-z_][A-Za-z0-9_]*)\b"
)


ACCEPTED_HELPER = "acceptedObligationEntryMatches"
ACCEPTED_PROJECT_HELPER = "acceptedProjectObligationEntryMatches"
REJECTED_SOURCE_HELPER = "rejectedSourceObligationEntryMatches"
DIAGNOSTIC_SOURCE_HELPER = "diagnosticSourceObligationEntryMatches"
ARTIFACT_BEHAVIOR_HELPER = "artifactBehaviorObligationEntryMatches"
REFERENCE_MODEL_HELPER = "referenceModelObligationEntryMatches"
CATALOG_AUDIT_HELPER_IMPORTS = {
    "acceptedProjectConformanceContains",
    "acceptedProjectConformanceLineContains",
    "acceptedProjectRunCase",
    "acceptedProjectTempFileContains",
    "acceptedProjectTempFileExists",
    "artifactProjectRunCase",
    "artifactProjectTempFileExcludes",
    "diagnosticExerciseFixture",
    "diagnosticExerciseProjectFixtureDirectory",
    "diagnosticIoSucceeded",
}


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


@dataclass(frozen=True)
class ProcedureInfo:
    path: pathlib.Path
    name: str
    attributes: tuple[str, ...]
    body: str
    normalized_body: str
    body_class: str


@dataclass(frozen=True)
class ProcedureCallGraphs:
    precise_graph: dict[tuple[pathlib.Path, str], set[tuple[pathlib.Path, str]]]
    approximate_graph: dict[tuple[pathlib.Path, str], set[tuple[pathlib.Path, str]]]


@dataclass(frozen=True)
class ExerciseQualityEntry:
    obligation_id: str
    target_module_path: str
    target_symbol: str
    target_source_path: str
    is_constant_literal_result: bool
    is_executed: bool
    is_broad_appendix_grammar_row: bool
    uses_appendix_b_composite_target: bool


def is_fixture_catalog_location(module_path: str, symbol: str) -> bool:
    return (
        module_path.startswith(f"{FIXTURE_CATALOG_MODULE}::") and
        symbol.startswith("validated") and
        symbol.endswith("FixtureCount")
    )


def is_fixture_catalog_reference_target(target: ReferenceTarget) -> bool:
    return is_fixture_catalog_location(target.module_path, target.symbol)


def is_fixture_catalog_fixture_target(target: FixtureTarget) -> bool:
    return is_fixture_catalog_location(target.module_path, target.symbol)


def explicit_non_fixture_target_obligation_ids(rows: list[CsvRow]) -> set[str]:
    obligation_ids: set[str] = set()
    for row in rows:
        try:
            target = missing_target(row)
        except RuntimeError:
            continue
        if not is_fixture_catalog_reference_target(target):
            obligation_ids.add(row.obligation_id)
    return obligation_ids


def fixture_marker_obligation_ids(rows: list[CsvRow]) -> set[str]:
    fixture_ids = {
        obligation_id
        for obligation_id, target in fixture_obligation_targets().items()
        if is_fixture_catalog_fixture_target(target)
    }
    for path in sorted(FIXTURE_CATALOG_ROOT.rglob("*.uv")):
        text = path.read_text(encoding="utf-8")
        fixture_ids.update(FIXTURE_EXPECTED_RE.findall(text))
        fixture_ids.update(ACCEPTED_PROJECT_EXPECTATION_RE.findall(text))
        fixture_ids.update(ARTIFACT_EXPECTATION_RE.findall(text))
        fixture_ids.update(FIXTURE_ENTRY_OBLIGATION_RE.findall(text))
    return fixture_ids - explicit_non_fixture_target_obligation_ids(rows)


def blocked_obligation_ids(rows: list[CsvRow]) -> set[str]:
    return (
        set(BLOCKED_OBLIGATIONS) |
        STALE_FIXTURE_TARGET_OBLIGATIONS |
        fixture_marker_obligation_ids(rows)
    )


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


def async_composition_runtime_target(
    existing_path: pathlib.Path,
    symbol: str,
) -> ReferenceTarget:
    return ReferenceTarget(
        path=existing_path,
        module_path="HelloUltraviolet::Reference::Async",
        symbol=symbol,
        source_path="Source/Reference/Async/CombinatorRuntimeForms.uv",
    )


def key_paths_target(existing_path: pathlib.Path) -> ReferenceTarget:
    return ReferenceTarget(
        path=existing_path,
        module_path="HelloUltraviolet::Reference::Keys",
        symbol="runKeysKeyPathsReference",
        source_path="Source/Reference/Keys/KeyPaths.uv",
    )


def key_acquisition_blocks_target() -> ReferenceTarget:
    return ReferenceTarget(
        path=CATALOG_ROOT / "KeySystem" / "KeyAcquisitionBlocks.uv",
        module_path="HelloUltraviolet::Reference::Keys",
        symbol="runKeysAcquisitionBlocksReference",
        source_path="Source/Reference/Keys/AcquisitionBlocks.uv",
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
            "HelloUltraviolet::Reference::AppendixB",
            "runAppendixBLexicalGrammarReference",
            "Source/Reference/AppendixB/CompleteGrammar.uv",
        ),
        "grammar.B.2.TypeGrammar": (
            "HelloUltraviolet::Reference::AppendixB",
            "runAppendixBTypeGrammarReference",
            "Source/Reference/AppendixB/CompleteGrammar.uv",
        ),
        "req.B.2.ClosureTypeUnionParameterParentheses": (
            "HelloUltraviolet::Reference::ModalTypes",
            "runModalTypesClosuresReference",
            "Source/Reference/ModalTypes/Closures.uv",
        ),
        "grammar.B.2.GenericRefinementModalTypeGrammar": (
            "HelloUltraviolet::Reference::AppendixB",
            "runAppendixBGenericRefinementModalTypeGrammarReference",
            "Source/Reference/AppendixB/CompleteGrammar.uv",
        ),
        "grammar.B.3.ExpressionGrammar": (
            "HelloUltraviolet::Reference::AppendixB",
            "runAppendixBExpressionGrammarReference",
            "Source/Reference/AppendixB/CompleteGrammar.uv",
        ),
        "req.B.3.ClosureExprUnionParameterParentheses": (
            "HelloUltraviolet::Reference::Expressions",
            "runExpressionsClosuresAndPipelinesReference",
            "Source/Reference/Expressions/ClosuresAndPipelines.uv",
        ),
        "grammar.B.3.ControlAndSpecialExpressionGrammar": (
            "HelloUltraviolet::Reference::AppendixB",
            "runAppendixBControlAndSpecialExpressionGrammarReference",
            "Source/Reference/AppendixB/CompleteGrammar.uv",
        ),
        "grammar.B.4.PatternGrammar": (
            "HelloUltraviolet::Reference::AppendixB",
            "runAppendixBPatternGrammarReference",
            "Source/Reference/AppendixB/CompleteGrammar.uv",
        ),
        "grammar.B.5.StatementGrammar": (
            "HelloUltraviolet::Reference::AppendixB",
            "runAppendixBStatementGrammarReference",
            "Source/Reference/AppendixB/CompleteGrammar.uv",
        ),
        "grammar.B.6.DeclarationGrammar": (
            "HelloUltraviolet::Reference::AppendixB",
            "runAppendixBDeclarationGrammarReference",
            "Source/Reference/AppendixB/CompleteGrammar.uv",
        ),
        "grammar.B.7.ContractGrammar": (
            "HelloUltraviolet::Reference::AppendixB",
            "runAppendixBContractGrammarReference",
            "Source/Reference/AppendixB/CompleteGrammar.uv",
        ),
        "grammar.B.8.AttributeGrammar": (
            "HelloUltraviolet::Reference::AppendixB",
            "runAppendixBAttributeGrammarReference",
            "Source/Reference/AppendixB/CompleteGrammar.uv",
        ),
        "grammar.B.9.KeySystemGrammar": (
            "HelloUltraviolet::Reference::AppendixB",
            "runAppendixBKeySystemGrammarReference",
            "Source/Reference/AppendixB/CompleteGrammar.uv",
        ),
        "grammar.B.10.ConcurrencyGrammar": (
            "HelloUltraviolet::Reference::AppendixB",
            "runAppendixBConcurrencyGrammarReference",
            "Source/Reference/AppendixB/CompleteGrammar.uv",
        ),
        "grammar.B.11.AsyncGrammar": (
            "HelloUltraviolet::Reference::AppendixB",
            "runAppendixBAsyncGrammarReference",
            "Source/Reference/AppendixB/CompleteGrammar.uv",
        ),
        "grammar.B.12.MetaprogrammingGrammar": (
            "HelloUltraviolet::Reference::AppendixB",
            "runAppendixBMetaprogrammingGrammarReference",
            "Source/Reference/AppendixB/CompleteGrammar.uv",
        ),
        "grammar.B.13.FFIGrammar": (
            "HelloUltraviolet::Reference::AppendixB",
            "runAppendixBFFIGrammarReference",
            "Source/Reference/AppendixB/CompleteGrammar.uv",
        ),
        "grammar.B.14.RegionGrammar": (
            "HelloUltraviolet::Reference::AppendixB",
            "runAppendixBRegionGrammarReference",
            "Source/Reference/AppendixB/CompleteGrammar.uv",
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
        module_path="HelloUltraviolet::Tests",
        symbol="runAttributesSourceNativeTestsReference",
        source_path="Source/Tests/SourceNativeTests.uv",
    )


def source_native_dynamic_semantics_target() -> ReferenceTarget:
    return ReferenceTarget(
        path=CATALOG_ROOT / "AttributesAndMetadata" / "SourceNativeTestAttributes.uv",
        module_path="HelloUltraviolet::Audit",
        symbol="attributesSourceNativeDynamicSemanticsExecute",
        source_path="Source/Audit/AttributesMetadataArtifactExecution.uv",
    )


def source_native_discovery_order_target() -> ReferenceTarget:
    return ReferenceTarget(
        path=CATALOG_ROOT / "AttributesAndMetadata" / "SourceNativeTestAttributes.uv",
        module_path="HelloUltraviolet::Audit",
        symbol="attributesSourceNativeDiscoveryOrderExecute",
        source_path="Source/Audit/AttributesMetadataArtifactExecution.uv",
    )


def source_native_metadata_discovery_target() -> ReferenceTarget:
    return ReferenceTarget(
        path=CATALOG_ROOT / "AttributesAndMetadata" / "SourceNativeTestAttributes.uv",
        module_path="HelloUltraviolet::Audit",
        symbol="attributesSourceNativeMetadataDiscoveryExecute",
        source_path="Source/Audit/AttributesMetadataArtifactExecution.uv",
    )


def source_native_authority_shape_target() -> ReferenceTarget:
    return ReferenceTarget(
        path=CATALOG_ROOT / "AttributesAndMetadata" / "SourceNativeTestAttributes.uv",
        module_path="HelloUltraviolet::Audit",
        symbol="attributesSourceNativeAuthorityAndShapeExecute",
        source_path="Source/Audit/AttributesMetadataArtifactExecution.uv",
    )


def source_native_diagnostics_target() -> ReferenceTarget:
    return ReferenceTarget(
        path=CATALOG_ROOT / "AttributesAndMetadata" / "SourceNativeTestAttributes.uv",
        module_path="HelloUltraviolet::Audit",
        symbol="attributesSourceNativeDiagnosticsCompositeExecute",
        source_path="Source/Audit/AttributesMetadataArtifactExecution.uv",
    )


SOURCE_NATIVE_METADATA_DISCOVERY_OBLIGATIONS = {
    "parse.TestAttributeByOrdinaryAttributeParser",
    "ast.TestProcedureClassification",
    "def.TestName",
    "def.TestCoverage",
    "def.TestAttributeArgsOk",
}

SOURCE_NATIVE_AUTHORITY_SHAPE_OBLIGATIONS = {
    "req.TestProcedureShape",
    "req.TestAuthority",
}


def general_attributes_target() -> ReferenceTarget:
    return ReferenceTarget(
        path=CATALOG_ROOT / "AttributesAndMetadata" / "AttributeSyntaxAndPlacement.uv",
        module_path="HelloUltraviolet::Reference::Attributes",
        symbol="runAttributesGeneralAttributesReference",
        source_path="Source/Reference/Attributes/GeneralAttributes.uv",
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

    if row.obligation_id == "rule.21.EvalSigma-Map-Resume-Complete":
        return async_composition_runtime_target(
            target.path,
            "runAsyncCompositionMapCompletionReference",
        )
    if row.obligation_id == "rule.21.EvalSigma-Map-Resume-Failed":
        return async_composition_runtime_target(
            target.path,
            "runAsyncCompositionMapFailureReference",
        )
    if row.obligation_id == "rule.21.EvalSigma-Fold-Resume-Failed":
        return async_composition_runtime_target(
            target.path,
            "runAsyncCompositionFoldFailureReference",
        )
    if row.obligation_id == "rule.21.EvalSigma-Chain-Resume-Source-Failed":
        return async_composition_runtime_target(
            target.path,
            "runAsyncCompositionChainSourceFailureReference",
        )
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
    if row.obligation_id == "conformance.TestAttributeDynamicSemantics":
        return source_native_dynamic_semantics_target()
    if row.obligation_id == "def.TestDiscoveryOrder":
        return source_native_discovery_order_target()
    if row.obligation_id in SOURCE_NATIVE_METADATA_DISCOVERY_OBLIGATIONS:
        return source_native_metadata_discovery_target()
    if row.obligation_id in SOURCE_NATIVE_AUTHORITY_SHAPE_OBLIGATIONS:
        return source_native_authority_shape_target()
    if row.obligation_id == "diagnostics.TestAttributes":
        return source_native_diagnostics_target()
    if row.obligation_id in {
        "grammar.TestAttribute",
        "req.TestAttributeProcedureTarget",
        "lowering.TestHarnessGeneration",
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
            if is_fixture_catalog_reference_target(target):
                continue
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
    if row.obligation_id in SOURCE_NATIVE_METADATA_DISCOVERY_OBLIGATIONS:
        return target, ACCEPTED_HELPER
    if row.obligation_id in SOURCE_NATIVE_AUTHORITY_SHAPE_OBLIGATIONS:
        return target, ACCEPTED_HELPER
    if row.obligation_id == "diagnostics.TestAttributes":
        return target, ACCEPTED_HELPER
    fixture = fixture_targets.get(row.obligation_id)
    if fixture is None or is_fixture_catalog_fixture_target(fixture):
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
    output_artifacts = (
        CATALOG_ROOT
        / "ProjectAndCompilationModel"
        / "OutputArtifactsAndLinking.uv"
    )
    deterministic_ordering = (
        CATALOG_ROOT
        / "ProjectAndCompilationModel"
        / "DeterministicOrderingAndCaseFolding.uv"
    )
    control_expressions = CATALOG_ROOT / "Expressions" / "ControlExpressions.uv"
    basic_patterns = CATALOG_ROOT / "Patterns" / "BasicPatterns.uv"
    case_clauses = CATALOG_ROOT / "Patterns" / "CaseClauses.uv"
    statement_blocks = CATALOG_ROOT / "StatementsAndBlocks" / "Blocks.uv"
    async_key_integration = (
        CATALOG_ROOT
        / "AsynchronousOperations"
        / "AsyncKeyIntegration.uv"
    )

    time_target = ReferenceTarget(
        path=host_primitives,
        module_path="HelloUltraviolet::Reference::Authority",
        symbol="runAuthorityTimeReference",
        source_path="Source/Reference/Authority/Time.uv",
    )
    io_target = ReferenceTarget(
        path=host_primitives,
        module_path="HelloUltraviolet::Reference::Authority",
        symbol="runAuthorityIOReference",
        source_path="Source/Reference/Authority/IO.uv",
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

    def artifact_output_target(symbol: str) -> ReferenceTarget:
        return ReferenceTarget(
            path=output_artifacts,
            module_path="HelloUltraviolet::Audit",
            symbol=symbol,
            source_path="Source/Audit/ArtifactProjectExecution.uv",
        )

    if row.obligation_id == "DirSeq-Rel-Fail":
        return ReferenceTarget(
            path=deterministic_ordering,
            module_path="HelloUltraviolet::Reference::Projects",
            symbol="moduleDiscoveryRelativizeFailureReference",
            source_path="Source/Reference/Projects/ModuleDiscovery.uv",
        )

    if row.obligation_id == "Out-Obj-Collision":
        return ReferenceTarget(
            path=output_artifacts,
            module_path="HelloUltraviolet::Reference::Projects",
            symbol="outputObjectPathCollisionReference",
            source_path="Source/Reference/Projects/OutputArtifacts.uv",
        )

    if row.obligation_id == "Out-IR-Collision":
        return ReferenceTarget(
            path=output_artifacts,
            module_path="HelloUltraviolet::Reference::Projects",
            symbol="outputIrPathCollisionReference",
            source_path="Source/Reference/Projects/OutputArtifacts.uv",
        )

    if row.obligation_id == "rule.17.Lower-Pat-Err":
        return ReferenceTarget(
            path=case_clauses,
            module_path="HelloUltraviolet::Reference::Patterns",
            symbol="patternLowerBindFailureReference",
            source_path="Source/Reference/Patterns/CaseClauses.uv",
        )

    if row.obligation_id == "rule.18.Lower-Stmt-Error":
        return ReferenceTarget(
            path=statement_blocks,
            module_path="HelloUltraviolet::Reference::Statements",
            symbol="lowerStmtErrorReference",
            source_path="Source/Reference/Statements/Errors.uv",
        )

    if row.obligation_id == "rule.21.Lower-Wait-Key-Illegal":
        return ReferenceTarget(
            path=async_key_integration,
            module_path="HelloUltraviolet::Reference::Async",
            symbol="asyncWaitHeldKeyLoweringFailureReference",
            source_path="Source/Reference/Async/AsyncKeyIntegration.uv",
        )

    if row.obligation_id in {
        "Parse-Attribute",
        "Parse-AttrArgsOpt-Empty",
    }:
        return general_attributes_target()

    if row.obligation_id == "requirement.19.OrderedKeyBlockOption":
        return key_acquisition_blocks_target()

    if row.obligation_id in {
        "def.RequiredOutputs",
        "def.IRSet",
        "def.PrimaryArtifactSet",
        "def.ImportLibSet",
        "def.OutputRoot",
        "def.OutputHygiene",
        "def.OutputPathsRoot",
        "def.OutputPathsDirectories",
        "def.ProjectOutputBinding",
        "def.ObjectPath",
        "def.FinalArtifactLibraryName",
        "def.FinalArtifactNames",
        "def.ArtifactPathContext",
        "def.EmitIRExtension",
        "def.ObjPath",
        "def.IRPath",
        "def.ExePath",
        "def.SharedLibPath",
        "def.StaticLibPath",
        "def.ImportLibPath",
        "def.PrimaryArtifact",
        "def.ArtifactOutputDirectoryUse",
        "def.ObjPaths",
        "def.IRPaths",
        "def.LinkJudg",
        "def.RuntimeLibName",
        "def.CompilerExecutableDir",
        "def.CompilerSidecarLayoutPredicates",
        "def.CompilerSupportRoot",
        "def.CompilerRuntimeLibPath",
        "def.RuntimeLibPath",
        "ResolveRuntimeLib-Ok",
        "def.LinkerSymbols",
        "def.LinkObjs",
    }:
        return artifact_output_target("artifactProjectAssemblyAndLinkKindsExercise")

    if row.obligation_id in {
        "def.LibraryArtifactInputs",
        "Link-Ok",
        "Out-Final-Link-Ok",
    }:
        return artifact_output_target("artifactProjectLibraryArtifactInputsExercise")

    if row.obligation_id in {
        "Archive-Ok",
        "Out-Final-Archive-Ok",
    }:
        return artifact_output_target("artifactProjectStaticLibraryExercise")

    if row.obligation_id in {
        "req.15.NoRuntimeOverloadSearch",
        "req.15.InvariantRuntimeChecks",
        "req.15.InvariantLoweringViaVerificationLogic",
    }:
        return ReferenceTarget(
            path=CATALOG_ROOT
            / "ProceduresAndContracts"
            / (
                "Overloading.uv"
                if row.obligation_id == "req.15.NoRuntimeOverloadSearch"
                else "Invariants.uv"
            ),
            module_path="HelloUltraviolet::Audit",
            symbol="artifactProjectEmitLlExercise",
            source_path="Source/Audit/ArtifactProjectExecution.uv",
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
        "def.IORemoveSemantics",
        "IORemove-RemoveFile",
        "IORemove-RemoveOtherEntry",
        "IORemove-RemoveEmptyDir",
        "IORemove-DirectoryNotEmpty",
    }:
        return io_target

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


def build_catalog_entries(
    rows: list[CsvRow],
    blocked_ids: set[str],
) -> list[CatalogEntry]:
    assigned = match_existing_targets(rows, current_catalog_entries())
    fixture_targets = fixture_obligation_targets()
    entries: list[CatalogEntry] = []
    for row in rows:
        if is_blocked_obligation(row, blocked_ids):
            continue
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


def write_catalog_root(total: int, blocked_total: int) -> None:
    write_generated(
        AUDIT_ROOT / "Catalog.uv",
        "//! Root catalog accounting for generated obligation entries.\n\n"
        f"public let EXPECTED_OBLIGATION_COUNT: usize = {total}\n\n"
        f"public let BLOCKED_OBLIGATION_COUNT: usize = {blocked_total}\n\n"
        "public procedure catalogObligationCount() -> usize {\n"
        "    return EXPECTED_OBLIGATION_COUNT\n"
        "}\n\n"
        "public procedure catalogBlockedObligationCount() -> usize {\n"
        "    return BLOCKED_OBLIGATION_COUNT\n"
        "}\n\n"
        "public procedure catalogAccountedObligationCount() -> usize {\n"
        "    return EXPECTED_OBLIGATION_COUNT + BLOCKED_OBLIGATION_COUNT\n"
        "}\n",
    )


def write_catalog_imports(entries: list[CatalogEntry]) -> None:
    helpers_by_directory: dict[pathlib.Path, set[str]] = defaultdict(set)
    for entry in entries:
        helpers_by_directory[entry.target.path.parent].add(entry.helper)

    for path in sorted(CATALOG_ROOT.rglob("*.uv")):
        if path.name == "Imports.uv":
            continue
        text = path.read_text(encoding="utf-8")
        for helper in CATALOG_AUDIT_HELPER_IMPORTS:
            if re.search(r"\b" + re.escape(helper) + r"\s*\(", text):
                helpers_by_directory[path.parent].add(helper)

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

    for path in sorted(by_file):
        if path not in names:
            raise RuntimeError(f"missing generated function names for {path}")

    for path in sorted(names):
        path_entries = by_file.get(path, [])
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


def write_membership(
    rows: list[CsvRow],
    entries: list[CatalogEntry],
    blocked_rows: list[CsvRow],
) -> None:
    csv_text = CSV_PATH.read_text(encoding="utf-8")
    csv_keys = sorted((row.obligation_id, row.internal_spec_line) for row in rows)
    catalog_keys = sorted((entry.row.obligation_id, entry.row.internal_spec_line) for entry in entries)
    blocked_keys = sorted(
        (row.obligation_id, row.internal_spec_line) for row in blocked_rows
    )
    accounted_keys = sorted([*catalog_keys, *blocked_keys])
    for old_group in AUDIT_ROOT.glob("CatalogCsvMembershipGroup*.uv"):
        remove_generated(old_group)

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
        "public procedure catalogBlockedObligationKeyCount() -> usize {",
        f"    return {len(blocked_keys)}usize",
        "}",
        "",
        "public procedure catalogAccountedObligationKeyCount() -> usize {",
        f"    return {len(accounted_keys)}usize",
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
        "public procedure catalogBlockedObligationKeyDigest() -> usize {",
        f"    return {key_digest(blocked_keys)}usize",
        "}",
        "",
        "public procedure catalogAccountedObligationKeyDigest() -> usize {",
        f"    return {key_digest(accounted_keys)}usize",
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
        "    return catalogAccountedObligationKeyCount() == expectedCsvObligationKeyCount() &&",
        "        catalogAccountedObligationKeyDigest() == expectedCsvObligationKeyDigest()",
        "}",
        "",
        "public procedure catalogMatchesCsvObligations(context: Context) -> bool {",
        '    let read_result: Outcome<unique string@Managed, IoError> = context.io~>read_file(',
        '        "Docs/Internal/UltravioletObligations.csv"',
        "    )",
        "    return if move read_result is {",
        "        Outcome::Value(value) {",
        "            let csv_text: string@View = string::as_view(value)",
        "            csvFileShapeMatches(csv_text) && generatedCatalogKeysMatchCsv()",
        "        }",
        "        Outcome::Error(_) {",
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
        remove_generated(old_group)

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
        "        Outcome::Value(_) {",
        "            true",
        "        }",
        "        Outcome::Error(_) {",
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
        "        Outcome::Value(value) {",
        "            let manifest_text: string@View = string::as_view(value)",
        "            sourcePathManifestAuditPasses(context, manifest_text)",
        "        }",
        "        Outcome::Error(_) {",
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
            if is_compiled_symbol_execution_target(entry.target.symbol)
        }
    )
    context_symbols = {
        "runAuthorityCapabilitiesReference",
        "runAuthorityIOReference",
        "runAuthorityNetworkReference",
        "runAuthoritySystemReference",
        "runAuthorityTimeReference",
        "runAppendixBAsyncGrammarReference",
        "runAppendixBAttributeGrammarReference",
        "runAppendixBConcurrencyGrammarReference",
        "runAppendixBContractGrammarReference",
        "runAppendixBControlAndSpecialExpressionGrammarReference",
        "runAppendixBDeclarationGrammarReference",
        "runAppendixBExpressionGrammarReference",
        "runAppendixBFFIGrammarReference",
        "runAppendixBGenericRefinementModalTypeGrammarReference",
        "runAppendixBKeySystemGrammarReference",
        "runAppendixBLexicalGrammarReference",
        "runAppendixBMetaprogrammingGrammarReference",
        "runAppendixBPatternGrammarReference",
        "runAppendixBRegionGrammarReference",
        "runAppendixBStatementGrammarReference",
        "runAppendixBTypeGrammarReference",
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
    heap_symbols = {
        "runModalTypesBytesReference",
        "runModalTypesStringsReference",
    }
    count_symbols = {
        symbol
        for _module_path, symbol, _source_path in targets
        if symbol.startswith("validated") and symbol.endswith("FixtureCount")
    }

    if not CHECK_MODE:
        SYMBOL_EXECUTION_ROOT.mkdir(parents=True, exist_ok=True)
    for old_file in SYMBOL_EXECUTION_ROOT.glob("*.uv"):
        remove_generated(old_file)

    support_lines = [
        "//! Shared support for generated compiled-symbol execution checks.",
        "",
        "internal procedure writeCatalogSymbolMessage(context: Context, text: string@View) -> bool {",
        "    let output: Outcome<(), IoError> = context.io~>write_stderr(text)",
        "    return if output is {",
        "        Outcome::Value(_) {",
        "            true",
        "        }",
        "        Outcome::Error(_) {",
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
    imported_group_symbols: set[tuple[str, str]] = set()
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
            import_key = (module_path, symbol)
            if import_key not in imported_group_symbols:
                group_lines.append(f"using {module_path}::{{ {symbol} }}")
                imported_group_symbols.add(import_key)
        group_lines.extend(["", f"public procedure {function_name}(context: Context) -> bool {{"])
        group_lines.append("    var passed: bool = true")
        for _module_path, symbol in sorted(
            {(module_path, symbol) for module_path, symbol, _source_path in group_targets}
        ):
            if symbol in context_symbols:
                call = f"{symbol}(context)"
            elif symbol in heap_symbols:
                call = f"{symbol}(context.heap)"
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


def source_path_to_file(source_path: str) -> pathlib.Path:
    if source_path.startswith("Source/") or source_path.startswith("Fixtures/"):
        return ROOT / "HelloUltraviolet" / source_path
    return ROOT / source_path


def procedure_body(text: str, search_start: int) -> str | None:
    brace = text.find("{", search_start)
    if brace < 0:
        return None

    depth = 0
    in_string = False
    in_line_comment = False
    escaped = False
    index = brace
    while index < len(text):
        char = text[index]

        if in_line_comment:
            if char == "\n":
                in_line_comment = False
            index += 1
            continue

        if in_string:
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == '"':
                in_string = False
            index += 1
            continue

        if char == "/" and index + 1 < len(text) and text[index + 1] == "/":
            in_line_comment = True
            index += 2
            continue
        if char == '"':
            in_string = True
            index += 1
            continue
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return text[brace + 1:index]
        index += 1
    return None


def normalize_body(body: str) -> str:
    lines: list[str] = []
    for line in body.splitlines():
        stripped = line.strip()
        if not stripped or stripped.startswith("//"):
            continue
        lines.append(stripped)
    return " ".join(lines)


def classify_body(normalized_body: str) -> str:
    if not normalized_body:
        return "empty"
    if normalized_body == "return true":
        return "constant_true"
    if normalized_body == "return false":
        return "constant_false"
    if re.fullmatch(
        r"return [0-9]+(?:usize|u128|u64|u32|u16|u8|i32|i64)?",
        normalized_body,
    ):
        return "constant_numeric"
    if "return" not in normalized_body:
        return "no_return"
    return "nontrivial"


def procedure_attributes(text: str, declaration_start: int) -> tuple[str, ...]:
    lines = text[:declaration_start].splitlines()
    attributes: list[str] = []
    index = len(lines) - 1
    while index >= 0 and not lines[index].strip():
        index -= 1
    while index >= 0 and lines[index].lstrip().startswith("#"):
        attributes.append(lines[index].strip())
        index -= 1
        while index >= 0 and not lines[index].strip():
            index -= 1
    return tuple(reversed(attributes))


def load_procedures() -> list[ProcedureInfo]:
    procedures: list[ProcedureInfo] = []
    for path in sorted(SOURCE_ROOT.rglob("*.uv")):
        text = path.read_text(encoding="utf-8")
        for match in PROCEDURE_RE.finditer(text):
            body = procedure_body(text, match.end())
            if body is None:
                continue
            normalized = normalize_body(body)
            procedures.append(
                ProcedureInfo(
                    path=path,
                    name=match.group("name"),
                    attributes=procedure_attributes(text, match.start()),
                    body=body,
                    normalized_body=normalized,
                    body_class=classify_body(normalized),
                )
            )
    return procedures


def procedure_key(procedure: ProcedureInfo) -> tuple[pathlib.Path, str]:
    return (procedure.path, procedure.name)


def index_procedures_by_name(
    procedures: list[ProcedureInfo],
) -> dict[str, list[ProcedureInfo]]:
    by_name: dict[str, list[ProcedureInfo]] = defaultdict(list)
    for procedure in procedures:
        by_name[procedure.name].append(procedure)
    return dict(by_name)


def index_procedures_by_path_and_name(
    procedures: list[ProcedureInfo],
) -> dict[tuple[pathlib.Path, str], ProcedureInfo]:
    by_path_and_name: dict[tuple[pathlib.Path, str], ProcedureInfo] = {}
    for procedure in procedures:
        by_path_and_name[(procedure.path, procedure.name)] = procedure
    return by_path_and_name


def resolve_target_procedure(
    target: ReferenceTarget,
    procedures_by_name: dict[str, list[ProcedureInfo]],
    procedures_by_path_and_name: dict[tuple[pathlib.Path, str], ProcedureInfo],
) -> ProcedureInfo | None:
    source_file = source_path_to_file(target.source_path)
    path_specific = procedures_by_path_and_name.get((source_file, target.symbol))
    if path_specific is not None:
        return path_specific

    candidates = procedures_by_name.get(target.symbol, [])
    if len(candidates) == 1:
        return candidates[0]
    return None


def procedure_calls(
    procedures: list[ProcedureInfo],
    procedures_by_name: dict[str, list[ProcedureInfo]],
    procedures_by_path_and_name: dict[tuple[pathlib.Path, str], ProcedureInfo],
) -> ProcedureCallGraphs:
    names = set(procedures_by_name.keys())
    call_re = re.compile(r"\b([A-Za-z_][A-Za-z0-9_]*)\s*\(")
    precise_graph: dict[tuple[pathlib.Path, str], set[tuple[pathlib.Path, str]]] = (
        defaultdict(set)
    )
    approximate_graph: dict[tuple[pathlib.Path, str], set[tuple[pathlib.Path, str]]] = (
        defaultdict(set)
    )
    for procedure in procedures:
        current_key = procedure_key(procedure)
        precise_graph.setdefault(current_key, set())
        approximate_graph.setdefault(current_key, set())
        for match in call_re.finditer(procedure.body):
            call_name = match.group(1)
            if call_name not in names or call_name == procedure.name:
                continue
            same_path = procedures_by_path_and_name.get((procedure.path, call_name))
            if same_path is not None:
                resolved_key = procedure_key(same_path)
                precise_graph[current_key].add(resolved_key)
                approximate_graph[current_key].add(resolved_key)
                continue

            candidates = procedures_by_name.get(call_name, [])
            if len(candidates) == 1:
                resolved_key = procedure_key(candidates[0])
                precise_graph[current_key].add(resolved_key)
                approximate_graph[current_key].add(resolved_key)
            else:
                for candidate in candidates:
                    approximate_graph[current_key].add(procedure_key(candidate))
    return ProcedureCallGraphs(
        precise_graph=dict(precise_graph),
        approximate_graph=dict(approximate_graph),
    )


def reachable_procedure_keys(
    call_graph: dict[tuple[pathlib.Path, str], set[tuple[pathlib.Path, str]]],
    roots: set[tuple[pathlib.Path, str]],
) -> set[tuple[pathlib.Path, str]]:
    reachable: set[tuple[pathlib.Path, str]] = set()
    queue: deque[tuple[pathlib.Path, str]] = deque(root for root in roots if root in call_graph)
    while queue:
        current = queue.popleft()
        if current in reachable:
            continue
        reachable.add(current)
        queue.extend(sorted(call_graph.get(current, set()) - reachable))
    return reachable


def exercise_quality_execution_roots(
    entries: list[CatalogEntry],
    procedures: list[ProcedureInfo],
    procedures_by_name: dict[str, list[ProcedureInfo]],
    procedures_by_path_and_name: dict[tuple[pathlib.Path, str], ProcedureInfo],
) -> set[tuple[pathlib.Path, str]]:
    roots: set[tuple[pathlib.Path, str]] = set()
    for procedure in procedures:
        if procedure.name in {"main", "runReferenceCorpus"}:
            roots.add(procedure_key(procedure))
        if any(attribute.startswith("#test") for attribute in procedure.attributes):
            roots.add(procedure_key(procedure))

    for entry in entries:
        if not is_compiled_symbol_execution_target(entry.target.symbol):
            continue
        procedure = resolve_target_procedure(
            entry.target,
            procedures_by_name,
            procedures_by_path_and_name,
        )
        if procedure is not None:
            roots.add(procedure_key(procedure))
    return roots


def is_constant_literal_result(procedure: ProcedureInfo | None) -> bool:
    if procedure is None:
        return False
    return procedure.body_class in {
        "constant_true",
        "constant_false",
        "constant_numeric",
    }


def is_appendix_b_composite_target(target: ReferenceTarget) -> bool:
    return (
        target.source_path.startswith("Source/Reference/AppendixB/") and
        target.symbol.startswith("runAppendixB") and
        target.symbol.endswith("GrammarReference")
    )


def build_exercise_quality_entries(entries: list[CatalogEntry]) -> list[ExerciseQualityEntry]:
    procedures = load_procedures()
    procedures_by_name = index_procedures_by_name(procedures)
    procedures_by_path_and_name = index_procedures_by_path_and_name(procedures)
    graphs = procedure_calls(
        procedures,
        procedures_by_name,
        procedures_by_path_and_name,
    )
    roots = exercise_quality_execution_roots(
        entries,
        procedures,
        procedures_by_name,
        procedures_by_path_and_name,
    )
    precise_reachable = reachable_procedure_keys(graphs.precise_graph, roots)

    quality_entries: list[ExerciseQualityEntry] = []
    for entry in entries:
        if entry.helper != ACCEPTED_HELPER:
            continue
        procedure = resolve_target_procedure(
            entry.target,
            procedures_by_name,
            procedures_by_path_and_name,
        )
        is_executed = (
            procedure is not None and
            procedure_key(procedure) in precise_reachable
        )
        quality_entries.append(
            ExerciseQualityEntry(
                obligation_id=entry.row.obligation_id,
                target_module_path=entry.target.module_path,
                target_symbol=entry.target.symbol,
                target_source_path=entry.target.source_path,
                is_constant_literal_result=is_constant_literal_result(procedure),
                is_executed=is_executed,
                is_broad_appendix_grammar_row=entry.row.obligation_id.startswith(
                    "grammar.B."
                ),
                uses_appendix_b_composite_target=is_appendix_b_composite_target(
                    entry.target
                ),
            )
        )
    return quality_entries


def bool_text(value: bool) -> str:
    return "true" if value else "false"


def uv_bool(value: bool) -> str:
    return "true" if value else "false"


def exercise_quality_entry_passes(entry: ExerciseQualityEntry) -> bool:
    if entry.is_constant_literal_result:
        return False
    if not entry.is_executed:
        return False
    if (
        entry.is_broad_appendix_grammar_row and
        not entry.uses_appendix_b_composite_target
    ):
        return False
    return True


def exercise_quality_manifest_text(entries: list[ExerciseQualityEntry]) -> str:
    handle = io.StringIO()
    writer = csv.writer(handle, lineterminator="\n")
    writer.writerow(
        [
            "obligation_id",
            "target_module_path",
            "target_symbol",
            "target_source_path",
            "is_constant_literal_result",
            "is_executed",
            "is_broad_appendix_grammar_row",
            "uses_appendix_b_composite_target",
            "passes_quality_gate",
        ]
    )
    for entry in entries:
        writer.writerow(
            [
                entry.obligation_id,
                entry.target_module_path,
                entry.target_symbol,
                entry.target_source_path,
                bool_text(entry.is_constant_literal_result),
                bool_text(entry.is_executed),
                bool_text(entry.is_broad_appendix_grammar_row),
                bool_text(entry.uses_appendix_b_composite_target),
                bool_text(exercise_quality_entry_passes(entry)),
            ]
        )
    return handle.getvalue()


def write_exercise_quality_groups(entries: list[ExerciseQualityEntry]) -> list[str]:
    for old_group in AUDIT_ROOT.glob("CatalogExerciseQualityGroup*.uv"):
        remove_generated(old_group)

    group_functions: list[str] = []
    for group_index, start in enumerate(
        range(0, len(entries), EXERCISE_QUALITY_GROUP_SIZE),
        start=1,
    ):
        group_entries = entries[start:start + EXERCISE_QUALITY_GROUP_SIZE]
        function_name = f"validatedCatalogAcceptedExerciseQualityGroup{group_index:03d}"
        group_functions.append(function_name)
        lines: list[str] = [
            f"//! Accepted-source exercise quality manifest group {group_index:03d}.",
            "",
            f"internal procedure {function_name}() -> usize {{",
            "    var count: usize = 0usize",
        ]
        for entry in group_entries:
            lines.extend(
                [
                    "    if catalogExerciseQualityEntryPasses(",
                    "        catalogExerciseQualityEntry(",
                    f'            "{entry.obligation_id}",',
                    f'            "{entry.target_module_path}",',
                    f'            "{entry.target_symbol}",',
                    f'            "{entry.target_source_path}",',
                    f"            {uv_bool(entry.is_constant_literal_result)},",
                    f"            {uv_bool(entry.is_executed)},",
                    f"            {uv_bool(entry.is_broad_appendix_grammar_row)},",
                    f"            {uv_bool(entry.uses_appendix_b_composite_target)}",
                    "        )",
                    "    ) {",
                    "        count = count + 1usize",
                    "    }",
                ]
            )
        lines.extend(["    return count", "}", ""])
        write_generated(
            AUDIT_ROOT / f"CatalogExerciseQualityGroup{group_index:03d}.uv",
            "\n".join(lines),
        )
    return group_functions


def write_exercise_quality(entries: list[CatalogEntry]) -> None:
    quality_entries = build_exercise_quality_entries(entries)
    manifest_text = exercise_quality_manifest_text(quality_entries)
    write_generated(EXERCISE_QUALITY_MANIFEST, manifest_text)
    group_functions = write_exercise_quality_groups(quality_entries)

    lines: list[str] = [
        "//! Runtime gate for generated accepted-source exercise quality evidence.",
        "",
        "internal record CatalogExerciseQualityEntry {",
        "    internal obligation_id: string@View",
        "    internal target_module_path: string@View",
        "    internal target_symbol: string@View",
        "    internal target_source_path: string@View",
        "    internal is_constant_literal_result: bool",
        "    internal is_executed: bool",
        "    internal is_broad_appendix_grammar_row: bool",
        "    internal uses_appendix_b_composite_target: bool",
        "}",
        "",
        "internal procedure catalogExerciseQualityEntry(",
        "    obligation_id: string@View,",
        "    target_module_path: string@View,",
        "    target_symbol: string@View,",
        "    target_source_path: string@View,",
        "    is_constant_literal_result: bool,",
        "    is_executed: bool,",
        "    is_broad_appendix_grammar_row: bool,",
        "    uses_appendix_b_composite_target: bool",
        ") -> CatalogExerciseQualityEntry {",
        "    return CatalogExerciseQualityEntry {",
        "        obligation_id: obligation_id,",
        "        target_module_path: target_module_path,",
        "        target_symbol: target_symbol,",
        "        target_source_path: target_source_path,",
        "        is_constant_literal_result: is_constant_literal_result,",
        "        is_executed: is_executed,",
        "        is_broad_appendix_grammar_row: is_broad_appendix_grammar_row,",
        "        uses_appendix_b_composite_target: uses_appendix_b_composite_target",
        "    }",
        "}",
        "",
        "internal procedure catalogExerciseQualityEntryPasses(",
        "    entry: CatalogExerciseQualityEntry",
        ") -> bool {",
        "    if entry.is_constant_literal_result {",
        "        return false",
        "    }",
        "    if !entry.is_executed {",
        "        return false",
        "    }",
        "    if entry.is_broad_appendix_grammar_row &&",
        "        !entry.uses_appendix_b_composite_target {",
        "        return false",
        "    }",
        "    return true",
        "}",
        "",
        "public procedure expectedCatalogAcceptedExerciseQualityEntryCount() -> usize {",
        f"    return {len(quality_entries)}usize",
        "}",
        "",
        "public procedure catalogAcceptedExerciseQualityEntryCount() -> usize {",
        f"    return {len(quality_entries)}usize",
        "}",
        "",
        "public procedure expectedCatalogAcceptedExerciseQualityEntryDigest() -> usize {",
        f"    return {text_digest(manifest_text)}usize",
        "}",
        "",
        "public procedure catalogAcceptedExerciseQualityEntryDigest() -> usize {",
        f"    return {text_digest(manifest_text)}usize",
        "}",
        "",
        "public procedure catalogAcceptedExerciseQualityManifestByteCount() -> usize {",
        f"    return {len(manifest_text.encode('utf-8'))}usize",
        "}",
        "",
        "public procedure catalogAcceptedExerciseQualityManifestLineBreakCount() -> usize {",
        f"    return {manifest_text.count(chr(10))}usize",
        "}",
        "",
        "internal procedure catalogAcceptedExerciseQualityManifestShapeMatches(",
        "    manifest_text: string@View",
        ") -> bool {",
        "    return string::length(manifest_text) ==",
        "        catalogAcceptedExerciseQualityManifestByteCount() &&",
        "        catalogAuditLineBreakCount(manifest_text) ==",
        "            catalogAcceptedExerciseQualityManifestLineBreakCount() &&",
        "        catalogAuditTextDigest(manifest_text) ==",
        "            catalogAcceptedExerciseQualityEntryDigest()",
        "}",
        "",
        "public procedure catalogAcceptedExerciseQualityValidatedEntryCount() -> usize {",
        "    var count: usize = 0usize",
    ]
    for function_name in group_functions:
        lines.append(f"    count = count + {function_name}()")
    lines.extend(
        [
            "    return count",
            "}",
            "",
            "public procedure catalogAcceptedExercisesAreNontrivialAndExecuted(",
            "    context: Context",
            ") -> bool {",
            "    if catalogAcceptedExerciseQualityEntryCount() !=",
            "        expectedCatalogAcceptedExerciseQualityEntryCount() {",
            "        return false",
            "    }",
            "    if catalogAcceptedExerciseQualityValidatedEntryCount() !=",
            "        expectedCatalogAcceptedExerciseQualityEntryCount() {",
            "        return false",
            "    }",
            "",
            "    let read_result: Outcome<unique string@Managed, IoError> =",
            "        context.io~>read_file(",
            '            "HelloUltraviolet/Audit/ExerciseQualityManifest.csv"',
            "        )",
            "    return if move read_result is {",
            "        Outcome::Value(value) {",
            "            let manifest_text: string@View = string::as_view(value)",
            "            catalogAcceptedExerciseQualityManifestShapeMatches(",
            "                manifest_text",
            "            )",
            "        }",
            "        Outcome::Error(_) {",
            "            false",
            "        }",
            "    }",
            "}",
            "",
        ]
    )
    write_generated(AUDIT_ROOT / "CatalogExerciseQuality.uv", "\n".join(lines))


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Regenerate or check HelloUltraviolet's generated obligation catalog source."
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="verify generated catalog files are current without rewriting them",
    )
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    global CHECK_MODE
    args = parse_args(argv)
    CHECK_MODE = args.check

    rows = load_csv_rows()
    blocked_ids = blocked_obligation_ids(rows)
    blocked_rows = blocked_obligation_rows(rows, blocked_ids)
    entries = build_catalog_entries(rows, blocked_ids)
    write_catalog_root(len(entries), len(blocked_rows))
    write_catalog_imports(entries)
    write_topic_files(entries)
    write_membership(rows, entries, blocked_rows)
    write_primary_references(entries)
    write_source_paths(entries)
    write_symbols(entries)
    write_exercise_quality(entries)
    check_stale_generated(
        [
            (AUDIT_ROOT, "CatalogCsvMembershipGroup*.uv"),
            (AUDIT_ROOT, "CatalogExerciseQualityGroup*.uv"),
            (AUDIT_ROOT, "CatalogPrimaryReferenceOrderGroup*.uv"),
            (SYMBOL_EXECUTION_ROOT, "*.uv"),
        ]
    )
    if CHECK_MODE and CHECK_FAILURES:
        for failure in CHECK_FAILURES:
            print(f"[hello-catalog] {failure}", file=sys.stderr)
        return 1
    if CHECK_MODE:
        print("[hello-catalog] PASS generated catalog is current")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
