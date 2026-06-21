# Ultraviolet Compiler Specification Audit

Date: 2026-06-12

Compiler under review: `Bootstrap\Ultraviolet\build\windows\out\uvc.exe`

Specification under review: `Docs\SPECIFICATION.md`

Test surface: `HelloUltraviolet`

## Scope

This document records confirmed and inspection-backed conformance findings from
the compiler audit against `Docs\SPECIFICATION.md`. It is an audit artifact, not
a specification amendment.

## Current Verification Baseline

- `uvc.exe --version`: `Ultraviolet 0.4.0-alpha`.
- `python Bootstrap\Ultraviolet\tools\validate_diagnostic_spec_sync.py`: passed.
- `python Bootstrap\Ultraviolet\tools\validate_ast_phase_coverage.py --source-root Bootstrap\Ultraviolet`: passed.
- `python Tools\ExtractObligationLedger.py --check`: passed.
- `python Tools\RunHelloVerification.py --target-profile x86_64-win64 --dry-run`: passed and confirmed the compiler path is `uvc.exe`.
- `uvc.exe build HelloUltraviolet --target-profile x86_64-win64 --build-progress off --color never`: passed with 27 warnings and 8 infos.
- `HelloUltraviolet\Build\Binary\HelloUltraviolet.exe`: failed three reference checks listed below.

Temporary repro projects were created outside the repository under
`%TEMP%\uv-audit-repros`.

## Findings

### UV-AUDIT-0001: Generic type applications accept bound violations

Severity: High

Status: Confirmed with `uvc`.

Spec anchors:

- `Docs\SPECIFICATION.md:12608` defines `WF-Apply` and requires generic
  arguments to satisfy bounds and predicate clauses.
- `Docs\SPECIFICATION.md:10700` applies the same requirement to modal
  state references.

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\typing\type_wf.cpp:425`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\type_wf.cpp:462`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\type_wf.cpp:557`

Observed behavior:

`uvc --check` accepted a repro where `Box<Unrelated>` instantiated a generic
type declared with `TValue <: Required`.

Expected behavior:

The instantiation should be rejected because the supplied type argument does not
satisfy the declared class bound.

Impact:

The type well-formedness pass can admit invalid type applications, allowing later
analysis and lowering to operate on types the specification excludes.

### UV-AUDIT-0002: Generic defaults do not enforce later-reference and bound rules

Severity: High

Status: Confirmed with `uvc`.

Spec anchors:

- `Docs\SPECIFICATION.md:12455` defines `DefaultRefsOk`.
- `Docs\SPECIFICATION.md:12456` defines `DefaultWF`.
- `Docs\SPECIFICATION.md:12459` includes both in `WF-Generic-Param`.

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\generic_params.cpp:111`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\generic_params.cpp:126`

Observed behavior:

`uvc --check` accepted a generic parameter default that referenced a later type
parameter.

Expected behavior:

The declaration should be rejected because generic defaults may only reference
earlier parameters and must satisfy the parameter's own bounds.

Impact:

Invalid generic declarations can enter the semantic model and create order-
dependent or underconstrained type behavior.

### UV-AUDIT-0003: Bounded type parameters fail to satisfy their own bounds

Severity: High

Status: Confirmed with `uvc`.

Spec anchors:

- `Docs\SPECIFICATION.md:12482` defines `T-Constraint-Sat`.
- `Docs\SPECIFICATION.md:12586` applies constraint satisfaction to generic
  calls.

Implementation anchor:

- `Bootstrap\Ultraviolet\src\04_analysis\composite\classes.cpp:1030`

Observed behavior:

A repro with `TValue <: Readable` passed to a callee requiring `U <: Readable`
was rejected with `E-TYP-2302`.

Expected behavior:

The type parameter should satisfy a compatible class bound already present in
its generic constraint environment.

Impact:

Valid generic code is rejected, limiting polymorphic composition and producing
incorrect diagnostics for conforming programs.

### UV-AUDIT-0004: HelloUltraviolet executable reference checks fail after a fresh build

Severity: High

Status: Confirmed with `uvc`.

Spec anchors:

- `Docs\SPECIFICATION.md:7276`
- `Docs\SPECIFICATION.md:14124`
- `Docs\SPECIFICATION.md:28717`

Test anchors:

- `HelloUltraviolet\Source\Audit\TargetProfile.uv:5`
- `HelloUltraviolet\Source\Api.uv:3387`
- `HelloUltraviolet\Source\Api.uv:3392`
- `HelloUltraviolet\Source\Api.uv:3688`
- `HelloUltraviolet\Source\Reference\Authority\IO.uv:713`
- `HelloUltraviolet\Source\Reference\Authority\IO.uv:1614`
- `HelloUltraviolet\Source\Reference\Authority\System.uv:93`
- `HelloUltraviolet\Source\Reference\Authority\System.uv:169`
- `HelloUltraviolet\Source\Reference\Polymorphism\CapabilityClassRuntime.uv:181`
- `HelloUltraviolet\Source\Reference\Polymorphism\CapabilityClasses.uv:4`

Observed behavior:

After `uvc build HelloUltraviolet --target-profile x86_64-win64`, running
`HelloUltraviolet\Build\Binary\HelloUltraviolet.exe` failed:

- `runAuthorityIOReference`
- `runAuthoritySystemReference`
- `runPolymorphismCapabilityClassesReference`

Targeted `uvc test HelloUltraviolet --coverage <reference-name>` invocations for
those names exited successfully with no output.

Additional root-cause evidence:

Running the same built executable with `HUV_TARGET_PROFILE=x86_64-win64` set
exited successfully. The failing executable references call
`auditTargetProfile(sys)`, which reads `HUV_TARGET_PROFILE`; the executable
reference path receives only `Context`, while source-native tests receive a
generated `TestAuthority` containing `target_profile`.

Expected behavior:

The executable reference gate and source-native coverage selectors should agree
on these reference outcomes, or the selector should expose why it is not running
the same checks. A fresh Windows executable verification run should not require
an undocumented ambient environment variable, or the verification runner should
set the required value explicitly.

Impact:

The primary test surface can report green coverage selectors while the executable
reference gate is failing. The executable reference gate also depends on host
environment state that is not part of the executable `main(Context)` contract.

### UV-AUDIT-0005: Key paths through dereference do not implement identity-root semantics

Severity: High

Status: Inspection-backed; no minimal runtime repro recorded yet.

Spec anchors:

- `Docs\SPECIFICATION.md:20372`
- `Docs\SPECIFICATION.md:20377`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\keys\key_paths.cpp:409`
- `Bootstrap\Ultraviolet\src\04_analysis\keys\key_paths.cpp:737`

Observed behavior:

The key-path builder derives dereference paths from the operand and records a
boundary, while root extraction uses a synthetic `$deref` root.

Expected behavior:

For `(*e').p`, the dereference itself requires read access to `KeyPath(e')`, and
field access on the dereferenced value uses a fresh identity root `id(*e')`.

Impact:

Key coverage and conflict analysis can conflate pointer-container access with
access to the referent object, or fail to model referent identity precisely.

### UV-AUDIT-0006: `parallel` and `dispatch` workgroup operands are parsed too narrowly

Severity: Medium

Status: Confirmed with `uvc` for `parallel`; implementation inspection covers
`dispatch`.

Spec anchors:

- `Docs\SPECIFICATION.md:21558`
- `Docs\SPECIFICATION.md:21563`
- `Docs\SPECIFICATION.md:21573`
- `Docs\SPECIFICATION.md:22332`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\02_source\parser\expr\parallel_expr.cpp:58`
- `Bootstrap\Ultraviolet\src\02_source\parser\expr\parallel_expr.cpp:157`
- `Bootstrap\Ultraviolet\src\02_source\parser\expr\dispatch_expr.cpp:38`
- `Bootstrap\Ultraviolet\src\02_source\parser\expr\dispatch_expr.cpp:242`

Observed behavior:

`uvc --check` reports syntax errors for expression-shaped invalid workgroup
operands.

Expected behavior:

The parser should accept an expression operand for these options, then the static
semantics should reject non-`dim3_const` shapes through `Dim3Const-Err`.

Impact:

Programs receive parser diagnostics where the specification assigns a semantic
diagnostic path, and valid expression forms may be rejected too early.

### UV-AUDIT-0007: Specific reserved-name diagnostics are collapsed

Severity: Medium

Status: Confirmed with `uvc`.

Spec anchors:

- `Docs\SPECIFICATION.md:4790`
- `Docs\SPECIFICATION.md:4830`
- `Docs\SPECIFICATION.md:4835`
- `Docs\SPECIFICATION.md:5931`
- `Docs\SPECIFICATION.md:6961`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\resolve\scopes_intro.cpp:92`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\scopes_intro.cpp:197`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\resolve_module.cpp:244`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\resolve_module.cpp:262`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\resolve_module.cpp:266`

Observed behavior:

Declarations using `gen_` and `ultraviolet` were both reported as `E-CNF-0401`.

Expected behavior:

`gen_` declarations should report `E-CNF-0406`, and user declarations in the
reserved `ultraviolet` namespace should report `E-CNF-0402`.

Impact:

The compiler emits non-canonical diagnostic codes for reserved names, weakening
diagnostic conformance and fixture precision.

### UV-AUDIT-0008: Source loading rejects prohibited scalars inside malformed terminated literals too early

Severity: Medium

Status: Confirmed with `uvc`.

Spec anchors:

- `Docs\SPECIFICATION.md:1877`
- `Docs\SPECIFICATION.md:2244`
- `Docs\SPECIFICATION.md:2678`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\00_core\unicode.cpp:511`
- `Bootstrap\Ultraviolet\src\00_core\unicode.cpp:559`
- `Bootstrap\Ultraviolet\src\00_core\source_load.cpp:341`
- `Bootstrap\Ultraviolet\src\00_core\source_load.cpp:353`

Observed behavior:

A terminated malformed character literal containing a NUL was rejected during
source loading with `E-SRC-0104`.

Expected behavior:

Terminated quoted spans should form literal tokens even when their interior is
ill-formed, allowing the lexer to report the corresponding literal diagnostic.

Impact:

The source pipeline reports the wrong phase and diagnostic for malformed quoted
literals containing prohibited scalars.

### UV-AUDIT-0009: Duplicate using-list entries are checked after target resolution

Severity: Medium

Status: Confirmed with `uvc`.

Spec anchors:

- `Docs\SPECIFICATION.md:7758`
- `Docs\SPECIFICATION.md:7773`
- `Docs\SPECIFICATION.md:7800`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\resolve\resolve_using.cpp:520`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\resolve_using.cpp:525`

Observed behavior:

`public using Missing::{ value, value }` reports `E-MOD-1204` for unresolved
path before reporting the duplicate list entry.

Expected behavior:

Duplicate list entries should be diagnosed from `UsingSpecNames(specs)` before
resolution of the target module or imported names can mask that error.

Impact:

First-failure behavior differs from the specification and can hide a purely local
using-list error.

### UV-AUDIT-0010: Manifest validation order does not match deterministic first-failure order

Severity: Low

Status: Inspection-backed.

Spec anchors:

- `Docs\SPECIFICATION.md:1003`
- `Docs\SPECIFICATION.md:1012`
- `Docs\SPECIFICATION.md:1029`
- `Docs\SPECIFICATION.md:1730`
- `Docs\SPECIFICATION.md:1732`
- `Docs\SPECIFICATION.md:1733`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\01_project\project_validate.cpp:499`
- `Bootstrap\Ultraviolet\src\01_project\project_validate.cpp:574`
- `Bootstrap\Ultraviolet\src\01_project\project_validate.cpp:604`

Observed behavior:

Implementation validates `[toolchain]` and `[build]` before required assembly
table checks.

Expected behavior:

Validation should preserve the deterministic first-failure order defined by the
manifest validation rules.

Impact:

Combined invalid manifests can produce a different first diagnostic than the
specification requires.

### UV-AUDIT-0011: Generated static-rule metadata misclassifies core diagnostics

Severity: Low

Status: Inspection-backed.

Spec anchors:

- `Docs\SPECIFICATION.md:244`
- `Docs\SPECIFICATION.md:458`
- `Docs\SPECIFICATION.md:505`
- `Docs\SPECIFICATION.md:591`
- `Docs\SPECIFICATION.md:8106`

Implementation anchors:

- `Bootstrap\Ultraviolet\tools\static_rule_mapping.json:6`
- `Bootstrap\Ultraviolet\src\00_core\generated\static_rule_registry.inc:669`
- `Bootstrap\Ultraviolet\src\00_core\generated\static_rule_registry.inc:1565`
- `Bootstrap\Ultraviolet\src\00_core\generated\static_rule_registry.inc:1572`

Observed behavior:

Core diagnostic rules such as `Emit-Append`, `NoSpan-External`, and `Order` are
registered under `WFModulePathJudg`.

Expected behavior:

Diagnostic infrastructure rules should be classified under their owning
judgment family rather than the module-path well-formedness family.

Impact:

Rule coverage metadata can report conformance under the wrong judgment family,
masking gaps in diagnostics infrastructure coverage.

### UV-AUDIT-0012: CLI help advertises an obsolete default target profile

Severity: Low

Status: Confirmed with `uvc`.

Spec anchors:

- `Docs\SPECIFICATION.md:364`
- `Docs\SPECIFICATION.md:367`
- `Docs\SPECIFICATION.md:368`
- `Docs\SPECIFICATION.md:1734`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\06_driver\cli.cpp:1154`
- `Bootstrap\Ultraviolet\src\06_driver\compiler_main.cpp:2592`
- `Bootstrap\Ultraviolet\src\06_driver\compiler_main.cpp:2631`

Observed behavior:

`uvc --help` reports `target_profile = "x86_64-sysv"  Default target profile`,
while the compiler implementation rejects invocations without a selected target
profile.

Expected behavior:

Help text should match the specification and implementation: no target profile
is inferred when neither CLI nor manifest selects one.

Impact:

Users receive stale CLI guidance for a mandatory project selection rule.

### UV-AUDIT-0013: Source-native `--coverage` selectors pass when no tests are selected

Severity: High

Status: Confirmed with `uvc`.

Spec anchors:

- `Docs\SPECIFICATION.md:7251`
- `Docs\SPECIFICATION.md:7276`
- `Docs\SPECIFICATION.md:7295`
- `Docs\SPECIFICATION.md:7331`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\06_driver\test_discovery.cpp:90`
- `Bootstrap\Ultraviolet\src\06_driver\test_discovery.cpp:322`
- `Bootstrap\Ultraviolet\src\06_driver\test_discovery.cpp:333`
- `Bootstrap\Ultraviolet\src\06_driver\compiler_main.cpp:3149`
- `Bootstrap\Ultraviolet\src\06_driver\compiler_main.cpp:3154`
- `Bootstrap\Ultraviolet\include\06_driver\test_discovery.h:61`

Observed behavior:

The following commands exited `0` with no output:

- `uvc.exe test HelloUltraviolet --target-profile x86_64-win64 --build-progress off --color never --coverage runAuthorityIOReference`
- `uvc.exe test HelloUltraviolet --target-profile x86_64-win64 --build-progress off --color never --coverage runAuthoritySystemReference`
- `uvc.exe test HelloUltraviolet --target-profile x86_64-win64 --build-progress off --color never --coverage runPolymorphismCapabilityClassesReference`
- `uvc.exe test HelloUltraviolet --target-profile x86_64-win64 --build-progress off --color never --coverage definitely-no-such-coverage-reference`

The driver filters by exact `covers(...)` metadata and returns success when
`selected_tests.empty()`.

Expected behavior:

A requested coverage selector that selects no tests should be reported as an
empty selection or failed selector, rather than silently succeeding. At minimum,
the verification surface should not allow a command that ran zero tests to be
indistinguishable from a passing selected-test run.

Impact:

The source-native test command can be used with a misspelled, stale, or
non-`#test` reference name and still report success. This masked the failing
HelloUltraviolet executable references in `UV-AUDIT-0004`.

### UV-AUDIT-0014: Derive contracts collapse unknown classes into subject-implementation errors

Severity: Medium

Status: Inspection-backed; needs a reduced fixture.

Spec anchors:

- `Docs\SPECIFICATION.md:25652`
- `Docs\SPECIFICATION.md:25654`
- `Docs\SPECIFICATION.md:25753`
- `Docs\SPECIFICATION.md:25755`
- `Docs\SPECIFICATION.md:25756`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\03_comptime\derive.cpp:628`
- `Bootstrap\Ultraviolet\src\03_comptime\derive.cpp:637`
- `Bootstrap\Ultraviolet\src\03_comptime\derive.cpp:646`
- `Bootstrap\Ultraviolet\src\00_core\generated\diag_registry.inc:156`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\typecheck_diag_map.inc:191`

Observed behavior:

`ValidateDeriveContracts` validates `requires(...)` and `emits(...)` entries
only against `DeclaredImplNames(D)`. Its reachable diagnostics are
`E-CTE-0330` and `E-CTE-0331`. `E-CTE-0321` is present in generated diagnostic
registries and maps, but `rg` found no implementation path outside those
registries that emits it.

Expected behavior:

Class names in derive contracts should first resolve to declared class
definitions. Unknown class references should reject with `E-CTE-0321`; known
classes missing from the derive subject's declared implementation names should
then reject with `E-CTE-0330` or `E-CTE-0331`.

Impact:

The compiler cannot distinguish a misspelled or nonexistent derive-contract
class from a real class that the subject failed to declare. That weakens the
specified diagnostic taxonomy and can send users toward the wrong remediation.

### UV-AUDIT-0015: Invalid derive target signatures use generic parse diagnostics

Severity: Low

Status: Inspection-backed; needs a reduced fixture.

Spec anchors:

- `Docs\SPECIFICATION.md:25564`
- `Docs\SPECIFICATION.md:25576`
- `Docs\SPECIFICATION.md:25754`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\02_source\parser\item\derive_target_decl.cpp:101`
- `Bootstrap\Ultraviolet\src\02_source\parser\item\derive_target_decl.cpp:112`
- `Bootstrap\Ultraviolet\src\02_source\parser\item\derive_target_decl.cpp:120`
- `Bootstrap\Ultraviolet\src\02_source\parser\item\derive_target_decl.cpp:128`
- `Bootstrap\Ultraviolet\src\02_source\parser\item\derive_target_decl.cpp:136`
- `Bootstrap\Ultraviolet\src\02_source\parser\item\derive_target_decl.cpp:144`
- `Bootstrap\Ultraviolet\src\00_core\generated\diag_registry.inc:157`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\typecheck_diag_map.inc:192`

Observed behavior:

Malformed derive target declaration signatures are rejected in the parser by
`EmitParseSyntaxErr(...)`. `E-CTE-0322` is present in generated diagnostic
registries and maps, but `rg` found no implementation path outside those
registries that emits it.

Expected behavior:

A derive target declaration with an invalid signature should reject with the
derive-specific diagnostic `E-CTE-0322`, as listed in the compile-time
diagnostics supplement.

Impact:

Users and conformance tooling see a broad syntax failure instead of the
specified derive-target signature failure. This also leaves `E-CTE-0322`
effectively unexercised by the compiler implementation.

### UV-AUDIT-0016: Derive target panic diagnostic is registered but not emitted

Severity: Medium

Status: Inspection-backed; needs a reduced fixture.

Spec anchors:

- `Docs\SPECIFICATION.md:25690`
- `Docs\SPECIFICATION.md:25758`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\03_comptime\derive.cpp:731`
- `Bootstrap\Ultraviolet\src\03_comptime\derive.cpp:753`
- `Bootstrap\Ultraviolet\src\03_comptime\derive.cpp:755`
- `Bootstrap\Ultraviolet\src\00_core\generated\diag_registry.inc:161`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\typecheck_diag_map.inc:196`

Observed behavior:

`RunDeriveTarget` detects derive execution failure through `!exec.ok` or newly
reported diagnostics and returns `std::nullopt`. The function records the
`DeriveTargetFailureSemantics` static rule but does not emit `E-CTE-0341`.
`E-CTE-0341` is present in generated diagnostic registries and maps, but `rg`
found no implementation path outside those registries that emits it.

Expected behavior:

When a derive target panics, Phase 2 should reject compilation with the
derive-target panic diagnostic `E-CTE-0341`. User-reported diagnostics from
`diagnostics.error` should remain distinguishable from an actual derive target
panic.

Impact:

Derive target execution failures can lose their specified panic classification.
That weakens Phase 2 failure reporting and makes it harder for conformance
fixtures to verify the derive-target execution taxonomy.

### UV-AUDIT-0017: Shared receiver calls can bypass key context

Severity: High

Status: Inspection-backed; needs a reduced fixture.

Spec anchors:

- `Docs\SPECIFICATION.md:7413`
- `Docs\SPECIFICATION.md:7414`
- `Docs\SPECIFICATION.md:20585`
- `Docs\SPECIFICATION.md:20593`
- `Docs\SPECIFICATION.md:20615`
- `Docs\SPECIFICATION.md:20617`
- `Docs\SPECIFICATION.md:20777`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\method_call.cpp:1147`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\method_call.cpp:1156`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\assign_stmt.cpp:782`
- `Bootstrap\Ultraviolet\src\00_core\generated\diag_registry.inc:26`

Observed behavior:

`check_shared_receiver_access` builds a key path for a `shared` receiver and
queries `CoveringKeyMode(...)`. If no covering key mode is present, the method
call path returns success. Assignment typing has a separate key-context check
that rejects missing write keys, so field writes and receiver calls do not share
the same enforcement behavior.

Expected behavior:

A method call through a `shared` path should require an active key context:
`~` receivers require a read key, and mutating receivers require a write key.
Missing key context should reject with the shared-access diagnostic rather than
being treated as allowed access.

Impact:

Shared receiver method calls can bypass the Chapter 19 key-context requirement
even where direct field mutation is rejected. This is an authority-boundary gap.

HelloUltraviolet fixture gap:

`PermissionForms.uv` covers key-held calls, and `SharedMutationWithoutKey`
covers field assignment without a key, but no rejected-source fixture covers a
shared receiver method call without a key.

### UV-AUDIT-0018: Method and transition bodies skip provenance checking

Severity: High

Status: Inspection-backed; needs reduced fixtures.

Spec anchors:

- `Docs\SPECIFICATION.md:4205`
- `Docs\SPECIFICATION.md:14098`
- `Docs\SPECIFICATION.md:14099`
- `Docs\SPECIFICATION.md:14101`
- `Docs\SPECIFICATION.md:14102`
- `Docs\SPECIFICATION.md:27025`
- `Docs\SPECIFICATION.md:27026`
- `Docs\SPECIFICATION.md:27029`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\procedure_decl.cpp:2412`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\procedure_decl.cpp:2689`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\record_decl.cpp:1109`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\modal_decl.cpp:777`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\modal_decl.cpp:877`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\class_decl.cpp:440`

Observed behavior:

Top-level procedure checking calls `CheckBodyMemory(...)`, which wraps
`BindCheckBody(...)` with the region/provenance pass. Record methods, modal
state methods, transitions, and class methods call `BindCheckBody(...)`
directly and do not call `CheckBodyMemory(...)`.

Expected behavior:

All procedure-like bodies listed by `DeclTypingItem`, including record methods,
modal state methods, modal transitions, and class methods with bodies, must run
`ProvBindCheck(...)` over their receiver and parameter bindings.

Impact:

Escaping shorter-lived provenance can be rejected in an ordinary procedure but
missed in equivalent method or transition bodies. This leaves memory-lifetime
conformance dependent on the declaration form.

HelloUltraviolet fixture gap:

`AssignmentProvenanceEscape` covers a top-level procedure escape. There is no
matching record-method, modal-method, transition, or class-method escape
fixture.

### UV-AUDIT-0019: User-defined capability classes are lost by capability-set inference

Severity: High

Status: Inspection-backed; needs a reduced fixture.

Spec anchors:

- `Docs\SPECIFICATION.md:3201`
- `Docs\SPECIFICATION.md:3203`
- `Docs\SPECIFICATION.md:3207`
- `Docs\SPECIFICATION.md:3214`
- `Docs\SPECIFICATION.md:3216`
- `Docs\SPECIFICATION.md:13717`
- `Docs\SPECIFICATION.md:26588`
- `Docs\SPECIFICATION.md:26590`

Implementation anchors:

- `Bootstrap\Ultraviolet\include\04_analysis\caps\cap_requirements.h:36`
- `Bootstrap\Ultraviolet\include\04_analysis\caps\cap_requirements.h:55`
- `Bootstrap\Ultraviolet\src\04_analysis\caps\cap_requirements.cpp:429`
- `Bootstrap\Ultraviolet\src\04_analysis\caps\cap_requirements.cpp:544`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\type_expr.cpp:4879`
- `Bootstrap\Ultraviolet\src\04_analysis\caps\context_caps.cpp:559`
- `Bootstrap\Ultraviolet\src\04_analysis\caps\context_caps.cpp:592`

Observed behavior:

The capability-requirement model is an enum over built-in capability roots.
`TypeDynamic` handling in capability-requirement inference returns an empty set
for non-built-in paths. Another capability path, `context_caps.cpp`, can
recognize user capability classes by superclass, but the requirement and FFI
capability inference path does not use that open-universe result.

Expected behavior:

`CapInType(TypeDynamic([p]))` should include `p` whenever `CapClass(p)` holds,
including user classes that declare a capability superclass. Capability-bearing
FFI and hosted-export signature checks should reject those user capability
classes just as they reject built-in capability classes.

Impact:

User-defined capability classes can be treated as non-capability-bearing in
authority and foreign-boundary checks. That can understate required authority
and weaken capability isolation.

HelloUltraviolet fixture gap:

The capability-class reference exercises built-in roots and ordinary dynamic
capability behavior, but there is no user capability subclass fixture for
`CapInType` or FFI isolation.

### UV-AUDIT-0020: Type invariant enforcement points are not implemented at construction or receiver calls

Severity: High

Status: Inspection-backed; needs reduced fixtures.

Spec anchors:

- `Docs\SPECIFICATION.md:15028`
- `Docs\SPECIFICATION.md:15031`
- `Docs\SPECIFICATION.md:15032`
- `Docs\SPECIFICATION.md:15282`
- `Docs\SPECIFICATION.md:15284`
- `Docs\SPECIFICATION.md:15287`
- `Docs\SPECIFICATION.md:15289`
- `Docs\SPECIFICATION.md:15292`
- `Docs\SPECIFICATION.md:15294`
- `Docs\SPECIFICATION.md:15387`
- `Docs\SPECIFICATION.md:15388`
- `Docs\SPECIFICATION.md:15389`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\record_decl.cpp:704`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\record_decl.cpp:715`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\modal_decl.cpp:903`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\record_literal.cpp:357`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\record_literal.cpp:477`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\method_call.cpp:1147`
- `Bootstrap\Ultraviolet\src\00_core\generated\diag_registry.inc:288`
- `Bootstrap\Ultraviolet\src\00_core\generated\diag_registry.inc:289`
- `Bootstrap\Ultraviolet\src\00_core\generated\diag_registry.inc:290`

Observed behavior:

Declaration-side invariant validation exists and rejects public mutable fields
on invariant-bearing records. Record and modal construction typing returns after
field checks and type construction. Method-call typing checks receiver
permissions and arguments, but there is no corresponding path that proves or
inserts the specified type-invariant checks. `E-SEM-2820`, `E-SEM-2821`, and
`E-SEM-2822` are registered but not reachable from the construction or
receiver-call paths found in the scan.

Expected behavior:

The compiler should enforce type invariants after construction, before public
receiver-taking calls, and after mutating receiver-taking calls. Static failure
should use `E-SEM-2820`, `E-SEM-2821`, or `E-SEM-2822`; dynamic mode should
insert `ContractCheck(..., TypeInv, ...)`.

Impact:

An invariant can be accepted at declaration time without being enforced at the
specified operational boundaries. This weakens the contract model for records
and modal states.

HelloUltraviolet fixture gap:

`InvariantDiagnostics` covers the public-mutable-field diagnostic
`E-SEM-2824`, but no rejected-source fixture covers construction, public-entry,
or mutator-return invariant violations.

### UV-AUDIT-0021: Runtime compatibility checks do not require the full `RuntimeSyms` set

Severity: High

Status: Inspection-backed with artifact-build evidence from `uvc`.

Spec anchors:

- `Docs\SPECIFICATION.md:1486`
- `Docs\SPECIFICATION.md:27394`
- `Docs\SPECIFICATION.md:29018`
- `Docs\SPECIFICATION.md:29027`
- `Docs\SPECIFICATION.md:29028`
- `Docs\SPECIFICATION.md:29029`
- `Docs\SPECIFICATION.md:29030`
- `Docs\SPECIFICATION.md:29031`
- `Docs\SPECIFICATION.md:29269`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\00_core\runtime_abi.cpp:28`
- `Bootstrap\Ultraviolet\src\01_project\link.cpp:1441`
- `Bootstrap\Ultraviolet\src\01_project\link.cpp:1779`
- `Bootstrap\Ultraviolet\src\01_project\link.cpp:2330`

Observed behavior:

The linker-required runtime symbol list is sourced from
`RuntimeLinkRequiredSyms(...)`. That core list does not include the five
`CancelToken` modal symbols in `BuiltinModalSymMap`. A `uvc` build of the
`RuntimeInterfaceSymbols` artifact project emits `CancelToken` runtime
references such as `@CancelToken_x3a_x3anew` and
`@CancelToken_x3a_x3aActive_x3a_x3await_x5fcancelled`, but the compatibility
list cannot require those symbols from a runtime library.

Expected behavior:

`RuntimeRequiredSyms` should equal `RuntimeSyms`. The compatibility check should
require every symbol in `RuntimeSyms`, including every `BuiltinModalSym` entry,
and should avoid requiring non-spec runtime symbols unless the specification is
changed.

Impact:

A runtime library can be accepted as compatible while missing symbols the
compiler can emit. That can defer a conformance failure into link-time or
runtime failure instead of the specified compatibility diagnostic.

HelloUltraviolet fixture gap:

`RuntimeIncompatible` checks `E-OUT-0408` generically, but it does not prove
that every Chapter 24 runtime symbol is required or that non-spec symbols are
excluded.

### UV-AUDIT-0022: `CancelToken` runtime symbols bypass `BuiltinModalSym`

Severity: Medium

Status: Inspection-backed with artifact-build evidence from `uvc`.

Spec anchors:

- `Docs\SPECIFICATION.md:16086`
- `Docs\SPECIFICATION.md:16091`
- `Docs\SPECIFICATION.md:28309`
- `Docs\SPECIFICATION.md:29018`
- `Docs\SPECIFICATION.md:29027`
- `Docs\SPECIFICATION.md:29028`
- `Docs\SPECIFICATION.md:29029`
- `Docs\SPECIFICATION.md:29030`
- `Docs\SPECIFICATION.md:29031`
- `Docs\SPECIFICATION.md:29034`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\05_codegen\intrinsics\builtins.cpp:190`
- `Bootstrap\Ultraviolet\src\05_codegen\intrinsics\builtins.cpp:504`
- `Bootstrap\Ultraviolet\src\05_codegen\intrinsics\builtins.cpp:509`
- `Bootstrap\Ultraviolet\src\05_codegen\intrinsics\builtins.cpp:514`
- `Bootstrap\Ultraviolet\src\05_codegen\intrinsics\builtins.cpp:519`
- `Bootstrap\Ultraviolet\src\05_codegen\intrinsics\builtins.cpp:524`
- `Bootstrap\Ultraviolet\src\05_codegen\intrinsics\builtins.cpp:1203`

Observed behavior:

`BuiltinModalSym(...)` only dispatches the Region modal symbol table. The
`CancelToken` symbols are defined separately as `BuiltinSymCancelToken...`
helpers and are registered through the generic built-in symbol path. The
`RuntimeInterfaceSymbols` artifact project still emits the expected
`CancelToken` declarations, but it reaches them through a non-spec judgment
path.

Expected behavior:

All five `CancelToken` modal procedures in `BuiltinModalSymMap` should resolve
through `BuiltinModalSym(proc)`, matching Chapter 24's runtime-interface
judgment.

Impact:

The emitted symbol strings can look correct while the compiler's conformance
surface is split across the wrong runtime-symbol judgments. This also
contributes to the incomplete runtime compatibility set in `UV-AUDIT-0021`.

HelloUltraviolet fixture gap:

`RuntimeInterfaceArtifactExecution.uv` checks emitted `CancelToken` IR strings,
but it does not validate that the `BuiltinModalSym` judgment owns those symbols.

### UV-AUDIT-0023: Backend `PanicSite` mapping omits Chapter 24 panic sites

Severity: Low

Status: Inspection-backed; no emitted-code failure reproduced.

Spec anchors:

- `Docs\SPECIFICATION.md:28767`
- `Docs\SPECIFICATION.md:28789`
- `Docs\SPECIFICATION.md:28802`
- `Docs\SPECIFICATION.md:28803`
- `Docs\SPECIFICATION.md:28804`
- `Docs\SPECIFICATION.md:28805`
- `Docs\SPECIFICATION.md:28807`

Implementation anchors:

- `Bootstrap\Ultraviolet\include\05_codegen\checks\panic.h:37`
- `Bootstrap\Ultraviolet\src\05_codegen\checks\panic.cpp:75`

Observed behavior:

The backend `PanicSite` enum contains ordinary arithmetic, bounds, dereference,
error, init, precondition, postcondition, async, and generic sites. It omits
`TypeInv`, `LoopInv`, `ForeignPre`, `ForeignPost`, and `MatchFail` site forms
from the Chapter 24 taxonomy, so `PanicReasonOf(...)` cannot map them through
this helper.

Expected behavior:

The `PanicSite` and `PanicReasonOf(...)` implementation should cover every
Chapter 24 site-to-reason mapping, or the stale helper should be removed from
the spec-conformance surface if another complete implementation owns those
mappings.

Impact:

The backend panic/lowering API surface is not isomorphic to the specified panic
taxonomy. Fixtures can still pass through other emitted panic paths while this
helper remains incomplete.

HelloUltraviolet fixture gap:

Fixtures exercise emitted panic code paths, but none directly validates that
the backend `PanicSite` surface covers the Chapter 24 `PanicSite` set.

### UV-AUDIT-0024: Default calling convention helper conflicts with the ABI spec

Severity: Low

Status: Inspection-backed; masked in primary LLVM emission paths.

Spec anchors:

- `Docs\SPECIFICATION.md:27684`
- `Docs\SPECIFICATION.md:27858`

Implementation anchors:

- `Bootstrap\Ultraviolet\include\05_codegen\abi\abi.h:22`
- `Bootstrap\Ultraviolet\include\05_codegen\abi\abi.h:25`
- `Bootstrap\Ultraviolet\src\05_codegen\abi\calling_conventions.cpp:37`
- `Bootstrap\Ultraviolet\src\05_codegen\abi\calling_conventions.cpp:40`
- `Bootstrap\Ultraviolet\src\05_codegen\abi\calling_conventions.cpp:77`
- `Bootstrap\Ultraviolet\src\05_codegen\abi\calling_conventions.cpp:87`
- `Bootstrap\Ultraviolet\include\05_codegen\llvm\emit\internal_helpers.h:116`

Observed behavior:

The ABI helper layer still documents and implements the empty ABI string as
`CallingConvention::Ultraviolet`, and `IsCCompatible(...)` returns false for
that value. Main LLVM emission appears to mask this because its helper maps
missing ABI attributes to LLVM C calling convention.

Expected behavior:

The default calling convention helper contract should expose `C`, matching
`CallConvDefault = C`, so all ABI consumers agree about the default convention.

Impact:

The primary emission path can remain correct while secondary ABI consumers see a
non-spec default convention. This is a low-severity drift now, but it is a sharp
edge for future backend or tooling code.

HelloUltraviolet fixture gap:

No fixture asserts the ABI helper/default-convention contract directly.

### UV-AUDIT-0025: Raw `#export` catch zeroable-return failures report an internal rule id

Severity: Medium

Status: Inspection-backed; needs a reduced fixture.

Spec anchors:

- `Docs\SPECIFICATION.md:26165`
- `Docs\SPECIFICATION.md:26171`
- `Docs\SPECIFICATION.md:26186`
- `Docs\SPECIFICATION.md:30518`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\procedure_decl.cpp:2176`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\procedure_decl.cpp:2178`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\procedure_decl.cpp:2181`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\typecheck_diag_map.inc:507`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\typecheck_diag_map.inc:511`
- `HelloUltraviolet\Source\Reference\FFI\BoundaryUnwinding.uv:21`

Observed behavior:

The export typing branch detects `#unwind("catch")` with a non-zeroable return.
For hosted exports it returns `E-TYP-2635`, but for raw exports it returns the
internal rule id `Export-Return-NotZeroable-Err`. `E-TYP-2631` is registered in
the typecheck diagnostic map but is not used on that raw-export branch.

Expected behavior:

A raw `#export("C-unwind") #unwind("catch")` procedure whose return type is not
zeroable should reject with `E-TYP-2631`.

Impact:

The compiler can reject the invalid raw export with an implementation-internal
diagnostic id instead of the specified public diagnostic. This breaks diagnostic
conformance and expected-diagnostic fixtures for the raw export boundary.

HelloUltraviolet fixture gap:

`BoundaryUnwinding.uv` covers a zeroable raw catch export. There is no rejected
raw-export fixture for a non-zeroable catch return.

### UV-AUDIT-0026: Extern function-pointer generic-parameter ban is not implemented

Severity: Medium

Status: Inspection-backed with existing-fixture evidence from `uvc`.

Spec anchors:

- `Docs\SPECIFICATION.md:26104`
- `Docs\SPECIFICATION.md:26110`
- `Docs\SPECIFICATION.md:26128`
- `Docs\SPECIFICATION.md:30517`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\extern_block.cpp:558`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\extern_block.cpp:568`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\extern_block.cpp:574`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\extern_block.cpp:607`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\type_predicates.cpp:1305`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\typecheck_diag_map.inc:467`
- `HelloUltraviolet\Fixtures\RejectedSource\FFI\ExternGenericSignature\Source\Main.uv:4`
- `HelloUltraviolet\Source\Reference\FFI\FfiSafe.uv:42`
- `HelloUltraviolet\Source\Reference\FFI\ExternProcedures.uv:11`

Observed behavior:

The extern procedure checker rejects generic parameters on the extern procedure
declaration itself. It then sends parameter and return types through FFI-safety
checks, where `TypeFunc` is accepted structurally and no scan rejects generic
type parameters nested inside sparse function pointer signatures. The existing
`ExternGenericSignature` rejected fixture reports `E-TYP-2306` for a generic
extern procedure declaration, not for a non-generic extern procedure whose
nested function-pointer type mentions a generic parameter.

Expected behavior:

Extern signatures whose nested sparse function pointer types mention generic
type parameters should reject with `E-TYP-2306`.

Impact:

The compiler enforces the top-level extern generic ban but can miss the
specified nested function-pointer generic ban, admitting FFI signatures that the
specification excludes.

HelloUltraviolet fixture gap:

Accepted FFI references cover non-generic function pointer signatures, and the
rejected fixture covers generic extern procedures. There is no rejected fixture
for a non-generic extern procedure with a nested generic function-pointer
signature.

### UV-AUDIT-0027: `Intro` checks reserved identifiers before duplicate and outer-scope reuse

Severity: Medium

Status: Inspection-backed; needs reduced priority-order fixtures.

Spec anchors:

- `Docs\SPECIFICATION.md:4820`
- `Docs\SPECIFICATION.md:4825`
- `Docs\SPECIFICATION.md:4830`
- `Docs\SPECIFICATION.md:4835`
- `Docs\SPECIFICATION.md:4840`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\resolve\scopes_intro.cpp:90`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\scopes_intro.cpp:92`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\scopes_intro.cpp:103`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\scopes_intro.cpp:107`

Observed behavior:

`Intro(...)` checks `ReservedGen(name)` / language-root reservation before it
checks same-scope duplicates and outer-scope reuse. For an identifier that is
both reserved and duplicate, or both reserved and already bound in an outer
scope, the implementation selects the reserved-name path first.

Expected behavior:

When multiple `Intro` rules apply, the specification requires the first matching
clause in the ordered priority list. `Intro-Dup` comes before
`Intro-Outer-Err`, and both come before the reserved-name rules.

Impact:

First diagnostic selection can differ from the specification in overlapping
name-error cases. This is distinct from `UV-AUDIT-0007`, which covers collapsed
reserved-name categories.

HelloUltraviolet fixture gap:

Existing fixtures cover duplicate names and reserved names separately, but none
combine duplicate or outer-scope reuse with `gen_` or `ultraviolet` to lock the
priority order.

### UV-AUDIT-0028: `Spawned` is included in `AsyncTypeNames`

Severity: Low

Status: Inspection-backed.

Spec anchors:

- `Docs\SPECIFICATION.md:2149`
- `Docs\SPECIFICATION.md:4784`
- `Docs\SPECIFICATION.md:4798`
- `Docs\SPECIFICATION.md:4804`
- `Docs\SPECIFICATION.md:4882`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\resolve\scopes.cpp:139`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\scopes.cpp:150`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\scopes.cpp:151`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\scopes.cpp:253`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\scopes.cpp:256`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\scopes.cpp:257`

Observed behavior:

`UniverseProtectedNames()` includes `Spawned`, matching the universe-protected
set. `AsyncTypeNames()` also includes `Spawned`, while the specification lists
only `Async`, `Future`, `Sequence`, `Stream`, `Pipe`, `Exchange`, and `Tracked`
as async type names.

Expected behavior:

`Spawned` should remain universe-protected, but it should not be classified as
an async type name unless the specification changes.

Impact:

Universe-name diagnostics and metadata can misclassify `Spawned` as an async
type. This weakens the name-taxonomy conformance surface even when ordinary
`Spawned` typing still works.

HelloUltraviolet fixture gap:

HelloUltraviolet exercises `Spawned` as a built-in modal/parallel handle, but
does not verify the universe-name taxonomy or that `Spawned` is excluded from
`AsyncTypeNames`.

### UV-AUDIT-0029: `E-MOD-1304` diagnostic message describes the wrong failure

Severity: Low

Status: Inspection-backed.

Spec anchors:

- `Docs\SPECIFICATION.md:4825`
- `Docs\SPECIFICATION.md:4826`
- `Docs\SPECIFICATION.md:5936`
- `Docs\SPECIFICATION.md:8856`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\00_core\diagnostic_messages_table.inc:122`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\typecheck_diag_map.inc:231`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\typecheck_diag_map.inc:577`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\resolve_module.cpp:268`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\resolve_module.cpp:269`

Observed behavior:

`Intro-Outer-Err` maps to `E-MOD-1304`, but the registered message for
`E-MOD-1304` is `Unresolved module: path prefix did not resolve to a module`.
That message text belongs to a module-path resolution failure rather than
enclosing-scope name reuse.

Expected behavior:

`E-MOD-1304` should describe name reuse against an enclosing scope, matching the
diagnostic table entry for `Intro-Outer-Err`. Module-path prefix failures are
owned separately by `E-MOD-1107`.

Impact:

The compiler can emit the correct diagnostic code for enclosing-scope name reuse
with a misleading message. This degrades user-facing diagnostics and makes
message-based conformance checks unreliable.

HelloUltraviolet fixture gap:

Existing fixtures assert `E-MOD-1304` codes, but do not validate the diagnostic
message text for enclosing-scope name reuse.

### UV-AUDIT-0030: Specific backend lowering diagnostics are registered but unreachable

Severity: Medium

Status: Inspection-backed.

Spec anchors:

- `Docs\SPECIFICATION.md:29418`
- `Docs\SPECIFICATION.md:30436`
- `Docs\SPECIFICATION.md:30453`
- `Docs\SPECIFICATION.md:30454`
- `Docs\SPECIFICATION.md:30455`
- `Docs\SPECIFICATION.md:30456`
- `Docs\SPECIFICATION.md:30457`
- `Docs\SPECIFICATION.md:30459`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\00_core\diagnostic_messages_table.inc:152`
- `Bootstrap\Ultraviolet\src\00_core\diagnostic_messages_table.inc:153`
- `Bootstrap\Ultraviolet\src\00_core\diagnostic_messages_table.inc:154`
- `Bootstrap\Ultraviolet\src\00_core\diagnostic_messages_table.inc:155`
- `Bootstrap\Ultraviolet\src\00_core\diagnostic_messages_table.inc:156`
- `Bootstrap\Ultraviolet\src\00_core\diagnostic_messages_table.inc:158`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\typecheck_diag_map.inc:262`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\typecheck_diag_map.inc:263`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\typecheck_diag_map.inc:264`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\typecheck_diag_map.inc:265`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\typecheck_diag_map.inc:266`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\typecheck_diag_map.inc:268`
- `Bootstrap\Ultraviolet\src\06_driver\output_pipeline.cpp:1354`
- `Bootstrap\Ultraviolet\src\06_driver\output_pipeline.cpp:1356`
- `Bootstrap\Ultraviolet\src\06_driver\output_pipeline.cpp:1718`
- `Bootstrap\Ultraviolet\src\06_driver\output_pipeline.cpp:1932`
- `Bootstrap\Ultraviolet\src\06_driver\output_pipeline.cpp:1943`
- `Bootstrap\Ultraviolet\src\06_driver\output_pipeline.cpp:2029`
- `Bootstrap\Ultraviolet\src\06_driver\output_pipeline.cpp:2041`

Observed behavior:

The compiler registers and rule-maps the specific backend diagnostics
`E-OUT-0412`, `E-OUT-0413`, `E-OUT-0414`, `E-OUT-0415`, `E-OUT-0416`, and
`E-OUT-0418`, but the driver output pipeline calls the object and IR codegen
callbacks and collapses failures into broad `E-OUT-0402` or `E-OUT-0403`
diagnostics. A repository search found no non-generated emission path for the
listed specific diagnostics.

Expected behavior:

Backend failures owned by the specification's binding-storage, LLVM call ABI,
vtable, literal-data, runtime-symbol, and poisoning-instrumentation diagnostic
rows should emit their corresponding `E-OUT-0412` through `E-OUT-0418` codes
when those specific failure classes occur, unless the specification is narrowed.

Impact:

Users and conformance fixtures cannot distinguish the backend failure class
promised by the specification. These registered diagnostics appear reachable in
the diagnostic tables but not in the compiler's output path.

HelloUltraviolet fixture gap:

HelloUltraviolet does not include fixtures that intentionally trigger these
backend-specific failure classes or assert the corresponding diagnostic codes.

### UV-AUDIT-0031: `E-CLI-0002` and `E-CLI-0003` have no emission path

Severity: Low

Status: Inspection-backed.

Spec anchors:

- `Docs\SPECIFICATION.md:647`
- `Docs\SPECIFICATION.md:648`
- `Docs\SPECIFICATION.md:649`
- `Docs\SPECIFICATION.md:30467`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\06_driver\cli.cpp:830`
- `Bootstrap\Ultraviolet\src\06_driver\compiler_main.cpp:371`
- `Bootstrap\Ultraviolet\src\06_driver\compiler_main.cpp:376`
- `Bootstrap\Ultraviolet\src\06_driver\compiler_main.cpp:382`
- `Bootstrap\Ultraviolet\src\06_driver\compiler_main.cpp:395`
- `Bootstrap\Ultraviolet\src\06_driver\compiler_main.cpp:407`
- `Bootstrap\Ultraviolet\src\06_driver\compiler_main.cpp:412`
- `Bootstrap\Ultraviolet\src\00_core\generated\diag_registry.inc:21`
- `Bootstrap\Ultraviolet\src\00_core\generated\diag_registry.inc:22`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\typecheck_diag_map.inc:56`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\typecheck_diag_map.inc:57`

Observed behavior:

`E-CLI-0001` is emitted for unknown commands, but fixed-string repository
searches for `E-CLI-0002` and `E-CLI-0003` found only specification, generated
registry, and generated diagnostic-map rows. The diagnostic rendering paths
write JSON and text output directly to `std::cout` or `std::cerr`, and there is
no corresponding diagnostic emission when command output or diagnostic writing
fails.

Expected behavior:

If the command-line diagnostic table remains normative, the compiler should
have reachable emission paths for the pipeline-unavailable and output-write
failure cases, or the specification and generated registry should remove or
narrow those rows.

Impact:

Two command-line diagnostic rows are exposed as supported diagnostics but cannot
be observed through the driver. Output-write failures may also be reported only
through process-level I/O behavior rather than the specified diagnostic stream.

HelloUltraviolet fixture gap:

HelloUltraviolet does not include command-line fixtures that force or assert
`E-CLI-0002` or `E-CLI-0003`.

### UV-AUDIT-0032: Built-in attribute arguments bypass the generic attribute parser

Severity: Medium

Status: Inspection-backed.

Spec anchors:

- `Docs\SPECIFICATION.md:6679`
- `Docs\SPECIFICATION.md:6687`
- `Docs\SPECIFICATION.md:6692`
- `Docs\SPECIFICATION.md:6865`
- `Docs\SPECIFICATION.md:6879`
- `Docs\SPECIFICATION.md:6978`
- `Docs\SPECIFICATION.md:6982`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\02_source\parser\item\attribute_list.cpp:159`
- `Bootstrap\Ultraviolet\src\02_source\parser\item\attribute_list.cpp:163`
- `Bootstrap\Ultraviolet\src\02_source\parser\item\attribute_list.cpp:305`
- `Bootstrap\Ultraviolet\src\02_source\parser\item\attribute_list.cpp:333`
- `Bootstrap\Ultraviolet\src\02_source\parser\item\attribute_list.cpp:395`
- `Bootstrap\Ultraviolet\src\02_source\parser\item\attribute_list.cpp:565`
- `Bootstrap\Ultraviolet\src\02_source\parser\item\attribute_list.cpp:580`
- `Bootstrap\Ultraviolet\src\02_source\parser\item\attribute_list.cpp:582`
- `Bootstrap\Ultraviolet\src\04_analysis\attributes\attribute_validate.cpp:46`
- `Bootstrap\Ultraviolet\src\04_analysis\attributes\attribute_validate.cpp:90`

Observed behavior:

The parser identifies built-in `layout` and `inline` attributes and dispatches
their argument lists to `ParseLayoutArgList` and `ParseInlineArgList` instead of
the generic `ParseAttrArgList`. These paths reject attribute-specific argument
forms during parsing even though later attribute validation also owns the
`#inline` and `#layout` argument constraints.

Expected behavior:

All attributes should first parse as ordinary `AttributeSpec` entries using the
generic `attribute_args` grammar. Built-in argument restrictions for `layout`
and `inline` should be enforced by `AttrArgsOk`/attribute validation unless the
specification is changed to introduce dedicated parser productions.

Impact:

Malformed built-in attribute arguments can receive parser diagnostics instead of
attribute-validation diagnostics, and the parser owns semantic knowledge that
the specification assigns to attribute validation. This makes diagnostic
ownership and first-failure ordering fragile for built-in attributes.

HelloUltraviolet fixture gap:

Existing attribute fixtures cover accepted built-in forms and some validation
errors, but they do not prove that malformed built-in attribute arguments pass
through the generic attribute parser before validation rejects them.

### UV-AUDIT-0033: Reflection supplement diagnostics are registered but not emitted

Severity: Low

Status: Inspection-backed.

Spec anchors:

- `Docs\SPECIFICATION.md:25246`
- `Docs\SPECIFICATION.md:25352`
- `Docs\SPECIFICATION.md:25353`
- `Docs\SPECIFICATION.md:25354`
- `Docs\SPECIFICATION.md:25721`
- `Docs\SPECIFICATION.md:25722`
- `Docs\SPECIFICATION.md:25723`
- `Docs\SPECIFICATION.md:25724`
- `Docs\SPECIFICATION.md:25761`
- `Docs\SPECIFICATION.md:25762`
- `Docs\SPECIFICATION.md:25763`
- `Docs\SPECIFICATION.md:25764`
- `Docs\SPECIFICATION.md:25765`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\03_comptime\reflect.cpp:1450`
- `Bootstrap\Ultraviolet\src\03_comptime\reflect.cpp:1558`
- `Bootstrap\Ultraviolet\src\03_comptime\reflect.cpp:1563`
- `Bootstrap\Ultraviolet\src\03_comptime\reflect.cpp:1567`
- `Bootstrap\Ultraviolet\src\03_comptime\reflect.cpp:1594`
- `Bootstrap\Ultraviolet\src\03_comptime\reflect.cpp:1599`
- `Bootstrap\Ultraviolet\src\03_comptime\reflect.cpp:1603`
- `Bootstrap\Ultraviolet\src\03_comptime\reflect.cpp:1647`
- `Bootstrap\Ultraviolet\src\03_comptime\reflect.cpp:1652`
- `Bootstrap\Ultraviolet\src\03_comptime\reflect.cpp:1656`
- `Bootstrap\Ultraviolet\src\03_comptime\reflect.cpp:1671`
- `Bootstrap\Ultraviolet\src\03_comptime\reflect.cpp:1690`
- `Bootstrap\Ultraviolet\src\03_comptime\reflect.cpp:1691`
- `Bootstrap\Ultraviolet\src\00_core\generated\diag_registry.inc:164`
- `Bootstrap\Ultraviolet\src\00_core\generated\diag_registry.inc:165`
- `Bootstrap\Ultraviolet\src\00_core\generated\diag_registry.inc:166`
- `Bootstrap\Ultraviolet\src\00_core\generated\diag_registry.inc:167`
- `Bootstrap\Ultraviolet\src\00_core\generated\diag_registry.inc:168`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\typecheck_diag_map.inc:199`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\typecheck_diag_map.inc:200`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\typecheck_diag_map.inc:201`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\typecheck_diag_map.inc:202`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\typecheck_diag_map.inc:203`

Observed behavior:

The compile-time reflection implementation emits the older reflection
diagnostics `E-CTE-0050`, `E-CTE-0051`, `E-CTE-0052`, and `E-CTE-0053` for
non-record, non-enum, non-modal, incomplete, and non-reflectable cases.
The later supplement diagnostics `E-CTE-0420`, `E-CTE-0430`, `E-CTE-0440`,
`E-CTE-0450`, and `E-CTE-0470` are present in generated registries and maps, but
fixed-string searches found no source emission path for them. The
`implements_form` reflection query returns `false` when the nominal target is
missing rather than emitting `E-CTE-0470`.

Expected behavior:

Reflection failures should either emit the supplement diagnostics promised by
the specification or the specification and generated tables should remove or
merge the duplicate rows with the older reflection diagnostics.

Impact:

Reflection conformance fixtures cannot observe the newer supplement diagnostic
codes. The compiler's diagnostic table suggests a finer-grained reflection
diagnostic surface than the implementation exposes.

HelloUltraviolet fixture gap:

HelloUltraviolet does not assert the newer reflection supplement diagnostics for
`category`, `fields`, `variants`, `states`, or type-predicate reflection
queries.

### UV-AUDIT-0034: `ProjectFiles` path diagnostics conflict with value-level file errors

Severity: Low

Status: Inspection-backed.

Spec anchors:

- `Docs\SPECIFICATION.md:7204`
- `Docs\SPECIFICATION.md:25135`
- `Docs\SPECIFICATION.md:25143`
- `Docs\SPECIFICATION.md:25176`
- `Docs\SPECIFICATION.md:25177`
- `Docs\SPECIFICATION.md:25179`
- `Docs\SPECIFICATION.md:25186`
- `Docs\SPECIFICATION.md:25187`
- `Docs\SPECIFICATION.md:25189`
- `Docs\SPECIFICATION.md:25196`
- `Docs\SPECIFICATION.md:25197`
- `Docs\SPECIFICATION.md:25199`
- `Docs\SPECIFICATION.md:25206`
- `Docs\SPECIFICATION.md:25207`
- `Docs\SPECIFICATION.md:25209`
- `Docs\SPECIFICATION.md:25725`
- `Docs\SPECIFICATION.md:25726`
- `Docs\SPECIFICATION.md:25727`
- `Docs\SPECIFICATION.md:25728`
- `Docs\SPECIFICATION.md:25729`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\03_comptime\common.cpp:89`
- `Bootstrap\Ultraviolet\src\03_comptime\files.cpp:264`
- `Bootstrap\Ultraviolet\src\03_comptime\files.cpp:301`
- `Bootstrap\Ultraviolet\src\03_comptime\files.cpp:326`
- `Bootstrap\Ultraviolet\src\03_comptime\files.cpp:372`
- `Bootstrap\Ultraviolet\src\03_comptime\files.cpp:459`
- `Bootstrap\Ultraviolet\src\03_comptime\files.cpp:461`
- `Bootstrap\Ultraviolet\src\03_comptime\files.cpp:463`
- `Bootstrap\Ultraviolet\src\03_comptime\files.cpp:465`
- `Bootstrap\Ultraviolet\src\03_comptime\files.cpp:485`
- `Bootstrap\Ultraviolet\src\00_core\generated\diag_registry.inc:129`
- `Bootstrap\Ultraviolet\src\00_core\generated\diag_registry.inc:130`
- `Bootstrap\Ultraviolet\src\00_core\generated\diag_registry.inc:131`
- `Bootstrap\Ultraviolet\src\00_core\generated\diag_registry.inc:132`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\typecheck_diag_map.inc:164`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\typecheck_diag_map.inc:165`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\typecheck_diag_map.inc:166`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\typecheck_diag_map.inc:167`

Observed behavior:

`ProjectFiles` without an available snapshot emits `E-CTE-0060`, but
`E-CTE-0061`, `E-CTE-0062`, `E-CTE-0063`, and `E-CTE-0064` appear only in
generated registry and diagnostic-map files. The file built-ins implement the
execution rules by returning `Outcome::Error(IoError::InvalidPath)` for
restricted paths and `IoError::NotFound` for missing paths rather than emitting
compile-time diagnostics.

Expected behavior:

The compile-time diagnostics table should agree with the `ProjectFiles`
execution rules. If invalid and missing paths are value-level `IoError`
outcomes, the diagnostic rows should be removed or narrowed. If they are meant
to be diagnostics, the implementation should emit the specific `E-CTE-0062`
through `E-CTE-0064` codes, and `#files` target misuse should emit
`E-CTE-0061`.

Impact:

Conformance tests cannot observe four registered `ProjectFiles` diagnostics,
and the specification simultaneously describes value-level errors and
diagnostic-table errors for overlapping cases.

HelloUltraviolet fixture gap:

HelloUltraviolet covers `ProjectFiles` behavior through ordinary outcomes, but
does not assert whether invalid or missing paths must be diagnostics or
value-level `IoError` results.

### UV-AUDIT-0035: Malformed `Type::<...>` literals never emit `E-CTE-0410`

Severity: Low

Status: Inspection-backed.

Spec anchors:

- `Docs\SPECIFICATION.md:25258`
- `Docs\SPECIFICATION.md:25259`
- `Docs\SPECIFICATION.md:25280`
- `Docs\SPECIFICATION.md:25345`
- `Docs\SPECIFICATION.md:25348`
- `Docs\SPECIFICATION.md:25759`
- `Docs\SPECIFICATION.md:25760`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\02_source\parser\expr\type_literal.cpp:24`
- `Bootstrap\Ultraviolet\src\02_source\parser\expr\type_literal.cpp:39`
- `Bootstrap\Ultraviolet\src\02_source\parser\expr\type_literal.cpp:47`
- `Bootstrap\Ultraviolet\src\02_source\parser\expr\type_literal.cpp:53`
- `Bootstrap\Ultraviolet\src\02_source\parser\expr\type_literal.cpp:63`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\type_expr.cpp:3804`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\type_expr.cpp:3807`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\type_expr.cpp:3810`
- `Bootstrap\Ultraviolet\src\00_core\generated\diag_registry.inc:162`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\typecheck_diag_map.inc:197`

Observed behavior:

Malformed `Type::<...>` syntax is rejected in the parser through
`EmitParseSyntaxErr` when the `<` or closing `>` is missing or when the nested
type parser fails. The type checker handles the valid AST form and emits
`E-CTE-0411` only when a type literal appears outside a compile-time context.
Fixed-string searches found no source emission of `E-CTE-0410`.

Expected behavior:

If `E-CTE-0410` is meant to be the diagnostic for ill-formed `Type::<...>`
contents, malformed type literals should route to that code. Otherwise the
diagnostic row should be removed or clarified as unreachable because general
parser diagnostics own malformed type syntax.

Impact:

The diagnostics table promises a type-literal-specific error that users and
fixtures cannot observe. Malformed type literals instead collapse into generic
parse errors.

HelloUltraviolet fixture gap:

HelloUltraviolet does not include rejected `Type::<...>` fixtures that assert
the diagnostic code for malformed type-literal syntax.

### UV-AUDIT-0036: Expression-scoped `#dynamic` loop invariants are accepted but not lowered

Severity: High

Status: Inspection-backed.

Spec anchors:

- `Docs\SPECIFICATION.md:7132`
- `Docs\SPECIFICATION.md:7135`
- `Docs\SPECIFICATION.md:7210`
- `Docs\SPECIFICATION.md:7214`
- `Docs\SPECIFICATION.md:15050`
- `Docs\SPECIFICATION.md:15078`
- `Docs\SPECIFICATION.md:15082`
- `Docs\SPECIFICATION.md:15083`
- `Docs\SPECIFICATION.md:15107`
- `Docs\SPECIFICATION.md:15110`
- `Docs\SPECIFICATION.md:15296`
- `Docs\SPECIFICATION.md:15299`
- `Docs\SPECIFICATION.md:15301`
- `Docs\SPECIFICATION.md:15304`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\typing\type_expr.cpp:4276`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\type_expr.cpp:4287`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\type_expr.cpp:4300`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\type_expr.cpp:4301`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\loop_conditional.cpp:127`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\loop_conditional.cpp:520`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\loop_conditional.cpp:522`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\expr_common.cpp:184`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\expr_common.cpp:192`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\expr_common.cpp:201`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\expr_common.cpp:209`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\lower_proc.cpp:1452`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\lower_proc.cpp:1464`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\lower_proc.cpp:1577`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\loop_conditional.cpp:40`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\loop_conditional.cpp:44`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\loop_infinite.cpp:37`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\loop_infinite.cpp:41`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\loop_iter.cpp:128`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\loop_iter.cpp:132`

Observed behavior:

Typing honors expression-scoped `#dynamic` by computing a dynamic contract
context for attributed expressions. Loop invariant typing then skips the
static proof rejection when that context is dynamic. Lowering does not preserve
the same expression-local dynamic context: attributed expression lowering
records only memory-order metadata before delegating to the inner expression,
and loop invariant lowering emits runtime contract checks only when
`ctx.dynamic_checks` is already enabled by a procedure-level or test-level
dynamic context.

Expected behavior:

An expression-scoped `#dynamic` annotation around a loop expression must select
runtime verification for the loop invariant enforcement points required by the
contract chapter. A non-dynamic procedure should not be able to accept an
unproved dynamic loop invariant without emitting the corresponding runtime
checks.

Impact:

The compiler can accept a dynamically verified loop invariant while omitting
the specified runtime enforcement at loop entry, back-edge, and continue paths.

HelloUltraviolet fixture gap:

HelloUltraviolet checks declaration-level loop invariant lowering through
`HelloUltraviolet\Fixtures\ArtifactProjects\EmitLlLibrary\Source\Library.uv:750`
and
`HelloUltraviolet\Source\Audit\ArtifactProjectExecution.uv:4529`, but no fixture
asserts expression-scoped `#dynamic` loop invariant lowering in a non-dynamic
procedure.

### UV-AUDIT-0037: `CancelToken@Active::cancel` is typed as const instead of shared

Severity: High

Status: Inspection-backed.

Spec anchors:

- `Docs\SPECIFICATION.md:10803`
- `Docs\SPECIFICATION.md:12795`
- `Docs\SPECIFICATION.md:12797`
- `Docs\SPECIFICATION.md:22631`
- `Docs\SPECIFICATION.md:29287`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\modal\builtin_modal_intrinsics.cpp:185`
- `Bootstrap\Ultraviolet\src\04_analysis\modal\builtin_modal_intrinsics.cpp:188`
- `Bootstrap\Ultraviolet\src\04_analysis\modal\builtin_modal_intrinsics.cpp:190`
- `Bootstrap\Ultraviolet\src\04_analysis\caps\cap_concurrency.cpp:451`
- `Bootstrap\Ultraviolet\src\04_analysis\caps\cap_concurrency.cpp:561`
- `Bootstrap\Ultraviolet\src\04_analysis\caps\cap_concurrency.cpp:563`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\method_call.cpp:1147`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\method_call.cpp:1148`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\method_call.cpp:1158`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\method_call.cpp:1733`

Observed behavior:

The specification models `CancelToken@Active::cancel` as
`ReceiverShorthand(shared)` and gives its runtime signature a
`TypePerm(shared, CancelToken@Active)` self parameter. Both compiler built-in
lookup paths currently synthesize `cancel` with `Permission::Const` /
`ReceiverPerm::Const`. The method-call checker maps const receiver
requirements on shared callers to read key coverage, so a cancellation request
can be admitted as a read-only shared operation.

Expected behavior:

`CancelToken@Active::cancel` should require shared receiver authority as
specified. Shared calls to cancellation must require the write-capable key
coverage used for non-const shared receiver methods.

Impact:

Cancellation mutates cancellation state but can bypass the expected shared
write authority check. That weakens the cancellation capability boundary and
can suppress the diagnostic that should appear under read-only key coverage.

HelloUltraviolet fixture gap:

HelloUltraviolet covers ordinary/local cancellation calls in
`HelloUltraviolet\Source\Reference\Parallelism\Cancellation.uv:4`, lowering
evidence in
`HelloUltraviolet\Fixtures\ArtifactProjects\StructuredParallelLoweringEvidence\Source\Main.uv:4`,
and runtime symbol coverage in
`HelloUltraviolet\Fixtures\ArtifactProjects\RuntimeInterfaceSymbols\Source\Library.uv:405`.
The only rejected cancellation option fixture found is
`HelloUltraviolet\Fixtures\RejectedSource\Parallelism\ParallelCancelOptionType\Source\Main.uv:1`;
there is no rejected fixture for calling `CancelToken@Active::cancel()` through
read-only shared key access.

### UV-AUDIT-0038: `copy` can erase region provenance before FFI raw-pointer return isolation

Severity: High

Status: Inspection-backed.

Spec anchors:

- `Docs\SPECIFICATION.md:13868`
- `Docs\SPECIFICATION.md:13871`
- `Docs\SPECIFICATION.md:13910`
- `Docs\SPECIFICATION.md:17271`
- `Docs\SPECIFICATION.md:17274`
- `Docs\SPECIFICATION.md:26588`
- `Docs\SPECIFICATION.md:26591`
- `Docs\SPECIFICATION.md:26606`
- `Docs\SPECIFICATION.md:26610`
- `Docs\SPECIFICATION.md:26621`
- `Docs\SPECIFICATION.md:26625`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\return_stmt.cpp:589`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\return_stmt.cpp:601`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\return_stmt.cpp:609`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\return_stmt.cpp:611`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\return_stmt.cpp:640`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\return_stmt.cpp:647`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\return_stmt.cpp:651`
- `Bootstrap\Ultraviolet\src\04_analysis\provenance\prov_stmt.cpp:695`
- `Bootstrap\Ultraviolet\src\04_analysis\provenance\prov_stmt.cpp:710`
- `Bootstrap\Ultraviolet\src\04_analysis\provenance\prov_expr.cpp:433`
- `Bootstrap\Ultraviolet\src\04_analysis\provenance\prov_expr.cpp:596`
- `Bootstrap\Ultraviolet\src\04_analysis\provenance\prov_expr.cpp:632`
- `Bootstrap\Ultraviolet\src\04_analysis\provenance\prov_expr.cpp:713`
- `Bootstrap\Ultraviolet\src\04_analysis\provenance\prov_expr.cpp:774`
- `Bootstrap\Ultraviolet\src\04_analysis\provenance\prov_expr.cpp:803`

Observed behavior:

Raw pointers are bitcopy types and `copy e` duplicates their value bits. The
FFI raw-pointer return checker first tries to recover a binding from the return
expression, but it unwraps identifiers, field/index/deref, `move`, and
attributed expressions only; it does not unwrap `CopyExpr`. The fallback
provenance tracker also has no explicit `CopyExpr` case and the child traversal
does not include `CopyExpr`, so `return copy pointer` falls through to empty
child provenance and reports bottom rather than region provenance.

Expected behavior:

Copying a raw pointer derived from region-local storage must not allow that
address to cross an FFI boundary. The checker should preserve the pointee
provenance through `CopyExpr` for this boundary rule and emit `E-SYS-3360`.

Impact:

An exported or host-exported procedure can return a region-local raw pointer to
foreign code by wrapping the returned pointer in `copy`, bypassing the required
capability-isolation diagnostic.

HelloUltraviolet fixture gap:

`HelloUltraviolet\Fixtures\RejectedSource\FFI\FfiReturnRegionLocalRawPtr\Source\Main.uv:8`
covers direct `return pointer` only.
`HelloUltraviolet\Source\Audit\FixtureCatalog\RejectedSource\FFI.uv:677`
registers only that direct-return specimen for
`rule.23.FFI-Return-RegionLocalRawPtr-Err`, and
`HelloUltraviolet\Source\Audit\Catalog\ForeignFunctionInterface\CapabilityIsolation.uv:65`
maps the obligation to the same direct-return fixture. No rejected fixture
covers `return copy pointer`.

### UV-AUDIT-0039: Single-form `if ... is` without `else` misses required no-match panic

Severity: High

Status: Inspection-backed.

Spec anchors:

- `Docs\SPECIFICATION.md:16994`
- `Docs\SPECIFICATION.md:16995`
- `Docs\SPECIFICATION.md:18745`
- `Docs\SPECIFICATION.md:18748`
- `Docs\SPECIFICATION.md:18755`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\expr_common.cpp:304`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\expr_common.cpp:312`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\if_case_expr.cpp:1034`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\if_case_expr.cpp:1056`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\if_case_expr.cpp:1060`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\if_case_expr.cpp:1111`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\control\if_case.cpp:673`

Observed behavior:

`IfIsExpr` lowers through `LowerIfCases` with `single_form=true`. When no
`else` is present, `LowerIfCases` synthesizes an empty unit block and appends it
as a wildcard arm. Wildcard arms always match, so a failed single-form match
returns unit instead of reaching the no-match panic path.

Expected behavior:

A failed `if ... is` match with no `else` should follow `EvalIfCases-None` and
panic. Lowering should emit the required trailing `LowerPanic(MatchFail)` arm
instead of a successful wildcard unit arm.

Impact:

User code can silently continue after a failed no-`else` `if ... is` test where
the specification requires a match-failure panic.

HelloUltraviolet fixture gap:

`HelloUltraviolet\Source\Reference\Expressions\Control.uv:192` covers this path
but asserts that state is preserved after a failed no-`else` match. No fixture
verifies the specified match-failure panic.

### UV-AUDIT-0040: Braced `if ... is { ... }` without `else` does not lower the required fallback arm

Severity: Medium

Status: Inspection-backed.

Spec anchors:

- `Docs\SPECIFICATION.md:18755`
- `Docs\SPECIFICATION.md:18763`
- `Docs\SPECIFICATION.md:18766`
- `Docs\SPECIFICATION.md:18802`
- `Docs\SPECIFICATION.md:18805`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\expr_common.cpp:299`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\expr_common.cpp:302`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\if_case_expr.cpp:1034`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\if_case_expr.cpp:1136`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\if_case_expr.cpp:1140`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\control\if_case.cpp:1565`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\control\if_case.cpp:1573`

Observed behavior:

Braced `IfCaseExpr` lowers through `LowerIfCases` with `single_form=false`. If
no `else` is present, `LowerIfCases` does not append a fallback arm before
assigning `if_case.arms`. The LLVM emitter later creates an unmatched block and
stores `MatchFail`, but that fallback is not a `LowerIfCases` trailing arm and
does not lower through the specified `LowerPanic(MatchFail)` path.

Expected behavior:

`LowerIfCases` must emit a trailing no-match arm that lowers to
`LowerPanic(MatchFail)` whenever `else_opt` is absent.

Impact:

The structured IR produced by lowering is non-conformant. LLVM currently
compensates, but any IR consumer before LLVM, or any alternate backend, sees an
`IRIfCase` missing the specified fallback arm.

HelloUltraviolet fixture gap:

`HelloUltraviolet\Source\Audit\PatternLoweringConformanceExecution.uv:246` and
`HelloUltraviolet\Source\Audit\PatternLoweringConformanceExecution.uv:363`
check marker payloads only. No fixture asserts that no-`else` case-list
lowering adds the required fallback arm.

### UV-AUDIT-0041: Assembly graph cycle validation ignores non-library vertices

Severity: High

Status: Inspection-backed.

Spec anchors:

- `Docs\SPECIFICATION.md:1227`
- `Docs\SPECIFICATION.md:1236`
- `Docs\SPECIFICATION.md:1240`
- `Docs\SPECIFICATION.md:1248`
- `Docs\SPECIFICATION.md:1743`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\resolve\assembly_import_graph.cpp:375`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\assembly_import_graph.cpp:382`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\assembly_import_graph.cpp:409`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\assembly_import_graph.cpp:423`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\assembly_import_graph.cpp:465`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\assembly_import_graph.cpp:476`
- `Bootstrap\Ultraviolet\src\06_driver\compiler_main.cpp:4167`
- `Bootstrap\Ultraviolet\src\06_driver\compiler_main.cpp:4172`
- `Bootstrap\Ultraviolet\src\06_driver\output_pipeline.cpp:1069`
- `Bootstrap\Ultraviolet\src\06_driver\output_pipeline.cpp:1071`

Observed behavior:

`ValidateAssemblyImportGraphStructure` computes reachability across all
assemblies, but its cycle DFS starts only from reachable libraries and follows
only dependencies whose target assembly is also a library. A reachable cycle
that contains at least one library but passes through a dependency assembly can
therefore be skipped.

Expected behavior:

`LibraryBoundaryCycle(P)` is defined over `AsmImportGraph(P)` without limiting
the path to library-only vertices. Any reachable cycle with at least one
library must fail `AssemblyGraph(P)` with `E-PRJ-0209`.

Impact:

Invalid mixed-kind assembly graphs can pass semantic and output-pipeline graph
validation. That can allow non-conforming link inputs, missing dependency
ownership decisions, or later failures instead of the required project
diagnostic.

HelloUltraviolet fixture gap:

`HelloUltraviolet\Source\Reference\Projects\AssemblyModel.uv:204` and
`HelloUltraviolet\Source\Reference\Projects\AssemblyModel.uv:206` model
dependency paths and library cycles separately, but there is no concrete
project fixture for a mixed-kind cycle such as selected executable to library to
dependency back to the same library.

### UV-AUDIT-0042: Compile-time unary expressions are not evaluated

Severity: High

Status: Inspection-backed; agent-reproduced with `uvc.exe`.

Spec anchors:

- `Docs\SPECIFICATION.md:24921`
- `Docs\SPECIFICATION.md:24923`
- `Docs\SPECIFICATION.md:25768`
- `Docs\SPECIFICATION.md:30668`
- `Docs\SPECIFICATION.md:30669`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\03_comptime\eval.cpp:686`
- `Bootstrap\Ultraviolet\src\03_comptime\eval.cpp:688`
- `Bootstrap\Ultraviolet\src\03_comptime\eval.cpp:802`
- `Bootstrap\Ultraviolet\src\03_comptime\eval.cpp:906`
- `Bootstrap\Ultraviolet\src\03_comptime\eval.cpp:1140`
- `Bootstrap\Ultraviolet\src\03_comptime\eval.cpp:1141`
- `Bootstrap\Ultraviolet\src\03_comptime\rewrite.cpp:527`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\unary.cpp:146`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\unary.cpp:152`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\unary.cpp:163`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\unary.cpp:52`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\unary.cpp:160`

Observed behavior:

Compile-time evaluation is required to use the ordinary operator semantics for
constructors not introduced by the compile-time chapter. `EvalExpr` handles
literals, identifiers, binary expressions, conditionals, method calls, type
literals, and quote expressions, but has no unary-expression case. Ordinary
typing and lowering both implement unary `!` and unary `-`, including integer
bitwise not and signed negation checks. Compile-time conditions such as
`!false` therefore fail evaluation instead of evaluating to `true`. The
comptime agent reproduced this with a temporary project where
`comptime { if (!false) diagnostics~>error(...) }` compiled successfully and did
not emit `E-CTE-0070`.

Expected behavior:

Compile-time unary `!` and unary `-` should evaluate with the same operator
semantics as ordinary expressions. A guarded branch using `!false` should
execute and `diagnostics.error` should emit `E-CTE-0070`.

Impact:

Compile-time guards and assertions using boolean negation can be silently
skipped, and compile-time numeric negation is unavailable despite ordinary
typing and lowering support.

HelloUltraviolet fixture gap:

`HelloUltraviolet\Fixtures\AcceptedProjects\ExpressionSemantics\Source\Library.uv:569`
covers ordinary unary typing, not compile-time evaluation.
`HelloUltraviolet\Fixtures\DiagnosticSource\Comptime\ProjectFilesInvalidPath\Source\Main.uv:34`
uses `if (!invalid_path)`, but that assertion is ineffective under this bug.

### UV-AUDIT-0043: `ProjectFiles` accepts absolute snapshot paths

Severity: Medium

Status: Inspection-backed; agent-reproduced with `uvc.exe`.

Spec anchors:

- `Docs\SPECIFICATION.md:25139`
- `Docs\SPECIFICATION.md:25141`
- `Docs\SPECIFICATION.md:25143`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\03_comptime\files.cpp:262`
- `Bootstrap\Ultraviolet\src\03_comptime\files.cpp:268`
- `Bootstrap\Ultraviolet\src\03_comptime\files.cpp:455`
- `Bootstrap\Ultraviolet\src\03_comptime\files.cpp:486`
- `Bootstrap\Ultraviolet\src\03_comptime\files.cpp:489`
- `Bootstrap\Ultraviolet\src\03_comptime\files.cpp:497`
- `Bootstrap\Ultraviolet\src\00_core\path.cpp:112`
- `Bootstrap\Ultraviolet\src\00_core\path.cpp:114`
- `Bootstrap\Ultraviolet\src\00_core\path.cpp:247`
- `Bootstrap\Ultraviolet\src\00_core\path.cpp:249`
- `Bootstrap\Ultraviolet\src\00_core\path.cpp:298`
- `Bootstrap\Ultraviolet\src\00_core\path.cpp:307`

Observed behavior:

The specification requires `ProjectFiles` paths to be project-root-relative and
forbids absolute paths. `RestrictProjectPath` delegates directly to
`core::Resolve(snapshot.root_text, raw_path)`. `core::Join` returns an absolute
second argument unchanged, and `core::Resolve` then accepts it when it
canonicalizes under the same root prefix. The comptime agent reproduced this
with a temporary project where `files~>read("C:/.../Ultraviolet.toml")`
selected the `Outcome::Value` branch rather than `IoError::InvalidPath`.

Expected behavior:

`files.read`, `files.read_bytes`, `files.exists`, and `files.list_dir` should
return `IoError::InvalidPath` for absolute arguments before resolving them
against the Phase 2 snapshot.

Impact:

Compile-time file access can depend on host-specific absolute paths when those
paths refer to entries in the snapshot, breaking the project-root-relative
contract and weakening deterministic Phase 2 portability.

HelloUltraviolet fixture gap:

`HelloUltraviolet\Fixtures\DiagnosticSource\Comptime\ProjectFilesInvalidPath\Source\Main.uv:6`
checks only `"../outside-project"`.
`HelloUltraviolet\Fixtures\AcceptedProjects\ComptimeConformance\Source\Library.uv:198`
covers relative reads, and
`HelloUltraviolet\Fixtures\AcceptedProjects\ComptimeConformance\Source\Library.uv:230`
checks only that `project_root()` is non-empty. No fixture checks absolute
`ProjectFiles` arguments.

### UV-AUDIT-0044: Known callee key-access summaries are built but not enforced

Severity: High

Status: Inspection-backed.

Spec anchors:

- `Docs\SPECIFICATION.md:20971`
- `Docs\SPECIFICATION.md:21005`
- `Docs\SPECIFICATION.md:21008`
- `Docs\SPECIFICATION.md:21010`
- `Docs\SPECIFICATION.md:21013`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\call.cpp:678`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\call.cpp:684`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\call.cpp:757`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\call.cpp:811`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\call.cpp:1323`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\call.cpp:1325`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\call.cpp:3802`

Observed behavior:

`ProcedureKeyAccessSummaryBuilder` computes direct and propagated key accesses
for known callees, but `EmitUnknownCalleeAccessWarningIfNeeded` returns as soon
as `summary.unknown` is false. The only hard call-site key check in this path is
`CheckSharedArgWriteRequirement`, and that check is limited to syntactically
`unique` parameters receiving shared actuals.

Expected behavior:

For statically resolved calls, the compiler should instantiate
`CalleeAccessSummary(f)` at the call site and reject uncovered or under-mode
accesses with `E-CON-0005`. A callee summary requiring `Write` must not be
covered by a caller holding only `Read`.

Impact:

A caller can hold a read key over a shared actual and call a known procedure
whose body acquires a write key through that shared parameter. That weakens the
specified reentrant key model.

HelloUltraviolet fixture gap:

`HelloUltraviolet\Source\Audit\FixtureCatalog\DiagnosticSource\Keys.uv:79`
checks unknown-callee coverage, but there is no rejected fixture for a known
callee whose summary requires `Write` while the caller holds only `Read`.

### UV-AUDIT-0045: Modal transition calls do not enforce move receiver syntax

Severity: High

Status: Inspection-backed.

Spec anchors:

- `Docs\SPECIFICATION.md:11195`
- `Docs\SPECIFICATION.md:11224`
- `Docs\SPECIFICATION.md:11225`
- `Docs\SPECIFICATION.md:12263`
- `Docs\SPECIFICATION.md:14456`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\method_call.cpp:1918`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\method_call.cpp:1923`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\method_call.cpp:1938`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\method_call.cpp:1992`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\method_call.cpp:2255`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\method_call.cpp:2305`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\method_call.cpp:2310`

Observed behavior:

Transition-call typing checks that the receiver permission admits `unique`, then
accepts the call without checking `RecvArgOk(e_self, move)`. Lowering sets
`move_receiver` for transitions and silently lowers a non-`MoveExpr` receiver
through `LowerMovePlace`.

Expected behavior:

The transition receiver mode is `move`. A transition invocation should require a
receiver expression that is explicitly a `MoveExpr`, matching `RecvArgOk`.

Impact:

Transition authority becomes implicit at the call site, accepting syntax the
spec rejects and making modal state consumption less visible.

HelloUltraviolet fixture gap:

`HelloUltraviolet\Source\Reference\ModalTypes\Transitions.uv:26`,
`:28`, `:30`, and `:33` use non-move transition calls. There is no rejected
fixture for invoking a transition without explicit move syntax.

### UV-AUDIT-0046: Dynamic-index conflict checking ignores compound assignments

Severity: Medium

Status: Inspection-backed.

Spec anchors:

- `Docs\SPECIFICATION.md:20890`
- `Docs\SPECIFICATION.md:20891`
- `Docs\SPECIFICATION.md:20905`
- `Docs\SPECIFICATION.md:20972`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\key_block_stmt.cpp:791`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\key_block_stmt.cpp:1359`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\key_block_stmt.cpp:1365`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\key_block_stmt.cpp:1885`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\compound_assign_stmt.cpp:351`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\compound_assign_stmt.cpp:421`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\compound_assign_stmt.cpp:491`

Observed behavior:

The key-block write-path collector treats `ast::CompoundAssignStmt` as a write
surface, and compound assignment typing enforces write-key requirements. The
same-statement dynamic-index conflict pass, however, only visits
`ast::AssignStmt` and skips compound assignments entirely.

Expected behavior:

`K-Dynamic-Index-Conflict` applies to dynamic indexed accesses within the same
statement when they are not provably disjoint, including compound assignment
statement surfaces.

Impact:

A compound write/read expression over dynamic indices can be classified as
statically safe when the equivalent ordinary assignment form would be rejected
with `E-CON-0010`.

HelloUltraviolet fixture gap:

`HelloUltraviolet\Source\Reference\Keys\ConflictDetection.uv:89` covers a
permitted compound writeback, and rejected key fixtures cover other conflict
forms, but there is no rejected fixture for compound assignment triggering
`K-Dynamic-Index-Conflict`.

### UV-AUDIT-0047: Binding record patterns ignore the expected record path

Severity: High

Status: Inspection-backed.

Spec anchors:

- `Docs\SPECIFICATION.md:18291`
- `Docs\SPECIFICATION.md:18292`
- `Docs\SPECIFICATION.md:19372`
- `Docs\SPECIFICATION.md:19383`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\stmt_common.cpp:3888`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\stmt_common.cpp:3892`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\stmt_common.cpp:3901`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\pattern\pattern_common.cpp:609`

Observed behavior:

Binding destructuring verifies only that the expected type is some
`TypePathType`, then looks up the record named by the pattern. It does not
require the expected path to match `node.path`. The case-pattern checker has the
missing nominal path check in its separate implementation.

Expected behavior:

`Pat-Record-R` requires `StripPerm(T) = TypePath(p)` for the same `p` named by
`RecordPattern(p, io)`.

Impact:

A binding can destructure a value of one nominal record type using another
record pattern with compatible field names, breaking nominal record safety and
lowering field projections against the wrong shape.

HelloUltraviolet fixture gap:

Existing record rejection fixtures cover missing fields on the same record, and
`HelloUltraviolet\Source\Reference\Patterns\TupleRecordPatterns.uv:31` covers
valid same-record destructuring. No fixture covers two distinct record types
with overlapping field names.

### UV-AUDIT-0048: Binding record patterns bypass `FieldVisible`

Severity: High

Status: Inspection-backed.

Spec anchors:

- `Docs\SPECIFICATION.md:9848`
- `Docs\SPECIFICATION.md:18291`
- `Docs\SPECIFICATION.md:18292`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\stmt_common.cpp:3903`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\stmt_common.cpp:3917`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\pattern\pattern_common.cpp:623`

Observed behavior:

Binding destructuring finds a field declaration by name and proceeds without a
visibility check. The separate case-pattern checker does call `FieldVisible`.

Expected behavior:

`Pat-Record-R` requires `FieldVisible(m, R, FieldName(fp))` for every record
field pattern.

Impact:

Private record fields can be destructured outside their declaring module through
`let`, `var`, and other binding-pattern surfaces using the statement checker.

HelloUltraviolet fixture gap:

Current record fixtures cover public fields and unknown fields. No fixture
covers a private record field destructured from outside the owning module.

### UV-AUDIT-0049: Binding typed patterns accept subtype compatibility

Severity: High

Status: Inspection-backed.

Spec anchors:

- `Docs\SPECIFICATION.md:18110`
- `Docs\SPECIFICATION.md:18118`
- `Docs\SPECIFICATION.md:18150`
- `Docs\SPECIFICATION.md:18156`
- `Docs\SPECIFICATION.md:19372`
- `Docs\SPECIFICATION.md:19461`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\stmt_common.cpp:3828`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\stmt_common.cpp:3841`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\stmt_common.cpp:3851`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\lower_bind.cpp:333`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\pattern\typed_pattern.cpp:313`

Observed behavior:

If exact equivalence fails, the binding-pattern `TypedPattern` branch accepts
`Subtyping(lowered.type, expected)` and binds the name at the lowered pattern
type.

Expected behavior:

`Pat-Typed-Exact-R` requires equivalence to `StripPerm(T)`, and
`Pat-Typed-Union-R` requires equivalence to an exact union member. General
subtype compatibility is not a typed-pattern rule.

Impact:

Binding subpatterns can narrow a value to a subtype the pattern did not prove.
Lowering then binds the value at the narrowed type without a runtime membership
check.

HelloUltraviolet fixture gap:

Existing typed-pattern fixtures cover exact typed patterns and incompatible
`if ... is` typed patterns. No fixture covers a binding typed subpattern whose
type is merely a subtype of the expected value type.

### UV-AUDIT-0050: Typed modal-state patterns count as modal exhaustiveness

Severity: High

Status: Inspection-backed.

Spec anchors:

- `Docs\SPECIFICATION.md:18110`
- `Docs\SPECIFICATION.md:18118`
- `Docs\SPECIFICATION.md:18832`
- `Docs\SPECIFICATION.md:18849`
- `Docs\SPECIFICATION.md:18867`
- `Docs\SPECIFICATION.md:18945`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\typing\pattern\pattern_common.cpp:508`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\pattern\pattern_common.cpp:524`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\pattern\pattern_common.cpp:551`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\if_case_check.cpp:1137`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\if_case_check.cpp:1164`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\if_case_check.cpp:1952`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\if_case_check.cpp:1958`

Observed behavior:

`TypePatternAgainstType` accepts `TypedPattern(_, M@State)` against a general
modal `M`, and `ArmStates` inserts typed modal-state patterns into the covered
state set.

Expected behavior:

Modal state matching over general modals is specified through `ModalPattern`.
`CoversState` explicitly holds only for `ModalPattern`, and does not hold for
any other pattern form.

Impact:

A non-spec pattern form can satisfy modal exhaustiveness and suppress
`E-TYP-2060` where the spec requires real modal-pattern coverage, an irrefutable
arm, or an `else`.

HelloUltraviolet fixture gap:

`HelloUltraviolet\Fixtures\RejectedSource\Patterns\IfCaseModalNonExhaustive\Source\Main.uv:13`
covers missing modal-pattern coverage. No fixture covers typed modal-state
patterns in modal case analysis.

### UV-AUDIT-0051: Generic overload candidates are excluded

Severity: High

Status: Inspection-backed.

Spec anchors:

- `Docs\SPECIFICATION.md:12566`
- `Docs\SPECIFICATION.md:12596`
- `Docs\SPECIFICATION.md:14561`
- `Docs\SPECIFICATION.md:14575`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\call.cpp:473`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\call.cpp:491`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\call.cpp:614`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\call.cpp:644`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\call.cpp:3687`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\call.cpp:3738`

Observed behavior:

`CheckFreeProcedureOverloadCandidate` returns non-viable for every procedure
with method-level generic parameters before arity, argument compatibility,
inference, exact-match preference, or specificity can run. Explicit generic
calls are handled later outside this overload-candidate path.

Expected behavior:

Free-procedure overload resolution should retain generic candidates when their
parameter count and argument compatibility match, then apply genericity and
constraint-specificity preferences. `ResolvedCallee(call)` must include the
selected generic substitution.

Impact:

Valid generic overload calls can be rejected with `E-SEM-3031`, and explicit
generic overload calls can bypass the overload algorithm that should select the
declaration.

HelloUltraviolet fixture gap:

`HelloUltraviolet\Source\Reference\Procedures\Overloading.uv:19` covers
non-generic overloads. No fixture covers a generic overload set requiring
generic candidate selection.

### UV-AUDIT-0052: Dynamic vtables omit inherited effective methods

Severity: High

Status: Inspection-backed.

Spec anchors:

- `Docs\SPECIFICATION.md:12868`
- `Docs\SPECIFICATION.md:13248`
- `Docs\SPECIFICATION.md:13330`
- `Docs\SPECIFICATION.md:13331`
- `Docs\SPECIFICATION.md:13348`
- `Docs\SPECIFICATION.md:13356`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\composite\classes.cpp:860`
- `Bootstrap\Ultraviolet\src\04_analysis\composite\classes.cpp:964`
- `Bootstrap\Ultraviolet\src\05_codegen\dyn_dispatch\dyn_dispatch.cpp:267`
- `Bootstrap\Ultraviolet\src\05_codegen\dyn_dispatch\dyn_dispatch.cpp:274`
- `Bootstrap\Ultraviolet\src\05_codegen\dyn_dispatch\dyn_dispatch.cpp:375`
- `Bootstrap\Ultraviolet\src\05_codegen\dyn_dispatch\dyn_dispatch.cpp:487`
- `Bootstrap\Ultraviolet\src\05_codegen\dyn_dispatch\dyn_dispatch.cpp:568`

Observed behavior:

Analysis resolves class methods through the effective method table, including
inherited methods. Vtable emission and `VSlot`, however, compute eligible slots
by iterating only `class_decl.items`.

Expected behavior:

`VTableEligible(Cl)` and vtable slots should be derived from `EffMethods(Cl)`,
not only the methods directly declared in the dynamic class.

Impact:

Dynamic calls through a subclass dynamic type can typecheck against an inherited
method, then lower with no vtable slot for that method.

HelloUltraviolet fixture gap:

`HelloUltraviolet\Source\Reference\Polymorphism\DynamicClassObjects.uv:33`
casts to a base dynamic class and calls a direct base method. No fixture calls
an inherited method through a subclass dynamic type.

### UV-AUDIT-0053: Dynamic dispatch eligibility uses non-spec `Self` occurrence logic

Severity: High

Status: Inspection-backed.

Spec anchors:

- `Docs\SPECIFICATION.md:13229`
- `Docs\SPECIFICATION.md:13247`
- `Docs\SPECIFICATION.md:13248`
- `Docs\SPECIFICATION.md:13288`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\composite\classes.cpp:782`
- `Bootstrap\Ultraviolet\src\04_analysis\composite\classes.cpp:784`
- `Bootstrap\Ultraviolet\src\04_analysis\composite\classes.cpp:806`
- `Bootstrap\Ultraviolet\src\04_analysis\composite\classes.cpp:811`
- `Bootstrap\Ultraviolet\src\04_analysis\composite\classes.cpp:1149`
- `Bootstrap\Ultraviolet\src\04_analysis\composite\classes.cpp:1173`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\method_call.cpp:2163`
- `Bootstrap\Ultraviolet\src\05_codegen\dyn_dispatch\dyn_dispatch.cpp:164`
- `Bootstrap\Ultraviolet\src\05_codegen\dyn_dispatch\dyn_dispatch.cpp:168`

Observed behavior:

One analysis `SelfOccurs` implementation recurses through safe pointer and raw
pointer types, even though the dynamic-dispatch spec says pointer and raw-pointer
`Self` do not count. The public `VTableEligible` method used by dynamic-call
typing ignores return types and only rejects direct `Self` parameter positions.
The backend helper has the correct pointer exclusions, creating another split
between analysis and lowering.

Expected behavior:

Dispatch eligibility should use one spec-equivalent `SelfOccurs(m)` definition:
return type plus nested parameter positions count, pointer and raw-pointer
positions do not.

Impact:

Valid dynamic classes can be rejected when `Self` appears through a pointer, and
invalid dynamic method calls with return or nested structural `Self` can reach
lowering.

HelloUltraviolet fixture gap:

Rejected fixtures cover direct `Self` parameters and generic methods. No fixture
covers pointer/raw-pointer `Self`, return `Self`, or nested `Self`.

### UV-AUDIT-0054: Unicode escapes with more than six hex digits are accepted

Severity: High

Status: Inspection-backed.

Spec anchors:

- `Docs\SPECIFICATION.md:2239`
- `Docs\SPECIFICATION.md:2379`
- `Docs\SPECIFICATION.md:2388`
- `Docs\SPECIFICATION.md:2419`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\02_source\lexer\lexer_literals.cpp:646`
- `Bootstrap\Ultraviolet\src\02_source\lexer\lexer_literals.cpp:658`
- `Bootstrap\Ultraviolet\src\02_source\lexer\lexer_literals.cpp:665`

Observed behavior:

`ScanEscapeMatch` accepts `\u{...}` escapes with one or more hex digits as long
as the resulting scalar value is valid. It does not enforce the six-hex-digit
width limit.

Expected behavior:

Unicode escapes with seven or more hex digits should be invalid escapes, even
when their scalar value would otherwise be valid.

Impact:

Invalid source can be tokenized as a valid string or character literal, and the
first bad-escape diagnostic misses a specified invalid class.

HelloUltraviolet fixture gap:

`HelloUltraviolet\Source\Reference\SourceText\Literals.uv` covers valid Unicode
escapes, and `InvalidStringEscape` covers `\q`. No fixture covers over-width
Unicode escapes in strings or chars.

### UV-AUDIT-0055: `extern`, `is`, and `where` are reserved by implementation only

Severity: Medium

Status: Inspection-backed.

Spec anchors:

- `Docs\SPECIFICATION.md:2133`
- `Docs\SPECIFICATION.md:2141`
- `Docs\SPECIFICATION.md:2443`
- `Docs\SPECIFICATION.md:2458`

Implementation anchors:

- `Bootstrap\Ultraviolet\include\00_core\keywords.h:23`
- `Bootstrap\Ultraviolet\include\00_core\keywords.h:35`
- `Bootstrap\Ultraviolet\include\00_core\keywords.h:41`
- `Bootstrap\Ultraviolet\include\00_core\keywords.h:70`
- `Bootstrap\Ultraviolet\src\02_source\lexer\lexer_ident.cpp:159`

Observed behavior:

`extern`, `is`, and `where` are present in `kUltravioletKeywords`, so they
tokenize as `Keyword` in the implementation.

Expected behavior:

Under the current `Reserved` set, those lexemes are not reserved and should
tokenize as identifiers unless the specification is explicitly expanded.

Impact:

Token streams, identifier availability, parser behavior, and diagnostics diverge
from the spec. Existing syntax using these words relies on implementation-only
keyword behavior.

HelloUltraviolet fixture gap:

Fixtures exercise `extern` and `is` syntax, but no fixture checks token-kind
conformance or identifier-position behavior for `extern`, `is`, or `where`.

### UV-AUDIT-0056: `ParseIdent` consumes keyword tokens

Severity: Medium

Status: Inspection-backed.

Spec anchors:

- `Docs\SPECIFICATION.md:2817`
- `Docs\SPECIFICATION.md:2853`
- `Docs\SPECIFICATION.md:2855`
- `Docs\SPECIFICATION.md:3170`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\02_source\parser\parser_paths.cpp:68`
- `Bootstrap\Ultraviolet\src\02_source\parser\parser_paths.cpp:219`
- `Bootstrap\Ultraviolet\src\02_source\parser\parser_paths.cpp:226`

Observed behavior:

When `ParseIdent` sees a `Keyword` token, it emits `E-CNF-0401`, advances the
parser, and returns the keyword text as the identifier.

Expected behavior:

`ParseIdent` succeeds only on identifier tokens. Non-identifier tokens should
take `Parse-Ident-Err`, emit the generic parse syntax error, not advance, and
return `_`.

Impact:

Parse recovery and AST shape differ from the specified generic parse rule,
which can affect downstream name resolution and first-failure ordering.

HelloUltraviolet fixture gap:

Reserved-name fixtures cover friendly diagnostics, but no fixture pins the
specified `ParseIdent` behavior for a keyword token in an identifier slot.

### UV-AUDIT-0057: Entry and lifecycle runtime declarations bypass `RuntimeDecls`

Severity: High

Status: Inspection-backed.

Spec anchors:

- `Docs\SPECIFICATION.md:28591`
- `Docs\SPECIFICATION.md:28597`
- `Docs\SPECIFICATION.md:28616`
- `Docs\SPECIFICATION.md:29281`
- `Docs\SPECIFICATION.md:29302`
- `Docs\SPECIFICATION.md:29308`
- `Docs\SPECIFICATION.md:29310`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\module_emit.cpp:454`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\entry_emit.cpp:571`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\entry_emit.cpp:578`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\entry_emit.cpp:582`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\entry_emit.cpp:662`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\entry_emit.cpp:684`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\entry_emit.cpp:687`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\entry_emit.cpp:733`

Observed behavior:

`RuntimeDeclsOk` and `RuntimeDeclsCover` run during module declaration emission.
Entry/lifecycle emission can later synthesize fallback declarations for runtime
panic, lifecycle functions, and context initialization. Those fallback
declarations are not produced by `RuntimeDecls` and do not pass the runtime
declaration attribute checks.

Expected behavior:

Runtime references should be declared from `RuntimeSig` through `LLVMCallSig`,
with `DeclAttrsOk` enforced on the final declarations that remain in the LLVM
module.

Impact:

Final LLVM IR can contain runtime declarations that passed no conformance check
and may lack required attributes such as panic `noreturn` or runtime
`nounwind`.

HelloUltraviolet fixture gap:

`HelloUltraviolet\Source\Audit\RuntimeInterfaceArtifactExecution.uv:763`
checks that runtime conformance log entries exist, but it does not inspect final
emitted IR after entry lowering for declaration attributes.

### UV-AUDIT-0058: Runtime ABI lowering uses foreign aggregate conventions

Severity: High

Status: Inspection-backed.

Spec anchors:

- `Docs\SPECIFICATION.md:27992`
- `Docs\SPECIFICATION.md:27994`
- `Docs\SPECIFICATION.md:28034`
- `Docs\SPECIFICATION.md:29281`
- `Docs\SPECIFICATION.md:29302`
- `Docs\SPECIFICATION.md:30243`
- `Docs\SPECIFICATION.md:30287`
- `Docs\SPECIFICATION.md:30292`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\05_codegen\intrinsics\intrinsics_interface.cpp:747`
- `Bootstrap\Ultraviolet\src\05_codegen\intrinsics\intrinsics_interface.cpp:751`
- `Bootstrap\Ultraviolet\src\05_codegen\intrinsics\intrinsics_interface.cpp:922`
- `Bootstrap\Ultraviolet\src\05_codegen\intrinsics\intrinsics_interface.cpp:1044`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\llvm_module.cpp:421`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\llvm_call.cpp:1312`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\llvm_call.cpp:1317`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\entry_emit.cpp:667`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\entry_emit.cpp:669`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\entry_emit.cpp:677`

Observed behavior:

Runtime declarations and calls are routed through `RuntimeUsesCAggregateABI`,
`RuntimeUsesForeignABI`, and `RuntimeUsesExplicitOutResultABI`. Context
initialization is forced through an explicit out-result path, and runtime calls
can be treated as C aggregate boundaries.

Expected behavior:

`ForeignABICall` is reserved for foreign-visible ABI boundaries. Runtime
declarations and runtime calls should be lowered from `RuntimeSig` using
`LLVMCallSig`.

Impact:

A runtime library implemented to the spec can disagree with compiler-generated
declarations or calls for aggregate-returning runtime functions, especially
context initialization and runtime methods returning non-scalar values.

HelloUltraviolet fixture gap:

Artifact fixtures check user `sret` behavior and tolerate multiple runtime call
shapes. They do not reject non-spec explicit-out or C-carrier runtime ABI forms.

### UV-AUDIT-0059: LLVM UB-safety is recorded instead of enforced

Severity: High

Status: Inspection-backed.

Spec anchors:

- `Docs\SPECIFICATION.md:27447`
- `Docs\SPECIFICATION.md:29491`
- `Docs\SPECIFICATION.md:29492`
- `Docs\SPECIFICATION.md:29493`
- `Docs\SPECIFICATION.md:29496`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\05_codegen\llvm\llvm_ub_safe.cpp:241`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\llvm_ub_safe.cpp:249`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\llvm_ub_safe.cpp:263`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\llvm_ub_safe.cpp:292`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\llvm_ub_safe.cpp:384`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\llvm_ub_safe.cpp:427`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\llvm_ub_safe.cpp:469`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\module_emit.cpp:77`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\module_emit.cpp:671`

Observed behavior:

Checked division, remainder, and shift helpers emit guard checks and then raw
LLVM integer operations such as `sdiv`, `udiv`, `srem`, `urem`, `shl`, `ashr`,
and `lshr`, followed by `freeze`. The module emitter records an
`llvm_ub_safe` conformance payload but does not reject artifact emission when
the predicate is false.

Expected behavior:

LLVM lowering should satisfy `LLVMUBSafe(LLVMIR)` as a condition of the emitted
artifact. The specified checked div/rem/shift intrinsics and overflow checks
should prevent raw UB-producing integer opcodes from remaining in final IR.

Impact:

LLVM can still see UB/poison-producing operations in generated IR, allowing
optimizer behavior that conflicts with language-level panic semantics.

HelloUltraviolet fixture gap:

`HelloUltraviolet\Source\Audit\Catalog\CommonLoweringProgramLifecycleAndBackend\BackendRequirements.uv:109`
records the backend obligation, but no fixture inspects emitted LLVM IR for
forbidden raw opcodes or requires `llvm_ub_safe=true`.

### UV-AUDIT-0060: Source-native execution uses boolean return values

Severity: High

Status: Inspection-backed.

Spec anchors:

- `Docs\SPECIFICATION.md:7266`
- `Docs\SPECIFICATION.md:7274`
- `Docs\SPECIFICATION.md:7284`
- `Docs\SPECIFICATION.md:7289`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\procedure_decl.cpp:177`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\procedure_decl.cpp:229`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\return_stmt.cpp:995`
- `Bootstrap\Ultraviolet\src\06_driver\compiler_main.cpp:997`
- `Bootstrap\Ultraviolet\src\06_driver\compiler_main.cpp:1029`
- `Bootstrap\Ultraviolet\src\06_driver\compiler_main.cpp:1042`
- `Bootstrap\Ultraviolet\src\06_driver\compiler_main.cpp:1194`

Observed behavior:

Source-native check shape validation requires a return annotation and a
postcondition, but does not restrict the return type to `bool`. The generated
harness still emits `if test_proc(...)`, treating the raw return value as the
pass/fail signal.

Expected behavior:

The runner should classify a check as passing when the procedure returns
normally and its postcondition is satisfied. It should not require or interpret
the raw return value as a boolean pass flag.

Impact:

Valid source-native checks with non-`bool` returns can be rejected by the
generated harness, and `bool` returns can be misclassified when postcondition
semantics do not match raw truthiness.

HelloUltraviolet fixture gap:

Current source-native fixtures couple postconditions to `@result`. There is no
fixture for a non-`bool` return with a satisfied postcondition, or a `bool false`
return with an independently satisfied postcondition.

### UV-AUDIT-0061: `#test` is accepted on extern imports

Severity: Medium

Status: Inspection-backed.

Spec anchors:

- `Docs\SPECIFICATION.md:7245`
- `Docs\SPECIFICATION.md:7256`
- `Docs\SPECIFICATION.md:7348`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\02_source\attributes\attribute_registry.cpp:512`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\extern_block.cpp:508`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\extern_block.cpp:509`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\procedure_decl.cpp:1988`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\procedure_decl.cpp:2451`
- `Bootstrap\Ultraviolet\src\06_driver\test_discovery.cpp:198`

Observed behavior:

`#test` is valid for `AttributeTarget::Procedure`, and extern procedures use the
same attribute target validation. The ordinary test-procedure shape validator
runs only in ordinary procedure typing paths, and source-native discovery scans
top-level `ast::ProcedureDecl`, not `ExternProcDecl`.

Expected behavior:

`#test` is valid only on ordinary source procedures. Applying it to an extern
import should be rejected with `E-TST-0109`.

Impact:

Invalid source can carry source-native metadata without producing an executable
check, hiding dead coverage markers and weakening placement diagnostics.

HelloUltraviolet fixture gap:

`HelloUltraviolet\Fixtures\RejectedSource\Attributes\TestAttributeWrongTarget\Source\Main.uv:3`
covers `#test` on a record. No fixture covers `#test` on an extern import.

### UV-AUDIT-0062: Static foreign postconditions are dropped when `@error` predicates exist

Severity: Medium

Status: Inspection-backed.

Spec anchors:

- `Docs\SPECIFICATION.md:26772`
- `Docs\SPECIFICATION.md:26776`
- `Docs\SPECIFICATION.md:26782`
- `Docs\SPECIFICATION.md:26788`
- `Docs\SPECIFICATION.md:26789`
- `Docs\SPECIFICATION.md:26819`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\stmt_common.cpp:1719`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\stmt_common.cpp:1747`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\stmt_common.cpp:1749`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\stmt_common.cpp:1760`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\stmt_common.cpp:1761`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\stmt_common.cpp:1771`

Observed behavior:

`ForeignPostconditionProofContextForLet` first scans the callee foreign
contracts for any non-empty `EnsuresError` clause. If one exists, it sets
`has_error_predicates` and then skips every ordinary
`ForeignContractKind::Ensures` clause when building downstream proof
assumptions.

Expected behavior:

Unconditional foreign postconditions remain part of the static foreign-contract
obligation set. With error predicates present, the unconditional predicates are
success-path obligations guarded by `SuccessCond`, while error predicates are
guarded by `ErrCond`.

Impact:

Valid callers lose specified postcondition facts after a static extern call that
also has error classification. This can make downstream static proofs fail or
prevent callers from relying on success-path guarantees that the foreign
contract explicitly provides.

HelloUltraviolet fixture gap:

`HelloUltraviolet\Source\Reference\FFI\ForeignContracts.uv:7` covers a static
unconditional foreign postcondition, and
`HelloUltraviolet\Fixtures\RejectedSource\FFI\ErrorPredicateWellFormedness\Source\Main.uv:5`
covers `@error` well-formedness on a void return. No fixture combines a static
ordinary foreign postcondition with an `@error` predicate and a downstream proof
that requires the success-path assumption.

### UV-AUDIT-0063: Ordinary compile-time expansion does not recurse through many item children

Severity: High

Status: Inspection-backed.

Spec anchors:

- `Docs\SPECIFICATION.md:24913`
- `Docs\SPECIFICATION.md:24923`
- `Docs\SPECIFICATION.md:25024`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\03_comptime\rewrite.cpp:755`
- `Bootstrap\Ultraviolet\src\03_comptime\rewrite.cpp:760`
- `Bootstrap\Ultraviolet\src\03_comptime\rewrite.cpp:771`
- `Bootstrap\Ultraviolet\src\03_comptime\rewrite.cpp:775`
- `Bootstrap\Ultraviolet\src\03_comptime\rewrite.cpp:779`
- `Bootstrap\Ultraviolet\src\03_comptime\rewrite.cpp:783`

Observed behavior:

`RewriteItem` rewrites static initializers and ordinary procedure bodies, but
record, enum, and modal declarations only have `derive` attributes stripped.
Class declarations, type aliases, extern blocks, contracts, generic defaults,
type children, member bodies, invariants, and field initializers can pass
through without recursive compile-time expansion.

Expected behavior:

For every non-Chapter-22 item constructor, the compile-time expansion relation
recursively expands direct child nodes in source order, rebuilds the same outer
constructor from expanded children, and removes compile-time constructs before
Phase 3.

Impact:

Compile-time effects in item children can be skipped, emitted declarations can
be lost or ordered incorrectly, and later compiler phases can receive
unexpanded Chapter 22 nodes.

HelloUltraviolet fixture gap:

Accepted fixtures cover procedure-local compile-time blocks and quote/splice
emission in procedure bodies. They do not cover compile-time forms inside
record, modal, or class members; field initializers; invariants; type aliases;
contracts; or generic/type children.

### UV-AUDIT-0064: Compile-time capability methods are dispatched by receiver name without enforcing the capability binding

Severity: High

Status: Inspection-backed.

Spec anchors:

- `Docs\SPECIFICATION.md:25063`
- `Docs\SPECIFICATION.md:25127`
- `Docs\SPECIFICATION.md:25718`
- `Docs\SPECIFICATION.md:25725`
- `Docs\SPECIFICATION.md:25745`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\03_comptime\common.cpp:72`
- `Bootstrap\Ultraviolet\src\03_comptime\files.cpp:413`
- `Bootstrap\Ultraviolet\src\03_comptime\files.cpp:429`
- `Bootstrap\Ultraviolet\src\03_comptime\eval.cpp:557`

Observed behavior:

`WithCtCaps` only binds `files` when the compile-time site has `#files`, and
only binds `emitter` when the site has `#emit` or is a derive target body.
However, `EvalProjectFilesMethod` recognizes a receiver spelled `files` before
checking that a `ProjectFiles` capability binding exists, and `files.project_root()`
returns the project root without consulting the snapshot path. `emitter.emit`
similarly recognizes the receiver spelling before it proves the `TypeEmitter`
binding is available.

Expected behavior:

Project-file methods are available only through the `ProjectFiles` capability,
which exists only under `#files`. `emitter.emit` is available only through the
`TypeEmitter` capability, which exists only under `#emit` or in derive target
bodies.

Impact:

Unauthorized compile-time project-root introspection is possible, and other
invalid capability uses can fail through incidental evaluation paths instead of
the specified capability diagnostics.

HelloUltraviolet fixture gap:

Positive `#files` and `#emit` paths are covered, along with wrong emitted item
kind under `#emit`. There are no rejected fixtures for missing `#files` or
missing `#emit` capability access.

### UV-AUDIT-0065: Derive target lookup misses cross-module Phase 2 emission dependency diagnostics

Severity: Medium

Status: Inspection-backed.

Spec anchors:

- `Docs\SPECIFICATION.md:24823`
- `Docs\SPECIFICATION.md:24832`
- `Docs\SPECIFICATION.md:25628`
- `Docs\SPECIFICATION.md:25749`
- `Docs\SPECIFICATION.md:25766`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\03_comptime\pass.cpp:944`
- `Bootstrap\Ultraviolet\src\03_comptime\reflect.cpp:280`
- `Bootstrap\Ultraviolet\src\03_comptime\reflect.cpp:295`
- `Bootstrap\Ultraviolet\src\03_comptime\reflect.cpp:304`
- `Bootstrap\Ultraviolet\src\03_comptime\derive.cpp:300`
- `Bootstrap\Ultraviolet\src\03_comptime\derive.cpp:350`
- `Bootstrap\Ultraviolet\src\03_comptime\derive.cpp:569`
- `Bootstrap\Ultraviolet\src\03_comptime\derive.cpp:715`

Observed behavior:

Reflection lookup searches the Phase 1 view first, then checks
`phase2_expanded_modules` to emit `E-CTE-0090` when a name would resolve only
through another module's Phase 2 emissions. Derive target lookup searches only
the available Phase 1 module view. A derive target emitted by another module's
Phase 2 execution therefore reports `E-CTE-0310` unknown derive target instead
of the cross-module Phase 2 dependency diagnostic.

Expected behavior:

Phase 2 execution must not depend on declarations emitted by Phase 2 execution
of another module. When derive target resolution would require such a
cross-module emitted declaration, it should reject for `CtExpand-CrossModule-Emit-Err`.

Impact:

The same phase-ordering rule is enforced differently for reflection and derive
lookup, and users receive an unknown-target error instead of the actual
cross-module emission dependency cause.

HelloUltraviolet fixture gap:

Fixtures cover truly unknown derive targets and same-module derive target use.
No fixture covers a derive target emitted in one module and consumed from
another module.

### UV-AUDIT-0066: `all` failure lowering does not cancel unfinished operands

Severity: High

Status: Inspection-backed.

Spec anchors:

- `Docs\SPECIFICATION.md:24096`
- `Docs\SPECIFICATION.md:24130`
- `Docs\SPECIFICATION.md:24374`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\all_expr.cpp:67`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\all.cpp:99`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\all.cpp:336`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\all.cpp:341`

Observed behavior:

The LLVM emission path for `all` detects a failed operand, extracts its error
payload, stores the coerced result, and branches directly to merge. There is no
lowered cancellation over the remaining live async operands.

Expected behavior:

`AllJoinIR` must preserve expression-order results on success and must cancel
unfinished operands on the first failure.

Impact:

Unfinished operands can continue executing after `all` has resolved to a
failure, allowing post-failure effects that the async semantics forbid.

HelloUltraviolet fixture gap:

`HelloUltraviolet\Fixtures\ArtifactProjects\ExpressionSemantics\Source\Library.uv:275`
covers a non-failing `all` expression. Rejected fixtures cover static typing
only and do not exercise failure cancellation.

### UV-AUDIT-0067: Return-mode `race` does not cancel losing arms on completion or failure

Severity: High

Status: Inspection-backed.

Spec anchors:

- `Docs\SPECIFICATION.md:23979`
- `Docs\SPECIFICATION.md:23988`
- `Docs\SPECIFICATION.md:24041`
- `Docs\SPECIFICATION.md:24353`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\race_return.cpp:220`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\race_return.cpp:243`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\race_return.cpp:350`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\race_return.cpp:379`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\race_return.cpp:381`

Observed behavior:

When a return-mode `race` arm completes, the emitter runs that handler, stores
the selected result, and branches to merge. When an arm fails, it stores the
failure result and branches to merge. Neither path cancels the remaining active
arms.

Expected behavior:

The completed winner path and the failed-arm path both cancel all remaining
active arms before producing the selected return or failure result.

Impact:

Losing `race` arms can continue executing and causing effects after the race
result has already been chosen.

HelloUltraviolet fixture gap:

`HelloUltraviolet\Fixtures\ArtifactProjects\ExpressionSemantics\Source\Library.uv:282`
uses non-failing operands. Rejected race fixtures exercise static diagnostics,
not cancellation on completion or failure.

### UV-AUDIT-0068: Streaming `race` does not preserve required resumption state or failure cancellation

Severity: High

Status: Inspection-backed.

Spec anchors:

- `Docs\SPECIFICATION.md:24024`
- `Docs\SPECIFICATION.md:24085`
- `Docs\SPECIFICATION.md:24094`
- `Docs\SPECIFICATION.md:24367`
- `Docs\SPECIFICATION.md:24372`

Implementation anchors:

- `Bootstrap\Ultraviolet\include\05_codegen\ir\ir_model.h:634`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\race_expr.cpp:239`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\race_yield.cpp:246`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\race_yield.cpp:290`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\race_yield.cpp:331`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\race_yield.cpp:435`

Observed behavior:

`IRRaceYield` stores only the arm list, result value, and stream type. Lowering
does not carry an explicit race-stream state containing the live arm vector and
yielded-arm index. The emitter allocates transient arm slots, stores only the
yielded arm frame pointer into the suspended stream, and on a failed arm stores
the failure result before branching to merge without cancelling remaining live
arms.

Expected behavior:

Streaming `race` suspension state contains the race state and yielded-arm index.
On resume, the previously yielded arm resumes first, then the remaining live
arms resume in declaration order. A failed streaming arm cancels the remaining
live arms before returning failure.

Impact:

Streaming `race` cannot implement the specified resume order from its IR shape,
and failure can leave live arms running after the stream has failed.

HelloUltraviolet fixture gap:

`HelloUltraviolet\Fixtures\ArtifactProjects\ExpressionSemantics\Source\Library.uv:289`
covers a non-failing streaming `race`, but not multi-resume order or failure
cancellation.

### UV-AUDIT-0069: CLI `--out-dir` overrides the spec-defined output root

Severity: Medium

Status: Inspection-backed.

Spec anchors:

- `Docs\SPECIFICATION.md:718`
- `Docs\SPECIFICATION.md:724`
- `Docs\SPECIFICATION.md:1088`
- `Docs\SPECIFICATION.md:1280`
- `Docs\SPECIFICATION.md:1290`
- `Docs\SPECIFICATION.md:1558`
- `Docs\SPECIFICATION.md:1566`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\06_driver\cli.cpp:659`
- `Bootstrap\Ultraviolet\src\06_driver\cli.cpp:676`
- `Bootstrap\Ultraviolet\src\01_project\outputs.cpp:115`
- `Bootstrap\Ultraviolet\src\01_project\outputs.cpp:123`
- `Bootstrap\Ultraviolet\src\01_project\outputs.cpp:232`
- `Bootstrap\Ultraviolet\src\01_project\outputs.cpp:240`
- `Bootstrap\Ultraviolet\src\06_driver\output_pipeline.cpp:1116`
- `Bootstrap\Ultraviolet\src\06_driver\output_pipeline.cpp:1118`

Observed behavior:

`--out-dir` accepts any non-empty path and becomes a global override.
`ComputeOutputPaths` uses that override before manifest `assembly.out_dir`, and
the conformance ledger records `root_source=cli_override`. Output hygiene is
then checked under the overridden root.

Expected behavior:

`OutputRoot(P)` is derived from the project root plus manifest
`assembly.out_dir`, or from the project root plus `Build` when no manifest
output directory is provided. Manifest `out_dir` is required to be relative.
The specification does not define a CLI output-root override.

Impact:

Build invocations can emit artifacts outside the specified project output model
while still recording output-root and output-hygiene obligations as satisfied.
This lets an implementation extension replace the project model without a spec
rule.

HelloUltraviolet fixture gap:

`HelloUltraviolet\Source\Audit\DiagnosticFixtureExecution.uv:94` builds
absolute temporary paths, `HelloUltraviolet\Source\Audit\ArtifactProjectExecution.uv:181`
passes them through `--out-dir`, and
`HelloUltraviolet\Source\Audit\ArtifactProjectExecution.uv:1548` asserts
`root_source=cli_override` with `all_under_root=true`. No fixture rejects or
isolates the non-spec CLI override path.

### UV-AUDIT-0070: Refinement predicates reject pure helper calls

Severity: Medium

Status: Inspection-backed.

Spec anchors:

- `Docs\SPECIFICATION.md:13474`
- `Docs\SPECIFICATION.md:14747`
- `Docs\SPECIFICATION.md:14752`
- `Docs\SPECIFICATION.md:14757`
- `Docs\SPECIFICATION.md:14762`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\typing\type_wf.cpp:497`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\type_wf.cpp:520`
- `Bootstrap\Ultraviolet\src\04_analysis\contracts\contract_purity.cpp:187`
- `Bootstrap\Ultraviolet\src\04_analysis\contracts\contract_purity.cpp:207`
- `Bootstrap\Ultraviolet\src\04_analysis\contracts\contract_purity.cpp:356`
- `Bootstrap\Ultraviolet\src\04_analysis\contracts\contract_purity.cpp:358`

Observed behavior:

Refinement type predicates are typed with a local `self` binding, then checked
by the contextless `CheckPurity(node.predicate)` helper. That helper rejects
every procedure call and method call because it has no resolved callee or method
metadata.

Expected behavior:

Predicate purity permits pure procedure calls, pure const method calls, and pure
compile-time calls when the corresponding purity requirements hold.

Impact:

Valid refinement aliases that factor their predicate through a pure helper
procedure or pure const method can be rejected as `E-TYP-1954` before static
proof or dynamic fallback has a chance to run.

HelloUltraviolet fixture gap:

`HelloUltraviolet\Source\Reference\Types\Refinements.uv:4` covers inline
`self` comparisons. Rejected fixtures cover call predicates only as rejected
cases; there is no accepted pure-call or pure-method refinement predicate
fixture.

### UV-AUDIT-0071: Loop invariant purity checking lacks the loop lexical type environment

Severity: Medium

Status: Inspection-backed.

Spec anchors:

- `Docs\SPECIFICATION.md:14752`
- `Docs\SPECIFICATION.md:15038`
- `Docs\SPECIFICATION.md:15046`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\loop_infinite.cpp:102`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\loop_infinite.cpp:109`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\loop_iter.cpp:201`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\loop_iter.cpp:208`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\loop_conditional.cpp:105`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\loop_conditional.cpp:112`
- `Bootstrap\Ultraviolet\src\04_analysis\contracts\contract_check.cpp:1879`
- `Bootstrap\Ultraviolet\src\04_analysis\contracts\contract_check.cpp:1885`
- `Bootstrap\Ultraviolet\src\04_analysis\contracts\contract_check.cpp:1916`
- `Bootstrap\Ultraviolet\src\04_analysis\contracts\contract_check.cpp:2160`

Observed behavior:

Each loop-expression type checker builds a `ContractContext` containing only
`scope_ctx`, calls `CheckLoopInvariant`, and only afterward types the invariant
predicate with the loop's lexical `TypeEnv`. The contract checker can recognize
pure const method calls only after inferring the receiver type from the contract
context; a missing receiver type is treated as impurity.

Expected behavior:

Loop invariant verification follows the same verification-mode rules as
contracts, and predicate purity permits pure const method calls when the
receiver and method can be resolved under the active lexical environment.

Impact:

A valid loop invariant that calls a pure const method on an in-scope receiver
can be rejected as an impure predicate.

HelloUltraviolet fixture gap:

`HelloUltraviolet\Source\Reference\Procedures\Contracts.uv:8` covers a pure
method call in an ordinary procedure contract. Loop invariant fixtures use
direct scalar comparisons, such as
`HelloUltraviolet\Source\Reference\Expressions\Control.uv:307`, and do not cover
pure method predicates in loop invariants.

### UV-AUDIT-0072: Root local assignment skips required drop-on-assign

Severity: High

Status: Inspection-backed.

Spec anchors:

- `Docs\SPECIFICATION.md:19616`
- `Docs\SPECIFICATION.md:27137`
- `Docs\SPECIFICATION.md:29882`
- `Docs\SPECIFICATION.md:30196`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\05_codegen\lower\stmt\assign_stmt.cpp:27`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\expr_place.cpp:743`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\expr_place.cpp:804`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\memory\store_var.cpp:33`
- `Bootstrap\Ultraviolet\src\05_codegen\cleanup\cleanup.cpp:1890`

Observed behavior:

Root identifier writes emit `IRStoreVar` directly, and the LLVM emitter stores
the new value without invoking `DropOnAssign`. Subplace assignment has explicit
drop-on-assign paths, so root and subvalue assignment disagree.

Expected behavior:

Lowering `StoreVarIR(x, v)` binds the destination slot, lowers
`DropOnAssign(x, slot)`, then stores the new value. `StoreVarNoDropIR` is the
separate no-drop form.

Impact:

Old responsible immovable values can bypass drop glue and child cleanup on root
reassignment.

HelloUltraviolet fixture gap:

`HelloUltraviolet\Source\Reference\Statements\Assignment.uv:47` reassigns an
immovable payload, but the reference only checks the final integer value. It
does not observe old-value cleanup.

### UV-AUDIT-0073: Deferred block lowering mutates live outer binding status during registration

Severity: High

Status: Inspection-backed.

Spec anchors:

- `Docs\SPECIFICATION.md:19767`
- `Docs\SPECIFICATION.md:19771`
- `Docs\SPECIFICATION.md:27190`
- `Docs\SPECIFICATION.md:28921`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\05_codegen\lower\stmt\defer_stmt.cpp:41`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\block_expr.cpp:137`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\block_expr.cpp:197`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\expr_context.cpp:416`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\expr_context.cpp:636`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\move_expr.cpp:72`

Observed behavior:

`LowerDeferStmt` lowers the deferred block immediately through the live
`LowerCtx`, then registers the resulting IR. Moves or partial moves inside the
deferred block update the same binding-status map that later statements in the
outer block use.

Expected behavior:

Defer registration preserves the current binding-state and provenance
environment and appends a deferred cleanup block for later execution.

Impact:

Code after a defer can be lowered as though deferred moves already happened,
causing false moved-value failures, incorrect partial-move cleanup, or missed
later drops.

HelloUltraviolet fixture gap:

`HelloUltraviolet\Source\Reference\Statements\Defer.uv:3` through line `71`
checks numeric cleanup order only. It does not cover a deferred move or partial
move of an outer responsible binding followed by later outer use.

### UV-AUDIT-0074: Poison propagation walks the initialization graph in the wrong direction

Severity: High

Status: Inspection-backed.

Spec anchors:

- `Docs\SPECIFICATION.md:8822`
- `Docs\SPECIFICATION.md:8827`
- `Docs\SPECIFICATION.md:28560`
- `Docs\SPECIFICATION.md:30397`
- `Docs\SPECIFICATION.md:30416`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\memory\init_planner.cpp:1364`
- `Bootstrap\Ultraviolet\src\04_analysis\memory\init_planner.cpp:2042`
- `Bootstrap\Ultraviolet\src\04_analysis\memory\init_planner.cpp:2120`
- `Bootstrap\Ultraviolet\src\05_codegen\checks\poison_instrument.cpp:111`
- `Bootstrap\Ultraviolet\src\05_codegen\checks\checks.cpp:676`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\llvm_ir_panic.cpp:666`

Observed behavior:

The initialization planner stores eager edges as dependency-to-dependent for
topological order. Poison instrumentation treats each flattened pair as
dependent-to-dependency and reverses it again before DFS. For a module `A` that
depends on module `B`, starting poison propagation from `B` does not reach `A`.

Expected behavior:

`PoisonSet(m)` includes `m` and every module that can reach `m` through eager
value dependencies. A dependency initialization panic must poison its dependent
modules.

Impact:

Dependent modules can remain unpoisoned after a dependency's static
initialization panic, allowing later access without the required init-panic
path.

HelloUltraviolet fixture gap:

Existing checks assert poison instrumentation and module-containment strings,
such as `HelloUltraviolet\Source\Audit\AcceptedProjectExecution.uv:843` and
`HelloUltraviolet\Source\Audit\AcceptedProjectExecution.uv:861`. They do not
prove dependent-module poisoning after a dependency init panic.

### UV-AUDIT-0075: Import bindings are excluded from using/import conflict classification

Severity: Medium

Status: Inspection-backed.

Spec anchors:

- `Docs\SPECIFICATION.md:5139`
- `Docs\SPECIFICATION.md:5143`
- `Docs\SPECIFICATION.md:5160`
- `Docs\SPECIFICATION.md:5932`
- `Docs\SPECIFICATION.md:5935`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\resolve\collect_toplevel.cpp:158`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\collect_toplevel.cpp:193`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\collect_toplevel.cpp:201`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\collect_toplevel.cpp:219`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\collect_toplevel.cpp:783`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\collect_toplevel.cpp:951`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\collect_toplevel.cpp:953`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\collect_toplevel.cpp:1061`

Observed behavior:

`ItemBindings` records import aliases with `EntitySource::Import`, but
`IsUsingImportSource` returns true only for `EntitySource::Using`.
`UsingImportConflict` depends on that helper for both duplicate bindings and
prior-name conflicts, so an import alias collision can be classified as
`Collect-Dup` / `Names-Step-Dup` instead of
`Import-Using-Name-Conflict`.

Expected behavior:

`UsingImportConflict(B, N)` is true when the colliding binding or existing name
has source `Using` or `Import`. Import alias conflicts therefore produce
`E-MOD-1203`, not the generic module-scope duplicate diagnostic.

Impact:

The compiler can emit the wrong first-failure obligation and diagnostic code
for `import as` collisions, weakening module-conformance diagnostics and
fixture coverage for import alias name conflicts.

HelloUltraviolet fixture gap:

`HelloUltraviolet\Fixtures\RejectedSource\Names\DuplicateModuleDeclaration\Expected.uv:3`
covers the generic `E-MOD-1302` path. A search found no rejected fixture
expecting `E-MOD-1203` / `Import-Using-Name-Conflict` for an import alias
collision.

### UV-AUDIT-0076: Static loop invariant maintenance ignores nested control-expression mutations

Severity: High

Status: Inspection-backed.

Spec anchors:

- `Docs\SPECIFICATION.md:15038`
- `Docs\SPECIFICATION.md:15050`
- `Docs\SPECIFICATION.md:15301`
- `Docs\SPECIFICATION.md:15393`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\loop_infinite.cpp:214`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\loop_infinite.cpp:228`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\loop_infinite.cpp:306`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\loop_conditional.cpp:342`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\loop_conditional.cpp:356`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\loop_conditional.cpp:392`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\loop_conditional.cpp:521`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\loop_iter.cpp:315`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\loop_iter.cpp:329`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\loop_iter.cpp:540`
- `Bootstrap\Ultraviolet\include\02_source\ast\nodes\ast_stmts.h:126`
- `Bootstrap\Ultraviolet\include\02_source\ast\nodes\ast_exprs.h:279`
- `Bootstrap\Ultraviolet\include\02_source\ast\nodes\ast_exprs.h:329`

Observed behavior:

The static invariant-maintenance scanners inspect direct assignment
statements, compound assignment statements, and a limited set of statement
wrappers. They do not inspect `ExprStmt` contents, so mutations inside `if`
expressions, block expressions, or other expression-level control bodies are
not treated as invariant-variable mutations.

Expected behavior:

Loop invariant maintenance is checked at every back-edge and `continue` path.
If static verification is selected and the invariant cannot be proved after an
iteration path, the compiler emits `E-SEM-2831`; if dynamic verification is
selected, the required loop-invariant checks are inserted at the enforcement
points.

Impact:

A loop can statically accept an invariant even when a nested branch mutates an
invariant variable and reaches the next iteration with the invariant false.
That bypasses the required compile-time maintenance diagnostic and can also
make downstream proofs rely on an invalid loop-exit fact.

HelloUltraviolet fixture gap:

`HelloUltraviolet\Source\Reference\Expressions\Control.uv:307` covers a direct
assignment loop invariant. Catalog entries mention
`rule.15.Insert-LoopInv-Maintenance-Check`, but there is no rejected fixture for
a nested `if` or block expression mutating an invariant variable before a loop
back-edge.

### UV-AUDIT-0077: Separate `#test` attributes can each carry a `name`

Severity: Medium

Status: Inspection-backed.

Spec anchors:

- `Docs\SPECIFICATION.md:7245`
- `Docs\SPECIFICATION.md:7248`
- `Docs\SPECIFICATION.md:7261`
- `Docs\SPECIFICATION.md:7350`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\02_source\attributes\attribute_registry.cpp:269`
- `Bootstrap\Ultraviolet\src\02_source\attributes\attribute_registry.cpp:276`
- `Bootstrap\Ultraviolet\src\02_source\attributes\attribute_registry.cpp:283`
- `Bootstrap\Ultraviolet\src\02_source\attributes\attribute_registry.cpp:718`
- `Bootstrap\Ultraviolet\src\06_driver\test_discovery.cpp:72`
- `Bootstrap\Ultraviolet\src\06_driver\test_discovery.cpp:80`

Observed behavior:

`ValidateTestAttributeArgs` tracks duplicate `name` arguments only within one
attribute item. A procedure with two separate `#test(name: "...")` attributes
can pass validation, and source-native test discovery overwrites
`display_name` with the later value.

Expected behavior:

For a source-native test procedure, the `name` argument is unique. Multiple
`name` arguments across all `#test` attributes on the same procedure must
produce `E-TST-0102`.

Impact:

Invalid test metadata is accepted, and test selection/display behavior becomes
source-order dependent.

HelloUltraviolet fixture gap:

`HelloUltraviolet\Fixtures\RejectedSource\Attributes\TestDuplicateName\Source\Main.uv:3`
covers duplicate `name` arguments inside one attribute. No fixture covers two
separate `#test` attributes on the same procedure.

### UV-AUDIT-0078: `#test` procedures still lower into production artifacts

Severity: High

Status: Inspection-backed.

Spec anchors:

- `Docs\SPECIFICATION.md:7293`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\06_driver\pipeline.cpp:729`
- `Bootstrap\Ultraviolet\src\06_driver\pipeline.cpp:743`
- `Bootstrap\Ultraviolet\src\06_driver\pipeline.cpp:1743`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\lower_module.cpp:1758`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\lower_module.cpp:1774`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\lower_module.cpp:2010`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\lower_module.cpp:2048`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\lower_proc.cpp:1460`

Observed behavior:

The normal codegen path lowers project modules through `LowerCodegenModule`.
`LowerModule` registers and lowers ordinary `ProcedureDecl` items without
filtering out declarations that carry `#test`. `LowerProc` changes some
contract handling for harness builds, but the production path still sees the
test procedure as an ordinary procedure.

Expected behavior:

`#test` procedures do not lower into production program artifacts.

Impact:

Test-only procedures can be emitted into normal IR/object output, expanding the
artifact surface and violating the source-native test erasure rule.

HelloUltraviolet fixture gap:

Harness/discovery fixtures cover test execution and metadata, including
`HelloUltraviolet\Fixtures\ArtifactProjects\AttributeTestHarness\Source\Tests\Harness.uv:3`
and
`HelloUltraviolet\Fixtures\ArtifactProjects\AttributeMetadataDiscovery\Source\Tests\Metadata.uv:3`.
No fixture asserts that normal production IR/object output excludes `#test`
procedures.

### UV-AUDIT-0079: Packaged `uvc` lacks the source-native coverage obligation ledger

Severity: Medium

Status: Inspection-backed.

Spec anchors:

- `Docs\SPECIFICATION.md:7263`
- `Docs\SPECIFICATION.md:7264`
- `Docs\SPECIFICATION.md:7355`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\02_source\attributes\attribute_registry.cpp:167`
- `Bootstrap\Ultraviolet\src\02_source\attributes\attribute_registry.cpp:178`
- `Bootstrap\Ultraviolet\src\02_source\attributes\attribute_registry.cpp:184`
- `Bootstrap\Ultraviolet\src\02_source\attributes\attribute_registry.cpp:186`
- `Bootstrap\Ultraviolet\src\CMakeLists.txt:462`
- `Tools\PackageRelease.py:20`
- `Tools\PackageRelease.py:135`
- `Tools\PackageRelease.py:136`
- `Tools\PackageRelease.py:349`
- `Tools\PackageRelease.py:357`

Observed behavior:

`covers(...)` validation loads known obligations from the build-time absolute
`Docs\Internal\UltravioletObligations.csv`, from support-root ledger paths, or
from an ancestor of the current working directory. Release packaging includes
`uvc` plus runtime/tooling files and only `LICENSE.md`,
`THIRD_PARTY_NOTICES.md`, and `README.md` as docs; it does not package
`UltravioletObligations.csv`.

Expected behavior:

Valid `covers(...)` references are accepted in packaged compiler installs as
well as in source-tree builds. Unknown references produce `E-TST-0107`;
missing compiler packaging data must not make valid references unknown.

Impact:

Source-native coverage validation is environment-dependent. A packaged `uvc`
outside the source checkout can reject valid coverage references because its
ledger lookup returns an empty set.

HelloUltraviolet fixture gap:

`TestUnknownCoverageReference` covers an actually invalid coverage row. No
fixture exercises a valid `covers(...)` reference under packaged-support or
missing-ledger conditions.

### UV-AUDIT-0080: Local `using source as alias` emits generic name diagnostics

Severity: Medium

Status: Inspection-backed.

Spec anchors:

- `Docs\SPECIFICATION.md:4848`
- `Docs\SPECIFICATION.md:4853`
- `Docs\SPECIFICATION.md:4858`
- `Docs\SPECIFICATION.md:4863`
- `Docs\SPECIFICATION.md:5938`
- `Docs\SPECIFICATION.md:5939`
- `Docs\SPECIFICATION.md:5940`
- `Docs\SPECIFICATION.md:19527`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\resolve\resolve_expr.cpp:2144`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\resolve_expr.cpp:2149`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\resolve_expr.cpp:2151`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\resolve_expr.cpp:2157`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\resolve_expr.cpp:2159`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\scopes_intro.cpp:90`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\typecheck_diag_map.inc:695`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\typecheck_diag_map.inc:697`

Observed behavior:

Local using resolves the source with `ResolveValueName`, so unresolved sources
surface as generic name-resolution diagnostics. Alias conflicts are delegated
to `Intro`, so duplicate, outer-scope, and reserved-name failures likewise use
generic intro diagnostics.

Expected behavior:

Local `using source as alias` evaluates the `UsingAlias` judgment. It should
emit `Using-Alias-Unresolved`, `Using-Alias-Dup`, or `Using-Alias-Reserved`,
mapped to `E-MOD-1308`, `E-MOD-1309`, and `E-MOD-1310`.

Impact:

Local alias failures report the wrong diagnostic family and can select the
wrong first failure when the alias name is both duplicate and reserved.

HelloUltraviolet fixture gap:

`HelloUltraviolet\Fixtures\RejectedSource\Statements\UsingLocalUnresolved\Expected.uv:3`
and
`HelloUltraviolet\Fixtures\RejectedSource\Statements\LocalUsingStatementsDiagnostic\Expected.uv:3`
currently expect `E-MOD-1301`. No fixture covers local using duplicate or
reserved-alias diagnostics.

### UV-AUDIT-0081: Public-API wildcard using warning is emitted before using path resolution

Severity: Medium

Status: Inspection-backed.

Spec anchors:

- `Docs\SPECIFICATION.md:7762`
- `Docs\SPECIFICATION.md:7763`
- `Docs\SPECIFICATION.md:7767`
- `Docs\SPECIFICATION.md:7768`
- `Docs\SPECIFICATION.md:7803`
- `Docs\SPECIFICATION.md:8287`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\resolve\visibility.cpp:721`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\visibility.cpp:729`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\visibility.cpp:737`
- `Bootstrap\Ultraviolet\src\06_driver\compiler_main.cpp:4055`
- `Bootstrap\Ultraviolet\src\06_driver\tooling\analysis.cpp:204`

Observed behavior:

`CheckModuleVisibility` emits `W-MOD-1201` syntactically for a wildcard using in
a public-API module. The check does not first resolve the using path, establish
`ImportOk`, or filter the imported item set.

Expected behavior:

`Using-Wildcard-Warn` applies only after `ResolveImportPath(mp_raw)`,
`ImportOk(m, mp)`, public-API classification, item filtering, and public
visibility checks all hold.

Impact:

Invalid wildcard using declarations or declarations missing import coverage can
receive a warning that is not backed by a valid using binding, polluting
diagnostic ordering.

HelloUltraviolet fixture gap:

Catalog markers mention `Using-Wildcard-Warn`, but no source fixture covers
`using ...::*`, and none covers an invalid wildcard path in a public-API module.

### UV-AUDIT-0082: Visibility lookup treats wildcard using as binding every name

Severity: Medium

Status: Inspection-backed.

Spec anchors:

- `Docs\SPECIFICATION.md:4997`
- `Docs\SPECIFICATION.md:5001`
- `Docs\SPECIFICATION.md:5046`
- `Docs\SPECIFICATION.md:7748`
- `Docs\SPECIFICATION.md:7758`
- `Docs\SPECIFICATION.md:7763`
- `Docs\SPECIFICATION.md:7768`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\resolve\visibility.cpp:138`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\visibility.cpp:151`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\visibility.cpp:170`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\visibility.cpp:175`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\visibility.cpp:205`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\visibility.cpp:225`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\visibility.cpp:681`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\visibility.cpp:704`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\resolve_using.cpp:168`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\resolve_using.cpp:204`

Observed behavior:

`UsingClauseBindsName` returns true for wildcard using regardless of the queried
name. `FindDeclVisibilityByName` can therefore return a wildcard using
declaration's visibility for a name that the wildcard does not actually import,
or before a later real declaration is inspected.

Expected behavior:

Wildcard using visibility is derived from resolved `ItemNames(mp)` in the
target `NameMap`, and `Vis(DeclOf(mp, name))` refers to the actual selected
binding.

Impact:

Using declaration accessibility and public re-export validation can be falsely
accepted or rejected when a target module mixes wildcard using declarations with
ordinary declarations.

HelloUltraviolet fixture gap:

No fixture covers wildcard using plus same-module declaration ordering, or
public re-export validation through a target module containing wildcard using.

### UV-AUDIT-0083: Attribute-name leaves accept arbitrary keywords

Severity: Medium

Status: Inspection-backed.

Spec anchors:

- `Docs\SPECIFICATION.md:6677`
- `Docs\SPECIFICATION.md:6686`
- `Docs\SPECIFICATION.md:6695`
- `Docs\SPECIFICATION.md:6915`
- `Docs\SPECIFICATION.md:6925`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\02_source\parser\item\attribute_list.cpp:193`
- `Bootstrap\Ultraviolet\src\02_source\parser\item\attribute_list.cpp:196`
- `Bootstrap\Ultraviolet\src\02_source\parser\item\attribute_list.cpp:486`
- `Bootstrap\Ultraviolet\src\02_source\parser\item\attribute_list.cpp:524`
- `Bootstrap\Ultraviolet\src\02_source\parser\item\attribute_list.cpp:560`

Observed behavior:

`ParseAttrLeafSegment` accepts any `Keyword` token as an attribute leaf. That
admits forms such as `#return` or `#vendor::return` into the attribute AST
before validation.

Expected behavior:

Only identifiers plus the reserved leaf keywords `dynamic` and `static` are
valid attribute-name leaves. Other keywords are attribute syntax failures.

Impact:

Parser diagnostics and AST construction diverge from the attribute grammar.
Unknown-attribute validation can mask malformed syntax, and future vendor
attributes could legitimize grammar-impossible source forms.

HelloUltraviolet fixture gap:

Existing attribute fixtures cover unknown attributes and reserved vendor
namespaces, but no rejected fixture asserts malformed keyword leaves such as
`#return` or scoped keyword leaves.

### UV-AUDIT-0084: Member parsers do not concatenate newline-separated attribute lists

Severity: Medium

Status: Inspection-backed.

Spec anchors:

- `Docs\SPECIFICATION.md:2164`
- `Docs\SPECIFICATION.md:6677`
- `Docs\SPECIFICATION.md:6687`
- `Docs\SPECIFICATION.md:6885`
- `Docs\SPECIFICATION.md:9719`
- `Docs\SPECIFICATION.md:11023`
- `Docs\SPECIFICATION.md:12661`
- `Docs\SPECIFICATION.md:26054`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\02_source\parser\item\parse_item.cpp:109`
- `Bootstrap\Ultraviolet\src\02_source\parser\item\parse_item.cpp:123`
- `Bootstrap\Ultraviolet\src\02_source\parser\item\record_decl.cpp:317`
- `Bootstrap\Ultraviolet\src\02_source\parser\item\record_decl.cpp:345`
- `Bootstrap\Ultraviolet\src\02_source\parser\item\modal_decl.cpp:210`
- `Bootstrap\Ultraviolet\src\02_source\parser\item\modal_decl.cpp:229`
- `Bootstrap\Ultraviolet\src\02_source\parser\item\class_decl.cpp:126`
- `Bootstrap\Ultraviolet\src\02_source\parser\item\class_decl.cpp:155`
- `Bootstrap\Ultraviolet\src\02_source\parser\item\extern_block.cpp:131`
- `Bootstrap\Ultraviolet\src\02_source\parser\item\extern_block.cpp:142`
- `Bootstrap\Ultraviolet\src\02_source\parser\item\attribute_list.cpp:613`
- `Bootstrap\Ultraviolet\src\02_source\parser\item\attribute_list.cpp:628`

Observed behavior:

Top-level item parsing explicitly skips newlines and merges repeated attribute
lists before the target declaration. Record members, modal state members, class
items, abstract-state fields, and extern procedures call
`ParseAttributeListOpt` once, skip newlines, and then proceed. A second `#` on
the next line is not concatenated into the same member target's attributes.

Expected behavior:

Multiple attribute lists on the same target are equivalent to one concatenated
list in source order, including member declaration targets.

Impact:

Stacked member attributes can be dropped or parsed as an unexpected token, so
member-level metadata and validation differ from top-level declaration
behavior.

HelloUltraviolet fixture gap:

`HelloUltraviolet\Source\Reference\Attributes\GeneralAttributes.uv:40` covers
stacked attributes on top-level procedures and single member attributes. No
fixture covers newline-separated stacked attributes on record fields or methods,
modal members, class items, abstract-state fields, or extern procedures.

### UV-AUDIT-0085: Member documentation comments are never attached to member AST nodes

Severity: Medium

Status: Inspection-backed.

Spec anchors:

- `Docs\SPECIFICATION.md:2717`
- `Docs\SPECIFICATION.md:2733`
- `Docs\SPECIFICATION.md:3097`
- `Docs\SPECIFICATION.md:3102`
- `Docs\SPECIFICATION.md:9782`
- `Docs\SPECIFICATION.md:9784`
- `Docs\SPECIFICATION.md:10025`
- `Docs\SPECIFICATION.md:10639`
- `Docs\SPECIFICATION.md:10641`
- `Docs\SPECIFICATION.md:12744`
- `Docs\SPECIFICATION.md:12747`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\02_source\parser\parser.cpp:284`
- `Bootstrap\Ultraviolet\src\02_source\parser\parser_docs.cpp:33`
- `Bootstrap\Ultraviolet\src\02_source\parser\parser_docs.cpp:45`
- `Bootstrap\Ultraviolet\src\02_source\parser\parser_docs.cpp:156`
- `Bootstrap\Ultraviolet\src\02_source\parser\parser_docs.cpp:184`
- `Bootstrap\Ultraviolet\src\02_source\parser\item\record_decl.cpp:151`
- `Bootstrap\Ultraviolet\src\02_source\parser\item\record_decl.cpp:260`
- `Bootstrap\Ultraviolet\src\02_source\parser\item\record_decl.cpp:308`
- `Bootstrap\Ultraviolet\src\02_source\parser\item\enum_decl.cpp:122`
- `Bootstrap\Ultraviolet\src\02_source\parser\item\enum_decl.cpp:295`
- `Bootstrap\Ultraviolet\src\02_source\parser\item\modal_decl.cpp:119`
- `Bootstrap\Ultraviolet\src\02_source\parser\item\modal_decl.cpp:171`
- `Bootstrap\Ultraviolet\src\02_source\parser\item\modal_decl.cpp:199`
- `Bootstrap\Ultraviolet\src\02_source\parser\item\class_decl.cpp:155`
- `Bootstrap\Ultraviolet\src\02_source\parser\item\class_decl.cpp:297`
- `Bootstrap\Ultraviolet\src\02_source\parser\item\class_decl.cpp:361`

Observed behavior:

`AttachLineDocs` walks top-level `ASTItem` values. Member parsers set member
`doc_opt` fields to null. A `///` comment inside a record, modal, class, or
enum body is therefore not attached to the following member node and can escape
to a later top-level item.

Expected behavior:

Member AST forms carry `doc_opt`, and line documentation immediately preceding
a member declaration should populate that member node.

Impact:

Member API documentation cannot be represented correctly in the AST, and inner
documentation comments can corrupt unrelated top-level documentation metadata.

HelloUltraviolet fixture gap:

Doc-comment coverage validates top-level line and module association only. No
fixture checks docs on record fields/methods, enum variants, modal
states/members, class fields/methods, associated types, or abstract states.

### UV-AUDIT-0086: Overload candidate-local diagnostics can abort valid overload resolution

Severity: High

Status: Inspection-backed.

Spec anchors:

- `Docs\SPECIFICATION.md:14561`
- `Docs\SPECIFICATION.md:14570`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\call.cpp:532`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\call.cpp:538`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\call.cpp:614`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\call.cpp:619`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\type_infer.cpp:1456`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\literals.cpp:521`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\literals.cpp:525`

Observed behavior:

Overload filtering treats candidate argument-check diagnostics other than
generic incompatibility as hard errors for the whole overload set. For example,
an `f64`-suffixed literal can fail a `f32` candidate with `E-TYP-1531` and stop
resolution before a valid `f64` overload is selected.

Expected behavior:

Overload resolution eliminates candidates whose arguments are incompatible
under the call-argument compatibility rules before no-match or ambiguity
diagnostics are selected.

Impact:

Valid overload calls can be rejected depending on candidate-local diagnostics
and candidate ordering.

HelloUltraviolet fixture gap:

Existing overload fixtures cover exact primitive overloads, no-match, and
duplicate erased signatures. No fixture covers a valid overload set where one
rejected candidate emits a specific argument diagnostic while another candidate
matches.

### UV-AUDIT-0087: Non-indexable bases can be reported as array non-`usize`

Severity: Medium

Status: Inspection-backed.

Spec anchors:

- `Docs\SPECIFICATION.md:9255`
- `Docs\SPECIFICATION.md:9258`
- `Docs\SPECIFICATION.md:9387`
- `Docs\SPECIFICATION.md:9390`
- `Docs\SPECIFICATION.md:9426`
- `Docs\SPECIFICATION.md:9429`
- `Docs\SPECIFICATION.md:15761`
- `Docs\SPECIFICATION.md:15789`
- `Docs\SPECIFICATION.md:17999`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\index_access.cpp:448`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\index_access.cpp:481`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\index_access.cpp:672`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\index_access.cpp:702`

Observed behavior:

Index typing validates scalar/range index shape before confirming the base is
an array or slice. For a non-indexable base with a non-`usize` index, it can
select `Index-Array-NonUsize` even though that rule's premise requires an array
base.

Expected behavior:

When the base type is not array or slice, the diagnostic is
`Index-NonIndexable` / `E-SEM-2527`, independent of whether the index expression
would also be invalid for an indexable base.

Impact:

Combined invalid indexing expressions can report the wrong first-failure rule
and diagnostic in expression and place contexts.

HelloUltraviolet fixture gap:

`HelloUltraviolet\Fixtures\RejectedSource\Expressions\IndexNonIndexable\Source\Main.uv:3`
uses a valid `usize` index. No fixture covers a non-indexable base plus invalid
index in expression or place context.

### UV-AUDIT-0088: `#layout(C)` record transmute targets are warned as invalid

Severity: Medium

Status: Inspection-backed.

Spec anchors:

- `Docs\SPECIFICATION.md:16483`
- `Docs\SPECIFICATION.md:16487`
- `Docs\SPECIFICATION.md:16635`
- `Docs\SPECIFICATION.md:16639`
- `Docs\SPECIFICATION.md:18015`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\transmute_expr.cpp:70`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\transmute_expr.cpp:97`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\transmute_expr.cpp:375`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\transmute_expr.cpp:378`

Observed behavior:

`KnownInvalidTransmuteTarget` treats every non-primitive, non-raw-pointer,
non-array, non-permission, non-type-variable target as invalid. It does not
inspect `TypePath(p)` records for `#layout(C)` or recursively validate their
fields.

Expected behavior:

`ValidTransmuteTarget` includes `#layout(C)` records whose fields are valid
transmute targets, so conforming unsafe transmutes to those record targets must
not receive `W-SAF-0100`.

Impact:

Valid layout-C record transmute targets can receive a spurious unsafe warning,
and any downstream checks using the same classification can be over-applied.

HelloUltraviolet fixture gap:

`ValidTransmuteTarget` coverage checks invalid `bool` target warning behavior.
No fixture asserts absence of `W-SAF-0100` for a valid `#layout(C)` record
target.

### UV-AUDIT-0089: Shared compound assignment acquires read-mode keys before write-mode keys

Severity: High

Status: Inspection-backed.

Spec anchors:

- `Docs\SPECIFICATION.md:20570`
- `Docs\SPECIFICATION.md:20574`
- `Docs\SPECIFICATION.md:20587`
- `Docs\SPECIFICATION.md:20595`
- `Docs\SPECIFICATION.md:20613`
- `Docs\SPECIFICATION.md:20619`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\05_codegen\lower\stmt\compound_assign_stmt.cpp:52`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\stmt\compound_assign_stmt.cpp:104`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\expr_place.cpp:281`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\expr_place.cpp:387`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\expr_common.cpp:903`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\expr_common.cpp:917`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\expr_common.cpp:1019`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\expr_common.cpp:1077`

Observed behavior:

`LowerCompoundAssignStmt` lowers the left place first with `LowerReadPlace`,
then later writes it back with `LowerWritePlace`. For an uncovered shared place,
the read half can acquire a read key before the write half requests a write key.

Expected behavior:

The left side of compound assignment is a write context. If an expression
appears in both read and write contexts, write wins, so the shared place access
should be covered by one write-mode requirement or otherwise treated as write
for the whole operation.

Impact:

Accepted shared compound assignment can produce incompatible implicit key
acquisition order or runtime conflict, weakening key authority semantics.

HelloUltraviolet fixture gap:

`HelloUltraviolet\Source\Reference\Keys\ConflictDetection.uv:86` covers shared
compound assignment only inside explicit `%write`.
`HelloUltraviolet\Source\Reference\Statements\Assignment.uv:19` covers compound
assignment only on non-shared locals. No accepted fixture proves a single
write-mode implicit acquisition for uncovered shared compound assignment.

### UV-AUDIT-0090: Capability call graph does not scan compound assignment operands

Severity: High

Status: Inspection-backed.

Spec anchors:

- `Docs\SPECIFICATION.md:3240`
- `Docs\SPECIFICATION.md:3243`
- `Docs\SPECIFICATION.md:4101`
- `Docs\SPECIFICATION.md:19579`
- `Docs\SPECIFICATION.md:19582`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\caps\callgraph_caps.cpp:375`
- `Bootstrap\Ultraviolet\src\04_analysis\caps\callgraph_caps.cpp:436`
- `Bootstrap\Ultraviolet\src\04_analysis\caps\callgraph_caps.cpp:447`
- `Bootstrap\Ultraviolet\src\04_analysis\caps\callgraph_caps.cpp:455`
- `Bootstrap\Ultraviolet\src\06_driver\compiler_main.cpp:4271`
- `Bootstrap\Ultraviolet\src\06_driver\compiler_main.cpp:4276`

Observed behavior:

The capability call-graph statement walker visits `let`, `var`, expression,
assignment, return, defer, unsafe, region, frame, and key-block statements, but
has no `CompoundAssignStmt` branch. Direct calls inside a compound-assignment
place or right operand are not collected as edges or unresolved direct calls.

Expected behavior:

`StmtExprs(CompoundAssignStmt(p, _, e)) = [p, e]`; every direct call reachable
through those operands must satisfy `CapReq(d_tgt) subseteq EffectiveCapReq(d_src)`.

Impact:

A capability-requiring direct call can bypass the active no-ambient-authority
call-chain validation when it appears inside compound assignment operands.

HelloUltraviolet fixture gap:

`HelloUltraviolet\Fixtures\RejectedSource\Permissions\DirectCallCapabilityInclusion\Source\Main.uv:11`
covers a return-expression direct call only. No rejected fixture places the
same capability-requiring direct call in a compound assignment operand.

### UV-AUDIT-0091: Empty string and bytes literals lose required static backing storage

Severity: Medium

Status: Inspection-backed.

Spec anchors:

- `Docs\SPECIFICATION.md:11450`
- `Docs\SPECIFICATION.md:27395`
- `Docs\SPECIFICATION.md:27396`
- `Docs\SPECIFICATION.md:27399`
- `Docs\SPECIFICATION.md:30359`
- `Docs\SPECIFICATION.md:30361`
- `Docs\SPECIFICATION.md:30367`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\05_codegen\globals\literal_emit.cpp:76`
- `Bootstrap\Ultraviolet\src\05_codegen\globals\literal_emit.cpp:538`
- `Bootstrap\Ultraviolet\src\05_codegen\globals\literal_emit.cpp:859`
- `Bootstrap\Ultraviolet\src\05_codegen\globals\literal_emit.cpp:872`
- `Bootstrap\Ultraviolet\src\05_codegen\intrinsics\intrinsics_interface.cpp:382`
- `Bootstrap\Ultraviolet\src\05_codegen\intrinsics\intrinsics_interface.cpp:383`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\value\evaluate.cpp:750`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\value\evaluate.cpp:753`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\value\evaluate.cpp:789`

Observed behavior:

`LiteralKindOfImmediate` returns no literal kind for any immediate with an empty
byte vector. `LiteralRefs` and `RefSyms` therefore do not collect a
`LiteralDataSym` for empty string or bytes literals, so `UniqueLiterals` does
not emit an empty literal global through the normal expansion path. In LLVM
constant-view construction, `data_ptr` starts as null and is replaced with a
global address only when `!val.bytes.empty()`, leaving empty literal views with
a null data pointer.

Expected behavior:

String literal bytes must be allocated in static read-only storage and the
resulting `string@View` must reference that storage even when the literal length
is zero. The literal-expansion rules also derive literal data globals from
`LiteralRefs(IR)` without excluding empty byte sequences, and bytes literals use
the same `EmitLiteralData(kind, bytes)` declaration path.

Impact:

Empty literal views can be emitted with null backing pointers and without the
required static literal object. Code that depends on stable literal identity,
linkage, or pointer provenance can observe behavior outside the specified
literal-storage model.

HelloUltraviolet fixture gap:

The reference and artifact fixtures exercise string and bytes modal behavior,
but no fixture verifies empty literal backing storage, emitted zero-length
literal globals, or non-null view data pointers for empty literals.

### UV-AUDIT-0092: No-payload enum LLVM type lowers to a scalar instead of the tagged struct

Severity: High

Status: Inspection-backed.

Spec anchors:

- `Docs\SPECIFICATION.md:29551`
- `Docs\SPECIFICATION.md:29552`
- `Docs\SPECIFICATION.md:29555`
- `Docs\SPECIFICATION.md:29646`
- `Docs\SPECIFICATION.md:29647`
- `Docs\SPECIFICATION.md:29649`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\layout\layout_aggregates.cpp:328`
- `Bootstrap\Ultraviolet\src\04_analysis\layout\layout_aggregates.cpp:337`
- `Bootstrap\Ultraviolet\src\04_analysis\layout\layout_aggregates.cpp:340`
- `Bootstrap\Ultraviolet\src\04_analysis\layout\layout_aggregates.cpp:342`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\llvm_types.cpp:1379`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\llvm_types.cpp:1380`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\llvm_types.cpp:1381`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\llvm_types.cpp:1383`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\llvm_types.cpp:1387`

Observed behavior:

Enum layout computation always records the tagged layout size, alignment,
discriminant type, payload size, and payload alignment. LLVM type emission then
special-cases `layout->payload_size == 0` and returns only the discriminant LLVM
type. The `CreateTaggedStructType` path is used only when payload size is
non-zero.

Expected behavior:

`LLVMTy-Enum` is defined in terms of `TaggedElems(disc, payload_size,
payload_align, size)` and always concludes `LLVMStruct(elems)`. The rule does
not replace no-payload enums with the bare discriminant type.

Impact:

No-payload enums can lose the aggregate representation required by the spec,
including layout padding and explicit alignment such as `#layout(align(N))`.
That can make the emitted LLVM ABI disagree with the layout analysis result.

HelloUltraviolet fixture gap:

HelloUltraviolet records broad `LLVMTy-Enum` obligation coverage, but no fixture
checks the actual emitted LLVM type or ABI layout for no-payload enums,
especially no-payload enums with explicit alignment.

### UV-AUDIT-0093: Declaration attributes infer panic semantics by substring

Severity: Medium

Status: Inspection-backed.

Spec anchors:

- `Docs\SPECIFICATION.md:28763`
- `Docs\SPECIFICATION.md:28765`
- `Docs\SPECIFICATION.md:29281`
- `Docs\SPECIFICATION.md:29306`
- `Docs\SPECIFICATION.md:29308`
- `Docs\SPECIFICATION.md:29309`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\05_codegen\llvm\llvm_attr.cpp:388`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\llvm_attr.cpp:394`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\llvm_attr.cpp:395`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\llvm_attr.cpp:397`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\llvm_attr.cpp:421`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\llvm_attr.cpp:422`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\llvm_attr.cpp:424`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\llvm_module.cpp:453`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\llvm_module.cpp:458`

Observed behavior:

`DeclAttrs` adds `NoReturn` when the LLVM symbol contains the substring
`panic` or `abort`. `DeclAttrsOk` uses the same substring predicate to validate
the result. `LLVMModuleEmitter` applies those inferred attributes to every
emitted function declaration.

Expected behavior:

The panic symbol is the specific `PanicSym` path
`["ultraviolet", "runtime", "panic"]`. `DeclAttrsOk` requires `noreturn` and
`nounwind` only when `sym = PanicSym`; for all other symbols it requires
`nounwind`. Ordinary returning symbols whose names merely contain `panic` or
`abort` should not gain panic-only control-flow semantics.

Impact:

A valid returning function or foreign import with those substrings in its
symbol can be marked `noreturn`, giving LLVM invalid optimization assumptions
and making the conformance checker accept the same incorrect classification.

HelloUltraviolet fixture gap:

No fixture defines or imports a returning symbol whose emitted name contains
`panic` or `abort` and then checks the emitted LLVM declaration attributes.

### UV-AUDIT-0094: Record LLVM type emission records the tuple rule

Severity: Low

Status: Inspection-backed.

Spec anchors:

- `Docs\SPECIFICATION.md:29597`
- `Docs\SPECIFICATION.md:29598`
- `Docs\SPECIFICATION.md:29600`
- `Docs\SPECIFICATION.md:29602`
- `Docs\SPECIFICATION.md:29605`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\05_codegen\llvm\llvm_types.cpp:1313`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\llvm_types.cpp:1314`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\llvm_types.cpp:1316`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\llvm_types.cpp:1359`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\llvm_types.cpp:1360`

Observed behavior:

The `LookupRecordDecl` branch in `LLVMEmitter::GetLLVMType` lowers a nominal
record by computing the record layout and creating an LLVM struct from the
record elements, but records `SPEC_RULE("LLVMTy-Tuple")` before doing so.

Expected behavior:

The record branch should record `LLVMTy-Record`; `LLVMTy-Tuple` is the distinct
rule for `TypeTuple([...])`.

Impact:

Conformance traces can over-report tuple coverage and under-report record LLVM
mapping coverage, hiding backend rule drift behind an apparently satisfied
obligation.

HelloUltraviolet fixture gap:

HelloUltraviolet records both `rule.24.LLVMTy-Record` and
`rule.24.LLVMTy-Tuple` as exercised through artifact emission, but no fixture
validates that the backend trace label for record lowering is the record rule
rather than the tuple rule.

### UV-AUDIT-0095: Drop release is recorded but never emitted

Severity: High

Status: Inspection-backed.

Spec anchors:

- `Docs\SPECIFICATION.md:13908`
- `Docs\SPECIFICATION.md:28832`
- `Docs\SPECIFICATION.md:28833`
- `Docs\SPECIFICATION.md:28877`
- `Docs\SPECIFICATION.md:28878`
- `Docs\SPECIFICATION.md:28880`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\05_codegen\cleanup\cleanup.cpp:1160`
- `Bootstrap\Ultraviolet\src\05_codegen\cleanup\cleanup.cpp:1163`
- `Bootstrap\Ultraviolet\src\05_codegen\cleanup\cleanup.cpp:1165`
- `Bootstrap\Ultraviolet\src\05_codegen\cleanup\cleanup.cpp:1220`
- `Bootstrap\Ultraviolet\src\05_codegen\cleanup\cleanup.cpp:1221`
- `Bootstrap\Ultraviolet\src\05_codegen\cleanup\cleanup.cpp:1324`
- `Bootstrap\Ultraviolet\src\05_codegen\cleanup\cleanup.cpp:1327`
- `Bootstrap\Ultraviolet\src\05_codegen\cleanup\cleanup.cpp:1496`
- `Bootstrap\Ultraviolet\src\05_codegen\cleanup\cleanup.cpp:1498`

Observed behavior:

`EmitReleaseValue` records the release rules and immediately returns empty IR.
`EmitDropImpl` only calls that helper on the early `!TypeNeedsDrop` path. The
drop paths that actually invoke child drops, record drop methods, enum payload
drops, or modal-state drops return the drop sequence directly, with no
post-drop release action.

Expected behavior:

`DropValueOut-Ok` requires `ReleaseValue(T, v, sigma_2)` after `DropCall`
succeeds and `DropList(DropChildren(T, v, F))` finishes successfully. The
release relation is the operation that ends the value domain, and the prose
requirement says the final owning cleanup invokes drop when needed, cleans
children, and releases the provenance/allocation domain.

Impact:

Any runtime state represented by value domains is never ended by the cleanup IR.
Types with destructors or owned children also skip the required final release
step entirely because their branches bypass `EmitReleaseValue`.

HelloUltraviolet fixture gap:

No fixture validates a cleanup trace or emitted IR shape that contains the
required final release action after a type with a drop method, owned children,
or managed literal storage is destroyed.

### UV-AUDIT-0096: Iterator loop LLVM binding ignores compound patterns

Severity: High

Status: Inspection-backed.

Spec anchors:

- `Docs\SPECIFICATION.md:16965`
- `Docs\SPECIFICATION.md:16966`
- `Docs\SPECIFICATION.md:16968`
- `Docs\SPECIFICATION.md:17117`
- `Docs\SPECIFICATION.md:17118`
- `Docs\SPECIFICATION.md:27286`
- `Docs\SPECIFICATION.md:27287`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\02_source\parser\expr\loop_iter.cpp:337`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\loop_iter.cpp:482`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\loop_iter.cpp:495`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\loop_iter.cpp:234`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\loop_iter.cpp:254`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\control\loop.cpp:389`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\control\loop.cpp:395`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\control\loop.cpp:407`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\control\loop.cpp:977`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\control\loop.cpp:983`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\control\loop.cpp:995`

Observed behavior:

Iterator loops preserve the full parsed pattern, typing checks that pattern
against the iterable element type, and lowering registers all pattern bindings.
`LowerIRPattern` can produce tuple, record, enum, modal, and range pattern IR.
At LLVM emission, both the async-iterator and ordinary iterator binding helpers
only handle identifier, typed, and wildcard IR patterns. Every compound pattern
node falls through without binding any nested names before the loop body emits.

Expected behavior:

`T-Loop-Iter` accepts a general `pat` and introduces all bindings from that
pattern. Runtime and IR iterator semantics execute `EvalBlockBindSigma` /
`ExecBlockBindIRSigma(pat, v, body, ...)` for each yielded element, so compound
patterns must destructure the element and bind their nested names for each
iteration.

Impact:

Valid iterator loops that destructure tuple, record, enum, modal, or range
elements can compile with registered names whose LLVM storage is never populated
from the current element, yielding stale, default, or invalid values inside the
body.

HelloUltraviolet fixture gap:

Current iterator-loop references use simple identifier or typed identifier
patterns, such as `loop value: usize in ...`. No source-native or artifact
fixture exercises a tuple, record, enum, or modal pattern in an ordinary or async
iterator loop body.

### UV-AUDIT-0097: `yield release` emits key release before the required snapshot step

Severity: Medium

Status: Inspection-backed.

Spec anchors:

- `Docs\SPECIFICATION.md:23440`
- `Docs\SPECIFICATION.md:23444`
- `Docs\SPECIFICATION.md:23447`
- `Docs\SPECIFICATION.md:24677`
- `Docs\SPECIFICATION.md:24681`
- `Docs\SPECIFICATION.md:24684`
- `Docs\SPECIFICATION.md:24688`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\yield.cpp:348`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\yield.cpp:353`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\yield.cpp:354`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\yield.cpp:355`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\yield.cpp:356`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\yield_from.cpp:536`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\yield_from.cpp:541`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\yield_from.cpp:542`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\yield_from.cpp:543`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\yield_from.cpp:544`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\llvm_emit_helpers.cpp:1219`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\llvm_emit_helpers.cpp:1286`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\key.c:634`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\key.c:650`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\key.c:657`

Observed behavior:

The `yield release` and `yield from release` emitters call `EmitKeyReleaseAll`
first, then store the returned handle in the async frame with
`StoreAsyncFrameKeySnapshot`. They also record `ReleaseHeldKeysIR` before
`SnapshotHeldKeysIR`. The runtime helper captures the released key set while it
unlinks keys, so the implementation relies on one combined release helper rather
than emitting the specified snapshot operation before the release operation.

Expected behavior:

`Lower-Yield-Release`, `Lower-Yield-Release-Keys`, and
`Lower-YieldFrom-Release-Keys` require the lowering order
`SnapshotHeldKeysIR(CurrentAsyncFrame)`, then `ReleaseHeldKeysIR`, then ordinary
yield lowering. The frame snapshot is the state later consumed by
`ReacquireHeldKeysIR` at resumption.

Impact:

The emitted IR and conformance trace do not match the required async key
suspension protocol. Tests can see all three component names and still miss that
the snapshot step is after, and hidden inside, the release call.

HelloUltraviolet fixture gap:

`HelloUltraviolet\Source\Reference\Async\AsyncKeyIntegration.uv` exercises
`yield release` and `yield from release` while keys are held.
`HelloUltraviolet\Source\Audit\AsyncArtifactExecution.uv` checks for the
component names but does not assert their emitted order.

### UV-AUDIT-0098: Same-module emitted derive targets are not visible to later derive lookup

Severity: Medium

Spec anchors:

- `Docs\SPECIFICATION.md:24915`
- `Docs\SPECIFICATION.md:24919`
- `Docs\SPECIFICATION.md:25660`
- `Docs\SPECIFICATION.md:25672`

Implementation anchors:

- `Bootstrap\Ultraviolet\include\03_comptime\comptime_internal.h:201`
- `Bootstrap\Ultraviolet\src\03_comptime\rewrite.cpp:799`
- `Bootstrap\Ultraviolet\src\03_comptime\rewrite.cpp:800`
- `Bootstrap\Ultraviolet\src\03_comptime\rewrite.cpp:852`
- `Bootstrap\Ultraviolet\src\03_comptime\rewrite.cpp:854`
- `Bootstrap\Ultraviolet\src\03_comptime\reflect.cpp:83`
- `Bootstrap\Ultraviolet\src\03_comptime\derive.cpp:310`
- `Bootstrap\Ultraviolet\src\03_comptime\derive.cpp:355`
- `Bootstrap\Ultraviolet\src\03_comptime\derive.cpp:357`
- `Bootstrap\Ultraviolet\src\03_comptime\derive.cpp:569`

Observed behavior:

`ExpandModuleItems` builds `visible_current_items`, stores it through
`env.current_module_items`, and appends emitted items to that list as Phase 2
sites run. Reflection lookup consults that expanded current-module item view.
Derive lookup does not: `FindVisibleDeriveTargetDeclInModule` resolves the
original available `ASTModule`, applies `VisibleItemLimit` against
`module.items.size()`, and scans `module->items[i]`.

Expected behavior:

An item emitted at a Phase 2 site must become visible immediately to later Phase
2 execution in the same module. `DeriveTargetDecl` specifically must be visible
to later derive lookup in the same Phase 2 module order, and `RunDeriveSet` must
resolve `VisibleDeriveTarget` from that updated view.

Impact:

A legal same-module sequence where compile-time execution emits a `derive
target` and a later declaration uses `#derive` can be rejected as an unknown
derive target. This is distinct from `UV-AUDIT-0065`, which covers
cross-module emitted derive targets.

HelloUltraviolet fixture gap:

Existing derive fixtures cover source-present derive targets and unknown target
diagnostics, but not a derive target emitted earlier in the same module and
consumed by a later declaration.

### UV-AUDIT-0099: `#emit` and `#files` can be accepted on non-comptime statements or expressions

Severity: Medium

Spec anchors:

- `Docs\SPECIFICATION.md:7202`
- `Docs\SPECIFICATION.md:7204`
- `Docs\SPECIFICATION.md:25719`
- `Docs\SPECIFICATION.md:25726`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\02_source\attributes\attribute_registry.cpp:502`
- `Bootstrap\Ultraviolet\src\02_source\attributes\attribute_registry.cpp:503`
- `Bootstrap\Ultraviolet\src\02_source\attributes\attribute_registry.cpp:508`
- `Bootstrap\Ultraviolet\src\02_source\attributes\attribute_registry.cpp:509`
- `Bootstrap\Ultraviolet\src\03_comptime\rewrite.cpp:270`
- `Bootstrap\Ultraviolet\src\03_comptime\rewrite.cpp:277`
- `Bootstrap\Ultraviolet\src\03_comptime\rewrite.cpp:513`
- `Bootstrap\Ultraviolet\src\03_comptime\rewrite.cpp:514`
- `Bootstrap\Ultraviolet\src\03_comptime\rewrite.cpp:552`
- `Bootstrap\Ultraviolet\src\03_comptime\rewrite.cpp:553`
- `Bootstrap\Ultraviolet\src\03_comptime\rewrite.cpp:555`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\type_expr.cpp:4286`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\type_expr.cpp:4287`

Observed behavior:

The generic attribute registry marks `#emit` and `#files` as valid on statement
and expression targets. The Phase 2 validator that emits the compile-time
capability diagnostics runs only once a node is already known to be
`ComptimeStmt` or an attributed `ComptimeExpr`. Ordinary attributed expressions
therefore pass the generic expression-target validator instead of receiving the
compile-time-form diagnostic.

Expected behavior:

`#emit` and `#files` grant compile-time capabilities only to annotated
compile-time statements or compile-time expressions. Applying either attribute
to a non-comptime form must be rejected with `E-CTE-0041` or `E-CTE-0061`.

Impact:

Capability-granting compile-time attributes can survive on ordinary runtime code
without the required diagnostic, weakening the Chapter 22 capability boundary and
making rejected-source coverage overly permissive.

HelloUltraviolet fixture gap:

Fixtures cover positive `#emit` and `#files` use, plus emitted-AST shape errors,
but not `#emit` or `#files` applied to ordinary non-comptime statements or
expressions.

### UV-AUDIT-0100: Method contracts are parsed but never lowered

Severity: High

Spec anchors:

- `Docs\SPECIFICATION.md:14299`
- `Docs\SPECIFICATION.md:14309`
- `Docs\SPECIFICATION.md:14311`
- `Docs\SPECIFICATION.md:14778`
- `Docs\SPECIFICATION.md:14780`
- `Docs\SPECIFICATION.md:14835`
- `Docs\SPECIFICATION.md:14953`
- `Docs\SPECIFICATION.md:14957`
- `Docs\SPECIFICATION.md:15107`
- `Docs\SPECIFICATION.md:15110`
- `Docs\SPECIFICATION.md:15364`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\05_codegen\lower\lower_module.cpp:968`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\lower_module.cpp:971`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\lower_module.cpp:998`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\lower_module.cpp:1002`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\lower_module.cpp:1021`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\lower_module.cpp:1044`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\lower_module.cpp:1049`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\lower_module.cpp:1231`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\lower_module.cpp:1235`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\lower_module.cpp:1281`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\lower_module.cpp:1285`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\lower_module.cpp:1362`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\lower_module.cpp:1401`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\lower_module.cpp:1445`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\lower_module.cpp:1447`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\lower_module.cpp:1797`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\lower_module.cpp:1806`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\lower_module.cpp:2073`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\lower_module.cpp:2091`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\lower_module.cpp:2113`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\method_call.cpp:2484`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\method_call.cpp:2530`

Observed behavior:

Record methods, modal state methods, and class default methods preserve a
`contract_opt` in the AST, and their lowering sets `ctx.dynamic_checks` when the
method or owner is dynamic. The actual body lowering calls `LowerProcLike`,
whose signature receives only a symbol, parameters, return type, body, and module
path. It resets the active contract state, lowers the body, and emits a return
without receiving or lowering the method's precondition, postcondition, or
`@entry` captures. The local-contract registration helper is defined only for
`ProcedureDecl`, and method-call lowering emits the direct `IRCall` sequence
without invoking local precondition insertion.

Expected behavior:

Method contract clauses are ordinary contract clauses. Their precondition context
includes the receiver and parameters, and their postcondition context includes
the receiver, parameters, `@result`, and `@entry`. When dynamic verification is
selected, preconditions must be checked before method body execution and before
entry capture; postconditions must be checked at return with `@result` bound to
the returned value.

Impact:

A `#dynamic` record method, state method, or class default method can violate its
declared contract without any emitted runtime contract check. Callers also do not
receive the local dynamic precondition checks that free procedure calls receive.

HelloUltraviolet fixture gap:

HelloUltraviolet has procedure-focused contract and pre/postcondition fixtures,
but no fixture that declares a contract on a record method, state method, or class
default method and verifies the emitted dynamic checks.

### UV-AUDIT-0101: Type and loop invariant `@result` misuse reports a foreign-contract diagnostic

Severity: Medium

Status: Inspection-backed; needs reduced fixtures.

Spec anchors:

- `Docs\SPECIFICATION.md:14853`
- `Docs\SPECIFICATION.md:14854`
- `Docs\SPECIFICATION.md:14855`
- `Docs\SPECIFICATION.md:15384`
- `Docs\SPECIFICATION.md:26848`
- `Docs\SPECIFICATION.md:30492`
- `Docs\SPECIFICATION.md:30522`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\contract_clause.cpp:481`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\contract_clause.cpp:524`
- `Bootstrap\Ultraviolet\src\04_analysis\contracts\contract_check.cpp:2144`
- `Bootstrap\Ultraviolet\src\04_analysis\contracts\contract_check.cpp:2173`

Observed behavior:

Both contract-clause typing and the contract checker reject `@result` inside
type and loop invariants by assigning `E-SEM-2854`. That diagnostic is owned by
the foreign-contract supplement for "`@result` used in non-return context".

Expected behavior:

The ordinary contract grammar says `@result` outside a postcondition is rejected
statically with `E-SEM-2806`; the §15 diagnostic table owns that condition.
Type and loop invariants are ordinary §15 contract contexts, not foreign
postcondition contexts, so they should report the §15 diagnostic.

Impact:

Users and tooling receive a foreign-contract error for non-FFI source. This
breaks diagnostic ownership and can cause rejected-source fixtures or IDE
consumers to classify the failure under the wrong language subsystem.

HelloUltraviolet fixture gap:

There is no rejected-source fixture that places `@result` inside a type
invariant or loop invariant and asserts the §15 diagnostic.

### UV-AUDIT-0102: Procedure `@entry` placement errors report the foreign out-of-scope diagnostic

Severity: Medium

Status: Inspection-backed; HelloUltraviolet fixture currently encodes the
implementation behavior.

Spec anchors:

- `Docs\SPECIFICATION.md:14851`
- `Docs\SPECIFICATION.md:14853`
- `Docs\SPECIFICATION.md:14854`
- `Docs\SPECIFICATION.md:14855`
- `Docs\SPECIFICATION.md:14939`
- `Docs\SPECIFICATION.md:14941`
- `Docs\SPECIFICATION.md:15376`
- `Docs\SPECIFICATION.md:15377`
- `Docs\SPECIFICATION.md:15385`
- `Docs\SPECIFICATION.md:26846`
- `Docs\SPECIFICATION.md:30492`
- `Docs\SPECIFICATION.md:30522`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\contracts\contract_check.cpp:2068`
- `Bootstrap\Ultraviolet\src\04_analysis\contracts\contract_intrinsics.cpp:810`
- `Bootstrap\Ultraviolet\src\04_analysis\contracts\contract_intrinsics.cpp:825`
- `Bootstrap\Ultraviolet\src\04_analysis\contracts\contract_intrinsics.cpp:1110`
- `Bootstrap\Ultraviolet\src\04_analysis\contracts\contract_intrinsics.cpp:1240`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\type_expr.cpp:3731`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\type_expr.cpp:3740`
- `HelloUltraviolet\Fixtures\RejectedSource\Procedures\ContractEntryOutsidePostcondition\Expected.uv:3`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\RejectedSource\Procedures.uv:304`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\RejectedSource\Procedures.uv:310`

Observed behavior:

Ordinary procedure-contract paths reject `@entry` outside a postcondition, empty
`@entry`, and `@entry` in a precondition by assigning `E-SEM-2852`. The
expression typer uses the same code for non-postcondition `@entry` and for
`@entry` expressions that reference values outside the active environment.
HelloUltraviolet's `ContractEntryOutsidePostcondition` fixture also expects
`E-SEM-2852`.

Expected behavior:

`E-SEM-2852` is owned by §23.6 foreign contracts and means "Predicate references
out-of-scope value". §15 owns ordinary `@entry` constraints through the procedure
contract diagnostics: capability-requiring `@entry` uses `E-CON-0415`,
side-effecting `@entry` uses `E-CON-0416`, and unavailable captured values use
`E-SEM-2807`. Plain placement outside the postcondition should not be reported
as a foreign-contract out-of-scope predicate.

Impact:

The compiler and fixture catalog currently make ordinary procedure `@entry`
placement failures look like FFI predicate-binding failures. That makes
diagnostic routing, fixture coverage, and user-facing explanations diverge from
the §15 diagnostic ownership model.

HelloUltraviolet fixture gap:

`ContractEntryOutsidePostcondition` should be used to lock the ordinary
procedure diagnostic once the implementation selects the correct §15 error code.

### UV-AUDIT-0103: Private-to-public type invariant recheck diagnostic is registered but unreachable

Severity: High

Status: Inspection-backed; supplement to `UV-AUDIT-0020`.

Spec anchors:

- `Docs\SPECIFICATION.md:15028`
- `Docs\SPECIFICATION.md:15031`
- `Docs\SPECIFICATION.md:15032`
- `Docs\SPECIFICATION.md:15036`
- `Docs\SPECIFICATION.md:15390`
- `Docs\SPECIFICATION.md:30492`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\00_core\generated\diag_registry.inc:291`
- `Bootstrap\Ultraviolet\src\00_core\diagnostic_messages_table.inc:210`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\typecheck_diag_map.inc:326`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\return_stmt.cpp:1192`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\method_call.cpp:1147`

Observed behavior:

`E-SEM-2823` is present in generated diagnostic registries and in the typecheck
diagnostic map, but a source scan found no analysis emission site outside those
registries. The return-statement and method-call typing paths do not distinguish
returning from a private receiver-taking procedure back into a public caller, and
the broader invariant-enforcement paths already noted in `UV-AUDIT-0020` do not
cover this diagnostic.

Expected behavior:

Private receiver-taking procedures are exempt from the pre-call invariant check,
but the invariant must be rechecked when control returns to a public caller.
Static failure at that boundary should use `E-SEM-2823`; dynamic verification
should insert the corresponding type-invariant check at the boundary.

Impact:

The compiler cannot produce the diagnostic assigned to this enforcement point,
and private procedures can re-enter public code after violating an invariant
without the specified recheck.

HelloUltraviolet fixture gap:

The existing invariant fixtures cover public mutable fields with `E-SEM-2824`;
there is no fixture that exercises a private receiver-taking procedure returning
to a public caller with an invariant violation.

### UV-AUDIT-0104: Propagation returns bypass dynamic postcondition checks

Severity: High

Status: Inspection-backed; needs reduced fixture.

Spec anchors:

- `Docs\SPECIFICATION.md:14919`
- `Docs\SPECIFICATION.md:14922`
- `Docs\SPECIFICATION.md:15276`
- `Docs\SPECIFICATION.md:15279`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\05_codegen\lower\stmt\return_stmt.cpp:144`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\stmt\return_stmt.cpp:153`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\stmt\return_stmt.cpp:263`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\propagate_expr.cpp:373`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\propagate_expr.cpp:390`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\propagate_expr.cpp:531`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\propagate_expr.cpp:541`

Observed behavior:

Normal return lowering calls `EmitDynamicPostconditionCheckForReturn`, which
binds `ctx.contract_result_value` and emits the postcondition check before the
`IRReturn`. Propagation lowering for both outcome and union propagation builds
cleanup and then emits `IRReturn` directly in the error arms. Those branches do
not call the postcondition helper and do not bind the propagated error value as
`@result`.

Expected behavior:

When `e?` propagates an error from a procedure with a postcondition,
postconditions are evaluated for that propagation return with `@result` bound
to the propagated error. Dynamic postcondition insertion applies before each
return from the procedure.

Impact:

A `#dynamic` postcondition can be skipped on error-propagation exits, so the
runtime behavior differs depending on whether the same return value is produced
by `return` or by `?`.

HelloUltraviolet fixture gap:

`HelloUltraviolet\Source\Reference\Procedures\Postconditions.uv:65` covers a
satisfying propagation case, but there is no fixture where a propagation return
violates a dynamic postcondition.

### UV-AUDIT-0105: Dynamic foreign postconditions do not bind parameter names

Severity: High

Status: Inspection-backed; needs reduced fixture.

Spec anchors:

- `Docs\SPECIFICATION.md:26757`
- `Docs\SPECIFICATION.md:26759`
- `Docs\SPECIFICATION.md:26766`
- `Docs\SPECIFICATION.md:26852`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\call.cpp:479`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\call.cpp:499`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\call.cpp:530`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\call.cpp:564`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\call.cpp:565`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\call.cpp:662`
- `HelloUltraviolet\Source\Reference\FFI\ForeignContracts.uv:7`
- `HelloUltraviolet\Source\Reference\FFI\ForeignContracts.uv:12`
- `HelloUltraviolet\Source\Reference\FFI\ForeignContracts.uv:16`

Observed behavior:

Foreign precondition lowering snapshots and binds parameter names in
`ctx.contract_param_entry_values` before lowering each predicate. Foreign
postcondition lowering preserves and sets only `ctx.contract_result_value`, then
lowers `@foreign_ensures`, `@error`, and `@null_result` predicates without
binding the callee's parameter names to the call arguments or output locations.

Expected behavior:

Foreign postcondition predicates may reference parameter names, specifically for
checking output parameters. Dynamic foreign postcondition checks must therefore
lower in an environment that binds those parameter names as well as `@result`.

Impact:

A valid dynamic `@foreign_ensures(parameter_name ...)` predicate can resolve to
the wrong local, fail to lower, or omit the intended output-parameter check.

HelloUltraviolet fixture gap:

The current foreign-contract reference fixtures exercise `@result` and
`@null_result`, but not a valid dynamic foreign postcondition that references a
parameter name for an output-parameter check.

### UV-AUDIT-0106: Dynamic procedure and loop checks ignore the required static-proof context

Severity: Medium

Status: Inspection-backed; scoped to §15 dynamic checks.

Spec anchors:

- `Docs\SPECIFICATION.md:7210`
- `Docs\SPECIFICATION.md:15102`
- `Docs\SPECIFICATION.md:15107`
- `Docs\SPECIFICATION.md:15276`
- `Docs\SPECIFICATION.md:15279`
- `Docs\SPECIFICATION.md:15296`
- `Docs\SPECIFICATION.md:15301`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\05_codegen\lower\lower_proc.cpp:1161`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\lower_proc.cpp:1502`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\lower_proc.cpp:1503`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\stmt\return_stmt.cpp:144`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\stmt\return_stmt.cpp:159`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\stmt\return_stmt.cpp:263`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\loop_infinite.cpp:45`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\loop_infinite.cpp:46`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\loop_conditional.cpp:48`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\loop_conditional.cpp:49`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\loop_iter.cpp:136`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\loop_iter.cpp:137`
- `HelloUltraviolet\Fixtures\ArtifactProjects\FlowProofRuntimeErasure\Source\Library.uv:1`
- `HelloUltraviolet\Source\Audit\ArtifactProjectExecution.uv:6820`
- `HelloUltraviolet\Source\Audit\ArtifactProjectExecution.uv:6826`

Observed behavior:

Procedure entry lowering emits a dynamic precondition check whenever a dynamic
contract precondition is present. Return lowering emits a dynamic postcondition
check whenever `ctx.active_contract_postcondition` is set. Neither path receives
or consults the proof context used by typing. Loop invariant lowering does call
`StaticProof`, but it builds an empty `StaticProofContext` instead of using the
proof context at the loop-entry or loop-body insertion point.

Expected behavior:

For §15 dynamic verification, runtime checks are inserted exactly when required:
`Contract-Dynamic-Elide`, `Insert-Precondition-Check`,
`Insert-Postcondition-Check`, and the loop-invariant insertion rules all depend
on the relevant `StaticProof`/`StaticProofAt` result at the insertion point.

Impact:

The compiler can emit runtime contract failure sites that the spec requires to
be elided, and loop invariant lowering can fail to elide checks that were proven
from local flow facts.

HelloUltraviolet fixture gap:

`FlowProofRuntimeErasure` checks erasure of static verification artifacts, but
there is no focused `#dynamic` fixture asserting that a statically proven
precondition, postcondition, or loop invariant omits the runtime check.

### UV-AUDIT-0107: Lifecycle initialization order is reversed relative to the written eager graph

Severity: High

Status: Inspection-backed; may indicate the spec text or implementation edge
direction needs correction.

Spec anchors:

- `Docs\SPECIFICATION.md:8827`
- `Docs\SPECIFICATION.md:8831`
- `Docs\SPECIFICATION.md:28487`
- `Docs\SPECIFICATION.md:28507`
- `Docs\SPECIFICATION.md:28510`
- `Docs\SPECIFICATION.md:28521`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\memory\init_planner.cpp:1364`
- `Bootstrap\Ultraviolet\src\04_analysis\memory\init_planner.cpp:1369`
- `Bootstrap\Ultraviolet\src\04_analysis\memory\init_planner.cpp:2121`
- `Bootstrap\Ultraviolet\src\04_analysis\memory\init_planner.cpp:2134`
- `Bootstrap\Ultraviolet\src\04_analysis\memory\init_planner.cpp:2139`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\entry_emit.cpp:773`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\entry_emit.cpp:778`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\entry_emit.cpp:1315`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\entry_emit.cpp:1689`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\entry_emit.cpp:1781`

Observed behavior:

`ValueDepsEagerForModule` computes a module's eager dependencies, but
`AddEdges` stores each dependency as the source and the dependent module as the
target. `TopoOrder` then emits dependency-before-dependent order into
`plan.init_order`, and lifecycle emission calls init functions in that order
with deinit reversed.

Expected behavior:

The written spec defines `E_val^{eager} = {(m, n) | n ∈ ValueDepsEager(P, m)}`;
with `TopoOrder`, every edge source appears before its target. Under that
definition, a module whose static initializer references another module appears
before the referenced module.

Impact:

The compiler's cross-module static initialization, deinitialization, and
lifecycle side-effect order differ from `InitOrder`, `DeinitOrder`, and
`EmitInitPlan` as written. This is distinct from poison propagation direction,
which is tracked separately in `UV-AUDIT-0074`.

HelloUltraviolet fixture gap:

`ModuleSourceProjectInfrastructureExecution` checks that eager-edge and
init-order metadata exist, but it does not assert the exact module order or a
runtime-visible lifecycle sequence for cross-module static dependencies.

### UV-AUDIT-0108: Static call initializers can be classified as alias responsibility

Severity: Medium

Status: Inspection-backed.

Spec anchors:

- `Docs\SPECIFICATION.md:4058`
- `Docs\SPECIFICATION.md:4183`
- `Docs\SPECIFICATION.md:28391`
- `Docs\SPECIFICATION.md:28431`
- `Docs\SPECIFICATION.md:28436`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\memory\borrow_bind.cpp:1478`
- `Bootstrap\Ultraviolet\src\04_analysis\memory\borrow_bind.cpp:1483`
- `Bootstrap\Ultraviolet\src\04_analysis\memory\borrow_bind.cpp:1489`
- `Bootstrap\Ultraviolet\src\04_analysis\memory\borrow_bind.cpp:3695`
- `Bootstrap\Ultraviolet\src\04_analysis\memory\return_responsibility.cpp:412`
- `Bootstrap\Ultraviolet\src\04_analysis\memory\return_responsibility.cpp:432`
- `Bootstrap\Ultraviolet\src\05_codegen\globals\globals.cpp:425`
- `Bootstrap\Ultraviolet\src\05_codegen\globals\globals.cpp:440`
- `HelloUltraviolet\Source\Reference\Modules\Statics.uv:6`

Observed behavior:

The analysis-side `RespOfInit` helper first checks whether a non-place
initializer is a `CallExpr` or `MethodCallExpr`. If return-responsibility
analysis says the callee result is non-responsible, the static binding is
recorded as alias. Codegen static metadata separately computes responsibility
for the same static item through `StaticHasResponsibilityLocal`.

Expected behavior:

The spec's `RespOfInit(init)` relation is unconditional for non-place
initializers: every non-place initializer maps to `resp`. Static call and
method-call initializers are non-place expressions and should therefore create
responsible static bindings.

Impact:

Analysis can mark a static initialized by a call as alias and immovable while
static lifecycle lowering reasons about the same binding through a different
responsibility path. That creates inconsistent ownership facts at the static
declaration boundary and can affect validity or cleanup behavior.

HelloUltraviolet fixture gap:

`Modules/Statics.uv` includes a runtime-initialized `i32` static, but there is no
fixture for a module static initialized by a function or method returning a
non-responsible value.

### UV-AUDIT-0109: Contract predicates can admit loop expressions as pure

Severity: Medium

Status: Inspection-backed.

Spec anchors:

- `Docs\SPECIFICATION.md:14648`
- `Docs\SPECIFICATION.md:14649`
- `Docs\SPECIFICATION.md:14722`
- `Docs\SPECIFICATION.md:14723`
- `Docs\SPECIFICATION.md:14725`
- `Docs\SPECIFICATION.md:14742`
- `Docs\SPECIFICATION.md:14750`
- `Docs\SPECIFICATION.md:14776`
- `Docs\SPECIFICATION.md:15380`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\contracts\contract_check.cpp:1943`
- `Bootstrap\Ultraviolet\src\04_analysis\contracts\contract_check.cpp:1952`
- `Bootstrap\Ultraviolet\src\04_analysis\contracts\contract_check.cpp:1965`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\procedure_decl.cpp:2317`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\loop_infinite.cpp:259`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\loop_infinite.cpp:296`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\loop_conditional.cpp:455`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\loop_conditional.cpp:509`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\loop_iter.cpp:378`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\loop_iter.cpp:530`
- `HelloUltraviolet\Source\Reference\Procedures\Contracts.uv:129`
- `HelloUltraviolet\Source\Reference\Procedures\Contracts.uv:164`
- `HelloUltraviolet\Source\Reference\Procedures\Contracts.uv:170`

Observed behavior:

The contract purity walk treats `LoopInfiniteExpr`, `LoopConditionalExpr`, and
`LoopIterExpr` as pure when their invariant, condition, iterator, and body are
pure. The later predicate typing pass sets `require_pure = true`, but the loop
typing implementations do not reject loops in pure contexts; they type the loop
body and invariants normally.

Expected behavior:

`WF-Contract` requires both preconditions and postconditions to satisfy the
§15 purity judgment. That judgment enumerates pure expression forms and includes
rules such as `Pure-Block`, `Pure-Call-Builtin`, and `Pure-Call-Procedure`, but
it has no corresponding pure-loop rule. A loop expression in a contract
predicate should therefore fail as an impure contract predicate with
`E-SEM-2802`.

Impact:

Contracts can contain non-terminating or control-flow-heavy predicates that the
spec does not classify as pure. If such a predicate is later dynamically
checked, the runtime contract check can loop or execute predicate-local control
flow that the static purity gate should have excluded.

HelloUltraviolet fixture gap:

`Contracts.uv` exercises accepted pure blocks, pure builtin calls, pure
procedure calls, and pure method calls, but there is no rejected-source fixture
for loop expressions in contract predicates.

### UV-AUDIT-0110: `@entry` rejects pure calls that the contract purity rules allow

Severity: Medium

Status: Inspection-backed.

Spec anchors:

- `Docs\SPECIFICATION.md:14742`
- `Docs\SPECIFICATION.md:14747`
- `Docs\SPECIFICATION.md:14752`
- `Docs\SPECIFICATION.md:14757`
- `Docs\SPECIFICATION.md:14939`
- `Docs\SPECIFICATION.md:14942`
- `Docs\SPECIFICATION.md:14944`
- `Docs\SPECIFICATION.md:14946`
- `Docs\SPECIFICATION.md:14957`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\contracts\contract_intrinsics.cpp:733`
- `Bootstrap\Ultraviolet\src\04_analysis\contracts\contract_intrinsics.cpp:734`
- `Bootstrap\Ultraviolet\src\04_analysis\contracts\contract_intrinsics.cpp:736`
- `Bootstrap\Ultraviolet\src\04_analysis\contracts\contract_intrinsics.cpp:737`
- `Bootstrap\Ultraviolet\src\04_analysis\contracts\contract_intrinsics.cpp:798`
- `Bootstrap\Ultraviolet\src\04_analysis\contracts\contract_intrinsics.cpp:845`
- `Bootstrap\Ultraviolet\src\04_analysis\contracts\contract_intrinsics.cpp:853`
- `Bootstrap\Ultraviolet\src\04_analysis\contracts\contract_intrinsics.cpp:1265`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\contract_entry.cpp:510`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\contract_entry.cpp:525`
- `HelloUltraviolet\Source\Reference\Procedures\Contracts.uv:187`
- `HelloUltraviolet\Source\Reference\Procedures\Contracts.uv:193`
- `HelloUltraviolet\Source\Reference\Procedures\Contracts.uv:199`

Observed behavior:

`ValidateEntryIntrinsic` rejects every `CallExpr` and `MethodCallExpr` inside
`@entry(...)` through its separate determinism check, after the side-effect
check has only recursed through the callee, receiver, and arguments. This occurs
before the richer contract purity/type path can classify pure helper calls,
pure comptime calls, builtin calls, or const receiver methods.

Expected behavior:

The `@entry(expr)` constraints require the inner expression to be pure, limited
to parameters and receiver bindings, and `BitcopyType`. The same section's
purity judgment explicitly admits `Pure-Call-Builtin`, `Pure-Call-Procedure`,
`Pure-Method-Const`, and `Pure-Comptime`. A pure, entry-scope, Bitcopy-returning
call should therefore be accepted inside `@entry(...)`.

Impact:

Valid postconditions such as comparing `@result` to an entry-state value
computed by a pure helper are rejected with `E-CON-0416`. That prevents users
from factoring entry-state computations through pure procedures or const
methods even though those calls are accepted in ordinary contract predicates.

HelloUltraviolet fixture gap:

`Contracts.uv` exercises pure procedure, method, and comptime calls in contract
predicates, and rejected fixtures cover side-effecting and capability-requiring
`@entry(...)` expressions, but there is no accepted `@entry(pure_call(...))`
fixture.

### UV-AUDIT-0111: Attribute-only lines suppress newlines without the required following operand

Severity: Medium

Status: Subagent-confirmed and locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:1918`
- `Docs\SPECIFICATION.md:1921`
- `Docs\SPECIFICATION.md:1924`
- `Docs\SPECIFICATION.md:1930`
- `Docs\SPECIFICATION.md:1939`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\02_source\lexer\lexer.cpp:336`
- `Bootstrap\Ultraviolet\src\02_source\lexer\lexer.cpp:350`
- `Bootstrap\Ultraviolet\src\02_source\lexer\lexer.cpp:382`
- `Bootstrap\Ultraviolet\src\02_source\lexer\lexer.cpp:387`
- `Bootstrap\Ultraviolet\src\02_source\lexer\lexer.cpp:628`
- `HelloUltraviolet\Source\Reference\Parsing\AttributeParsing.uv`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\RejectedSource\Parsing.uv`

Observed behavior:

`ContinuesLineImpl` treats a line containing only one or more attribute
specifications as continued whenever `LineIsOnlyAttributeList(...)` succeeds.
That branch does not check whether the next non-newline token exists or whether
it begins an operand.

Expected behavior:

The attribute continuation disjunct in `Continue(K, i)` requires
`AttrBefore(K, i)`, `Next(K, i) = u`, and `BeginsOperand(u)`. Attribute-only
lines before `}`, `;`, end of file, or another non-operand boundary should
therefore leave the newline in the filtered token stream.

Impact:

Malformed attribute placement can be parsed from a token stream that differs
from the spec-defined `Filter(K)`. This can move, mask, or reclassify parser
diagnostics and may let an attribute list recover across a boundary that should
have remained a statement or item terminator.

HelloUltraviolet fixture gap:

The reference parsing fixtures cover valid attribute shapes, and the rejected
parsing catalog covers missing terminators and enum comma separation, but there
is no rejected-source fixture for an attribute-only line followed by a
non-operand boundary.

### UV-AUDIT-0112: Tooling emits an unowned diagnostic for invalid assembly targets

Severity: Medium

Status: Subagent-reported and locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:453`
- `Docs\SPECIFICATION.md:989`
- `Docs\SPECIFICATION.md:1050`
- `Docs\SPECIFICATION.md:1082`
- `Docs\SPECIFICATION.md:1111`
- `Docs\SPECIFICATION.md:1724`
- `Docs\SPECIFICATION.md:1739`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\06_driver\tooling\analysis.cpp:32`
- `Bootstrap\Ultraviolet\src\06_driver\tooling\analysis.cpp:39`
- `Bootstrap\Ultraviolet\src\06_driver\tooling\analysis.cpp:87`
- `Bootstrap\Ultraviolet\src\06_driver\tooling\analysis.cpp:90`
- `Bootstrap\Ultraviolet\src\06_driver\tooling\analysis.cpp:92`
- `Bootstrap\Ultraviolet\src\01_project\load_project.cpp:449`
- `Bootstrap\Ultraviolet\src\01_project\load_project.cpp:453`
- `Bootstrap\Ultraviolet\src\01_project\load_project.cpp:456`
- `Bootstrap\Ultraviolet\src\06_driver\compiler_main.cpp:3065`
- `Bootstrap\Ultraviolet\src\06_driver\compiler_main.cpp:3067`
- `Bootstrap\Ultraviolet\src\06_driver\compiler_main.cpp:3442`
- `Bootstrap\Ultraviolet\src\06_driver\compiler_main.cpp:3444`

Observed behavior:

`AnalyzeWorkspace` parses `options.assembly_target` before calling
`LoadProject`. If `ParseAssemblyTarget` rejects the string, tooling calls
`EmitInternal`, which constructs a diagnostic with only severity and message
and no spec-owned code. The main driver paths for the same parse failure emit
`E-PRJ-0205` instead.

Expected behavior:

Diagnostic records with `code = bottom` are auxiliary diagnostics and are
admitted only where a feature section defines them explicitly. Assembly target
selection is part of project loading, and the project diagnostics section owns
`E-PRJ-0205` for `Assembly-Select-Err`. Tooling should therefore surface a
stable spec-owned diagnostic for the same invalid target path unless the
specification defines a tooling-specific auxiliary diagnostic.

Impact:

Editor and analysis clients cannot reliably classify the invalid target error,
and `uvc` command-line analysis differs from workspace analysis for the same
assembly-target input. That breaks diagnostic determinism at the tool boundary
even though the project layer already has a canonical code.

HelloUltraviolet fixture gap:

The existing project and fixture catalogs exercise compiler project-loading
paths, but there is no tooling or workspace-analysis fixture that supplies an
invalid `assembly_target` and asserts a canonical project diagnostic code.

### UV-AUDIT-0113: Compile-time prohibited constructs are not uniformly checked across comptime surfaces

Severity: High

Status: Inspection-backed.

Spec anchors:

- `Docs\SPECIFICATION.md:24727`
- `Docs\SPECIFICATION.md:24730`
- `Docs\SPECIFICATION.md:24858`
- `Docs\SPECIFICATION.md:24859`
- `Docs\SPECIFICATION.md:24860`
- `Docs\SPECIFICATION.md:24861`
- `Docs\SPECIFICATION.md:24862`
- `Docs\SPECIFICATION.md:24863`
- `Docs\SPECIFICATION.md:25709`
- `Docs\SPECIFICATION.md:25715`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\stmt_common.cpp:393`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\stmt_common.cpp:590`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\stmt_common.cpp:601`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\type_expr.cpp:3841`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\type_expr.cpp:3848`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\typecheck.cpp:212`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\typecheck.cpp:268`
- `Bootstrap\Ultraviolet\src\03_comptime\rewrite.cpp:519`
- `Bootstrap\Ultraviolet\src\03_comptime\rewrite.cpp:553`
- `Bootstrap\Ultraviolet\src\03_comptime\rewrite.cpp:559`
- `Bootstrap\Ultraviolet\src\03_comptime\rewrite.cpp:593`
- `Bootstrap\Ultraviolet\src\03_comptime\rewrite.cpp:599`
- `Bootstrap\Ultraviolet\src\03_comptime\eval.cpp:437`
- `Bootstrap\Ultraviolet\src\03_comptime\derive.cpp:747`
- `HelloUltraviolet\Fixtures\RejectedSource\Comptime\ComptimeProhibitedWait\Source\Main.uv:4`
- `HelloUltraviolet\Fixtures\RejectedSource\Comptime\DeriveTargetProhibitedWait\Source\Main.uv:3`

Observed behavior:

`TypeCtStmt` and the Phase 2 `RewriteBlock` path run
`CtProhibitedConstructFinder` before accepting or executing a `comptime`
statement block. Derive target execution has a separate body-restriction pass.
The `ComptimeExpr` typing branch instead directly type-checks `node.body` in
an extended compile-time environment, and the Phase 2 expression rewrite paths
directly call `EvalExpr` for `comptime` expressions. Compile-time procedure
typing builds the procedure environment and calls `TypeBlock`, while
compile-time procedure evaluation calls `EvalBlock`, without a comparable
general prohibited-construct scan.

Expected behavior:

The specification defines `comptime_expr` and `comptime_procedure_decl` as
compile-time execution surfaces and states that the listed runtime constructs
are prohibited inside compile-time execution. The diagnostics table assigns
`E-CTE-0020` to compile-time blocks containing prohibited runtime constructs
and `E-CTE-0032` to compile-time procedure bodies that violate compile-time
restrictions. The same restriction set should therefore be enforced for
compile-time expressions and compile-time procedure bodies, not only
`comptime` statement blocks and derive targets.

Impact:

Prohibited constructs in `comptime { expression }` or in a compile-time
procedure body can fall through to ordinary expression diagnostics or Phase 2
evaluation failure instead of the compile-time restriction diagnostics. For
semantic prohibitions such as calls crossing an FFI boundary, the syntax-only
statement walker also cannot classify the call before the general call checker
uses the ordinary unsafe-boundary diagnostic.

HelloUltraviolet fixture gap:

The rejected-source catalog has a `comptime` statement fixture for prohibited
`wait` and a derive-target fixture for the same family of restriction. There
is no rejected fixture for prohibited constructs in a `comptime` expression,
and no compile-time procedure fixture that expects `E-CTE-0032` for the
procedure-body restriction path.

### UV-AUDIT-0114: Command diagnostics and fixtures still use the obsolete `uv` command name

Severity: Medium

Status: User-correction-backed and inspection-backed.

Spec anchors:

- `Docs\SPECIFICATION.md:643`
- `Docs\SPECIFICATION.md:647`
- `Docs\SPECIFICATION.md:7295`
- `Docs\SPECIFICATION.md:7335`
- `Docs\SPECIFICATION.md:7356`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\06_driver\compiler_main.cpp:357`
- `Bootstrap\Ultraviolet\src\06_driver\compiler_main.cpp:368`
- `Bootstrap\Ultraviolet\src\06_driver\compiler_main.cpp:579`
- `Bootstrap\Ultraviolet\src\06_driver\compiler_main.cpp:587`
- `Bootstrap\Ultraviolet\src\06_driver\cli.cpp:1095`
- `Bootstrap\Ultraviolet\src\06_driver\cli.cpp:1107`
- `Bootstrap\Ultraviolet\src\00_core\generated\diag_registry.inc:355`
- `README.md:176`
- `README.md:351`
- `README.md:364`
- `README.md:391`
- `HelloUltraviolet\README.md:86`
- `HelloUltraviolet\README.md:89`
- `HelloUltraviolet\README.md:93`
- `HelloUltraviolet\README.md:96`
- `HelloUltraviolet\README.md:99`
- `HelloUltraviolet\Fixtures\OutputDiagnostics\CommandLine\UnknownCommand\Invocation.txt:1`
- `HelloUltraviolet\Fixtures\OutputDiagnostics\CommandLine\UnknownCommand\Output.txt:1`
- `HelloUltraviolet\Fixtures\OutputDiagnostics\CommandLine\UnknownCommand\Expected.uv:3`

Observed behavior:

The active driver command surface resolves the fallback command name as `uvc`,
and top-level usage/help renders subcommands through the resolved command name.
The release and install documentation also describes `uvc` as the default
installed compiler command, with `uv` only as an optional alias. However, the
source-native test section of the specification still defines `TestArg` as the
argument to `uv test`, states that `uv test` generates the harness, and names
`E-TST-0108` as "Unknown `uv test` target". The generated diagnostic registry
therefore emits the obsolete `uv test` wording, and the fallback internal text
in `EmitUnknownTestTargetDiagnostic` also says "unknown uv test target".
HelloUltraviolet's test-running README still instructs users to invoke
`uv test`, and its unknown-command output-diagnostic fixture still records
`Build/Binary/uv.exe` as the command path.

Expected behavior:

The command name in user-visible diagnostics, examples, and fixture
documentation should match the current `uvc` command surface. Because the
diagnostic registry is generated from the specification, the wording should be
corrected through a spec-approved update before regenerating compiler
diagnostic metadata.

Impact:

Users running the default installed compiler see a mixed command vocabulary:
`uvc --help` advertises `uvc test`, but the unknown-target diagnostic and
HelloUltraviolet test instructions refer to `uv test`. This also makes future
diagnostic fixture expectations ambiguous because the canonical code remains
`E-TST-0108` while the owned message describes the old command name.

HelloUltraviolet fixture gap:

`HelloUltraviolet\Fixtures\OutputDiagnostics\CommandLine\UnknownTestTarget`
asserts only the diagnostic code and authority. It does not assert the
user-facing message text, so the stale `uv test` wording is not caught by the
output-diagnostic fixture. The sibling `UnknownCommand` fixture still records
the old executable name and also asserts only code/authority metadata.

### UV-AUDIT-0115: ELF and Mach-O shared-library data exports are hidden by LLVM visibility

Severity: High

Status: Subagent-reported and locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:28381`
- `Docs\SPECIFICATION.md:28468`
- `Docs\SPECIFICATION.md:28469`
- `Docs\SPECIFICATION.md:28651`
- `Docs\SPECIFICATION.md:28652`
- `Docs\SPECIFICATION.md:28653`
- `Docs\SPECIFICATION.md:28654`
- `Docs\SPECIFICATION.md:28655`
- `Docs\SPECIFICATION.md:28656`
- `Docs\SPECIFICATION.md:28657`
- `Docs\SPECIFICATION.md:30154`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\06_driver\shared_library_exports.cpp:116`
- `Bootstrap\Ultraviolet\src\06_driver\shared_library_exports.cpp:162`
- `Bootstrap\Ultraviolet\src\06_driver\shared_library_exports.cpp:167`
- `Bootstrap\Ultraviolet\src\06_driver\shared_library_exports.cpp:194`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\entry_emit.cpp:1960`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\entry_emit.cpp:1963`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\entry_emit.cpp:1970`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\entry_emit.cpp:1971`
- `Bootstrap\Ultraviolet\src\01_project\targets\target_platform.cpp:304`
- `Bootstrap\Ultraviolet\src\01_project\targets\target_platform.cpp:307`
- `Bootstrap\Ultraviolet\src\01_project\targets\target_platform.cpp:315`
- `Bootstrap\Ultraviolet\src\01_project\targets\target_platform.cpp:331`
- `Bootstrap\Ultraviolet\src\01_project\targets\target_platform.cpp:725`
- `Bootstrap\Ultraviolet\src\01_project\targets\target_platform.cpp:800`

Observed behavior:

The driver computes shared-library data export symbols for module poison flags
and public static declarations. Platform export-list generation includes those
data symbols for ELF and Mach-O targets. The codegen context, however, writes
only `exports.export_symbols` back to `cache.ctx.shared_library_export_symbols`.
`ApplySharedLibraryDefinitionVisibility` builds its default-visibility set
from that procedure-symbol list and assigns hidden visibility to every other
defined global on targets that use explicit shared-library definition
visibility.

Expected behavior:

Every exported shared-library state symbol, including public static data and
the required poison flags, must remain externally visible when the platform
export mechanism names it. The object-level LLVM visibility must therefore be
consistent with the exported procedure and data symbol set.

Impact:

ELF and Mach-O shared libraries can advertise data exports to the linker while
the object marks those global definitions hidden. That can break dynamic
export binding for shared-library static state even though the generated export
list names the state symbol.

HelloUltraviolet fixture gap:

`SharedLibraryImageLifecycle` exercises linked procedure calls and private
provider state, but there is no fixture that exports or imports a public static
data symbol across a shared-library boundary. The macOS hosted fixtures check
artifact shape and lifecycle symbols, not dynamic data export visibility.

### UV-AUDIT-0116: `LinkArgsOk` is recorded while linker invocation inputs exceed `LinkInputs(P)`

Severity: Medium

Status: Subagent-reported and locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:1501`
- `Docs\SPECIFICATION.md:1505`
- `Docs\SPECIFICATION.md:1509`
- `Docs\SPECIFICATION.md:1524`
- `Docs\SPECIFICATION.md:1529`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\01_project\link.cpp:2158`
- `Bootstrap\Ultraviolet\src\01_project\link.cpp:2159`
- `Bootstrap\Ultraviolet\src\01_project\link.cpp:2172`
- `Bootstrap\Ultraviolet\src\01_project\link.cpp:2188`
- `Bootstrap\Ultraviolet\src\01_project\link.cpp:2252`
- `Bootstrap\Ultraviolet\src\01_project\link.cpp:2255`
- `Bootstrap\Ultraviolet\src\01_project\link.cpp:2260`
- `Bootstrap\Ultraviolet\src\01_project\link.cpp:2264`
- `Bootstrap\Ultraviolet\src\01_project\link.cpp:2276`
- `Bootstrap\Ultraviolet\src\01_project\link.cpp:2282`
- `Bootstrap\Ultraviolet\src\01_project\link.cpp:2291`
- `Bootstrap\Ultraviolet\src\01_project\targets\target_platform.cpp:1032`
- `HelloUltraviolet\Source\Audit\ArtifactProjectExecution.uv:1520`
- `HelloUltraviolet\Source\Audit\ArtifactProjectExecution.uv:1528`
- `HelloUltraviolet\Source\Audit\ArtifactProjectExecution.uv:1529`

Observed behavior:

The linker computes `logical_inputs = LinkInputs(...)`, then separately
discovers target runtime sidecars and an optional startup object. The actual
linker invocation input list appends materialized extra inputs, the startup
object, the runtime library, and platform sidecar inputs. ELF also wraps
linkable sidecars in `--no-as-needed` and `--as-needed` arguments. After
constructing this expanded invocation list, the driver records `def.LinkArgsOk`
with both `logical_input_count` and `invocation_input_count`, plus
`target_inputs_added` to acknowledge that the invocation differs from the
logical list.

Expected behavior:

The specification defines `LinkInputs(P)` as `LinkObjs(P) ++
LibraryArtifactInputs(P) ++ [RuntimeLibPath(P)]`, and `LinkArgsOk(P, L, out,
imp)` requires `L = LinkInputs(P)`. The `Link-Ok`, incompatible-runtime, and
link-failure rules invoke the linker with exactly `Objs ++
LibraryArtifactInputs(P) ++ [lib]`. Any target startup object, runtime
sidecar, or linker control argument must be part of the spec-defined input or
flag relation before `def.LinkArgsOk` is recorded for the real invocation.

Impact:

Conformance logs can claim `LinkArgsOk` for a linker command whose actual
input vector is larger than the Chapter 3 relation permits. Runtime/link
diagnostics and reproducibility checks reason over the logical input list while
the real link uses extra target inputs and control arguments.

HelloUltraviolet fixture gap:

`ArtifactProjectExecution.uv` checks for `def.LinkArgsOk`, but it explicitly
accepts both `target_inputs_added=false` and `target_inputs_added=true`, so the
fixture encodes the deviation instead of rejecting invocation inputs outside
`LinkInputs(P)`.

### UV-AUDIT-0117: Duplicate enum record-payload fields produce an unclassified typing failure

Severity: Medium

Status: Subagent-reported and locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:10155`
- `Docs\SPECIFICATION.md:10156`
- `Docs\SPECIFICATION.md:10160`
- `Docs\SPECIFICATION.md:10161`
- `Docs\SPECIFICATION.md:10249`
- `Docs\SPECIFICATION.md:10261`
- `Docs\SPECIFICATION.md:30487`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\enum_literal.cpp:37`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\enum_literal.cpp:39`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\enum_literal.cpp:41`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\enum_literal.cpp:211`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\enum_literal.cpp:214`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\enum_literal.cpp:215`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\enum_literal.cpp:230`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\enum_literal.cpp:231`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\enum_literal.cpp:232`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\enum_literal.cpp:239`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\enum_literal.cpp:240`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\enum_literal.cpp:258`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\enum_literal.cpp:291`
- `HelloUltraviolet\Fixtures\RejectedSource\Expressions\EnumRecordMissingField\Expected.uv:3`

Observed behavior:

`CheckEnumLiteralPayloadAgainst` detects duplicate field names in a
record-payload enum literal with a `seen` set. When insertion fails, it returns
the default failed `CheckResult` without setting `diag_id` and without
recording either `T-Enum-Lit-Record` or an enum-literal error rule. The same
record-payload checker assigns `E-TYP-2009` for missing or unknown fields, and
`CopyCheckFailure` forwards the empty diagnostic result to the outer expression
typing path.

Expected behavior:

`T-Enum-Lit-Record` requires `Distinct(FieldInitNames(fields))`. A duplicate
record-payload initializer therefore cannot satisfy the typing rule and should
be rejected through a spec-owned enum construction diagnostic or through an
explicitly specified diagnostic rule. The current spec owns
`Enum-Lit-Record-MissingField` for the missing-field subset case, but the
implementation's duplicate-field path is neither accepted by the success rule
nor classified by the enum diagnostics table.

Impact:

Malformed enum record constructors can fail without a canonical diagnostic code
or conformance rule, making first-failure ordering and fixture expectations
unstable. The behavior also diverges from ordinary record construction, where
duplicate field initializers have an owned diagnostic path.

HelloUltraviolet fixture gap:

The expression rejected-source catalog covers unknown enum variants, tuple
arity, missing enum record fields, duplicate variants, duplicate discriminants,
empty enums, and invalid discriminants. It has no rejected fixture for a
record-like enum constructor with the same payload field initialized twice.

### UV-AUDIT-0118: `#library` named arguments are order-sensitive despite the spec only naming them semantically

Severity: Medium

Status: Locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:6806`
- `Docs\SPECIFICATION.md:6879`
- `Docs\SPECIFICATION.md:26445`
- `Docs\SPECIFICATION.md:26457`
- `Docs\SPECIFICATION.md:26458`
- `Docs\SPECIFICATION.md:26475`
- `Docs\SPECIFICATION.md:26480`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\02_source\attributes\attribute_registry.cpp:579`
- `Bootstrap\Ultraviolet\src\02_source\attributes\attribute_registry.cpp:582`
- `Bootstrap\Ultraviolet\src\02_source\attributes\attribute_registry.cpp:584`
- `Bootstrap\Ultraviolet\src\02_source\attributes\attribute_registry.cpp:585`
- `Bootstrap\Ultraviolet\src\02_source\attributes\attribute_registry.cpp:1034`
- `Bootstrap\Ultraviolet\src\02_source\attributes\attribute_registry.cpp:1057`
- `Bootstrap\Ultraviolet\src\02_source\attributes\attribute_registry.cpp:1066`
- `Bootstrap\Ultraviolet\src\04_analysis\attributes\ffi_library_attrs.cpp:72`
- `Bootstrap\Ultraviolet\src\04_analysis\attributes\ffi_library_attrs.cpp:78`
- `Bootstrap\Ultraviolet\src\04_analysis\attributes\ffi_library_attrs.cpp:88`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\extern_block.cpp:485`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\extern_block.cpp:489`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\extern_block.cpp:493`
- `HelloUltraviolet\Fixtures\ArtifactProjects\RawDylibLibrary\Source\Main.uv:3`
- `HelloUltraviolet\Fixtures\ArtifactProjects\MacOSDylibLibrary\Source\Main.uv:3`
- `HelloUltraviolet\Fixtures\ArtifactProjects\SharedLibraryImageLifecycle\Source\SharedLibraryImageLifecycleHostHarness\Main.uv:23`
- `HelloUltraviolet\Fixtures\RejectedSource\FFI\UnsupportedLibraryKind\Source\Main.uv:3`

Observed behavior:

The registry declares `#library` with keyed `name` and `kind` arguments and a
default `kind` of `dylib`, but the validator rejects any `name` argument whose
position is not index `0`, and rejects any `kind` argument unless `name` was
already seen at index `0` and `kind` itself is at index `1`. The later
`NormalizeLibraryAttribute` path repeats the same positional checks before it
records `requirement.23.LibraryAttributeSemantics` and before extern-block
typing applies `LibraryKindSupported`.

Expected behavior:

Chapter 9 represents attribute arguments as an ordered list, but `AttrArgsOk`
delegates attribute-specific constraints to the owning attribute section.
Section 23.4.4.2 defines the `#library` contract in terms of the `name`
argument, the optional `kind` argument, the default `dylib` kind, and the
`LibraryKindSupported` relation. It does not specify that keyed arguments are
valid only in the spelling order `name` followed by `kind`. Either
`#library(kind: "...", name: "...")` should be accepted as the same named
argument set, or the specification must explicitly make the argument order
normative before the implementation can reject it as malformed attribute
syntax.

Impact:

Valid-looking source can be rejected with `E-MOD-2450` before reaching the
Chapter 23 library-kind checks. This also means conformance instrumentation for
`requirement.23.LibraryAttributeSemantics` is only emitted for one spelling
order, even though the spec describes the semantics by keyed argument names.

HelloUltraviolet fixture gap:

The existing artifact and rejected-source fixtures exercise only
`#library(name: ..., kind: ...)`. There is no accepted fixture proving that an
omitted `kind` uses the `dylib` default through the whole pipeline, and no
rejected-source fixture documenting that reversed keyed arguments are intended
to be ill-formed.

### UV-AUDIT-0119: Explicit `public using` can report ordinary access failure instead of public re-export failure

Severity: Medium

Status: Subagent-reported and locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:5933`
- `Docs\SPECIFICATION.md:7747`
- `Docs\SPECIFICATION.md:7752`
- `Docs\SPECIFICATION.md:7757`
- `Docs\SPECIFICATION.md:7777`
- `Docs\SPECIFICATION.md:7801`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\resolve\collect_toplevel.cpp:380`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\collect_toplevel.cpp:381`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\collect_toplevel.cpp:382`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\collect_toplevel.cpp:387`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\collect_toplevel.cpp:388`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\collect_toplevel.cpp:747`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\resolve_using.cpp:432`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\resolve_using.cpp:433`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\resolve_using.cpp:442`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\resolve_using.cpp:544`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\resolve_using.cpp:552`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\resolve_using.cpp:553`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\resolve_module.cpp:228`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\resolve_module.cpp:229`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\resolve_module.cpp:230`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\resolve_module.cpp:289`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\resolve_module.cpp:290`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\using_decl.cpp:213`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\using_decl.cpp:258`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\using_decl.cpp:259`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\using_decl.cpp:261`
- `HelloUltraviolet\Source\Audit\Catalog\ModuleLevelForms\UsingDeclarations.uv:191`
- `HelloUltraviolet\Fixtures\RejectedSource\Modules\UsingListDuplicate\Expected.uv:3`
- `HelloUltraviolet\Fixtures\RejectedSource\Names\PrivateAccess\Expected.uv:3`
- `HelloUltraviolet\Fixtures\RejectedSource\Names\InternalAccess\Expected.uv:3`

Observed behavior:

During name-map collection, `UsingNames` resolves explicit `UsingItem` and
`UsingList` clauses and calls `CanAccess` before checking whether a `public
using` target is itself public. If the target is private outside the current
module or internal outside the current assembly, `CanAccess` returns
`Access-Err`, which later maps to `E-MOD-1207`. The code paths that return
`Using-Path-Item-Public-Err` or `Using-List-Public-Err`, which map to
`E-MOD-1205`, are only reached after ordinary access has already succeeded.
The later `TypeUsingDecl` helper has the same shape: `ResolveUsingItem` checks
`IsItemVisible` and directly reports `E-MOD-1207`, with no public re-export
diagnostic branch.

Expected behavior:

For an explicit `public using` whose path resolves to a non-public item, the
using-declaration rules define `Using-Item-Public-Err` /
`Using-Path-Item-Public-Err` and `Using-List-Public-Err`, and the Chapter 11
diagnostics table maps both to `E-MOD-1205`. Ordinary `Access-Err` remains the
Chapter 7 diagnostic for non-public access, but it should not preempt the
specific public API re-export failure once the source construct is a `public
using` of a resolved non-public declaration.

Impact:

The compiler still rejects the source, but the diagnostic family and rule
identity are wrong. Tooling and fixtures see an ordinary visibility-access
failure where the spec requires a public API surface violation, and first
failure ordering can differ from the Chapter 11 using-declaration judgment.

HelloUltraviolet fixture gap:

The audit catalog lists `Using-List-Public-Err`, but the concrete rejected
module fixture only covers duplicate using-list entries. Existing private and
internal access fixtures assert `E-MOD-1207` for ordinary access expressions,
not `E-MOD-1205` for `public using` re-exports of non-public items.

### UV-AUDIT-0120: Unregistered `E-TYP-1501` escapes enum record-pattern resolution

Severity: High

Status: Subagent-reported and locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:453`
- `Docs\SPECIFICATION.md:484`
- `Docs\SPECIFICATION.md:485`
- `Docs\SPECIFICATION.md:488`
- `Docs\SPECIFICATION.md:18358`
- `Docs\SPECIFICATION.md:18361`
- `Docs\SPECIFICATION.md:18947`
- `Docs\SPECIFICATION.md:18968`
- `Docs\SPECIFICATION.md:18969`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\resolve\resolve_pattern.cpp:259`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\resolve_pattern.cpp:273`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\resolve_pattern.cpp:280`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\resolve_pattern.cpp:287`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\resolve_pattern.cpp:299`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\resolve_pattern.cpp:300`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\resolve_pattern.cpp:316`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\resolve_module.cpp:195`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\resolve_module.cpp:207`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\resolve_module.cpp:208`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\resolve_module.cpp:292`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\resolve_module.cpp:326`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\resolve_module.cpp:338`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\resolve_module.cpp:909`
- `Bootstrap\Ultraviolet\src\00_core\generated\diag_registry.inc:396`
- `Bootstrap\Ultraviolet\src\00_core\generated\diag_registry.inc:948`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\RejectedSource\Patterns.uv:45`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\RejectedSource\Patterns.uv:49`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\RejectedSource\Patterns.uv:100`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\RejectedSource\Patterns.uv:253`

Observed behavior:

The enum-pattern resolver has a record-payload disambiguation path for
`A::B { ... }`. When `A` resolves as a type path, is not an enum declaration,
and `A::B` also resolves as a type path but is not a record declaration, the
resolver returns the raw diagnostic id `E-TYP-1501`. `CodeForResolveDiag`
explicitly maps some raw diagnostic codes such as `E-TYP-2007`, but has no
mapping for `E-TYP-1501`; `EmitResolveDiag` therefore emits its internal
unmapped-resolver diagnostic instead of a registry-owned compiler diagnostic.

Expected behavior:

The spec requires code-owned diagnostics to come from normative diagnostic
tables, and code-free auxiliary diagnostics are admitted only where a feature
section defines them explicitly. Chapter 17 defines the `A::B { ... }`
record-pattern versus enum-record-payload disambiguation behavior and owns the
pattern diagnostic table, but it does not define `E-TYP-1501` or a diagnostic
rule for this failure. This branch must report a spec-owned diagnostic, or the
spec must first define the diagnostic and the registry must be regenerated.

Impact:

Rejected source in this resolver path receives an internal diagnostic without
a stable public code. That breaks diagnostic ownership, fixture matching,
registry/message consistency, and IDE or editor classification for this
pattern-resolution failure.

HelloUltraviolet fixture gap:

Pattern fixtures cover adjacent failures such as record-pattern unknown fields
with `E-SEM-2731` and enum/modal pattern diagnostics with `E-SEM-2741`. No
fixture or expected metadata mentions `E-TYP-1501`, and no rejected-source
specimen exercises the enum record-pattern fallback failure where the joined
path resolves to a non-record type.

### UV-AUDIT-0121: Non-public `#host_export` reports the raw `#export` visibility diagnostic

Severity: Medium

Status: Locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:26185`
- `Docs\SPECIFICATION.md:26233`
- `Docs\SPECIFICATION.md:26516`
- `Docs\SPECIFICATION.md:26519`
- `Docs\SPECIFICATION.md:26566`
- `Docs\SPECIFICATION.md:26567`
- `Docs\SPECIFICATION.md:30518`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\procedure_decl.cpp:1806`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\procedure_decl.cpp:1815`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\procedure_decl.cpp:1817`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\procedure_decl.cpp:1818`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\procedure_decl.cpp:1820`
- `Bootstrap\Ultraviolet\src\00_core\generated\diag_registry.inc:342`
- `Bootstrap\Ultraviolet\src\00_core\generated\diag_registry.inc:894`
- `Bootstrap\Ultraviolet\src\00_core\diagnostic_messages_table.inc:257`
- `HelloUltraviolet\Fixtures\RejectedSource\FFI\RawExportNonPublic\Expected.uv:3`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\RejectedSource\FFI.uv:358`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\RejectedSource\FFI.uv:364`
- `HelloUltraviolet\Fixtures\RejectedSource\FFI\HostedExportDiagnostics\Expected.uv:3`

Observed behavior:

`ValidateProcedureFfiAttributes` computes `has_foreign_export` as
`has_export || has_host_export`. If any foreign export is non-public, it records
`Export-Vis-Err`, records `diagnostics.23.RawExportDiagnostics` only when
`has_export` is true, and returns `E-SYS-3353` for both raw exports and hosted
exports. The generated registry message for `E-SYS-3353` says only "`#export`
requires `public` visibility". The hand-maintained diagnostic message table
has broader text that mentions both attributes, so the generated registry and
spec-derived diagnostics are stale relative to the implementation path.

Expected behavior:

The `#host_export` rules independently require a public procedure, and
`HostExportSig-Ok` has `vis = public` as a premise. However, the normative
`E-SYS-3353` row in the spec describes the raw `#export` visibility failure,
while hosted-export-specific rows cover library kind, mixed export mode,
generic procedures, context-bundle shape, and catch return zeroability. A
non-public `#host_export` rejection should be owned by a hosted-export
diagnostic rule/message, or the spec and generated registry must explicitly
broaden `E-SYS-3353` to cover both `#export` and `#host_export`.

Impact:

The source is rejected, but the public diagnostic text and expected ownership
point to raw export diagnostics. That gives users and tools the wrong FFI
surface, and conformance output lacks a hosted-export visibility rule for this
failure.

HelloUltraviolet fixture gap:

HelloUltraviolet covers non-public raw export with `RawExportNonPublic`, and
covers hosted-export library, mixed-mode, generic, missing-context, raw-context,
move-context, and diagnostics-table cases. It does not include a non-public
`#host_export` fixture, so this message/ownership mismatch is not pinned.

### UV-AUDIT-0122: Hosted exports are rejected for static library assemblies

Severity: High

Status: Locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:601`
- `Docs\SPECIFICATION.md:613`
- `Docs\SPECIFICATION.md:614`
- `Docs\SPECIFICATION.md:1534`
- `Docs\SPECIFICATION.md:1554`
- `Docs\SPECIFICATION.md:26210`
- `Docs\SPECIFICATION.md:26237`
- `Docs\SPECIFICATION.md:26368`
- `Docs\SPECIFICATION.md:26384`
- `Docs\SPECIFICATION.md:26566`
- `Docs\SPECIFICATION.md:28478`
- `Docs\SPECIFICATION.md:28692`
- `Docs\SPECIFICATION.md:28697`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\06_driver\output_pipeline.cpp:545`
- `Bootstrap\Ultraviolet\src\06_driver\output_pipeline.cpp:760`
- `Bootstrap\Ultraviolet\src\06_driver\output_pipeline.cpp:766`
- `Bootstrap\Ultraviolet\src\06_driver\output_pipeline.cpp:768`
- `Bootstrap\Ultraviolet\src\06_driver\output_pipeline.cpp:771`
- `Bootstrap\Ultraviolet\src\06_driver\output_pipeline.cpp:791`
- `Bootstrap\Ultraviolet\src\06_driver\output_pipeline.cpp:1043`
- `Bootstrap\Ultraviolet\src\06_driver\pipeline.cpp:609`
- `Bootstrap\Ultraviolet\src\06_driver\pipeline.cpp:610`
- `Bootstrap\Ultraviolet\src\06_driver\pipeline.cpp:634`
- `Bootstrap\Ultraviolet\src\06_driver\pipeline.cpp:635`
- `HelloUltraviolet\Fixtures\AcceptedProjects\HostedExportLibrary\Ultraviolet.toml:4`
- `HelloUltraviolet\Fixtures\AcceptedProjects\StaticLibrary\Ultraviolet.toml:4`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\AcceptedProjects\Coverage.uv:18`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\AcceptedProjects\Coverage.uv:67`

Observed behavior:

`BuildOutputLinkPlan` computes `hosted_library` from `ContainsHostExports` and
then rejects the project whenever `hosted_library && !IsSharedLibrary(project)`.
The emitted diagnostic text says "`#host_export` requires a shared-library final
artifact". After that branch, non-shared libraries return the base link plan and
static library finalization proceeds only for projects without hosted exports.
The lowering context similarly distinguishes `shared_library_project` from
`hosted_library`, but the output pipeline prevents a static library with
`#host_export` from reaching archive finalization.

Expected behavior:

The public spec defines `LinkKind = {shared, static}`, defines both
`SharedLibrary(P)` and `StaticLibrary(P)`, and defines `HostedLibrary(P)` as
`Library(P) && HostExports(P) != []`. The hosted-export static rule rejects
only `!Library(P)` with `HostExport-Library-Err` / `E-SYS-3357`; it does not
require `SharedLibrary(P)`. Hosted session state, hosted calls, and hosted thunk
emission are all written over `HostedLibrary(P)`, with separate additional
image-state behavior only when `HostedLibrary(P) && SharedLibrary(P)`. A static
library with hosted exports should therefore be accepted and archived with the
hosted lifecycle/thunk symbols in its object set, or the specification must add
a shared-library-only restriction and diagnostic.

Impact:

A conforming static library assembly that contains a public valid
`#host_export` procedure is rejected during output planning after semantic
analysis. This narrows the accepted language below the spec and uses an
implementation-only diagnostic condition that is not represented by the
hosted-export diagnostic table.

HelloUltraviolet fixture gap:

HelloUltraviolet covers hosted exports only through `HostedExportLibrary`, whose
manifest uses `link_kind = "shared"`. It separately covers static-library
artifact output with `StaticLibrary` and `StaticLibraryArchive`, but there is no
fixture combining `#host_export` with `link_kind = "static"`.

### UV-AUDIT-0123: `#test` authority parameters are checked by spelling before alias normalization

Severity: Medium

Status: Locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:7222`
- `Docs\SPECIFICATION.md:7256`
- `Docs\SPECIFICATION.md:7266`
- `Docs\SPECIFICATION.md:7274`
- `Docs\SPECIFICATION.md:7352`
- `Docs\SPECIFICATION.md:10481`
- `Docs\SPECIFICATION.md:10483`
- `Docs\SPECIFICATION.md:10508`
- `Docs\SPECIFICATION.md:10548`
- `Docs\SPECIFICATION.md:13774`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\procedure_decl.cpp:220`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\procedure_decl.cpp:226`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\procedure_decl.cpp:229`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\procedure_decl.cpp:247`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\procedure_decl.cpp:248`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\procedure_decl.cpp:1988`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\procedure_decl.cpp:2451`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\type_wf.cpp:157`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\type_wf.cpp:400`
- `HelloUltraviolet\Fixtures\RejectedSource\Attributes\TestInvalidAuthorityParameter\Source\Main.uv:3`
- `HelloUltraviolet\Fixtures\RejectedSource\Attributes\TestInvalidAuthorityParameter\Source\Main.uv:4`
- `HelloUltraviolet\Fixtures\RejectedSource\Attributes\TestInvalidAuthorityParameter\Expected.uv:3`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\RejectedSource\Attributes.uv:155`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\RejectedSource\Attributes.uv:170`

Observed behavior:

`ValidateTestProcedureShape` runs directly over the procedure AST and calls
`IsBareTestAuthorityType` for one-parameter `#test` procedures. That helper
accepts only a `TypePathType` whose path is exactly `["TestAuthority"]` and has
no generic arguments. A procedure parameter whose type is an alias of
`TestAuthority`, or a using alias bound to the same type entity, is therefore
classified as an invalid authority parameter and rejected with `E-TST-0105`
before alias normalization can prove that the parameter has the required type.

Expected behavior:

The `#test` procedure rule requires either no parameter or exactly one
parameter whose type is the toolchain-provided `TestAuthority` type. Type alias
semantics define `AliasNorm`, `AliasTransparent(T, U)`, and state that type
aliases introduce no distinct runtime values. Other capability-sensitive type
rules, such as `ContextBundleType`, are written over `AliasNorm(T)`. The
authority-parameter check should therefore accept a parameter whose normalized
type is `TestAuthority`, while still rejecting genuinely different parameter
types with `E-TST-0105`.

Impact:

Valid source-native tests can be rejected solely because they use a transparent
alias for the runner authority type. This narrows the accepted source language,
makes `req.TestAuthority` inconsistent with the alias model, and gives users an
unnecessary spelling constraint not stated by the test attribute specification.

HelloUltraviolet fixture gap:

The existing invalid-authority fixture covers only a plain wrong type
(`value: i32`) and expects `E-TST-0105`. There is no accepted source-native test
fixture where the authority parameter is written through a type alias or a using
alias for `TestAuthority`.

### UV-AUDIT-0124: `Outcome` propagation rejects aliased return types before `OutcomeSig`

Severity: Medium

Status: Locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:10481`
- `Docs\SPECIFICATION.md:10483`
- `Docs\SPECIFICATION.md:10508`
- `Docs\SPECIFICATION.md:10548`
- `Docs\SPECIFICATION.md:10884`
- `Docs\SPECIFICATION.md:17288`
- `Docs\SPECIFICATION.md:17289`
- `Docs\SPECIFICATION.md:17290`
- `Docs\SPECIFICATION.md:17291`
- `Docs\SPECIFICATION.md:17292`
- `Docs\SPECIFICATION.md:17300`
- `Docs\SPECIFICATION.md:17301`
- `Docs\SPECIFICATION.md:17302`
- `Docs\SPECIFICATION.md:17303`
- `Docs\SPECIFICATION.md:17304`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\typing\outcome.cpp:87`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\outcome.cpp:97`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\outcome.cpp:104`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\outcome.cpp:108`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\propagate_expr.cpp:107`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\propagate_expr.cpp:165`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\propagate_expr.cpp:181`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\propagate_expr.cpp:199`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\propagate_expr.cpp:203`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\propagate_expr.cpp:220`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\propagate_expr.cpp:38`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\propagate_expr.cpp:296`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\propagate_expr.cpp:306`
- `HelloUltraviolet\Fixtures\AcceptedProjects\ExpressionSemantics\Source\Library.uv:194`
- `HelloUltraviolet\Fixtures\AcceptedProjects\ExpressionSemantics\Source\Library.uv:199`
- `HelloUltraviolet\Fixtures\AcceptedProjects\ExpressionSemantics\Source\Library.uv:204`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\AcceptedProjects\Coverage.uv:436`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\AcceptedProjects\Coverage.uv:451`

Observed behavior:

The propagation type checker normalizes aliases on the source expression type
with `NormalizeAliasTopLevel` before checking `OutcomeSigOf(source_type)`.
However, the non-async `Outcome` propagation branch derives the propagated
error target by calling `OutcomeSigOf(type_ctx.return_type)` directly. The
`OutcomeSigOf` helper only recognizes syntactic `Outcome` type paths,
applications, or modal-state types after permission stripping; it has no scope
parameter and cannot expand type aliases. A procedure whose declared return type
is a transparent alias of `Outcome<TValue, TError>` therefore fails to enter
`T-Propagate-Outcome`, even when the propagated expression has a valid
`Outcome` type and the error type is a subtype of the aliased return's error
member.

Expected behavior:

The spec defines `OutcomeSig(T)` exactly by `AliasNorm(T) =
TypeApply(["Outcome"], [TValue, TError])`, and the `T-Propagate-Outcome` and
`T-Async-Try-Outcome` rules consume `OutcomeSig` for the source and return
types. A transparent alias of `Outcome<TValue, TError>` must therefore behave
like the underlying `Outcome` type for propagation typing. The code generation
path already resolves aliases for both the expression type and the procedure
return type before lowering, so the typing phase is the inconsistent rejection
point.

Impact:

Spec-valid procedures that name their `Outcome` return through a type alias can
reject at the `?` expression despite having the same normalized type as an
accepted spelling. This breaks alias transparency for a core control-flow form
and prevents users from using local API aliases around effectful result types.

HelloUltraviolet fixture gap:

Accepted expression fixtures cover direct union propagation and direct
`Outcome` propagation obligations, and the catalog records `SuccessMember` and
`SuccessMemberAsync` coverage. No accepted or rejected fixture covers `?` inside
a procedure whose return type is an alias of `Outcome<TValue, TError>`.

### UV-AUDIT-0125: Async `chain` rejects callback return aliases before `AsyncSig`

Severity: Medium

Status: Locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:10481`
- `Docs\SPECIFICATION.md:10508`
- `Docs\SPECIFICATION.md:22944`
- `Docs\SPECIFICATION.md:22945`
- `Docs\SPECIFICATION.md:22953`
- `Docs\SPECIFICATION.md:22957`
- `Docs\SPECIFICATION.md:23836`
- `Docs\SPECIFICATION.md:23841`
- `Docs\SPECIFICATION.md:23871`
- `Docs\SPECIFICATION.md:23875`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\method_call.cpp:115`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\method_call.cpp:1040`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\method_call.cpp:1276`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\method_call.cpp:1568`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\method_call.cpp:1586`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\method_call.cpp:1613`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\type_refs.cpp:1644`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\type_refs.cpp:1681`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\type_refs.cpp:1682`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\type_refs.cpp:1722`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\signature.cpp:455`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\signature.cpp:470`
- `HelloUltraviolet\Fixtures\ArtifactProjects\AsyncObligations\Source\CombinatorRuntimeForms.uv:64`
- `HelloUltraviolet\Fixtures\ArtifactProjects\AsyncObligations\Source\CombinatorRuntimeForms.uv:66`
- `HelloUltraviolet\Fixtures\ArtifactProjects\AsyncObligations\Source\CombinatorRuntimeForms.uv:150`
- `HelloUltraviolet\Fixtures\ArtifactProjects\AsyncObligations\Source\CombinatorRuntimeForms.uv:152`
- `HelloUltraviolet\Fixtures\ArtifactProjects\AsyncObligations\Source\CombinatorRuntimeForms.uv:160`
- `HelloUltraviolet\Fixtures\ArtifactProjects\AsyncObligations\Source\CombinatorRuntimeForms.uv:162`
- `HelloUltraviolet\Fixtures\AcceptedProjects\ExpressionSemantics\Source\Library.uv:297`
- `HelloUltraviolet\Fixtures\AcceptedProjects\ExpressionSemantics\Source\Library.uv:313`

Observed behavior:

The async method-call path correctly recognizes the receiver type with
`AsyncSigOf(ctx, lookup_base)`, which expands transparent aliases before
checking the built-in async combinator surface. In the `chain` branch, however,
the callback is reduced to `CallableSigOf(fn_typed.type)` and the callback
return is then checked with `GetAsyncSig(fn_sig->ret)`. `CallableSigOf` returns
the function or closure return type as stored in the callable signature, and
`BuildProcedureSignature` stores `LowerTypeWithWF(ctx, return_type_opt)` as
`result.return_type`; that preserves an alias spelling rather than replacing it
with the alias body. `GetAsyncSig` has no scope parameter and recognizes only
syntactic `Async` and built-in async alias paths. Therefore a valid callback
whose return type is a transparent user alias of `Future<U, E>` or
`Async<(), (), U, E>` is rejected with the generic chain shape error before the
alias-aware `AsyncSigOf` logic can classify it.

Expected behavior:

The spec defines `AsyncSig(T)` by `AliasNorm(T) = TypeApply(["Async"], args)`.
The async combinator section states that built-in modal member lookup on
`Async`, including aliases normalized through `AsyncSig`, governs combinator
typing, and `chain` is specified as accepting a callback returning
`Future<U, E>`. A transparent alias whose normalized type has that future
signature should therefore satisfy the `chain` callback return requirement just
as the direct `Future` or `Async` spelling does.

Impact:

This narrows the accepted async language around one of the core composition
operators. Users can define API-level aliases for futures and use them in
ordinary async returns, but the same alias becomes unusable as the return type
of a `chain` continuation, producing an implementation-only spelling
restriction that is not present in the specification.

HelloUltraviolet fixture gap:

Existing async composition artifacts cover direct `chain` continuations that
return `Async<(), (), i32, bool>` and accepted expression coverage also uses the
direct async spelling. No accepted fixture covers `chain` with a continuation
whose declared return type is a user-defined alias of `Future<U, E>` or
`Async<(), (), U, E>`, and there is no rejected fixture that documents the
current alias rejection as an intentional diagnostic.

### UV-AUDIT-0126: Async composition lowering drops alias-aware operand signatures

Severity: High

Status: Locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:22953`
- `Docs\SPECIFICATION.md:22954`
- `Docs\SPECIFICATION.md:22957`
- `Docs\SPECIFICATION.md:23721`
- `Docs\SPECIFICATION.md:23731`
- `Docs\SPECIFICATION.md:23801`
- `Docs\SPECIFICATION.md:24353`
- `Docs\SPECIFICATION.md:24360`
- `Docs\SPECIFICATION.md:24374`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\all_expr.cpp:85`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\all_expr.cpp:86`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\all_expr.cpp:87`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\all_expr.cpp:100`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\all_expr.cpp:102`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\all_expr.cpp:121`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\all_expr.cpp:123`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\race_expr.cpp:137`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\race_expr.cpp:138`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\race_expr.cpp:139`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\race_expr.cpp:140`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\race_expr.cpp:142`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\race_expr.cpp:158`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\race_expr.cpp:159`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\race_expr.cpp:164`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\race_expr.cpp:252`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\race_expr.cpp:261`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\race_expr.cpp:263`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\race_expr.cpp:293`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\all.cpp:55`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\all.cpp:56`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\all.cpp:58`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\all.cpp:59`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\all.cpp:64`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\race_return.cpp:74`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\race_return.cpp:75`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\race_return.cpp:77`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\race_return.cpp:78`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\race_return.cpp:83`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\race_yield.cpp:20`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\race_yield.cpp:27`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\race_yield.cpp:52`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\race_yield.cpp:256`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\race_yield.cpp:260`
- `HelloUltraviolet\Fixtures\ArtifactProjects\AsyncObligations\Source\CompositionForms.uv:217`
- `HelloUltraviolet\Fixtures\ArtifactProjects\AsyncObligations\Source\CompositionForms.uv:218`
- `HelloUltraviolet\Fixtures\ArtifactProjects\AsyncObligations\Source\CompositionForms.uv:251`
- `HelloUltraviolet\Fixtures\ArtifactProjects\AsyncObligations\Source\CompositionForms.uv:252`
- `HelloUltraviolet\Fixtures\ArtifactProjects\AsyncObligations\Source\CompositionForms.uv:286`
- `HelloUltraviolet\Fixtures\ArtifactProjects\AsyncObligations\Source\CompositionForms.uv:287`
- `HelloUltraviolet\Fixtures\AcceptedProjects\ExpressionSemantics\Source\Library.uv:275`
- `HelloUltraviolet\Fixtures\AcceptedProjects\ExpressionSemantics\Source\Library.uv:276`
- `HelloUltraviolet\Fixtures\AcceptedProjects\ExpressionSemantics\Source\Library.uv:282`
- `HelloUltraviolet\Fixtures\AcceptedProjects\ExpressionSemantics\Source\Library.uv:283`
- `HelloUltraviolet\Fixtures\AcceptedProjects\ExpressionSemantics\Source\Library.uv:289`
- `HelloUltraviolet\Fixtures\AcceptedProjects\ExpressionSemantics\Source\Library.uv:291`

Observed behavior:

The type checker accepts async composition operands through the alias-aware
`AsyncSigOf(ctx, type)` path. Lowering for `all` and `race`, however, asks the
expression-type callback for each operand type and extracts the signature with
`GetAsyncSig(async_type)`, which has no scope and only recognizes syntactic
`Async` plus the built-in async aliases. For operands whose accepted type is a
transparent user alias of an async type, `LowerAllExpr` fails to collect result
and error members, leaves `all.tuple_type` unset, and registers the result as
unit. `LowerRaceExpr` similarly fails to register the pattern input type and
omits async error members from the race result or stream type. The LLVM lowering
for `IRAll`, `IRRaceReturn`, and `IRRaceYield` repeats the syntactic
`GetAsyncSig` extraction for stored operand types, so the backend does not
recover the alias-aware signature metadata later.

Expected behavior:

`AsyncSig(T)` is specified through `AliasNorm(T) = TypeApply(["Async"], args)`.
The `T-All`, `T-Race`, and `T-Race-Stream` rules derive tuple members, pattern
types, and error unions from `AsyncSig(T_i)`, and the lowering rules must
preserve those typed composition semantics. Accepted operands whose types are
transparent aliases of async types must therefore lower exactly as their
normalized async bodies do.

Impact:

Valid async composition can type check and then lower with missing result,
pattern, or error metadata. For `all`, this can register the composed value as
unit instead of the required tuple-or-error union. For `race`, it can lower
handler binding and stream/error result construction without the operand
signature data required by the accepted source program.

HelloUltraviolet fixture gap:

The async composition artifact fixtures cover direct `Async`, `Future`, and
`Stream` operand spellings for `all`, return-mode `race`, and streaming `race`.
Accepted expression fixtures also use direct async spellings. No fixture covers
the same composition forms where the operand expression type is a user-defined
alias of the async type accepted by the front end.

### UV-AUDIT-0127: Lexical security skips sensitive Unicode inside unterminated quoted recovery spans

Severity: Medium

Status: Locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:1792`
- `Docs\SPECIFICATION.md:1793`
- `Docs\SPECIFICATION.md:2383`
- `Docs\SPECIFICATION.md:2384`
- `Docs\SPECIFICATION.md:2414`
- `Docs\SPECIFICATION.md:2415`
- `Docs\SPECIFICATION.md:2523`
- `Docs\SPECIFICATION.md:2524`
- `Docs\SPECIFICATION.md:2525`
- `Docs\SPECIFICATION.md:2553`
- `Docs\SPECIFICATION.md:2554`
- `Docs\SPECIFICATION.md:2620`
- `Docs\SPECIFICATION.md:2621`
- `Docs\SPECIFICATION.md:2625`
- `Docs\SPECIFICATION.md:2626`
- `Docs\SPECIFICATION.md:2685`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\02_source\lexer\lexer_literals.cpp:968`
- `Bootstrap\Ultraviolet\src\02_source\lexer\lexer_literals.cpp:988`
- `Bootstrap\Ultraviolet\src\02_source\lexer\lexer_literals.cpp:1008`
- `Bootstrap\Ultraviolet\src\02_source\lexer\lexer_literals.cpp:1029`
- `Bootstrap\Ultraviolet\src\02_source\lexer\lexer_security.cpp:248`
- `Bootstrap\Ultraviolet\src\02_source\lexer\lexer_security.cpp:271`
- `Bootstrap\Ultraviolet\src\02_source\lexer\lexer_security.cpp:273`
- `Bootstrap\Ultraviolet\src\02_source\lexer\lexer_security.cpp:279`
- `Bootstrap\Ultraviolet\src\02_source\lexer\lexer_security.cpp:281`
- `Bootstrap\Ultraviolet\src\02_source\lexer\lexer_security.cpp:312`
- `Bootstrap\Ultraviolet\src\02_source\lexer\tokenize.cpp:765`
- `HelloUltraviolet\Fixtures\RejectedSource\SourceText\UnterminatedString\Source\Main.uv:4`
- `HelloUltraviolet\Fixtures\RejectedSource\SourceText\UnterminatedString\Expected.uv:3`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\RejectedSource\SourceText.uv:35`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\RejectedSource\SourceText.uv:39`
- `HelloUltraviolet\Source\Audit\Catalog\SourceTextAndLexicalStructure\LexicalAnalysis.uv:1037`

Observed behavior:

`LexSensitivePos` excludes characters by rescanning comments and quoted forms.
For a string or character quote, it skips the scanned span when
`lit.ok || lit.next > i`. `ScanStringLiteral` and `ScanCharLiteral` set
`ok = false` for unterminated input but still advance `next` to the line-feed or
EOF recovery position. As a result, a sensitive scalar between the opening quote
and the unterminated-literal recovery point is suppressed from the sensitive
position list. `TokenizeWithDiagnostics` then runs `LexSecure` over that
recomputed list, so the required `E-SRC-0308` is never produced for that
position.

Expected behavior:

The spec excludes sensitive scalars only when `InsideLiteralOrComment(i)` is
true. `InsideLiteralOrComment` depends on `StringRange` and `CharRange`, which
are built from successful `StringLiteral` and `CharLiteral` parses, while
unterminated quoted text follows explicit recovery rules. A sensitive scalar
inside an unterminated quoted span is therefore outside a successful literal
range and must remain visible to `LexSecure-Err` unless covered by an unsafe
span.

Impact:

Lexically sensitive Unicode can be hidden by placing it inside an unterminated
string or character recovery span. That weakens the source-level security pass
and makes the diagnostic set depend on a recovery artifact rather than the
specification's successful literal/comment ranges.

HelloUltraviolet fixture gap:

The current `UnterminatedString` rejected fixture covers only ASCII
unterminated text and expects `E-SRC-0301`. The lexical-security catalog records
`LexSecure-Err` separately, but no fixture combines unterminated quoted recovery
with a sensitive Unicode scalar that should still require `E-SRC-0308`.

### UV-AUDIT-0128: Raw-dylib thunk calls use source-mode ABI classification

Severity: High

Status: Locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:26228`
- `Docs\SPECIFICATION.md:26361`
- `Docs\SPECIFICATION.md:26454`
- `Docs\SPECIFICATION.md:26473`
- `Docs\SPECIFICATION.md:26476`
- `Docs\SPECIFICATION.md:26482`
- `Docs\SPECIFICATION.md:26483`
- `Docs\SPECIFICATION.md:27991`
- `Docs\SPECIFICATION.md:27992`
- `Docs\SPECIFICATION.md:27994`
- `Docs\SPECIFICATION.md:28024`
- `Docs\SPECIFICATION.md:28029`
- `Docs\SPECIFICATION.md:28032`
- `Docs\SPECIFICATION.md:28034`

Implementation anchors:

- `Bootstrap\Ultraviolet\include\05_codegen\llvm\llvm_call.h:186`
- `Bootstrap\Ultraviolet\include\05_codegen\llvm\llvm_call.h:192`
- `Bootstrap\Ultraviolet\include\05_codegen\llvm\llvm_call.h:193`
- `Bootstrap\Ultraviolet\include\05_codegen\llvm\llvm_call.h:199`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\module_emit.cpp:392`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\module_emit.cpp:396`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\module_emit.cpp:397`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\module_emit.cpp:789`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\module_emit.cpp:796`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\module_emit.cpp:797`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\llvm_call.cpp:1317`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\llvm_call.cpp:1321`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\llvm_call.cpp:1323`
- `HelloUltraviolet\Fixtures\ArtifactProjects\RawDylibLibrary\Source\Main.uv:3`
- `HelloUltraviolet\Fixtures\ArtifactProjects\RawDylibLibrary\Source\Main.uv:5`
- `HelloUltraviolet\Fixtures\ArtifactProjects\RawDylibLibrary\Source\Main.uv:6`
- `HelloUltraviolet\Source\Audit\Catalog\ForeignFunctionInterface\FfiAttributes.uv:96`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\ArtifactProjects\Coverage.uv:15`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\ArtifactProjects\Coverage.uv:19`

Observed behavior:

Normal extern declarations are emitted with `ComputeCallABI(...,
use_c_abi_aggregate_sret = true, foreign_boundary_mode_independent = true)`,
which applies the foreign-visible ABI classification. The raw-dylib thunk
resolves the DLL symbol and then calls the resolved pointer through
`EmitABICall` with `use_c_abi_aggregate_sret = false`,
`ffi_import_boundary = false`, and no foreign-boundary override. `EmitABICall`
forwards `ffi_import_boundary || foreign_boundary_mode_independent` into
`ComputeCallABI`, so the thunk's call into the resolved DLL entry point is
classified as an ordinary source-mode call rather than a foreign ABI call.

Expected behavior:

A raw-dylib import resolves a Windows DLL entry point, and the call from the
generated thunk to that resolved symbol crosses the FFI boundary. Its
parameters and return must therefore be classified with `ForeignABIParam` and
`ForeignABICall`, independent of source parameter modes, with the same aggregate
carrier and indirect-return decisions used for foreign-visible boundaries.

Impact:

Raw-dylib imports whose signatures contain aggregates can lower a wrapper call
whose ABI does not match the actual DLL entry point. In particular, a parameter
or return that should be passed by value at the foreign boundary can be
classified as a source by-reference parameter, or vice versa, causing
call-frame and data-layout mismatches.

HelloUltraviolet fixture gap:

The raw-dylib artifact fixture imports only `GetLastError() -> u32`. That
exercises symbol resolution but not aggregate parameters, aggregate returns,
source-mode versus foreign-mode parameter classification, or C aggregate
carrier handling inside the raw-dylib thunk.

### UV-AUDIT-0129: Modal implementations skip class method obligations

Severity: High

Status: Locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:10713`
- `Docs\SPECIFICATION.md:13078`
- `Docs\SPECIFICATION.md:13083`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\modal_decl.cpp:915`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\modal_decl.cpp:931`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\modal_decl.cpp:948`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\modal_decl.cpp:949`
- `HelloUltraviolet\Source\Reference\Polymorphism\Implementations.uv:39`
- `HelloUltraviolet\Source\Reference\Polymorphism\Implementations.uv:46`

Observed behavior:

The modal declaration checker iterates implemented classes and validates class
existence, orphan ownership, and required class fields. If the implemented class
is not a modal class, it immediately continues without building
`ClassMethodTable`, without checking abstract methods, and without checking
default-method overrides. Records do run the method-table implementation
checks, and enums reject missing abstract methods, but modal implementations of
ordinary classes can accept a class whose required method set is unsatisfied.

Expected behavior:

`WF-Impl` applies to every implementing type named by `Implements(T)`. A modal
declaration must satisfy `ImplementsOk` before it establishes `T <: Cl`, so
every class method in `ClassMethodTable(Cl)` must be implemented, overridden, or
accepted through a concrete class default just as it is for records and enums.

Impact:

A modal can be accepted as implementing a class while omitting required methods.
That makes subtype evidence available without the method bodies that the class
contract requires, and it can defer the failure to later method lookup or
dynamic-dispatch surfaces instead of rejecting the declaration.

HelloUltraviolet fixture gap:

`ImplementationState` currently implements only an empty marker class. The
polymorphism reference surface therefore proves marker implementation syntax
for modals but does not force a modal to satisfy an ordinary class with an
abstract method or a concrete default override rule.

### UV-AUDIT-0130: Enum and modal implementations skip associated-type binding obligations

Severity: High

Status: Locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:13013`
- `Docs\SPECIFICATION.md:13163`
- `Docs\SPECIFICATION.md:13173`
- `Docs\SPECIFICATION.md:13177`
- `Docs\SPECIFICATION.md:13198`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\record_decl.cpp:233`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\record_decl.cpp:258`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\record_decl.cpp:750`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\enum_decl.cpp:397`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\enum_decl.cpp:423`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\modal_decl.cpp:915`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\modal_decl.cpp:931`
- `HelloUltraviolet\Fixtures\RejectedSource\Polymorphism\AssociatedTypeMissingBinding\Expected.uv:3`
- `HelloUltraviolet\Source\Reference\Polymorphism\Implementations.uv:41`
- `HelloUltraviolet\Source\Reference\Polymorphism\Implementations.uv:46`

Observed behavior:

Record implementations call `BuildClassAssociatedTypeBindings` for each
implemented class and reject missing abstract associated-type bindings with
`Impl-AssocType-Missing`. Enum implementation checking validates class
existence, orphan ownership, modal-class misuse, required fields, and required
methods with concrete defaults, but never inspects class associated-type items.
Modal implementation checking likewise validates class existence, orphan
ownership, fields, and modal states, but never checks associated-type bindings.

Expected behavior:

The specification requires every implementation of a class with an abstract
associated type to bind that associated type, with class defaults used only when
present. Because enum and modal declarations can name implemented classes, they
must either provide valid associated-type evidence or be rejected when the class
requires evidence that their declaration shape cannot provide.

Impact:

Enums and modals can be accepted as implementing classes whose associated-type
requirements are unbound. Any later method signature, dynamic dispatch, or
generic bound that relies on the associated type is then working from an
incomplete implementation relation.

HelloUltraviolet fixture gap:

The rejected associated-type fixtures cover record implementations. The enum and
modal implementation reference specimens implement only `ImplementationMarker`,
so the test surface does not cover abstract associated-type requirements on
enum or modal implementation declarations.

### UV-AUDIT-0131: Class default method calls do not substitute associated-type bindings

Severity: Medium

Status: Locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:13163`
- `Docs\SPECIFICATION.md:13173`
- `Docs\SPECIFICATION.md:13177`
- `Docs\SPECIFICATION.md:14542`
- `Docs\SPECIFICATION.md:14544`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\method_call.cpp:2474`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\method_call.cpp:2475`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\method_call.cpp:2497`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\method_call.cpp:2572`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\method_call.cpp:2577`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\type_lower.cpp:282`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\signature.cpp:289`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\record_decl.cpp:750`
- `HelloUltraviolet\Source\Reference\Polymorphism\AssociatedTypes.uv:3`
- `HelloUltraviolet\Source\Reference\Polymorphism\AssociatedTypes.uv:7`
- `HelloUltraviolet\Source\Reference\Polymorphism\AssociatedTypes.uv:21`

Observed behavior:

Method-call typing switches to a declaration-local lowering function only when
the resolved method is a concrete record method. For a class default method,
`method_lower_type` remains the ambient type lowerer. That lowerer preserves a
path such as `Self::Item` as a raw `TypePath`. The associated-type substitution
logic exists in `SubstSelfType` and is used while checking record
implementations, but the static call path for default methods does not build or
apply the implementing type's associated-type map before lowering the default
method parameters or return type.

Expected behavior:

Associated-type lookup for an implementing type must first use the
implementation binding and then the class default. A class default method
selected by `LookupMethod(T, name)` still belongs to the implementation
relation for concrete `T`, so any `Self::Assoc` in the default method signature
must be elaborated with that concrete associated-type binding before call
typing reports argument and result types.

Impact:

Default methods whose signatures mention associated types can produce unresolved
`Self::Assoc` path types at call sites or fail compatibility checks even though
the implementing type has a valid associated-type binding. This makes associated
types work for implementation checking while remaining incomplete for the
ordinary method-call surface.

HelloUltraviolet fixture gap:

The associated-type reference uses default methods that return concrete `i32`
types. It does not exercise a class default method whose parameter or return
type is `Self::Item`, so the call-site substitution path is not covered.

### UV-AUDIT-0132: Hosted export aggregate thunks use Win64-only aggregate carriers

Severity: High

Status: Locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:26296`
- `Docs\SPECIFICATION.md:26299`
- `Docs\SPECIFICATION.md:26303`
- `Docs\SPECIFICATION.md:26308`
- `Docs\SPECIFICATION.md:26353`
- `Docs\SPECIFICATION.md:27994`
- `Docs\SPECIFICATION.md:28034`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\hosted_emit.cpp:1865`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\hosted_emit.cpp:1870`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\hosted_emit.cpp:1939`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\module_emit.cpp:396`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\llvm_call.cpp:2502`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\llvm_call.cpp:2503`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\llvm_call.cpp:2628`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\llvm_call.cpp:2714`
- `HelloUltraviolet\Fixtures\AcceptedProjects\HostedExportLibrary\Source\Library.uv:39`
- `HelloUltraviolet\Fixtures\AcceptedProjects\HostedExportLibrary\Source\Library.uv:48`
- `HelloUltraviolet\Fixtures\AcceptedProjects\HostedExportLibrary\Source\Library.uv:60`
- `HelloUltraviolet\Fixtures\AcceptedProjects\HostedExportLibrary\Source\Library.uv:69`
- `HelloUltraviolet\Fixtures\AcceptedProjects\HostedExportLibrary\Source\Library.uv:82`
- `HelloUltraviolet\Source\Audit\AcceptedProjectExecution.uv:2525`
- `HelloUltraviolet\Source\Audit\AcceptedProjectExecution.uv:2531`

Observed behavior:

Hosted export thunk emission prepends the hosted session handle and computes the
foreign-visible thunk ABI with `foreign_boundary_mode_independent = true`, but
sets `use_c_abi_aggregate_sret` only when the target profile is `X86_64Win64`.
`ComputeCallABI` uses that flag for both aggregate return boundary handling and
foreign C aggregate parameter carriers. Extern imports force the same flag for
all targets, and raw ABI-bearing procedures also force it through the
symbol-based ABI path.

Expected behavior:

Hosted export thunk lowering must use `HostThunkParamShape(proc)` and
`HostThunkRetShape(proc)`, which are defined from `ForeignABICall` for the
thunk's foreign-visible signature. The thunk's stack layout, register
assignment, indirect return slot, and aggregate carriers must therefore match
the corresponding raw exported procedure signature on every target, not only on
Win64.

Impact:

Hosted exports with aggregate visible parameters or aggregate returns can expose
ABI-incompatible thunks on SysV, AAPCS64, or Darwin targets. Foreign callers can
pass or receive the aggregate according to the platform C ABI while the emitted
hosted thunk expects a direct LLVM aggregate shape.

HelloUltraviolet fixture gap:

`HostedExportLibrary` includes aggregate hosted exports and the audit harness
checks emitted hosted carrier markers, but it does not call those hosted exports
from a foreign host using the platform aggregate ABI. The current macOS release
verification exercises the hosted reference increment path, not aggregate
parameters or returns.

### UV-AUDIT-0133: Hosted session destroy stops deinit after the first panic

Severity: High

Status: Locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:28579`
- `Docs\SPECIFICATION.md:28583`
- `Docs\SPECIFICATION.md:28701`
- `Docs\SPECIFICATION.md:28971`
- `Docs\SPECIFICATION.md:28974`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\hosted_emit.cpp:1357`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\hosted_emit.cpp:1364`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\hosted_emit.cpp:1368`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\hosted_emit.cpp:1373`
- `HelloUltraviolet\Source\Audit\AcceptedProjectExecution.uv:2731`
- `HelloUltraviolet\Source\Audit\AcceptedProjectExecution.uv:2794`

Observed behavior:

`__ultraviolet_host_session_destroy` walks module deinit functions in reverse
init order. After each deinit call it branches to `host.destroy.deinit.fail` if
the hosted panic record is set. That block records `had_panic` and branches
directly to `host.destroy.deinit.done`, so no earlier modules in the remaining
reverse-order list are deinitialized after the first deinit panic.

Expected behavior:

`HostSessionDestroySigma` is defined through `Deinit(P)`, and `Deinit-Panic`
uses `Cleanup(DeinitList(P), sigma)`. The cleanup rule for a panicking static
drop preserves the panic result while continuing with the remaining cleanup
list. Hosted session destruction must therefore continue deinitializing the
remaining modules while preserving the panic status.

Impact:

One panicking module deinit can prevent all earlier module deinit actions in the
hosted session from running. Static destructor effects, resource release, and
host-owned teardown invariants can be skipped even though the abstract cleanup
relation requires the rest of the list to run.

HelloUltraviolet fixture gap:

The hosted lifecycle audit checks lifecycle conformance markers, but there is
no hosted-session fixture with multiple initialized modules where a later
deinit panics and an earlier deinit sentinel must still run.

### UV-AUDIT-0134: Shared-library constructor rewrites init panic into `ForeignPre`

Severity: Medium

Status: Locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:28653`
- `Docs\SPECIFICATION.md:28658`
- `Docs\SPECIFICATION.md:28760`
- `Docs\SPECIFICATION.md:28767`
- `Docs\SPECIFICATION.md:28778`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\entry_emit.cpp:1688`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\entry_emit.cpp:1702`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\entry_emit.cpp:1718`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\entry_emit.cpp:1885`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\entry_emit.cpp:1901`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\entry_emit.cpp:1905`
- `HelloUltraviolet\Fixtures\ArtifactProjects\SharedLibraryImageLifecycle\Source\SharedLibraryImageLifecycleProvider\Library.uv:33`
- `HelloUltraviolet\Source\Audit\ArtifactProjectExecution.uv:2075`
- `HelloUltraviolet\Source\Audit\ArtifactProjectExecution.uv:2204`

Observed behavior:

The shared-library loader attach path calls module init procedures with a panic
out slot. On init failure it clears the image panic record, deinitializes
successfully initialized earlier modules, marks the image as unattached, and
returns `0`. The ELF/Mach-O constructor wrapper treats that `0` as constructor
failure and calls the runtime panic function with `PanicReason::ForeignPre`.

Expected behavior:

Module initialization panic is owned by `InitPanicHandle(m)`, which lowers to
`LowerPanic(InitPanic(m))`. A shared-library attach failure caused by module
initialization must preserve or re-raise the init-panic outcome rather than
substituting a foreign-precondition panic reason.

Impact:

On constructor-array targets, a shared-library static initialization failure is
reported with the wrong lifecycle panic code. Tooling and runtime handlers
cannot distinguish a module init panic from a foreign precondition panic at that
boundary.

HelloUltraviolet fixture gap:

The shared-library image lifecycle fixture validates successful load and unload
sentinels. It does not force static initialization to panic and assert the
constructor-emitted panic code.

### UV-AUDIT-0135: Rejected-source fixtures miss character-literal diagnostics

Severity: Low

Status: Locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:2414`
- `Docs\SPECIFICATION.md:2424`
- `Docs\SPECIFICATION.md:2625`
- `Docs\SPECIFICATION.md:2681`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\02_source\lexer\lexer_literals.cpp:1011`
- `Bootstrap\Ultraviolet\src\02_source\lexer\lexer_literals.cpp:1013`
- `Bootstrap\Ultraviolet\src\02_source\lexer\lexer_literals.cpp:1027`
- `Bootstrap\Ultraviolet\src\02_source\lexer\lexer_literals.cpp:1048`
- `Bootstrap\Ultraviolet\src\02_source\lexer\tokenize.cpp:664`
- `Bootstrap\Ultraviolet\src\02_source\lexer\tokenize.cpp:666`
- `Bootstrap\Ultraviolet\src\00_core\generated\diag_registry.inc:322`
- `Bootstrap\Ultraviolet\src\00_core\generated\diag_registry.inc:1109`
- `Bootstrap\Ultraviolet\src\00_core\generated\diag_registry.inc:1110`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\RejectedSource\SourceText.uv:35`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\RejectedSource\SourceText.uv:70`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\RejectedSource\ExpectedFiles.uv:32`

Observed behavior:

The lexer has concrete emission paths for `Lex-Char-Unterminated` and
`Lex-Char-Invalid`, both mapped to `E-SRC-0303`. The rejected-source
SourceText fixture catalog covers unterminated strings, invalid escapes,
numeric errors, block comments, BOM handling, prohibited controls, and invalid
UTF-8, but it has no fixture whose expected file asserts `E-SRC-0303`.
`InvalidStringEscape` records `Lex-Char-BadEscape` under `E-SRC-0302`; it does
not cover the character-literal diagnostic code.

Expected behavior:

The HelloUltraviolet rejected-source surface should include character-literal
fixtures that force `Lex-Char-Unterminated` and `Lex-Char-Invalid`, with
expected metadata for `E-SRC-0303`.

Impact:

Regressions in character-literal diagnostic selection or recovery can pass the
rejected-source fixture suite even though the compiler has distinct specified
diagnostic paths for those failures.

### UV-AUDIT-0136: Rejected-source fixtures miss lexical security and max-munch diagnostics

Severity: Low

Status: Locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:2448`
- `Docs\SPECIFICATION.md:2508`
- `Docs\SPECIFICATION.md:2553`
- `Docs\SPECIFICATION.md:2567`
- `Docs\SPECIFICATION.md:2572`
- `Docs\SPECIFICATION.md:2684`
- `Docs\SPECIFICATION.md:2685`
- `Docs\SPECIFICATION.md:2686`
- `Docs\SPECIFICATION.md:2687`
- `Docs\SPECIFICATION.md:2688`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\02_source\lexer\lexer_ident.cpp:151`
- `Bootstrap\Ultraviolet\src\02_source\lexer\lexer_ident.cpp:153`
- `Bootstrap\Ultraviolet\src\02_source\lexer\lexer_security.cpp:299`
- `Bootstrap\Ultraviolet\src\02_source\lexer\lexer_security.cpp:312`
- `Bootstrap\Ultraviolet\src\02_source\lexer\lexer_security.cpp:353`
- `Bootstrap\Ultraviolet\src\02_source\lexer\lexer_security.cpp:366`
- `Bootstrap\Ultraviolet\src\02_source\lexer\lexer.cpp:474`
- `Bootstrap\Ultraviolet\src\02_source\lexer\lexer.cpp:546`
- `Bootstrap\Ultraviolet\src\02_source\lexer\tokenize.cpp:690`
- `Bootstrap\Ultraviolet\src\02_source\lexer\tokenize.cpp:794`
- `Bootstrap\Ultraviolet\src\00_core\generated\diag_registry.inc:325`
- `Bootstrap\Ultraviolet\src\00_core\generated\diag_registry.inc:326`
- `Bootstrap\Ultraviolet\src\00_core\generated\diag_registry.inc:327`
- `Bootstrap\Ultraviolet\src\00_core\generated\diag_registry.inc:328`
- `Bootstrap\Ultraviolet\src\00_core\generated\diag_registry.inc:329`
- `HelloUltraviolet\Source\Audit\Catalog\SourceTextAndLexicalStructure\LexicalAnalysis.uv:857`
- `HelloUltraviolet\Source\Audit\Catalog\SourceTextAndLexicalStructure\LexicalAnalysis.uv:974`
- `HelloUltraviolet\Source\Audit\Catalog\SourceTextAndLexicalStructure\LexicalAnalysis.uv:1037`
- `HelloUltraviolet\Source\Audit\Catalog\SourceTextAndLexicalStructure\LexicalAnalysis.uv:1064`
- `HelloUltraviolet\Source\Audit\Catalog\SourceTextAndLexicalStructure\LexicalAnalysis.uv:1073`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\RejectedSource\SourceText.uv:35`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\RejectedSource\ExpectedFiles.uv:32`

Observed behavior:

The compiler has concrete emission paths for invalid Unicode inside
identifiers, sensitive Unicode outside unsafe spans, max-munch failure,
confusable identifiers, and mixed-script identifiers. The source-text audit
catalog records these obligations, but the rejected-source fixture catalog has
no expected entries for `E-SRC-0307`, `E-SRC-0308`, `E-SRC-0309`,
`E-SRC-0310`, or `E-SRC-0311`.

Expected behavior:

The HelloUltraviolet rejected-source surface should include fixtures that force
each specified lexical-security, identifier, and max-munch diagnostic and assert
the emitted diagnostic code plus obligation identifier.

Impact:

Regressions in source lexical security, invalid identifier Unicode, and
unclassifiable token handling can pass rejected-source fixture coverage. This is
separate from `UV-AUDIT-0127`, which covers one specific implementation bug in
unterminated quoted recovery spans.

### UV-AUDIT-0137: `Tracked` registration never preserves pending async state

Severity: High

Status: Agent-reported and locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:10839`
- `Docs\SPECIFICATION.md:10840`
- `Docs\SPECIFICATION.md:10845`
- `Docs\SPECIFICATION.md:10854`
- `Docs\SPECIFICATION.md:10858`
- `Docs\SPECIFICATION.md:13687`
- `Docs\SPECIFICATION.md:23220`
- `Docs\SPECIFICATION.md:23235`
- `Docs\SPECIFICATION.md:23240`
- `Docs\SPECIFICATION.md:23241`
- `Docs\SPECIFICATION.md:23268`
- `Docs\SPECIFICATION.md:23270`
- `Docs\SPECIFICATION.md:23271`
- `Docs\SPECIFICATION.md:24181`

Implementation anchors:

- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:2058`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:2154`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:2163`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:2175`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:2206`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:2213`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:2221`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:2225`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:2232`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:2239`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:2240`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\wait.cpp:46`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\wait.cpp:58`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\wait.cpp:59`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\wait.cpp:70`
- `HelloUltraviolet\Source\Reference\Async\SuspensionForms.uv:31`
- `HelloUltraviolet\Source\Reference\Async\SuspensionForms.uv:32`
- `HelloUltraviolet\Source\Reference\Async\SuspensionForms.uv:73`
- `HelloUltraviolet\Source\Reference\Async\SuspensionForms.uv:76`
- `HelloUltraviolet\Source\Reference\Async\SuspensionForms.uv:77`
- `HelloUltraviolet\Fixtures\ArtifactProjects\AsyncObligations\Source\Main.uv:361`
- `HelloUltraviolet\Fixtures\ArtifactProjects\AsyncObligations\Source\Main.uv:363`
- `HelloUltraviolet\Fixtures\ArtifactProjects\AsyncObligations\Source\Main.uv:364`
- `HelloUltraviolet\Fixtures\ArtifactProjects\RuntimeInterfaceSymbols\Source\Library.uv:430`
- `HelloUltraviolet\Fixtures\ArtifactProjects\RuntimeInterfaceSymbols\Source\Library.uv:431`
- `HelloUltraviolet\Fixtures\ArtifactProjects\RuntimeInterfaceSymbols\Source\Library.uv:465`
- `HelloUltraviolet\Fixtures\ArtifactProjects\RuntimeInterfaceSymbols\Source\Library.uv:466`

Observed behavior:

`Reactor::register` creates its returned handle by calling `uv_spawn_create`
with a null body. That path sets the spawn handle state to ready immediately.
The register hook then tries to resolve the future synchronously through
`uv_reactor_resolve_future`; completed and failed futures write ready payloads,
but any still-suspended future writes an immediate zeroed payload and returns
the already-ready handle. `wait` lowering classifies `Tracked` handles
separately for conformance tracing, but still calls the same runtime spawn-wait
hook.

Expected behavior:

`Reactor::register<T, E>` should be able to return a
`Tracked<T, E>@Pending` handle. Waiting on that pending handle should block or
advance scheduler progress until the handle transitions to `@Ready`, including
futures suspended on `until`, key release, cancellation, or other reactor-driven
events.

Impact:

Pending reactor work can be observed as an immediate ready value carrying
default data instead of a pending tracked handle. This breaks event-driven
async semantics and can hide failures in workflows that depend on external
resumption.

Fixture gap:

The existing HelloUltraviolet references and artifact projects register futures
that complete during bounded local stepping. They do not register a future that
remains pending until an external event and then verify the `Tracked@Pending`
state before `wait`.

### UV-AUDIT-0138: Region state types use full Region modal layout

Severity: High

Status: Agent-reported and locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:10951`
- `Docs\SPECIFICATION.md:10990`
- `Docs\SPECIFICATION.md:10995`
- `Docs\SPECIFICATION.md:10999`
- `Docs\SPECIFICATION.md:11000`
- `Docs\SPECIFICATION.md:11004`
- `Docs\SPECIFICATION.md:29678`
- `Docs\SPECIFICATION.md:29679`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\layout\layout_dispatch.cpp:1037`
- `Bootstrap\Ultraviolet\src\04_analysis\layout\layout_dispatch.cpp:1038`
- `Bootstrap\Ultraviolet\src\04_analysis\layout\layout_dispatch.cpp:1041`
- `Bootstrap\Ultraviolet\src\04_analysis\layout\layout_dispatch.cpp:1043`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\llvm_types.cpp:1516`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\llvm_types.cpp:1518`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\llvm_types.cpp:1541`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\llvm_types.cpp:1543`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\llvm_types.cpp:1583`
- `Bootstrap\Ultraviolet\src\04_analysis\modal\builtin_modal_intrinsics.cpp:317`
- `Bootstrap\Ultraviolet\src\04_analysis\modal\builtin_modal_intrinsics.cpp:401`
- `Bootstrap\Ultraviolet\src\04_analysis\modal\builtin_modal_intrinsics.cpp:403`
- `Bootstrap\Ultraviolet\src\04_analysis\memory\region_type.cpp:207`
- `Bootstrap\Ultraviolet\src\04_analysis\memory\region_type.cpp:228`
- `Bootstrap\Ultraviolet\src\04_analysis\memory\region_type.cpp:232`
- `Bootstrap\Ultraviolet\src\04_analysis\memory\region_type.cpp:237`
- `Bootstrap\Ultraviolet\src\04_analysis\memory\region_type.cpp:242`

Observed behavior:

`Region` is not treated as a runtime-handle modal, so `TypeModalState` layout
continues through the normal state-type path. Before applying state-specific
payload layout, both layout analysis and LLVM type emission call
`LookupBuiltinModalLayout(Region)` and return the full tagged Region modal
shape. The state-specific branch that would synthesize the record payload is
therefore bypassed. The built-in Region declaration gives each state a single
`usize handle` field, so `Region@Active`, `Region@Frozen`, and `Region@Freed`
should not contain the full modal discriminant layout.

Expected behavior:

State-specific modal values must use `ModalPayload(modal_ref, S)` record layout
and LLVM lowering. The full general modal layout belongs to `ModalRefType` for
the modal, not to `TypeModalState(modal_ref, S)`.

Impact:

`Region@Active`, `Region@Frozen`, and `Region@Freed` locals, parameters,
returns, and temporaries can get the wrong ABI shape, size, alignment, and
field offsets. This also makes layout traces report the right rule name while
returning a value shape that does not match the state-type rule.

Fixture gap:

HelloUltraviolet references Region states, but does not assert the emitted
LLVM type or layout for `Region@Active`, `Region@Frozen`, or `Region@Freed`:

- `HelloUltraviolet\Source\Reference\Authority\RegionsAndFrames.uv:66`
- `HelloUltraviolet\Source\Reference\Authority\RegionsAndFrames.uv:73`
- `HelloUltraviolet\Fixtures\ArtifactProjects\RuntimeInterfaceSymbols\Source\Library.uv:371`
- `HelloUltraviolet\Fixtures\ArtifactProjects\RuntimeInterfaceSymbols\Source\Library.uv:375`
- `HelloUltraviolet\Fixtures\ArtifactProjects\RuntimeInterfaceSymbols\Source\Library.uv:376`
- `HelloUltraviolet\Fixtures\ArtifactProjects\RuntimeInterfaceSymbols\Source\Library.uv:377`
- `HelloUltraviolet\Fixtures\ArtifactProjects\RuntimeInterfaceSymbols\Source\Library.uv:378`

### UV-AUDIT-0139: Empty ordinary `assembly` arrays are classified as empty assembly lists

Severity: Medium

Status: Agent-reported and locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:746`
- `Docs\SPECIFICATION.md:747`
- `Docs\SPECIFICATION.md:748`
- `Docs\SPECIFICATION.md:749`
- `Docs\SPECIFICATION.md:764`
- `Docs\SPECIFICATION.md:768`
- `Docs\SPECIFICATION.md:769`
- `Docs\SPECIFICATION.md:774`
- `Docs\SPECIFICATION.md:778`
- `Docs\SPECIFICATION.md:779`
- `Docs\SPECIFICATION.md:1016`
- `Docs\SPECIFICATION.md:1017`
- `Docs\SPECIFICATION.md:1730`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\01_project\project_validate.cpp:436`
- `Bootstrap\Ultraviolet\src\01_project\project_validate.cpp:438`
- `Bootstrap\Ultraviolet\src\01_project\project_validate.cpp:442`
- `Bootstrap\Ultraviolet\src\01_project\project_validate.cpp:450`
- `Bootstrap\Ultraviolet\src\01_project\project_validate.cpp:461`
- `Bootstrap\Ultraviolet\src\01_project\project_validate.cpp:463`
- `Bootstrap\Ultraviolet\src\01_project\project_validate.cpp:464`
- `Bootstrap\Ultraviolet\src\01_project\project_validate.cpp:608`
- `Bootstrap\Ultraviolet\src\01_project\project_validate.cpp:610`
- `Bootstrap\Ultraviolet\src\01_project\project_validate.cpp:611`
- `Bootstrap\Ultraviolet\src\01_project\project_validate.cpp:615`
- `Bootstrap\Ultraviolet\src\01_project\project_validate.cpp:617`

Observed behavior:

`assembly = []` is an ordinary TOML array, not a table or array of tables.
`AsmTables` nevertheless accepts that form when the array is empty, returns
success with zero assembly tables, and lets the later count check emit the
empty-list diagnostic.

Expected behavior:

An ordinary array assigned to `assembly` must produce `AsmTables(T) = bottom`
and fail through `WF-Assembly-Table-Err`. Only a table or array of tables can
advance to the assembly-count rule.

Impact:

Malformed manifests with the wrong `assembly` shape are reported as empty
assembly lists instead of invalid assembly table form. That changes the
specified first-failure diagnostic and can make conformance traces point at the
wrong rule.

Fixture gap:

The manifest diagnostic fixtures cover other invalid manifest field types, but
not an empty ordinary `assembly` array:

- `HelloUltraviolet\Fixtures\OutputDiagnostics\Projects\ManifestRequiredTypesInvalid\Ultraviolet.toml:1`
- `HelloUltraviolet\Fixtures\OutputDiagnostics\Projects\ManifestOutDirTypeInvalid\Ultraviolet.toml:1`

### UV-AUDIT-0140: Expression-scoped `#dynamic` call preconditions are accepted but not lowered

Severity: High

Status: Agent-reported and locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:7124`
- `Docs\SPECIFICATION.md:7132`
- `Docs\SPECIFICATION.md:7135`
- `Docs\SPECIFICATION.md:7210`
- `Docs\SPECIFICATION.md:7214`
- `Docs\SPECIFICATION.md:15078`
- `Docs\SPECIFICATION.md:15082`
- `Docs\SPECIFICATION.md:15097`
- `Docs\SPECIFICATION.md:15108`
- `Docs\SPECIFICATION.md:15110`
- `Docs\SPECIFICATION.md:15271`
- `Docs\SPECIFICATION.md:15272`
- `Docs\SPECIFICATION.md:15274`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\typing\type_expr.cpp:532`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\type_expr.cpp:3643`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\type_expr.cpp:3644`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\expr_common.cpp:185`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\expr_common.cpp:186`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\expr_common.cpp:205`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\expr_common.cpp:207`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\expr_common.cpp:208`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\call.cpp:366`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\call.cpp:370`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\call.cpp:1961`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\call.cpp:2346`

Observed behavior:

Typing honors an attributed expression by recomputing the inner expression's
dynamic contract context. That allows an otherwise unproved callee precondition
inside `#dynamic callee(...)` to pass static checking. Lowering of
`AttributedExpr` only preserves memory-order state around the inner expression;
it does not enable `ctx.dynamic_checks`. The local precondition check helper
therefore returns before emitting a `ContractCheck`, even at call-lowering sites
that invoke it.

Expected behavior:

An expression-local `#dynamic` scope must lower to the same runtime contract
verification selected during typing. If a call precondition cannot be proven in
that expression scope, lowering must insert the required `ContractCheck(...,
Pre, ...)`.

Impact:

`uvc` can accept a non-dynamic procedure containing an expression-scoped dynamic
call whose precondition is not statically proven, then emit the call without the
runtime precondition check required by the accepted source semantics.

Fixture gap:

HelloUltraviolet has declaration-level dynamic precondition coverage and a
rejected non-dynamic caller, but no fixture for expression-scoped `#dynamic`
around a call expression in a non-dynamic procedure:

- `HelloUltraviolet\Source\Reference\Procedures\Preconditions.uv:25`
- `HelloUltraviolet\Source\Reference\Procedures\Preconditions.uv:26`
- `HelloUltraviolet\Fixtures\RejectedSource\Procedures\UnsatisfiedPrecondition\Source\Main.uv:10`
- `HelloUltraviolet\Fixtures\RejectedSource\Procedures\ContractDynamicAttribute\Source\Main.uv:4`

### UV-AUDIT-0141: Static foreign postcondition facts are not imported for `var` bindings

Severity: Medium

Status: Agent-reported and locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:15131`
- `Docs\SPECIFICATION.md:15132`
- `Docs\SPECIFICATION.md:15136`
- `Docs\SPECIFICATION.md:15138`
- `Docs\SPECIFICATION.md:26816`
- `Docs\SPECIFICATION.md:26817`
- `Docs\SPECIFICATION.md:26819`
- `Docs\SPECIFICATION.md:26827`
- `Docs\SPECIFICATION.md:26828`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\stmt_common.cpp:1470`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\stmt_common.cpp:1678`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\stmt_common.cpp:1719`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\stmt_common.cpp:1729`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\stmt_common.cpp:1779`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\stmt_common.cpp:1781`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\stmt_common.cpp:1796`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\stmt_common.cpp:1800`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\stmt_common.cpp:1813`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\stmt_common.cpp:1816`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\stmt_common.cpp:1817`

Observed behavior:

`LetBindingProofContextForStmt` handles both `let` and `var` initializer
bindings and imports native call postcondition facts for either form by wrapping
the binding in a temporary `LetStmt`. Static foreign postconditions, however,
are imported only when the original statement is a `let`. A `var` initialized
from a successful `#static` foreign call receives no downstream proof facts.

Expected behavior:

A successful statically resolved foreign call in `#static` mode must make its
postconditions available as assumptions for later proofs on the normal-return
path. That applies to the initialized binding regardless of whether the
binding keyword is `let` or `var`; later mutation can invalidate the fact.

Impact:

The compiler can reject valid downstream proofs after a `var` binding
initialized by a static foreign call while accepting the equivalent `let`
binding.

Fixture gap:

Existing static foreign postcondition coverage uses `let result`, not
`var result`:

- `HelloUltraviolet\Fixtures\ArtifactProjects\EmitLlLibrary\Source\Library.uv:780`
- `HelloUltraviolet\Fixtures\ArtifactProjects\EmitLlLibrary\Source\Library.uv:800`
- `HelloUltraviolet\Source\Reference\FFI\ForeignContracts.uv:7`
- `HelloUltraviolet\Source\Reference\FFI\ForeignContracts.uv:58`
- `HelloUltraviolet\Source\Reference\FFI\ForeignContracts.uv:61`

### UV-AUDIT-0142: `@entry(...)` entry-scope validation admits path references

Severity: High

Status: Agent-reported and locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:14778`
- `Docs\SPECIFICATION.md:14780`
- `Docs\SPECIFICATION.md:14939`
- `Docs\SPECIFICATION.md:14943`
- `Docs\SPECIFICATION.md:14946`
- `Docs\SPECIFICATION.md:14949`
- `Docs\SPECIFICATION.md:14984`
- `Docs\SPECIFICATION.md:15376`
- `Docs\SPECIFICATION.md:15377`
- `Docs\SPECIFICATION.md:15383`
- `Docs\SPECIFICATION.md:15385`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\contract_entry.cpp:395`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\contract_entry.cpp:400`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\contract_entry.cpp:404`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\contract_entry.cpp:424`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\contract_entry.cpp:432`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\contract_entry.cpp:470`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\contract_entry.cpp:498`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\contract_entry.cpp:507`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\contract_entry.cpp:513`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\contract_entry.cpp:518`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\contract_entry.cpp:528`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\path.cpp:142`

Observed behavior:

`ExprUsesOnlyEntryEnvBindings` rejects plain identifiers absent from the entry
environment and rejects `@result`. For unhandled non-recursive forms it returns
true. `PathExpr` is not rejected by that validator, so a qualified module or
static path can pass the entry-scope check and then be typed normally.

Expected behavior:

`@entry(expr)` may reference only procedure parameters and the receiver. Module
paths, static bindings, and other non-entry bindings must be rejected before the
entry expression is typed for capture.

Impact:

Contracts can capture module or static state through `@entry(...)`, widening
the postcondition environment beyond the entry state permitted by the
specification.

Fixture gap:

HelloUltraviolet covers `@entry` placement, non-bitcopy captures, side effects,
capability operations, and moved-parameter references, but not `@entry` over a
module/static path:

- `HelloUltraviolet\Fixtures\RejectedSource\Procedures\ContractEntryOutsidePostcondition\Source\Main.uv:4`
- `HelloUltraviolet\Fixtures\RejectedSource\Procedures\ContractEntryNonBitcopy\Source\Main.uv:4`
- `HelloUltraviolet\Fixtures\RejectedSource\Procedures\ContractEntrySideEffect\Source\Main.uv:4`
- `HelloUltraviolet\Fixtures\RejectedSource\Procedures\ContractEntryCapability\Source\Main.uv:4`
- `HelloUltraviolet\Fixtures\RejectedSource\Procedures\ContractEntryMovedParameter\Source\Main.uv:4`
- `HelloUltraviolet\Source\Reference\Modules\Statics.uv:5`

### UV-AUDIT-0143: Partial field moves never collapse to `Moved`

Severity: Medium

Status: Agent-reported and locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:3931`
- `Docs\SPECIFICATION.md:3936`
- `Docs\SPECIFICATION.md:3940`
- `Docs\SPECIFICATION.md:3941`
- `Docs\SPECIFICATION.md:4035`
- `Docs\SPECIFICATION.md:4036`
- `Docs\SPECIFICATION.md:4038`
- `Docs\SPECIFICATION.md:4039`
- `Docs\SPECIFICATION.md:4045`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\memory\borrow_bind.cpp:1541`
- `Bootstrap\Ultraviolet\src\04_analysis\memory\borrow_bind.cpp:1542`
- `Bootstrap\Ultraviolet\src\04_analysis\memory\borrow_bind.cpp:1544`
- `Bootstrap\Ultraviolet\src\04_analysis\memory\borrow_bind.cpp:2162`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\expr_context.cpp:681`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\expr_context.cpp:688`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\expr_context.cpp:689`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\expr_context.cpp:690`
- `Bootstrap\Ultraviolet\src\05_codegen\cleanup\cleanup.cpp:2033`

Observed behavior:

The semantic binding-state helper `PM` and lowering's `MarkFieldMoved` only
accumulate moved field names. Neither compares the moved-field set with
`AllFields(TypeOf(x))`, so a value whose every field has been moved remains
`PartiallyMoved(F)` instead of becoming `Moved`. Cleanup classification then
continues to use the partial-move path whenever the field set is non-empty.

Expected behavior:

After the moved field set equals all fields of the binding type,
`Trans-Partial-To-Moved` requires the binding state to become `Moved`.

Impact:

Binding-state diagnostics and drop/cleanup behavior can treat a fully moved
aggregate as partially moved. That can produce the wrong access state and the
wrong cleanup path for values whose fields have all been consumed.

Fixture gap:

HelloUltraviolet maps the abstract binding-runtime-state reference surface but
does not include a concrete `uvc` fixture that moves every field of a record:

- `HelloUltraviolet\Source\Reference\Authority\BindingRuntimeState.uv:30`
- `HelloUltraviolet\Source\Audit\Catalog\AbstractMachineObjectsResponsibilityAndAuthority\BindingAndPermissionRuntimeState.uv:347`

### UV-AUDIT-0144: Subplace assignment resets an unrelated partial move

Severity: High

Status: Agent-reported and locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:3945`
- `Docs\SPECIFICATION.md:3946`
- `Docs\SPECIFICATION.md:3948`
- `Docs\SPECIFICATION.md:4035`
- `Docs\SPECIFICATION.md:4036`
- `Docs\SPECIFICATION.md:4045`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\memory\borrow_bind.cpp:3415`
- `Bootstrap\Ultraviolet\src\04_analysis\memory\borrow_bind.cpp:3437`
- `Bootstrap\Ultraviolet\src\04_analysis\memory\borrow_bind.cpp:3442`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\expr_common.cpp:1214`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\expr_common.cpp:1228`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\field_access.cpp:95`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\field_access.cpp:110`

Observed behavior:

After an assignment statement whose place has a mutable root, semantic
binding-state checking sets the root binding state to `Valid` regardless of the
specific subplace assigned. Lowering's field-assignment updater instead removes
only the assigned field from `moved_fields`, so semantic checking and lowering
disagree. More importantly, assigning `x.b` can restore access to a previously
moved `x.a`.

Expected behavior:

Only reassignment of the root binding `x` restores `x` under `Trans-Reassign`.
Assigning a subplace must not restore unrelated moved fields of the same root.

Impact:

`uvc --check` can accept later access to a still-moved field after an unrelated
field assignment, breaking the specified partial-move access rule.

Fixture gap:

HelloUltraviolet has assignment diagnostics and abstract partial-move catalog
coverage, but no rejected-source fixture for moving one field, assigning a
different field, and then using the moved field:

- `HelloUltraviolet\Source\Audit\Catalog\StatementsAndBlocks\AssignmentStatements.uv:128`
- `HelloUltraviolet\Source\Reference\Authority\BindingRuntimeState.uv:30`

### UV-AUDIT-0145: Region provenance is applied to multi-name destructuring

Severity: High

Status: Agent-reported and locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:19413`
- `Docs\SPECIFICATION.md:19414`
- `Docs\SPECIFICATION.md:19418`
- `Docs\SPECIFICATION.md:19419`
- `Docs\SPECIFICATION.md:19423`
- `Docs\SPECIFICATION.md:19424`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\provenance\prov_stmt.cpp:604`
- `Bootstrap\Ultraviolet\src\04_analysis\provenance\prov_stmt.cpp:619`
- `Bootstrap\Ultraviolet\src\04_analysis\provenance\prov_stmt.cpp:622`
- `Bootstrap\Ultraviolet\src\04_analysis\provenance\prov_stmt.cpp:624`
- `Bootstrap\Ultraviolet\src\04_analysis\provenance\prov_stmt.cpp:642`
- `Bootstrap\Ultraviolet\src\04_analysis\provenance\prov_stmt.cpp:643`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\let_stmt.cpp:172`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\let_stmt.cpp:195`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\let_stmt.cpp:198`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\let_stmt.cpp:380`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\let_stmt.cpp:388`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\var_stmt.cpp:171`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\var_stmt.cpp:194`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\var_stmt.cpp:197`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\var_stmt.cpp:383`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\var_stmt.cpp:391`
- `Bootstrap\Ultraviolet\src\04_analysis\memory\regions.cpp:2786`
- `Bootstrap\Ultraviolet\src\04_analysis\memory\regions.cpp:2801`

Observed behavior:

Fresh Region creation is guarded by `names.size() == 1`, but rebinding an
initializer whose provenance is already `Region` does not require a single
pattern name. `TrackBindingProvenance` returns `ProvenanceKind::Region` for the
whole binding, and `ApplyBindingMetadata` applies that provenance seed to every
name collected from the destructuring pattern. The provenance environment path
also introduces all pattern names under the bound provenance.

Expected behavior:

Region alias and fresh-region binding provenance are specified only for
`PatNames(pat) = [x]`. Ordinary multi-name bindings must not introduce multiple
Region aliases from one destructuring pattern unless the specification is
extended.

Impact:

Destructuring can create multiple bindings with Region provenance that the
specification never introduces. That weakens provenance and escape reasoning by
giving unrelated destructured names the same Region authority.

Fixture gap:

HelloUltraviolet covers tuple/record destructuring with non-Region values and
single-name Region bindings, but no fixture destructures a value carrying Region
provenance into multiple names:

- `HelloUltraviolet\Source\Reference\Statements\Bindings.uv:36`
- `HelloUltraviolet\Source\Reference\Statements\Bindings.uv:73`
- `HelloUltraviolet\Source\Reference\Patterns\TupleRecordPatterns.uv:19`
- `HelloUltraviolet\Source\Reference\Authority\RegionsAndFrames.uv:66`

### UV-AUDIT-0146: `yield from` failed-state lowering bypasses async failure cleanup

Severity: High

Status: Agent-reported and locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:23340`
- `Docs\SPECIFICATION.md:23365`
- `Docs\SPECIFICATION.md:23369`
- `Docs\SPECIFICATION.md:24525`
- `Docs\SPECIFICATION.md:24544`
- `Docs\SPECIFICATION.md:24593`
- `Docs\SPECIFICATION.md:24596`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\yield_from_expr.cpp:71`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\yield_from_expr.cpp:79`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\yield_from.cpp:683`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\yield_from.cpp:695`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\yield_from.cpp:700`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\yield_from.cpp:780`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\yield_from.cpp:1000`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\yield_from.cpp:1002`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\yield_from.cpp:1014`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\yield_from.cpp:1024`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\propagate_expr.cpp:340`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\propagate_expr.cpp:344`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\propagate_expr.cpp:348`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\propagate_expr.cpp:490`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\propagate_expr.cpp:494`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\propagate_expr.cpp:528`

Observed behavior:

`IRYieldFrom` captures source, release, result, and result type data, but not a
cleanup plan for exiting the current async frame. The failed branch in the LLVM
emitter materializes the outer `@Failed` state and calls `emit_async_return`
without running current-frame deferred blocks or dropping live bindings. The
ordinary propagation lowering path computes `ComputeCleanupPlanToFunctionRoot`
and sequences `EmitCleanup` before the async failure return.

Expected behavior:

The specification defines failed `yield from` as propagation out of the current
async frame. Async failure lowering must therefore perform the same function-root
cleanup obligations as ordinary propagation before materializing `@Failed`.

Impact:

Resources live across a failing `yield from` can miss destructor or deferred-block
effects. The compiler can record the async conformance marker while emitting a
failure path that does not satisfy the cleanup semantics.

Fixture gap:

HelloUltraviolet covers async and `yield from` typing paths, but it does not
cover an accepted program where the outer async frame owns a live drop or defer
sentinel across a delegated failure.

### UV-AUDIT-0147: Record methods without block bodies parse as empty methods

Severity: Medium

Status: Agent-reported and locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:3183`
- `Docs\SPECIFICATION.md:9748`
- `Docs\SPECIFICATION.md:14309`
- `Docs\SPECIFICATION.md:14311`
- `Docs\SPECIFICATION.md:16850`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\02_source\parser\item\record_decl.cpp:77`
- `Bootstrap\Ultraviolet\src\02_source\parser\item\record_decl.cpp:201`
- `Bootstrap\Ultraviolet\src\02_source\parser\item\record_decl.cpp:239`
- `Bootstrap\Ultraviolet\src\02_source\parser\item\record_decl.cpp:245`
- `Bootstrap\Ultraviolet\src\02_source\parser\item\record_decl.cpp:330`
- `Bootstrap\Ultraviolet\src\02_source\parser\item\record_decl.cpp:360`
- `Bootstrap\Ultraviolet\src\02_source\parser\stmt\stmt_common.cpp:617`

Observed behavior:

`ParseMethodDefAfterVis` skips newlines after the method signature and contracts,
then parses a body only when the next token is `{`. When no block follows, it
silently creates an empty block. The record member list then consumes the member
terminator, allowing a method declaration without a body to reach later phases as
an empty method body.

Expected behavior:

The `method_def` grammar requires a block expression. Missing `{` at the method
body position should be a source syntax failure, reported before type checking.

Impact:

Invalid unit-returning record methods can be accepted. Non-unit methods can
produce later body or type diagnostics instead of the required first source
diagnostic.

Fixture gap:

Existing rejected record-method fixtures cover other well-formedness failures,
but not a missing method body:

- `HelloUltraviolet\Fixtures\RejectedSource\Procedures\RecordMethodWellFormedness\Source\Main.uv:6`
- `HelloUltraviolet\Fixtures\RejectedSource\Procedures\RecordMethodBodyExplicitReturn\Source\Main.uv:6`

### UV-AUDIT-0148: Modal state members can abut without the required member terminator

Severity: Medium

Status: Agent-reported and locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:1936`
- `Docs\SPECIFICATION.md:1942`
- `Docs\SPECIFICATION.md:3067`
- `Docs\SPECIFICATION.md:3087`
- `Docs\SPECIFICATION.md:3183`
- `Docs\SPECIFICATION.md:10594`
- `Docs\SPECIFICATION.md:10595`
- `Docs\SPECIFICATION.md:11086`
- `Docs\SPECIFICATION.md:11094`
- `Docs\SPECIFICATION.md:11174`
- `Docs\SPECIFICATION.md:11180`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\02_source\parser\item\modal_decl.cpp:210`
- `Bootstrap\Ultraviolet\src\02_source\parser\item\modal_decl.cpp:239`
- `Bootstrap\Ultraviolet\src\02_source\parser\item\modal_decl.cpp:268`
- `Bootstrap\Ultraviolet\src\02_source\parser\item\modal_decl.cpp:270`
- `Bootstrap\Ultraviolet\src\02_source\parser\parser_terminator.cpp:108`
- `Bootstrap\Ultraviolet\src\02_source\parser\parser_terminator.cpp:117`

Observed behavior:

`ParseStateMemberList` parses a state member, assigns `cur = mem.parser`, skips
newlines, and immediately loops. It never requires a member terminator before
the next state member, so adjacent fields, methods, or transitions on the same
line can parse as separate members.

Expected behavior:

Before parsing another state member, the parser should require `;` or a newline
unless the next token is `}`. Missing separation should emit the source missing
terminator diagnostic as the first failure.

Impact:

Invalid modal declarations can reach the AST, allowing later phases to mask the
required source-level first failure.

Fixture gap:

HelloUltraviolet has statement missing-terminator coverage but no fixture for a
missing modal state-member separator:

- `HelloUltraviolet\Fixtures\RejectedSource\Statements\MissingRequiredTerminator\Source\Main.uv:14`

### UV-AUDIT-0149: `TypeApply` nominal arguments do not participate in class conformance

Severity: Medium

Status: Agent-reported and locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:12609`
- `Docs\SPECIFICATION.md:12632`
- `Docs\SPECIFICATION.md:13265`
- `Docs\SPECIFICATION.md:13842`
- `Docs\SPECIFICATION.md:16198`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\typing\type_lower.cpp:299`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\call.cpp:1743`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\method_call.cpp:1017`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\subtyping.cpp:1024`
- `Bootstrap\Ultraviolet\src\04_analysis\composite\classes.cpp:1068`
- `Bootstrap\Ultraviolet\src\04_analysis\composite\classes.cpp:1072`

Observed behavior:

Generic nominal syntax lowers to `TypeApply`. Class-bound and dynamic
conformance checks pass that applied type into `TypeImplementsClass`, but
`TypeImplementsClass` only recognizes `TypePathType` after the dynamic-type
special case. Applied generic record, enum, or modal implementers therefore fall
through as not implementing the class.

Expected behavior:

`TypeApply(path, args)` should use the nominal declaration at `path`, with the
applied arguments and bounds, when deciding class implementation.

Impact:

Valid generic implementers can fail class bounds, method bounds, or
dynamic-class conversions depending on representation.

Fixture gap:

Existing polymorphism reference coverage exercises non-generic implementers, but
not a generic implementer passed through class conformance:

- `HelloUltraviolet\Source\Reference\Polymorphism\GenericParameters.uv:44`
- `HelloUltraviolet\Source\Reference\Polymorphism\GenericParameters.uv:97`

### UV-AUDIT-0150: Dynamic method calls check only the selected method, not class-wide dispatchability

Severity: High

Status: Agent-reported and locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:13248`
- `Docs\SPECIFICATION.md:13265`
- `Docs\SPECIFICATION.md:13269`
- `Docs\SPECIFICATION.md:13270`
- `Docs\SPECIFICATION.md:13288`
- `Docs\SPECIFICATION.md:13331`
- `Docs\SPECIFICATION.md:13375`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\typing\type_wf.cpp:465`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\subtyping.cpp:1022`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\subtyping.cpp:1024`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\cast.cpp:60`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\cast.cpp:64`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\method_call.cpp:2152`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\method_call.cpp:2164`
- `Bootstrap\Ultraviolet\src\04_analysis\composite\classes.cpp:988`
- `Bootstrap\Ultraviolet\src\04_analysis\composite\classes.cpp:1000`

Observed behavior:

Casts to `$Class` check class-wide dispatchability, but annotations,
parameters, and subtype conversion paths can produce `TypeDynamic` without that
check. Dynamic method-call typing then rejects only when the selected method is
not vtable eligible, so it can accept a vtable-eligible method on a class whose
other effective methods make the class non-dispatchable.

Expected behavior:

Dynamic dispatch is permitted only for dispatchable classes. That requirement
applies regardless of which method is selected at a call site.

Impact:

The compiler can typecheck dynamic calls for classes that do not have a complete
dynamic dispatch contract, leaving lowering and runtime behavior inconsistent
with the source semantics.

Fixture gap:

Existing rejected fixtures cover dynamic construction or casts, but not an
eligible method call on a `$Class` parameter where another effective method makes
the class non-dispatchable:

- `HelloUltraviolet\Fixtures\RejectedSource\Polymorphism\DynamicNonDispatchableRequirement\Source\Main.uv:13`
- `HelloUltraviolet\Fixtures\RejectedSource\Polymorphism\DynamicGenericMethodNonDispatchable\Source\Main.uv:13`

### UV-AUDIT-0151: Compile-time logical operators eagerly evaluate skipped operands

Severity: High

Status: Agent-reported and locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:24921`
- `Docs\SPECIFICATION.md:16319`
- `Docs\SPECIFICATION.md:16324`
- `Docs\SPECIFICATION.md:16329`
- `Docs\SPECIFICATION.md:16334`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\03_comptime\eval.cpp:802`
- `Bootstrap\Ultraviolet\src\03_comptime\eval.cpp:803`
- `Bootstrap\Ultraviolet\src\03_comptime\eval.cpp:807`
- `Bootstrap\Ultraviolet\src\03_comptime\eval.cpp:816`
- `Bootstrap\Ultraviolet\src\03_comptime\eval.cpp:820`

Observed behavior:

Compile-time binary evaluation evaluates the left operand, then evaluates the
right operand unconditionally, and only then handles `&&` or `||`.

Expected behavior:

Compile-time evaluation must preserve ordinary logical-operator semantics.
`false && rhs` skips `rhs`, and `true || rhs` skips `rhs`.

Impact:

Compile-time diagnostics, emissions, or capability effects on a skipped right
operand can still execute, causing valid programs to fail or emit unintended
declarations.

Fixture gap:

Existing comptime coverage uses boolean chains, but not a skipped right operand
with an effect or diagnostic:

- `HelloUltraviolet\Source\Reference\Comptime\Reflection.uv:42`
- `HelloUltraviolet\Source\Reference\Comptime\CompileTimeForms.uv:128`
- `HelloUltraviolet\Fixtures\AcceptedProjects\ComptimeConformance\Source\Library.uv:94`

### UV-AUDIT-0152: Compile-time block and procedure execution skip assignment statements

Severity: High

Status: Agent-reported and locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:18984`
- `Docs\SPECIFICATION.md:18985`
- `Docs\SPECIFICATION.md:19534`
- `Docs\SPECIFICATION.md:19535`
- `Docs\SPECIFICATION.md:24921`
- `Docs\SPECIFICATION.md:24979`
- `Docs\SPECIFICATION.md:25677`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\03_comptime\eval.cpp:446`
- `Bootstrap\Ultraviolet\src\03_comptime\eval.cpp:610`
- `Bootstrap\Ultraviolet\src\03_comptime\eval.cpp:615`
- `Bootstrap\Ultraviolet\src\03_comptime\eval.cpp:632`
- `Bootstrap\Ultraviolet\src\03_comptime\eval.cpp:649`
- `Bootstrap\Ultraviolet\src\03_comptime\eval.cpp:660`
- `Bootstrap\Ultraviolet\src\03_comptime\eval.cpp:669`
- `Bootstrap\Ultraviolet\src\03_comptime\rewrite.cpp:486`
- `Bootstrap\Ultraviolet\src\03_comptime\rewrite.cpp:491`
- `Bootstrap\Ultraviolet\src\03_comptime\rewrite.cpp:527`

Observed behavior:

`EvalBlock` handles `let`, `var`, expression statements, returns, and tail
expressions. It has no execution branch for assignment or compound-assignment
statements, so local mutation in a compile-time block or compile-time procedure
body is ignored. `EvalCall` executes compile-time procedure bodies through the
same block evaluator.

Expected behavior:

Compile-time execution uses ordinary statement semantics for assignment and
compound assignment where the target is a compile-time local.

Impact:

Compile-time local mutation can be silently skipped. Loops or procedures that
depend on mutation can produce stale values while the source appears accepted.

Fixture gap:

Existing comptime examples exercise assignments produced by comptime loop
expansion, but not assignment inside an evaluated compile-time block or
procedure body:

- `HelloUltraviolet\Source\Reference\Comptime\CompileTimeForms.uv:209`
- `HelloUltraviolet\Source\Reference\Comptime\CompileTimeForms.uv:212`
- `HelloUltraviolet\Source\Reference\Comptime\CompileTimeForms.uv:215`
- `HelloUltraviolet\Source\Reference\Comptime\CompileTimeForms.uv:221`
- `HelloUltraviolet\Source\Reference\Comptime\CompileTimeForms.uv:228`

### UV-AUDIT-0153: Compile-time binary operator support is narrower than ordinary binary semantics

Severity: Medium

Status: Agent-reported and locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:24921`
- `Docs\SPECIFICATION.md:16229`
- `Docs\SPECIFICATION.md:16234`
- `Docs\SPECIFICATION.md:16239`
- `Docs\SPECIFICATION.md:16244`
- `Docs\SPECIFICATION.md:16249`
- `Docs\SPECIFICATION.md:16254`
- `Docs\SPECIFICATION.md:16336`
- `Docs\SPECIFICATION.md:16337`
- `Docs\SPECIFICATION.md:16339`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\03_comptime\eval.cpp:802`
- `Bootstrap\Ultraviolet\src\03_comptime\eval.cpp:816`
- `Bootstrap\Ultraviolet\src\03_comptime\eval.cpp:820`
- `Bootstrap\Ultraviolet\src\03_comptime\eval.cpp:839`
- `Bootstrap\Ultraviolet\src\03_comptime\eval.cpp:856`
- `Bootstrap\Ultraviolet\src\03_comptime\eval.cpp:873`
- `Bootstrap\Ultraviolet\src\03_comptime\eval.cpp:877`
- `Bootstrap\Ultraviolet\src\03_comptime\eval.cpp:881`
- `Bootstrap\Ultraviolet\src\03_comptime\eval.cpp:889`
- `Bootstrap\Ultraviolet\src\03_comptime\eval.cpp:901`

Observed behavior:

Compile-time binary evaluation supports boolean equality and logical operators,
string equality, enum equality, integer addition, integer multiplication,
integer equality, and integer ordering. It omits several ordinary binary
operators, including integer subtraction, division, remainder, exponentiation,
bitwise operators, shifts, and float arithmetic and comparisons.

Expected behavior:

Compile-time evaluation of ordinary expressions must match ordinary binary
operator semantics for values that are representable as compile-time values.

Impact:

Valid compile-time expressions can fail to evaluate or fail to literalize even
though their ordinary operator semantics are specified.

Fixture gap:

The comptime reference coverage exercises addition, but not the omitted operator
families:

- `HelloUltraviolet\Source\Reference\Comptime\CompileTimeForms.uv:233`

### UV-AUDIT-0154: Generic compile-time procedure calls ignore explicit type arguments

Severity: Medium

Status: Agent-reported and locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:24730`
- `Docs\SPECIFICATION.md:12524`
- `Docs\SPECIFICATION.md:12596`
- `Docs\SPECIFICATION.md:12628`
- `Docs\SPECIFICATION.md:12632`
- `Docs\SPECIFICATION.md:15937`
- `Docs\SPECIFICATION.md:15939`
- `Docs\SPECIFICATION.md:16114`
- `Docs\SPECIFICATION.md:24921`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\02_source\parser\expr\call_type_args.cpp:100`
- `Bootstrap\Ultraviolet\src\02_source\parser\expr\call_type_args.cpp:126`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\resolve_expr.cpp:1152`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\resolve_expr.cpp:1178`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\resolve_expr.cpp:1179`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\call.cpp:3739`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\call.cpp:3742`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\call.cpp:3806`
- `Bootstrap\Ultraviolet\src\03_comptime\eval.cpp:395`
- `Bootstrap\Ultraviolet\src\03_comptime\eval.cpp:400`
- `Bootstrap\Ultraviolet\src\03_comptime\eval.cpp:415`
- `Bootstrap\Ultraviolet\src\03_comptime\eval.cpp:446`

Observed behavior:

The parser builds `CallTypeArgs`, the resolver rewrites it to `CallExpr` with
`generic_args`, and typing validates and substitutes the call. Phase 2 compile-
time evaluation then accepts only an identifier callee and value arguments,
looks up the original compile-time procedure, binds value parameters, and
executes the original body. It does not read `call.generic_args` or the generic
substitution recorded by typing.

Expected behavior:

Explicit or inferred type-argument calls elaborate to a monomorphic call after
substitution before compile-time evaluation. The compile-time evaluator should
therefore execute the substituted body and signature, or an equivalent
monomorphic instantiation.

Impact:

Type-dependent generic compile-time procedures can evaluate with unresolved type
parameters or wrong quoted type values.

Fixture gap:

Existing comptime generic procedure coverage uses inferred type arguments and a
value-dependent body, but not explicit type arguments or a type-dependent body:

- `HelloUltraviolet\Source\Reference\Comptime\CompileTimeForms.uv:28`
- `HelloUltraviolet\Source\Reference\Comptime\CompileTimeForms.uv:236`

### UV-AUDIT-0155: Compile-time string values use raw literal spelling instead of decoded bytes

Severity: Medium

Status: Locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:2360`
- `Docs\SPECIFICATION.md:2363`
- `Docs\SPECIFICATION.md:2368`
- `Docs\SPECIFICATION.md:15576`
- `Docs\SPECIFICATION.md:24921`
- `Docs\SPECIFICATION.md:25014`
- `Docs\SPECIFICATION.md:30344`
- `Docs\SPECIFICATION.md:30345`
- `Docs\SPECIFICATION.md:30350`
- `Docs\SPECIFICATION.md:30352`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\03_comptime\eval.cpp:699`
- `Bootstrap\Ultraviolet\src\03_comptime\eval.cpp:704`
- `Bootstrap\Ultraviolet\src\03_comptime\common.cpp:306`
- `Bootstrap\Ultraviolet\src\03_comptime\common.cpp:311`
- `Bootstrap\Ultraviolet\src\03_comptime\common.cpp:459`
- `Bootstrap\Ultraviolet\src\03_comptime\common.cpp:460`

Observed behavior:

Compile-time evaluation of a string literal copies the token lexeme, strips only
the surrounding quotes, and stores the remaining source spelling in `CtString`.
It does not decode escape sequences through `StringBytes`. The reverse path for
`CtString` literalization wraps the stored string in quotes and appends it
directly, without escaping bytes that would be special in a string literal.

Expected behavior:

`CtEval` of ordinary string literals must use the same `LiteralValue` semantics
as ordinary evaluation, which defines string values by `StringBytes(lit)`.
`CtLiteralize(CtString(v))` must produce a string literal whose `LiteralValue`
is exactly `v`.

Impact:

Compile-time logic can observe escaped spelling instead of the specified string
value. User diagnostics, generated identifiers derived from strings, and string
comparisons involving escaped literals can be wrong. Literalizing `CtString`
values that contain quotes, backslashes, control bytes, or Windows path
separators can emit source text whose later `StringBytes` value differs from the
compile-time value or is not a valid string token.

Fixture gap:

HelloUltraviolet exercises plain compile-time strings, diagnostics strings, and
reflection names, but no fixture uses escaped string content in compile-time
evaluation or a `CtString` value that must be escaped during literalization:

- `HelloUltraviolet\Fixtures\RejectedSource\Comptime\UserDiagnosticError\Source\Main.uv:5`
- `HelloUltraviolet\Fixtures\AcceptedProjects\ComptimeConformance\Source\Library.uv:69`
- `HelloUltraviolet\Fixtures\AcceptedProjects\ComptimeConformance\Source\Library.uv:249`
- `HelloUltraviolet\Fixtures\AcceptedProjects\ComptimeConformance\Source\Library.uv:346`
- `HelloUltraviolet\Source\Reference\Comptime\QuoteSpliceEmission.uv:20`
- `HelloUltraviolet\Source\Reference\Comptime\CompileTimeForms.uv:48`

### UV-AUDIT-0156: Compile-time expression typing can capture runtime locals

Severity: High

Status: Locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:24823`
- `Docs\SPECIFICATION.md:24902`
- `Docs\SPECIFICATION.md:24942`
- `Docs\SPECIFICATION.md:24984`
- `Docs\SPECIFICATION.md:25026`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\typing\type_expr.cpp:1139`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\type_expr.cpp:1140`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\type_expr.cpp:3841`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\type_expr.cpp:3847`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\type_expr.cpp:3853`
- `Bootstrap\Ultraviolet\src\03_comptime\eval.cpp:725`
- `Bootstrap\Ultraviolet\src\03_comptime\eval.cpp:726`
- `Bootstrap\Ultraviolet\src\03_comptime\rewrite.cpp:603`
- `Bootstrap\Ultraviolet\src\03_comptime\rewrite.cpp:604`
- `Bootstrap\Ultraviolet\src\03_comptime\rewrite.cpp:630`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\expr_common.cpp:343`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\expr_common.cpp:352`

Observed behavior:

`ExtendComptimeEnv` builds a compile-time type environment by pushing a new
scope on top of the existing expression environment. That leaves ordinary
runtime locals visible to `TypeExpr` inside `comptime { ... }`. If such a local
has a compile-time-available type, `T-CtExpr` can be recorded successfully. Phase
2 evaluation starts from `CtEmptyEnv` and identifier evaluation only consults
`env.values`, so the same identifier cannot evaluate. The rewrite path emits no
diagnostic when `EvalExpr` returns a non-ok result; it rebuilds and preserves the
`ComptimeExpr`. Lowering later treats the surviving `ComptimeExpr` as an
unlowerable codegen failure.

Expected behavior:

The compile-time typing environment should include only local bindings of the
current compile-time body, earlier Phase 2 compile-time procedure bindings, and
admitted compile-time capabilities. Runtime locals must not type-check as
ordinary values inside Phase 2 evaluation, and a `CtExpr` must be replaced before
Phase 3 by `CtLiteralize` or a compatible `CtAst` payload.

Impact:

Programs can pass type checking with `comptime { runtime_local }`, fail to
execute during Phase 2 without a source-level compile-time diagnostic, and then
reach codegen with a Chapter 22 form that should no longer exist.

Fixture gap:

HelloUltraviolet exercises compile-time loop bindings and compile-time procedure
locals inside `comptime { ... }`, but it does not include a rejected fixture for
an ordinary runtime local referenced from a compile-time expression:

- `HelloUltraviolet\Source\Reference\Comptime\CompileTimeForms.uv:66`
- `HelloUltraviolet\Source\Reference\Comptime\CompileTimeForms.uv:141`
- `HelloUltraviolet\Source\Reference\Comptime\CompileTimeForms.uv:226`
- `HelloUltraviolet\Fixtures\AcceptedProjects\ComptimeConformance\Source\Library.uv:282`

### UV-AUDIT-0157: Generic superclass arguments are parsed and discarded

Severity: High

Status: Locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:8789`
- `Docs\SPECIFICATION.md:8806`
- `Docs\SPECIFICATION.md:12373`
- `Docs\SPECIFICATION.md:12658`
- `Docs\SPECIFICATION.md:12679`
- `Docs\SPECIFICATION.md:12684`
- `Docs\SPECIFICATION.md:12694`
- `Docs\SPECIFICATION.md:12741`
- `Docs\SPECIFICATION.md:12915`
- `Docs\SPECIFICATION.md:30830`
- `Docs\SPECIFICATION.md:30831`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\02_source\parser\item\class_decl.cpp:443`
- `Bootstrap\Ultraviolet\src\02_source\parser\item\class_decl.cpp:444`
- `Bootstrap\Ultraviolet\src\02_source\parser\item\class_decl.cpp:446`
- `Bootstrap\Ultraviolet\src\02_source\parser\item\class_decl.cpp:450`
- `Bootstrap\Ultraviolet\src\02_source\parser\item\class_decl.cpp:456`
- `Bootstrap\Ultraviolet\src\02_source\parser\item\class_decl.cpp:463`
- `Bootstrap\Ultraviolet\src\02_source\parser\item\class_decl.cpp:475`
- `Bootstrap\Ultraviolet\src\02_source\parser\item\class_decl.cpp:488`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\resolve_items.cpp:974`
- `Bootstrap\Ultraviolet\include\02_source\ast\nodes\ast_fwd.h:30`

Observed behavior:

`ParseClassBound` parses a superclass path, optionally consumes `<...>` type
arguments with `ParseType`, and then returns only `cls.elem`. The parsed
arguments are not stored in the AST. `ParseSuperclassBounds` and
`ParseSuperclassBoundsTail` therefore accept `class Child <: Parent<i32> { ... }`
but persist only `Parent` in `ClassDecl.supers`; resolution later calls
`ResolveClassPathList(class_ctx, node.supers)` over those bare paths.

Expected behavior:

The superclass relation must either follow the formal class parser and store only
`ClassPath` values that were parsed by `ParseClassPath`, or it must preserve the
`class_bound` arguments implied by the appendix grammar. The current compiler
does neither: it accepts and advances past type arguments while erasing them from
`ClassDecl.supers`.

Impact:

Generic superclass clauses can be silently weakened to their unapplied class
path. That can change inheritance, capability-class classification, effective
method/field selection, superclass-cycle checks, and class well-formedness for
programs that write applied class bounds in a class `<:` clause.

Fixture gap:

HelloUltraviolet has fixtures for generic parameters, class parsing, and class
implementation, but no rejected or accepted fixture asserts that generic
arguments in a class superclass clause are either preserved or rejected:

- `HelloUltraviolet\Source\Reference\Polymorphism\Classes.uv:1`
- `HelloUltraviolet\Source\Reference\Polymorphism\GenericClassBounds.uv:1`
- `HelloUltraviolet\Source\Audit\Catalog\AbstractionAndPolymorphism\Classes.uv:137`
- `HelloUltraviolet\Source\Audit\Catalog\AbstractionAndPolymorphism\GenericParameters.uv:1`

### UV-AUDIT-0158: Declared field key boundaries do not truncate key paths

Severity: High

Status: Locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:2928`
- `Docs\SPECIFICATION.md:2933`
- `Docs\SPECIFICATION.md:9719`
- `Docs\SPECIFICATION.md:9760`
- `Docs\SPECIFICATION.md:11023`
- `Docs\SPECIFICATION.md:11029`
- `Docs\SPECIFICATION.md:12661`
- `Docs\SPECIFICATION.md:12719`
- `Docs\SPECIFICATION.md:20372`
- `Docs\SPECIFICATION.md:20373`
- `Docs\SPECIFICATION.md:20656`

Implementation anchors:

- `Bootstrap\Ultraviolet\include\02_source\ast\nodes\ast_items.h:592`
- `Bootstrap\Ultraviolet\include\02_source\ast\nodes\ast_items.h:690`
- `Bootstrap\Ultraviolet\include\02_source\ast\nodes\ast_items.h:763`
- `Bootstrap\Ultraviolet\include\02_source\ast\nodes\ast_items.h:802`
- `Bootstrap\Ultraviolet\src\04_analysis\keys\key_paths.cpp:246`
- `Bootstrap\Ultraviolet\src\04_analysis\keys\key_paths.cpp:255`
- `Bootstrap\Ultraviolet\src\04_analysis\keys\key_paths.cpp:262`
- `Bootstrap\Ultraviolet\src\04_analysis\keys\key_paths.cpp:320`
- `Bootstrap\Ultraviolet\src\04_analysis\keys\key_paths.cpp:347`
- `Bootstrap\Ultraviolet\src\04_analysis\keys\key_paths.cpp:428`
- `Bootstrap\Ultraviolet\src\04_analysis\keys\key_paths.cpp:828`
- `Bootstrap\Ultraviolet\src\04_analysis\keys\key_paths.cpp:835`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\key_block_stmt.cpp:792`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\key_block_stmt.cpp:838`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\key_block_stmt.cpp:1566`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\key_block_stmt.cpp:1582`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\key_block_stmt.cpp:1586`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\key_block_stmt.cpp:1717`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\key_block_stmt.cpp:1873`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\key_block_stmt.cpp:1930`

Observed behavior:

The parser and AST retain field-level key-boundary markers on record, modal
state, class, and abstract-state fields. Ordinary key-path construction does not
receive type context and appends field/index syntax directly: `BuildPathSegments`
sets each ordinary field segment's `boundary` flag to `false`, and `BuildKeyPath`
uses that untyped segment list. The declared-boundary helpers in `key_paths.cpp`
return `false` and `{}` and have no call sites outside their declarations.

The one declaration-aware check is limited to explicit key-block syntax:
`PathMarkerMatchesTypeBoundary` only warns when a written path marker already
matches a record field whose `key_boundary` flag is set. It does not transform
unmarked key paths, ordinary assignment paths collected by
`CollectWrittenPathsFromStmt`, or `PathCoveredByExplicitKeys`.

Expected behavior:

A field declaration marked with `#` must establish a permanent type-level key
boundary. Key paths for shared accesses through that field must truncate at the
declared boundary, with the same acquisition granularity as an explicit marker
at that segment.

Impact:

Shared access checks can reason about paths below a declared boundary as if the
boundary did not exist. For example, a read key block over `container.leaf.left`
can fail to classify a write to `container.leaf.right` as writing under the same
declared boundary, so the Chapter 19 read-block write rejection and implicit
acquisition coverage can be computed over paths that are too narrow.

Fixture gap:

HelloUltraviolet has reference entries for declared field boundaries, but the
reference body only performs reads under `%read`; it does not assert compiler
lowering, read/write rejection, or coverage equivalence for paths that differ
below a declared boundary. The compiler conformance artifact checks inline path
markers and generic key-path well-formedness, but not declared-boundary
truncation:

- `HelloUltraviolet\Source\Reference\Keys\KeyPaths.uv:21`
- `HelloUltraviolet\Source\Reference\Keys\KeyPaths.uv:31`
- `HelloUltraviolet\Source\Reference\Keys\KeyPaths.uv:51`
- `HelloUltraviolet\Source\Reference\Keys\KeyPaths.uv:64`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:3912`
- `HelloUltraviolet\Source\Audit\Catalog\KeySystem\KeyAcquisitionBlocks.uv:191`
- `HelloUltraviolet\Source\Audit\Catalog\KeySystem\CompilerConformanceExecution.uv:223`
- `HelloUltraviolet\Source\Audit\Catalog\KeySystem\CompilerConformanceExecution.uv:265`
- `HelloUltraviolet\Fixtures\RejectedSource\Keys\KeyPathsDiagnostic\Source\Main.uv:1`

### UV-AUDIT-0159: Dynamic key-block paths encode as invalid runtime prefixes

Severity: High

Status: Locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:20824`
- `Docs\SPECIFICATION.md:21358`
- `Docs\SPECIFICATION.md:21359`
- `Docs\SPECIFICATION.md:21361`
- `Docs\SPECIFICATION.md:21363`
- `Docs\SPECIFICATION.md:21365`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\05_codegen\lower\stmt\key_block_stmt.cpp:63`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\stmt\key_block_stmt.cpp:92`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\stmt\key_block_stmt.cpp:130`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\stmt\key_block_stmt.cpp:133`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\stmt\key_block_stmt.cpp:142`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\stmt\key_block_stmt.cpp:143`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\stmt\key_block_stmt.cpp:197`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\stmt\key_block_stmt.cpp:204`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\stmt\key_block_stmt.cpp:872`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\stmt\key_block_stmt.cpp:885`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\stmt\key_block_stmt.cpp:892`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\expr_common.cpp:807`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\expr_common.cpp:830`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\expr_common.cpp:834`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\expr_common.cpp:846`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\expr_common.cpp:1025`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\key.c:304`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\key.c:327`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\key.c:333`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\key.c:345`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\key.c:350`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\key.c:358`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\key.c:372`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\key.c:405`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\key.c:412`

Observed behavior:

`LowerKeyBlockStmt` detects dynamic key-block indexes with
`KeyPathHasDynamicRuntimeIndex` and records Chapter 19 conformance payloads
claiming a canonical coarsened path and sound static prefix. The runtime path
encoding used for those same explicit key blocks does not produce such a
prefix. `EncodeKeyPath` appends `"."` before it decides which segment kind is
being encoded; when the next segment is a dynamic index,
`RuntimeKeyIndexIsStatic` is false, so the function breaks before appending an
`i:` segment or value. A path such as `%read values[index]` therefore lowers as
the runtime path string `values.`.

The implicit shared-access encoder in `expr_common.cpp` uses a different rule:
for a dynamic `IndexAccessExpr`, it returns after encoding the base path and
does not append the segment delimiter. The runtime key parser accepts roots and
tagged segments, but after a dot it requires a valid segment tag and a non-empty
segment value. `values.` therefore fails `uv_key_parse_path`, and
`uv_key_paths_overlap` treats any parse failure as overlapping every other path.

Expected behavior:

A non-statically-safe dynamic indexed key-block path may be conservatively
coarsened only to a valid static prefix that soundly covers all runtime indexes.
Runtime synchronization must be performed on that parseable coarsened path, with
the same canonical ordering and value-deterministic behavior claimed by the
Chapter 19 conformance records.

Impact:

Explicit dynamic key blocks synchronize on an invalid runtime key string rather
than on the specified static prefix. The runtime fallback makes that malformed
path conflict with every other path, including unrelated roots, so dynamic
key-block synchronization can be stronger than the static-prefix semantics and
the emitted conformance records overstate canonical coarsening, cross-task
consistency, and observational equivalence.

Fixture gap:

HelloUltraviolet contains dynamic key-block references and checks for the
conformance payloads that claim dynamic coarsening, but it does not assert that
the actual `LowerKeyPaths` encoded path for a dynamic key block is a parseable
static prefix rather than a trailing-delimiter string:

- `HelloUltraviolet\Fixtures\AcceptedProjects\KeySystemConformance\Source\Library.uv:85`
- `HelloUltraviolet\Fixtures\AcceptedProjects\KeySystemConformance\Source\Library.uv:86`
- `HelloUltraviolet\Fixtures\AcceptedProjects\KeySystemConformance\Source\Library.uv:104`
- `HelloUltraviolet\Fixtures\AcceptedProjects\KeySystemConformance\Source\Library.uv:105`
- `HelloUltraviolet\Fixtures\AcceptedProjects\KeySystemConformance\Source\Library.uv:128`
- `HelloUltraviolet\Fixtures\AcceptedProjects\KeySystemConformance\Source\Library.uv:129`
- `HelloUltraviolet\Fixtures\AcceptedProjects\KeySystemConformance\Source\Library.uv:139`
- `HelloUltraviolet\Fixtures\AcceptedProjects\KeySystemConformance\Source\Library.uv:140`
- `HelloUltraviolet\Fixtures\DiagnosticSource\Keys\DynamicKeyRuntimeInfo\Source\Main.uv:9`
- `HelloUltraviolet\Fixtures\DiagnosticSource\Keys\DynamicKeyRuntimeInfo\Source\Main.uv:10`
- `HelloUltraviolet\Fixtures\DiagnosticSource\Keys\DynamicKeyRuntimeSyncInfo\Source\Main.uv:10`
- `HelloUltraviolet\Fixtures\DiagnosticSource\Keys\DynamicKeyRuntimeSyncInfo\Source\Main.uv:18`
- `HelloUltraviolet\Source\Reference\Keys\DynamicVerification.uv:7`
- `HelloUltraviolet\Source\Reference\Keys\DynamicVerification.uv:8`
- `HelloUltraviolet\Source\Reference\Keys\DynamicVerification.uv:68`
- `HelloUltraviolet\Source\Reference\Keys\DynamicVerification.uv:72`
- `HelloUltraviolet\Source\Audit\Catalog\KeySystem\CompilerConformanceExecution.uv:108`
- `HelloUltraviolet\Source\Audit\Catalog\KeySystem\CompilerConformanceExecution.uv:169`
- `HelloUltraviolet\Source\Audit\Catalog\KeySystem\CompilerConformanceExecution.uv:175`

### UV-AUDIT-0160: Yield-release key reacquisition orders encoded indexes bytewise

Severity: High

Status: Locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:20806`
- `Docs\SPECIFICATION.md:20811`
- `Docs\SPECIFICATION.md:20813`
- `Docs\SPECIFICATION.md:20824`
- `Docs\SPECIFICATION.md:21062`
- `Docs\SPECIFICATION.md:21063`
- `Docs\SPECIFICATION.md:21064`
- `Docs\SPECIFICATION.md:23288`
- `Docs\SPECIFICATION.md:24662`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\05_codegen\lower\stmt\key_block_stmt.cpp:56`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\stmt\key_block_stmt.cpp:146`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\stmt\key_block_stmt.cpp:147`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\stmt\key_block_stmt.cpp:803`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\stmt\key_block_stmt.cpp:806`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\expr_common.cpp:1138`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\expr_common.cpp:1144`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\expr_common.cpp:1145`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\yield.cpp:352`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\yield.cpp:353`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\yield.cpp:540`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\yield.cpp:543`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\yield_from.cpp:540`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\yield_from.cpp:541`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\yield_from.cpp:857`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\yield_from.cpp:860`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\llvm_emit_helpers.cpp:1286`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\llvm_emit_helpers.cpp:1297`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\key.c:471`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\key.c:475`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\key.c:479`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\key.c:487`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\key.c:701`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\key.c:738`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\key.c:742`

Observed behavior:

Explicit key blocks are initially sorted by `analysis::KeyPathLess`, but the
runtime key strings stored in held-key records preserve the formatted source
text for static index literals. `EncodeIndexSegment` calls `FormatIndexExpr`,
and `FormatIndexExpr` returns `lit->literal.lexeme`; a path such as
`values[10usize]` is therefore stored in the runtime as a string containing the
`10usize` index text.

On `yield release`, the async emitters call `uv_key_release_all` and later
`uv_key_reacquire`. The reacquire helper copies the recorded held-key pointers
into an array and calls `uv_key_insertion_sort`. That sort delegates to
`uv_key_order_compare`, which compares the encoded path bytes with
`uv_key_mem_lex`. It does not parse key segments, strip integer suffixes, or
compare index segments by `IndexValue`.

Expected behavior:

Resuming from `yield release` must reacquire recorded keys in Chapter 19
canonical order. For index segments, `KeyPathLess` requires
`IndexValue(s_1) < IndexValue(s_2)`, so `values[2usize]` must order before
`values[10usize]` even though the encoded string fragment `i:10usize` is
bytewise before `i:2usize`.

Impact:

A task that releases and later reacquires multiple indexed keys can reacquire
them in a different order than ordinary key-block lowering. For example, an
ordinary block can acquire `values[2usize]` before `values[10usize]`, while a
resumed `yield release` path reacquires the same two keys bytewise as
`values[10usize]` before `values[2usize]`. That violates the specified
canonical reacquisition order and can undermine the Chapter 19 deadlock-freedom
argument when resumed async tasks interleave with ordinary key acquisitions.

Fixture gap:

HelloUltraviolet exercises ordinary key-block ordering and async
`yield release`, but it does not combine `yield release` with multiple held
indexed keys whose source spelling distinguishes numeric order from byte order:

- `HelloUltraviolet\Fixtures\AcceptedProjects\KeySystemConformance\Source\Library.uv:14`
- `HelloUltraviolet\Source\Audit\Catalog\KeySystem\CompilerConformanceExecution.uv:228`
- `HelloUltraviolet\Source\Reference\Async\AsyncKeyIntegration.uv:52`
- `HelloUltraviolet\Source\Reference\Async\AsyncKeyIntegration.uv:55`
- `HelloUltraviolet\Source\Reference\Async\AsyncKeyIntegration.uv:61`
- `HelloUltraviolet\Source\Reference\Async\AsyncKeyIntegration.uv:64`
- `HelloUltraviolet\Fixtures\ArtifactProjects\AsyncObligations\Source\Main.uv:56`
- `HelloUltraviolet\Fixtures\ArtifactProjects\AsyncObligations\Source\Main.uv:59`
- `HelloUltraviolet\Fixtures\ArtifactProjects\AsyncObligations\Source\Main.uv:64`
- `HelloUltraviolet\Fixtures\ArtifactProjects\AsyncObligations\Source\Main.uv:67`
- `HelloUltraviolet\Source\Audit\AsyncArtifactExecution.uv:214`
- `HelloUltraviolet\Source\Audit\AsyncArtifactExecution.uv:294`
- `HelloUltraviolet\Source\Audit\AsyncArtifactExecution.uv:349`
- `HelloUltraviolet\Source\Audit\AsyncArtifactExecution.uv:350`

### UV-AUDIT-0161: Source-native selectors compare test string spellings instead of string values

Severity: Medium

Status: Locally verified from Poincare subagent report.

Spec anchors:

- `Docs\SPECIFICATION.md:2237`
- `Docs\SPECIFICATION.md:2239`
- `Docs\SPECIFICATION.md:2244`
- `Docs\SPECIFICATION.md:7240`
- `Docs\SPECIFICATION.md:7248`
- `Docs\SPECIFICATION.md:7251`
- `Docs\SPECIFICATION.md:7260`
- `Docs\SPECIFICATION.md:7311`
- `Docs\SPECIFICATION.md:7324`
- `Docs\SPECIFICATION.md:7335`
- `Docs\SPECIFICATION.md:11450`
- `Docs\SPECIFICATION.md:30352`
- `Docs\SPECIFICATION.md:30903`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\06_driver\test_discovery.cpp:16`
- `Bootstrap\Ultraviolet\src\06_driver\test_discovery.cpp:80`
- `Bootstrap\Ultraviolet\src\06_driver\test_discovery.cpp:90`
- `Bootstrap\Ultraviolet\src\06_driver\test_discovery.cpp:91`
- `Bootstrap\Ultraviolet\src\06_driver\test_discovery.cpp:333`
- `Bootstrap\Ultraviolet\src\06_driver\test_discovery.cpp:337`
- `Bootstrap\Ultraviolet\src\06_driver\compiler_main.cpp:761`
- `Bootstrap\Ultraviolet\src\06_driver\compiler_main.cpp:762`
- `Bootstrap\Ultraviolet\src\06_driver\compiler_main.cpp:871`
- `Bootstrap\Ultraviolet\src\06_driver\compiler_main.cpp:876`
- `Bootstrap\Ultraviolet\src\06_driver\cli.cpp:772`
- `Bootstrap\Ultraviolet\src\06_driver\cli.cpp:784`

Observed behavior:

Source-native test discovery extracts `#test(name: ...)` and `covers(...)`
metadata with `NormalizeStringLiteral`. That helper only removes the leading and
trailing double quote characters. It does not apply the language string-literal
decoder or `StringBytes` semantics. `descriptor.display_name` and
`descriptor.coverage_references` therefore store source spellings for escaped
strings rather than their semantic string values.

`FilterSourceNativeTests` then compares `uvc test --test` against the stored
display name and `uvc test --coverage` against the stored coverage-reference
strings with exact `std::string` equality. A source test named `"line\nbreak"`
would be selected only by an argument containing the two-character spelling
backslash-`n`, while the language value of the test name contains a line feed.

Expected behavior:

The `name: string_literal` and `covers(string_literal)` arguments are string
arguments, not raw token spellings. Test discovery and selector filtering should
compare the current `uvc` command arguments against the decoded string values
defined by ordinary string-literal semantics.

Impact:

Source-native test selection can depend on how a string literal was spelled
rather than on the test metadata value. Escaped test names and escaped coverage
anchors can be misreported in conformance payloads and missed by valid selector
arguments, while a spelling-shaped selector can match a value that the language
does not expose as that string.

Fixture gap:

HelloUltraviolet source-native metadata fixtures use unescaped string literals
for `#test(name: ...)` and `covers(...)`. They validate discovery, ordering, and
coverage selectors without exercising semantic decoding of metadata strings:

- `HelloUltraviolet\Source\Tests\SourceNativeTests.uv:70`
- `HelloUltraviolet\Source\Tests\SourceNativeTests.uv:80`
- `HelloUltraviolet\Source\Tests\AuditExecution.uv:78`
- `HelloUltraviolet\Source\Tests\AuditExecution.uv:87`
- `HelloUltraviolet\Source\Tests\AuditExecution.uv:96`
- `HelloUltraviolet\Source\Tests\AuditExecution.uv:105`
- `HelloUltraviolet\Source\Tests\AuditExecution.uv:114`

### UV-AUDIT-0162: Accepted-project catalog credit does not require project builds

Severity: Medium

Status: Locally verified from Poincare subagent report.

Scope:

This is a HelloUltraviolet testing-surface finding, not a direct compiler
implementation defect.

Spec anchors:

- `Docs\SPECIFICATION.md:362`
- `Docs\SPECIFICATION.md:363`
- `Docs\SPECIFICATION.md:369`
- `Docs\SPECIFICATION.md:672`
- `Docs\SPECIFICATION.md:678`
- `Docs\SPECIFICATION.md:1111`
- `Docs\SPECIFICATION.md:1113`
- `Docs\SPECIFICATION.md:1116`
- `Docs\SPECIFICATION.md:1118`
- `Docs\SPECIFICATION.md:15074`
- `Docs\SPECIFICATION.md:15078`

Implementation anchors:

- `HelloUltraviolet\Source\Audit\FixtureCatalog\AcceptedProjects\Coverage.uv:3`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\AcceptedProjects\Coverage.uv:63`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\AcceptedProjects\Coverage.uv:109`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\AcceptedProjects\Coverage.uv:115`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\AcceptedProjects\Coverage.uv:879`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\AcceptedProjects\Coverage.uv:887`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\AcceptedProjects\Coverage.uv:1307`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\AcceptedProjects\Coverage.uv:1308`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\AcceptedProjects\Artifacts.uv:12`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\AcceptedProjects\Artifacts.uv:25`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\AcceptedProjects\Artifacts.uv:26`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\AcceptedProjects\Artifacts.uv:60`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\AcceptedProjects\Artifacts.uv:61`
- `HelloUltraviolet\Source\Audit\AcceptedProjectExecution.uv:373`
- `HelloUltraviolet\Source\Audit\AcceptedProjectExecution.uv:409`
- `HelloUltraviolet\Source\Audit\AcceptedProjectExecution.uv:475`
- `HelloUltraviolet\Source\Audit\AcceptedProjectExecution.uv:579`
- `HelloUltraviolet\Source\Audit\AcceptedProjectExecution.uv:661`
- `HelloUltraviolet\Source\Audit\AcceptedProjectExecution.uv:705`
- `HelloUltraviolet\Source\Audit\AcceptedProjectExecution.uv:744`
- `HelloUltraviolet\Source\Audit\AcceptedProjectExecution.uv:2937`
- `HelloUltraviolet\Source\Audit\AcceptedProjectExecution.uv:3039`
- `HelloUltraviolet\Source\Audit\AcceptedProjectExecution.uv:3050`

Observed behavior:

The accepted-project fixture catalog records `128` expected entries and
`validatedAcceptedProjectFixtureCount` credits entries by checking generated
metadata and file existence. Examples include `MacOSTargetProfile` and
`VerificationFactPrecondition`, which are present in the accepted-project
catalog and artifact-existence checks.

The executable accepted-project test surface is a fixed sequence of
`acceptedProjectRunCase` calls. The aggregate runner builds only static library,
executable main, cross-assembly implementation, pointer-null return,
expression-semantics, and hosted-export-library projects. It finishes by
comparing the executed obligation count to
`ACCEPTED_PROJECT_EXECUTED_OBLIGATION_COUNT`; it does not compare the set of
built accepted-project roots against `expectedAcceptedProjectFixtureCount`.

Expected behavior:

If a fixture is counted as an accepted-project coverage entry, the testing
surface should either build that project with `uvc` or classify it in a separate
metadata-only category that cannot be mistaken for accepted-project execution.

Impact:

Accepted-project catalog coverage can remain green while a cataloged project no
longer loads, type-checks, lowers, links, or emits the claimed conformance
evidence. This weakens regression detection for target-profile resolution,
manifest/project loading, and dynamic verification artifacts.

Fixture gap:

Rejected-source and diagnostic-source project fixture runners traverse their
fixture directories and compare the executed count against the expected catalog
count. The accepted-project runner does not have an analogous catalog-to-build
coverage assertion:

- `HelloUltraviolet\Source\Audit\DiagnosticFixtureExecution.uv:1037`
- `HelloUltraviolet\Source\Audit\DiagnosticFixtureExecution.uv:1063`
- `HelloUltraviolet\Source\Audit\DiagnosticFixtureExecution.uv:1087`
- `HelloUltraviolet\Source\Audit\DiagnosticFixtureExecution.uv:1100`
- `HelloUltraviolet\Source\Audit\DiagnosticFixtureExecution.uv:1124`

### UV-AUDIT-0163: Source-native outcome markers are user-writable stderr text

Severity: Medium

Status: Inspection-backed.

Spec anchors:

- `Docs\SPECIFICATION.md:3685`
- `Docs\SPECIFICATION.md:3862`
- `Docs\SPECIFICATION.md:7276`
- `Docs\SPECIFICATION.md:7284`
- `Docs\SPECIFICATION.md:7285`
- `Docs\SPECIFICATION.md:7286`
- `Docs\SPECIFICATION.md:7287`
- `Docs\SPECIFICATION.md:7288`
- `Docs\SPECIFICATION.md:13540`
- `Docs\SPECIFICATION.md:29173`
- `Docs\SPECIFICATION.md:29262`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\caps\cap_system.cpp:179`
- `Bootstrap\Ultraviolet\src\04_analysis\caps\cap_system.cpp:290`
- `Bootstrap\Ultraviolet\src\04_analysis\caps\cap_system.cpp:301`
- `Bootstrap\Ultraviolet\src\04_analysis\caps\cap_system.cpp:302`
- `Bootstrap\Ultraviolet\src\04_analysis\caps\cap_system.cpp:367`
- `Bootstrap\Ultraviolet\src\04_analysis\caps\cap_io.cpp:421`
- `Bootstrap\Ultraviolet\src\05_codegen\intrinsics\builtins.cpp:1163`
- `Bootstrap\Ultraviolet\src\06_driver\compiler_main.cpp:752`
- `Bootstrap\Ultraviolet\src\06_driver\compiler_main.cpp:769`
- `Bootstrap\Ultraviolet\src\06_driver\compiler_main.cpp:780`
- `Bootstrap\Ultraviolet\src\06_driver\compiler_main.cpp:783`
- `Bootstrap\Ultraviolet\src\06_driver\compiler_main.cpp:785`
- `Bootstrap\Ultraviolet\src\06_driver\compiler_main.cpp:1011`
- `Bootstrap\Ultraviolet\src\06_driver\compiler_main.cpp:1042`
- `Bootstrap\Ultraviolet\src\06_driver\compiler_main.cpp:1047`
- `Bootstrap\Ultraviolet\src\06_driver\compiler_main.cpp:1218`
- `Bootstrap\Ultraviolet\src\06_driver\compiler_main.cpp:1221`
- `Bootstrap\Ultraviolet\src\06_driver\compiler_main.cpp:1222`

Observed behavior:

The generated source-native harness communicates per-test outcomes by writing a
plain text marker with prefix `uv-source-native-test-outcome` to stderr. The
driver then searches captured stdout, stderr, and combined output for that
marker as a substring.

That channel is not reserved to the harness. A one-parameter source-native test
receives `TestAuthority`, whose `io` field can call `write_stderr` and whose
`sys` field exposes `exit`. A test can therefore write the exact pass marker and
terminate with exit status `0` without returning normally, which satisfies the
driver's pass branch. Similarly, an authority-using test that writes its own
fail marker before a panic or abort can be counted as failed rather than errored
because the failed branch does not require a normal harness return.

Expected behavior:

`uvc test` should classify a source-native test from the execution state owned
by the generated harness: passed only when the procedure returns normally and
its postcondition is satisfied, failed only when it returns normally and the
postcondition is violated, and errored when it panics, aborts, cannot be invoked,
or exits outside normal harness control. User-writable stdout and stderr must not
be able to forge or downgrade those runner outcomes.

Impact:

An authority-using source-native test can hide a non-normal outcome behind text
that looks like runner protocol output. This weakens source-native regression
tests for panic, abort, unavailable authority, and harness-invocation failure
paths, and makes pass/fail/error counts depend on user output rather than the
spec-defined test semantics.

HelloUltraviolet fixture gap:

`HelloUltraviolet\Source\Audit\AttributesMetadataArtifactExecution.uv:1266`
checks ordinary pass, fail, and error classification and records
`panic_without_marker_is_error=true` at line `1296`. It does not include a
source-native authority test that writes a matching runner marker to stderr and
then panics, aborts, or exits before returning normally.

### UV-AUDIT-0164: Dynamic dispatch mismatch rule is credited without a mismatch path

Severity: Medium

Status: Inspection-backed.

Scope:

This is a HelloUltraviolet testing-surface and conformance-accounting finding.
It may also indicate a specification reachability issue, but it is not recorded
here as a direct compiler implementation defect.

Spec anchors:

- `Docs\SPECIFICATION.md:12812`
- `Docs\SPECIFICATION.md:13043`
- `Docs\SPECIFICATION.md:13044`
- `Docs\SPECIFICATION.md:13304`
- `Docs\SPECIFICATION.md:13305`
- `Docs\SPECIFICATION.md:13306`
- `Docs\SPECIFICATION.md:13343`
- `Docs\SPECIFICATION.md:13344`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\05_codegen\dyn_dispatch\dyn_dispatch.cpp:313`
- `Bootstrap\Ultraviolet\src\05_codegen\dyn_dispatch\dyn_dispatch.cpp:327`
- `Bootstrap\Ultraviolet\src\05_codegen\dyn_dispatch\dyn_dispatch.cpp:661`
- `Bootstrap\Ultraviolet\src\05_codegen\dyn_dispatch\vtable_emit.cpp:481`
- `HelloUltraviolet\Source\Audit\Catalog\AbstractionAndPolymorphism\DynamicClassObjects.uv:290`
- `HelloUltraviolet\Source\Reference\Polymorphism\DynamicClassObjects.uv:26`
- `HelloUltraviolet\Source\Reference\Polymorphism\DynamicClassObjects.uv:39`
- `HelloUltraviolet\Source\Reference\Polymorphism\DynamicClassObjects.uv:40`
- `HelloUltraviolet\Source\Reference\Polymorphism\DynamicClassObjects.uv:41`
- `HelloUltraviolet\Source\Audit\ArtifactProjectExecution.uv:5864`
- `HelloUltraviolet\Source\Audit\ArtifactProjectExecution.uv:5872`
- `HelloUltraviolet\Source\Audit\ArtifactProjectExecution.uv:5927`
- `HelloUltraviolet\Source\Audit\ArtifactProjectExecution.uv:5928`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\RejectedSource\Polymorphism.uv:317`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\RejectedSource\Polymorphism.uv:321`
- `HelloUltraviolet\Fixtures\RejectedSource\Polymorphism\ImplementationDefaultMethodSignatureMismatch\Expected.uv:3`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:2791`

Observed behavior:

The dynamic-class object catalog credits
`rule.14.DispatchSym-Default-Mismatch` to
`runPolymorphismDynamicClassObjectsReference` in
`Source/Reference/Polymorphism/DynamicClassObjects.uv`. That reference function
exercises a record implementation call, a class default call where the record has
no same-named method, and an inherited class call. It does not define or call a
record method with the same name as a class default and a non-matching signature.

The executable artifact check for dynamic dispatch requires conformance payloads
for `DispatchSym-Impl` and `DispatchSym-Default-None`, but has no corresponding
`DispatchSym-Default-Mismatch` assertion. The live dynamic-dispatch lowering path
records the implementation branch when `lookup.record_method` is present and the
default-none branch when `class_method->body_opt` is present; the mismatch rule is
present only in anchor registration. A separate rejected-source fixture covers
the same-name concrete-default signature mismatch as `Impl-Sig-Err-Concrete`, not
as an executable dynamic-dispatch fallback.

Expected behavior:

Coverage for `rule.14.DispatchSym-Default-Mismatch` should be tied to an
artifact that actually reaches the mismatch premise of the rule, or the rule
should be classified as diagnostic-only/unreachable for well-formed accepted
source if `Impl-Sig-Err-Concrete` intentionally rejects every such case before
lowering. In either case, the accepted reference function should not receive
exercise credit for a branch it cannot produce.

Impact:

The coverage manifest can report `DispatchSym-Default-Mismatch` as exercised even
though no accepted reference or artifact assertion demonstrates that branch. This
masks whether the specification intends the mismatch fallback to be reachable at
runtime, rejected during declaration checking, or retained only as an anchor-only
obligation.

### UV-AUDIT-0165: Capability authority validation skips method and transition bodies

Severity: High

Status: Agent-reported and locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:3232`
- `Docs\SPECIFICATION.md:3240`
- `Docs\SPECIFICATION.md:3241`
- `Docs\SPECIFICATION.md:3243`
- `Docs\SPECIFICATION.md:3262`
- `Docs\SPECIFICATION.md:3264`
- `Docs\SPECIFICATION.md:3267`
- `Docs\SPECIFICATION.md:14098`
- `Docs\SPECIFICATION.md:14099`
- `Docs\SPECIFICATION.md:14101`
- `Docs\SPECIFICATION.md:14102`

Implementation anchors:

- `Bootstrap\Ultraviolet\include\04_analysis\caps\authority_model.h:97`
- `Bootstrap\Ultraviolet\include\04_analysis\caps\authority_model.h:100`
- `Bootstrap\Ultraviolet\include\04_analysis\caps\authority_model.h:183`
- `Bootstrap\Ultraviolet\src\04_analysis\caps\authority_model.cpp:1341`
- `Bootstrap\Ultraviolet\src\04_analysis\caps\authority_model.cpp:1355`
- `Bootstrap\Ultraviolet\src\04_analysis\caps\authority_model.cpp:1364`
- `Bootstrap\Ultraviolet\src\04_analysis\caps\authority_model.cpp:1468`
- `Bootstrap\Ultraviolet\src\04_analysis\caps\authority_model.cpp:1499`
- `Bootstrap\Ultraviolet\src\04_analysis\caps\authority_model.cpp:1809`
- `Bootstrap\Ultraviolet\src\04_analysis\caps\authority_model.cpp:1811`
- `Bootstrap\Ultraviolet\src\04_analysis\caps\authority_model.cpp:1817`
- `Bootstrap\Ultraviolet\src\04_analysis\caps\authority_model.cpp:1872`
- `Bootstrap\Ultraviolet\src\04_analysis\caps\authority_model.cpp:1874`
- `Bootstrap\Ultraviolet\src\04_analysis\caps\authority_model.cpp:1880`
- `Bootstrap\Ultraviolet\src\06_driver\compiler_main.cpp:4309`
- `HelloUltraviolet\Fixtures\RejectedSource\Permissions\DirectCallCapabilityInclusion\Source\Main.uv:11`
- `HelloUltraviolet\Fixtures\RejectedSource\Permissions\AttenuationParentDropWithLiveChild\Source\Main.uv:3`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\RejectedSource\Permissions.uv:83`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\RejectedSource\Permissions.uv:147`

Observed behavior:

The driver invokes `ValidateModuleAuthority` after type checking. Both public
authority-validation entry points iterate only top-level `ProcedureDecl` items and
`ExternBlock` items. For top-level procedures they call `CheckAmbientAuthority`
and `CheckAttenuationParentLiveness`; both checks are declared and implemented
against `ast::ProcedureDecl`.

Record methods, class methods with bodies, modal state methods, and modal
transitions are not visited by this validator. Those declaration forms still have
procedure-like bodies, receivers, and parameters under `DeclTypingItem`, but the
authority-model pass never runs the no-ambient-authority or attenuation
parent-liveness checks over them.

Expected behavior:

Capability authority validation should cover every declaration body whose
receiver or parameters participate in `CapReq(d)` and `EffectiveCapReq(d)`,
including record methods, class methods, modal state methods, and modal
transitions. An authority violation rejected in an equivalent top-level procedure
must not become accepted solely because the body is attached to a type.

Impact:

Capability and attenuation violations can hide inside method and transition
bodies. This leaves the no-ambient-authority discipline and parent-drop rejection
dependent on declaration shape, even though the specification states those rules
over declarations with parameters and receivers.

HelloUltraviolet fixture gap:

The current rejected-source authority fixtures exercise top-level procedures:
`DirectCallCapabilityInclusion` and `AttenuationParentDropWithLiveChild` both
place the offending body in a `public procedure`. The catalog records those
fixtures, but there is no corresponding rejected fixture for a record method,
class method, modal state method, or modal transition.

### UV-AUDIT-0166: Composite attenuation tracking preserves only one child origin

Severity: High

Status: Agent-reported and locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:3262`
- `Docs\SPECIFICATION.md:3264`
- `Docs\SPECIFICATION.md:3265`
- `Docs\SPECIFICATION.md:3267`
- `Docs\SPECIFICATION.md:3268`
- `Docs\SPECIFICATION.md:4748`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\caps\authority_model.cpp:691`
- `Bootstrap\Ultraviolet\src\04_analysis\caps\authority_model.cpp:696`
- `Bootstrap\Ultraviolet\src\04_analysis\caps\authority_model.cpp:823`
- `Bootstrap\Ultraviolet\src\04_analysis\caps\authority_model.cpp:848`
- `Bootstrap\Ultraviolet\src\04_analysis\caps\authority_model.cpp:873`
- `Bootstrap\Ultraviolet\src\04_analysis\caps\authority_model.cpp:889`
- `Bootstrap\Ultraviolet\src\04_analysis\caps\authority_model.cpp:903`
- `Bootstrap\Ultraviolet\src\04_analysis\caps\authority_model.cpp:999`
- `Bootstrap\Ultraviolet\src\04_analysis\caps\authority_model.cpp:1042`
- `Bootstrap\Ultraviolet\src\04_analysis\caps\authority_model.cpp:1045`
- `Bootstrap\Ultraviolet\src\04_analysis\caps\authority_model.cpp:1048`
- `Bootstrap\Ultraviolet\src\04_analysis\caps\authority_model.cpp:1055`
- `Bootstrap\Ultraviolet\src\04_analysis\caps\authority_model.cpp:1057`
- `Bootstrap\Ultraviolet\src\04_analysis\caps\authority_model.cpp:1064`
- `Bootstrap\Ultraviolet\src\04_analysis\caps\authority_model.cpp:1075`
- `Bootstrap\Ultraviolet\src\04_analysis\caps\authority_model.cpp:1229`
- `Bootstrap\Ultraviolet\src\04_analysis\caps\authority_model.cpp:1237`
- `HelloUltraviolet\Fixtures\RejectedSource\Permissions\AttenuationParentDropWithLiveChild\Source\Main.uv:4`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\RejectedSource\Permissions.uv:147`

Observed behavior:

Attenuation analysis represents expression provenance as
`std::optional<AttenuationOrigin>`, where each origin stores exactly one parent
name. `FirstOrigin` returns the left origin when present and otherwise the right
origin. Tuple, array, array-repeat, record, and enum expression analysis merge
child expressions through `FirstOrigin`, so every later child origin is discarded
once an earlier child has one.

Binding analysis then collects every name from a pattern and binds each name to
that single optional origin. For destructuring, every bound name receives the same
parent, even if different fields or tuple elements were derived from different
parents. If the discarded child escapes or if its parent is dropped while the
child remains live, the later checks can only report the retained parent.

Expected behavior:

Composite values that contain derived capabilities must preserve each child
capability's parent relationship with enough path information to enforce the
attenuation liveness rules for every element or field. Destructuring should bind
each name to the origin of the value part that actually produced that name.

Impact:

An attenuation violation can be hidden behind a composite value whose first
tracked element is valid while a later element is invalid. Parent-drop and escape
checks then reason about the wrong parent or no parent at all, so `uvc --check`
can accept code that violates the monotone attenuation and child-liveness
requirements.

HelloUltraviolet fixture gap:

The existing `AttenuationParentDropWithLiveChild` fixture covers one escaped
derived capability from one parent. The rejected-source catalog has no
multi-origin tuple, record, enum, array, or destructuring specimen that proves
every child origin is preserved independently.

### UV-AUDIT-0167: Defer non-local control checking uses a shallow duplicate walker

Severity: Medium

Status: Locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:19735`
- `Docs\SPECIFICATION.md:19744`
- `Docs\SPECIFICATION.md:19745`
- `Docs\SPECIFICATION.md:19751`
- `Docs\SPECIFICATION.md:19756`
- `Docs\SPECIFICATION.md:19761`
- `Docs\SPECIFICATION.md:19763`
- `Docs\SPECIFICATION.md:19765`
- `Docs\SPECIFICATION.md:20258`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\defer_stmt.cpp:47`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\defer_stmt.cpp:59`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\defer_stmt.cpp:63`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\defer_stmt.cpp:65`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\defer_stmt.cpp:67`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\defer_stmt.cpp:69`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\defer_stmt.cpp:81`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\defer_stmt.cpp:88`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\defer_stmt.cpp:96`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\defer_stmt.cpp:108`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\defer_stmt.cpp:339`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\defer_stmt.cpp:344`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\stmt_common.cpp:1882`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\stmt_common.cpp:1931`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\stmt_common.cpp:1978`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\stmt_common.cpp:1991`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\stmt_common.cpp:2036`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\stmt_common.cpp:2044`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\stmt_common.cpp:2056`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\stmt_common.cpp:3023`
- `HelloUltraviolet\Fixtures\RejectedSource\Statements\DeferNonLocal\Source\Main.uv:4`
- `HelloUltraviolet\Fixtures\RejectedSource\Statements\DeferNonLocal\Source\Main.uv:5`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\RejectedSource\Statements.uv:338`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\RejectedSource\Statements.uv:345`

Observed behavior:

`TypeDeferStmt` calls a local `LocalDeferSafe` helper in `defer_stmt.cpp`. That
helper detects direct `return`, direct `break` outside a loop, direct `continue`
outside a loop, nested `defer` bodies, loop bodies, and block expressions. Its
statement walker returns `false` for expression statements, bindings,
assignments, region/frame/unsafe/key blocks, and other statement forms. Its
expression walker does not traverse `if`, `if case`, `if is`, calls, method
calls, tuples, records, arrays, or operator operands.

The broader statement implementation in `stmt_common.cpp` has a more complete
`DeferSafe` walker that does traverse expression statements, block-bearing
statements, and conditional expressions, but `TypeDeferStmt` does not call that
helper.

Expected behavior:

`DeferSafe(b)` must implement `HasNonLocalCtrl-Child` over nested expressions and
statements, so any non-local `return`, `break`, or `continue` inside a deferred
block is rejected with `E-SEM-3152`, regardless of whether it appears directly,
inside an expression statement, inside a conditional branch, or inside another
block-bearing statement.

Impact:

Source with non-local control flow hidden under ordinary nested defer contents
can bypass the `Defer-NonLocal-Err` check. That can allow a deferred cleanup body
to perform control transfer that the static semantics reserve as ill-formed.

HelloUltraviolet fixture gap:

`DeferNonLocal` and `DeferDiagnostic` cover only a direct `return` statement
inside the deferred block. The rejected-source catalog has no nested conditional,
expression-statement, binding-initializer, region/frame/unsafe/key block, call
argument, or operator-operand specimen for `E-SEM-3152`.

### UV-AUDIT-0168: Key escape checking for defer skips expression-bodied execution forms

Severity: Medium

Status: Locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:20765`
- `Docs\SPECIFICATION.md:20767`
- `Docs\SPECIFICATION.md:20769`
- `Docs\SPECIFICATION.md:20779`
- `Docs\SPECIFICATION.md:22293`
- `Docs\SPECIFICATION.md:22294`
- `Docs\SPECIFICATION.md:22367`
- `Docs\SPECIFICATION.md:22500`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\defer_stmt.cpp:139`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\defer_stmt.cpp:151`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\defer_stmt.cpp:183`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\defer_stmt.cpp:191`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\defer_stmt.cpp:205`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\defer_stmt.cpp:215`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\defer_stmt.cpp:269`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\defer_stmt.cpp:273`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\defer_stmt.cpp:283`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\defer_stmt.cpp:287`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\defer_stmt.cpp:316`
- `Bootstrap\Ultraviolet\include\02_source\ast\nodes\ast_exprs.h:495`
- `Bootstrap\Ultraviolet\include\02_source\ast\nodes\ast_exprs.h:500`
- `Bootstrap\Ultraviolet\include\02_source\ast\nodes\ast_exprs.h:516`
- `Bootstrap\Ultraviolet\include\02_source\ast\nodes\ast_exprs.h:530`
- `Bootstrap\Ultraviolet\include\02_source\ast\nodes\ast_exprs.h:556`
- `Bootstrap\Ultraviolet\include\02_source\ast\nodes\ast_exprs.h:563`
- `Bootstrap\Ultraviolet\include\02_source\ast\nodes\ast_exprs.h:566`
- `Bootstrap\Ultraviolet\include\02_source\ast\nodes\ast_exprs.h:568`
- `Bootstrap\Ultraviolet\src\04_analysis\keys\key_capture.cpp:369`
- `Bootstrap\Ultraviolet\src\04_analysis\keys\key_capture.cpp:371`
- `Bootstrap\Ultraviolet\src\04_analysis\keys\key_capture.cpp:577`
- `Bootstrap\Ultraviolet\src\04_analysis\keys\key_capture.cpp:683`
- `HelloUltraviolet\Fixtures\RejectedSource\Keys\KeyEscapeDeferredAccess\Source\Main.uv:6`
- `HelloUltraviolet\Fixtures\RejectedSource\Keys\KeyEscapeDeferredAccess\Source\Main.uv:7`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\RejectedSource\Keys.uv:266`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\RejectedSource\Keys.uv:270`

Observed behavior:

`TypeDeferStmt` checks `LocalBlockNeedsKeyAccess` before type-checking the
deferred block and emits `E-CON-0006` when it sees key-dependent access. The
statement walker recognizes direct key blocks and ordinary expression statements,
and the expression walker traverses calls, method calls, conditionals, blocks,
arrays, tuples, and records.

That expression walker does not handle `RaceExpr`, `AllExpr`, `ParallelExpr`,
`SpawnExpr`, `WaitExpr`, or `DispatchExpr`. Those AST nodes contain executable
subexpressions or blocks. `DispatchExpr` also carries an optional key clause, and
the key-capture code treats dispatch key clauses and dispatch bodies as key
analysis inputs. A deferred block that reaches a key block or shared access
inside one of these expression-bodied forms can therefore bypass the early
`E-CON-0006` check.

Expected behavior:

The key-escape check for deferred blocks should traverse every expression form
that can evaluate a child expression or execute a child block from the deferred
body, including dispatch key clauses, dispatch bodies, parallel/spawn bodies, and
race/all operands. Any key acquisition or key-dependent access that would execute
from a deferred body must be rejected with the Chapter 19 deferred-key diagnostic.

Impact:

Key-dependent access can be hidden behind a structured execution expression in a
deferred block. That permits source whose access is no longer guaranteed to occur
before the key scope exits, weakening the key lifetime restriction that `defer`
is supposed to enforce.

HelloUltraviolet fixture gap:

`KeyEscapeDeferredAccess` and `KeyEscapePrecedence` cover only a direct
`let delayed = shared_value` statement inside a deferred block. The key rejected
fixtures do not include a deferred `dispatch` key clause, deferred dispatch body,
deferred parallel/spawn body, or deferred race/all operand that touches a keyed
path.

### UV-AUDIT-0169: Speculative impure-call checking scans only expression statements

Severity: Medium

Status: Locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:21153`
- `Docs\SPECIFICATION.md:21160`
- `Docs\SPECIFICATION.md:21161`
- `Docs\SPECIFICATION.md:21163`
- `Docs\SPECIFICATION.md:21295`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\key_block_stmt.cpp:577`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\key_block_stmt.cpp:590`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\key_block_stmt.cpp:591`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\key_block_stmt.cpp:592`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\key_block_stmt.cpp:628`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\key_block_stmt.cpp:637`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\key_block_stmt.cpp:695`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\key_block_stmt.cpp:725`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\key_block_stmt.cpp:1999`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\key_block_stmt.cpp:2001`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\key_block_stmt.cpp:2003`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\key_block_stmt.cpp:2004`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\key_block_stmt.cpp:2005`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\key_block_stmt.cpp:2009`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\key_block_stmt.cpp:2011`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\key_block_stmt.cpp:2012`
- `HelloUltraviolet\Fixtures\RejectedSource\Keys\SpeculativeImpureCall\Source\Main.uv:5`
- `HelloUltraviolet\Fixtures\RejectedSource\Keys\SpeculativeImpureCall\Source\Main.uv:6`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\RejectedSource\Keys.uv:198`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\RejectedSource\Keys.uv:202`

Observed behavior:

The speculative key-block implementation defines `FindImpureSpeculativeCallExpr`,
which can inspect many nested expression forms and reports a non-pure `CallExpr`
or `MethodCallExpr`. However, `TypeKeyBlockStmt` invokes that helper only for
top-level `ExprStmt` nodes in the speculative block body and for the block tail.
It does not invoke the helper for `let` or `var` initializers, assignments,
compound assignments, return or break values, region/frame/block-bearing
statement contents, or other statement positions that contain subexpressions.

Expected behavior:

`K-Spec-No-Impure-Call` is stated over `c in Subexpressions(B)`. The speculative
block validator should therefore search all subexpressions of the block body, not
only expression statements and the tail expression.

Impact:

An impure capability call can be hidden in a speculative block by placing it in a
binding initializer or another non-expression-statement subexpression. That lets
externally observable effects occur in speculative execution despite the
Chapter 19 restriction.

HelloUltraviolet fixture gap:

`SpeculativeImpureCall` covers only a direct expression statement
`context.io~>exists(...)` in the speculative body. There is no rejected-source
fixture for the same impure call inside a `let` initializer, assignment value,
return value, nested block, or other non-expression-statement subexpression.

### UV-AUDIT-0170: Expression-level attributes miss required newline continuation

Severity: Medium

Status: Agent-reported and locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:1918`
- `Docs\SPECIFICATION.md:1921`
- `Docs\SPECIFICATION.md:1924`
- `Docs\SPECIFICATION.md:1930`
- `Docs\SPECIFICATION.md:30640`
- `Docs\SPECIFICATION.md:30641`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\02_source\lexer\lexer.cpp:336`
- `Bootstrap\Ultraviolet\src\02_source\lexer\lexer.cpp:350`
- `Bootstrap\Ultraviolet\src\02_source\lexer\lexer.cpp:363`
- `Bootstrap\Ultraviolet\src\02_source\lexer\lexer.cpp:382`
- `Bootstrap\Ultraviolet\src\02_source\lexer\lexer.cpp:387`
- `Bootstrap\Ultraviolet\src\02_source\parser\expr\expr_common.cpp:613`
- `Bootstrap\Ultraviolet\src\02_source\parser\expr\expr_common.cpp:616`
- `Bootstrap\Ultraviolet\src\02_source\parser\expr\expr_common.cpp:619`
- `HelloUltraviolet\Source\Reference\Attributes\GeneralAttributes.uv:40`
- `HelloUltraviolet\Fixtures\RejectedSource\Procedures\ContractDynamicAttribute\Source\Main.uv:4`
- `HelloUltraviolet\Fixtures\RejectedSource\Permissions\SharedMutationWithoutKey\Source\Main.uv:9`

Observed behavior:

`ContinuesLineImpl` implements the attribute continuation case by walking back to
the start of the physical line and requiring `LineIsOnlyAttributeList(...)` for
the whole previous line. If an expression-level attribute is written after
earlier tokens on that same physical line and the attributed operand starts on
the next line, the branch does not classify the newline as continued. The parser
does support expression-level attributes by parsing an optional attribute list
before an expression body, but the filtered token stream can terminate before the
operand reaches that parser path.

Expected behavior:

`AttrBefore(K, i)` depends on whether `Prev(K, i)` is the final token of an
attribute specification parsed from an earlier `#` token, not on whether the
entire physical line contains only attributes. When `Next(K, i)` begins an
operand, `Filter(K)` must remove the newline so `attributed_expr ::= attribute_list
expression` can parse.

Impact:

`uvc` can parse a spec-valid attributed expression as two syntactic units when
the attribute list is not the only content on the line before the operand. That
can produce a missing-terminator or generic syntax first failure instead of
parsing the attributed expression.

HelloUltraviolet fixture gap:

Existing coverage includes declaration attributes and same-line attributed
expressions, but no fixture covers an expression-level attribute after preceding
expression syntax with the operand on the following line.

### UV-AUDIT-0171: Top-level using aliases reject permitted identifier splices

Severity: Medium

Status: Locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:25427`
- `Docs\SPECIFICATION.md:25429`
- `Docs\SPECIFICATION.md:25505`
- `Docs\SPECIFICATION.md:25507`
- `Docs\SPECIFICATION.md:25509`
- `Docs\SPECIFICATION.md:25526`
- `Docs\SPECIFICATION.md:25535`

Implementation anchors:

- `Bootstrap\Ultraviolet\include\02_source\ast\nodes\ast_items.h:73`
- `Bootstrap\Ultraviolet\include\02_source\ast\nodes\ast_items.h:80`
- `Bootstrap\Ultraviolet\include\02_source\ast\nodes\ast_items.h:104`
- `Bootstrap\Ultraviolet\src\02_source\parser\item\using_decl.cpp:78`
- `Bootstrap\Ultraviolet\src\02_source\parser\item\using_decl.cpp:207`
- `Bootstrap\Ultraviolet\src\02_source\parser\parser_paths.cpp:51`
- `Bootstrap\Ultraviolet\src\02_source\parser\parser_paths.cpp:219`
- `Bootstrap\Ultraviolet\src\02_source\parser\parser_paths.cpp:471`
- `Bootstrap\Ultraviolet\src\03_comptime\quote.cpp:1099`
- `HelloUltraviolet\Fixtures\AcceptedProjects\ComptimeConformance\Source\Library.uv:383`

Observed behavior:

Inside quoted item content, a top-level `using ... as $alias` alias cannot reach
identifier-splice rendering. `UsingSpec`, `UsingItem`, and `UsingDecl` store only
plain identifiers, `ParseUsingSpec` and the single-item using parser call
`ParseAliasOpt`, and `ParseAliasOpt` calls `ParseIdent`. In quote mode,
`ParseIdent` treats `$identifier` through `RecordRestrictedSpliceIdentIfPresent`
and then emits a generic identifier parse failure. The quote rebuild path also
returns `UsingDecl` unchanged, so there is no later opportunity to evaluate a
`SpliceIdentNode` for a top-level using alias.

Expected behavior:

The quoted-content parser must admit `SpliceIdentNode` in `using ... as` alias
names, preserve that splice in the using-declaration AST, and rebuild it by
evaluating the string-valued splice with the identifier render rule. The alias
is intentionally unhygienic when it comes from a string splice, while unaliased
using names remain preserved as written.

Impact:

`uvc` rejects a spec-valid metaprogramming form that should let compile-time code
generate explicit using aliases from string values. The current accepted
quote/splice conformance surface emits a normal top-level using alias, but it
does not exercise a spliced alias in that position.

HelloUltraviolet fixture gap:

`QuoteSpliceEmission` and `ComptimeConformance` cover expression identifier
splices, pattern identifier splices, parameter identifier splices, local using
alias splices, and a non-spliced top-level using alias. There is no accepted
fixture for a quoted top-level `using ... as $alias` alias and no rejected
fixture proving the structural-name restriction stays limited to the disallowed
identifier positions.

### UV-AUDIT-0172: Quote splice traversal skips `copy` operands

Severity: Medium

Status: Locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:17206`
- `Docs\SPECIFICATION.md:17225`
- `Docs\SPECIFICATION.md:25515`
- `Docs\SPECIFICATION.md:25521`
- `Docs\SPECIFICATION.md:25537`
- `Docs\SPECIFICATION.md:25542`

Implementation anchors:

- `Bootstrap\Ultraviolet\include\02_source\ast\nodes\ast_exprs.h:153`
- `Bootstrap\Ultraviolet\src\02_source\parser\expr\unary.cpp:289`
- `Bootstrap\Ultraviolet\src\03_comptime\quote.cpp:1380`
- `Bootstrap\Ultraviolet\src\03_comptime\quote.cpp:1386`
- `Bootstrap\Ultraviolet\src\03_comptime\quote.cpp:1392`
- `Bootstrap\Ultraviolet\src\03_comptime\quote.cpp:1761`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\type_expr.cpp:1916`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\type_expr.cpp:1918`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\type_expr.cpp:1920`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\type_expr.cpp:1922`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\type_expr.cpp:2221`

Observed behavior:

`copy e` is parsed as `CopyExpr(value)`, but quote splice checking and quote
rebuilding do not have a `CopyExpr` case. The rebuild visitor handles nearby
unary-like forms such as `DerefExpr`, `AddressOfExpr`, `MoveExpr`, and
`AllocExpr`, then falls through to returning the original expression for any
unlisted expression node. The static quote-splice checker follows the same
shape: it checks dereference, address-of, move, and allocation operands, then
the default branch reports success without visiting unlisted children.

Expected behavior:

Quoted expression payloads must be traversed recursively so every `SpliceExprNode`
inside the payload is statically compatibility-checked and then rendered during
`QuoteBuild`. A splice under `copy`, such as `quote { copy $(expr_ast) }`, must
evaluate and substitute the splice source before the returned `Ast::Expr` is
emitted or used.

Impact:

`uvc` can accept a quote whose static splice checker never validates a splice
source under `copy`, and the compile-time quote builder can return an `Ast::Expr`
payload that still contains an unresolved splice node. Later consumers of that
AST do not receive the expression payload promised by `RenderSplice`, so emitted
or inspected code can diverge from the quoted source.

HelloUltraviolet fixture gap:

The compile-time quote/splice reference covers expression, type, pattern,
statement, item, local using, and parameter splice positions, but it does not
exercise a splice nested under `copy`.

### UV-AUDIT-0173: Modal widening catalog credits unexercised warning and error paths

Severity: Medium

Status: Locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:11302`
- `Docs\SPECIFICATION.md:11307`
- `Docs\SPECIFICATION.md:11312`
- `Docs\SPECIFICATION.md:11322`
- `Docs\SPECIFICATION.md:11326`
- `Docs\SPECIFICATION.md:11328`
- `Docs\SPECIFICATION.md:11333`
- `Docs\SPECIFICATION.md:11344`

Implementation and testing anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\unary.cpp:91`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\unary.cpp:98`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\unary.cpp:101`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\unary.cpp:117`
- `Bootstrap\Ultraviolet\src\04_analysis\modal\modal_widen.cpp:565`
- `Bootstrap\Ultraviolet\src\04_analysis\modal\modal_widen.cpp:572`
- `Bootstrap\Ultraviolet\src\04_analysis\modal\modal_widen.cpp:579`
- `Bootstrap\Ultraviolet\src\04_analysis\modal\modal_widen.cpp:591`
- `HelloUltraviolet\Source\Reference\ModalTypes\Widening.uv:3`
- `HelloUltraviolet\Source\Reference\ModalTypes\Widening.uv:24`
- `HelloUltraviolet\Source\Audit\Catalog\ModalAndSpecialTypes\ModalWidening.uv:55`
- `HelloUltraviolet\Source\Audit\Catalog\ModalAndSpecialTypes\ModalWidening.uv:64`
- `HelloUltraviolet\Source\Audit\Catalog\ModalAndSpecialTypes\ModalWidening.uv:82`
- `HelloUltraviolet\Source\Audit\Catalog\ModalAndSpecialTypes\ModalWidening.uv:91`
- `HelloUltraviolet\Source\Audit\Catalog\ModalAndSpecialTypes\ModalWidening.uv:100`
- `HelloUltraviolet\Source\Audit\Catalog\ModalAndSpecialTypes\ModalWidening.uv:109`
- `HelloUltraviolet\Fixtures\RejectedSource\Expressions\WidenTypingDiagnosticsOwnership\Source\Main.uv:4`

Observed behavior:

The generated catalog credits `rule.13.T-Modal-Widen-Perm`,
`rule.13.Widen-AlreadyGeneral`, `def.NicheCompatible`,
`rule.13.Chk-Subsumption-Modal-NonNiche`, `def.WidenWarnCond`,
`rule.13.Warn-Widen-LargePayload`, and `rule.13.Warn-Widen-Ok` to
`runModalTypesWideningReference`. That reference declares one modal with an
empty state and an `i32` state, performs an implicit assignment from another
modal state to a general modal value, and explicitly evaluates `widen small`.
It does not apply `widen` to a permission-wrapped modal state, does not apply
`widen` to an already-general modal value, and does not define a payload larger
than `WIDEN_LARGE_PAYLOAD_THRESHOLD_BYTES`.

Expected behavior:

Each credited widening obligation should be backed by a specimen that actually
forces the relevant implementation path: permission preservation for
`T-Modal-Widen-Perm`, rejected-source coverage for `Widen-AlreadyGeneral`,
large non-niche payload coverage for `Warn-Widen-LargePayload`, and a separate
small or niche-compatible case for `Warn-Widen-Ok`. Non-niche implicit
subsumption should likewise be credited only by a specimen that exercises that
specific diagnostic or success path.

Impact:

The HelloUltraviolet catalog can report modal widening conformance while several
specified branches are never executed by the cited specimens. A regression in
large-payload warning emission, already-general rejection, or permission
preservation would not be caught by the current accepted and rejected surfaces.

HelloUltraviolet fixture gap:

`WidenTypingDiagnosticsOwnership` covers `Widen-NonModal` only. There is no
rejected-source fixture for `Widen-AlreadyGeneral`, no accepted project with a
large non-niche modal payload that must emit `W-SYS-4010`, and no accepted
fixture that explicitly verifies `TypePerm(p, TypeModalState(...))` widens to
`TypePerm(p, ModalRefType(...))`.

### UV-AUDIT-0174: Non-niche modal subsumption ignores generic argument identity

Severity: High

Status: Locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:6195`
- `Docs\SPECIFICATION.md:6198`
- `Docs\SPECIFICATION.md:6543`
- `Docs\SPECIFICATION.md:6544`
- `Docs\SPECIFICATION.md:10672`
- `Docs\SPECIFICATION.md:10673`
- `Docs\SPECIFICATION.md:12273`

Implementation anchors:

- `Bootstrap\Ultraviolet\include\04_analysis\typing\types.h:151`
- `Bootstrap\Ultraviolet\include\04_analysis\typing\types.h:163`
- `Bootstrap\Ultraviolet\include\04_analysis\typing\types.h:167`
- `Bootstrap\Ultraviolet\include\04_analysis\typing\types.h:249`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\type_refs.cpp:575`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\subtyping.cpp:1232`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\subtyping.cpp:1240`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\subtyping.cpp:1249`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\type_infer.cpp:584`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\type_infer.cpp:592`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\type_infer.cpp:601`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\type_infer.cpp:611`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\type_infer.cpp:1833`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\type_infer.cpp:1835`

Observed behavior:

The dedicated `ModalNonNiche` predicate strips permissions, requires the source
to be `TypeModalState`, requires the target to have the same applied type path,
and checks only that source and target generic argument vectors have the same
length before returning `!NicheCompatible(...)`. It does not compare each
`TypeModalState::generic_args` element against the target `AppliedTypeArgs`.
`CheckExprAgainstType` runs this predicate before ordinary subsumption failure,
so `uvc` can report `Chk-Subsumption-Modal-NonNiche` for an expression of
`GenericModalReference<i32>@Present` checked against
`GenericModalReference<bool>` even though the source and target do not share the
same `modal_ref`.

Expected behavior:

The modal-specific non-niche diagnostic is valid only when `StripPerm(S)` is
`TypeModalState(modal_ref, S_s)` and `StripPerm(T)` is
`ModalRefType(modal_ref)`. For generic modal references, `modal_ref` includes
the generic arguments, so the diagnostic guard must establish generic argument
equivalence before applying `Chk-Subsumption-Modal-NonNiche`. Otherwise the
checker should fall through to the ordinary type mismatch path.

Impact:

Programs with mismatched generic modal instantiations can receive the wrong
first-failure diagnostic. This makes `E-TYP-2070` appear to be a layout/niche
problem when the actual failure is a generic type mismatch, and it can mask
regressions in generic modal type equivalence because the modal-specific branch
short-circuits first.

HelloUltraviolet fixture gap:

`HelloUltraviolet\Source\Reference\ModalTypes\ModalDeclarations.uv:44`
declares `GenericModalReference<TValue>`, but the accepted and rejected fixture
surfaces do not check a state value of one generic modal instantiation against a
different general modal instantiation. The catalog also credits
`Chk-Subsumption-Modal-NonNiche` to
`HelloUltraviolet\Source\Reference\Types\Inference.uv:9`, whose reference body
contains only primitive, tuple, and boolean inference specimens.

### UV-AUDIT-0175: Quoted zero-parameter function types skip return-type splices

Severity: High

Status: Locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:572`
- `Docs\SPECIFICATION.md:12041`
- `Docs\SPECIFICATION.md:12044`
- `Docs\SPECIFICATION.md:12056`
- `Docs\SPECIFICATION.md:12059`
- `Docs\SPECIFICATION.md:12087`
- `Docs\SPECIFICATION.md:12088`
- `Docs\SPECIFICATION.md:25518`
- `Docs\SPECIFICATION.md:25524`
- `Docs\SPECIFICATION.md:25537`
- `Docs\SPECIFICATION.md:25542`

Implementation anchors:

- `Bootstrap\Ultraviolet\include\02_source\ast\nodes\ast_types.h:83`
- `Bootstrap\Ultraviolet\include\02_source\ast\nodes\ast_types.h:84`
- `Bootstrap\Ultraviolet\include\02_source\ast\nodes\ast_types.h:85`
- `Bootstrap\Ultraviolet\src\02_source\parser\type\function_type.cpp:150`
- `Bootstrap\Ultraviolet\src\02_source\parser\type\function_type.cpp:157`
- `Bootstrap\Ultraviolet\src\03_comptime\quote.cpp:529`
- `Bootstrap\Ultraviolet\src\03_comptime\quote.cpp:531`
- `Bootstrap\Ultraviolet\src\03_comptime\quote.cpp:533`
- `Bootstrap\Ultraviolet\src\03_comptime\quote.cpp:537`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\type_expr.cpp:2251`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\type_expr.cpp:2258`

Observed behavior:

`BuildType` handles `ast::TypeFunc` by iterating over `out.params` and, inside
that loop, rebuilding both the current parameter type and `out.ret`. When the
quoted function type has no parameters, the loop body never executes, so the
return type is returned unchanged. The static quote-splice checker does visit
`node.ret` after the parameter loop, so a return-type splice can be accepted as
compatible and then left unresolved by the compile-time quote builder.

Expected behavior:

`TypeFunc(params, ret)` always contains a return type. `ParseParamTypeList` may
produce an empty parameter list, and `WF-Func` requires the return type to be
well formed independently of the parameter count. `QuoteBuild` must therefore
visit and render splices in `ret` for both `() -> T` and non-empty function
types.

Impact:

`uvc` can accept a type quote such as a zero-parameter function type whose
return is supplied by a splice, statically verify that splice as a type splice,
and still return an `Ast::Type` payload containing an unresolved
`SpliceExprNode` in the function return position. Any later emission or
inspection of that AST sees a different payload from the one promised by
`RenderSplice` and `QuoteBuild`.

HelloUltraviolet fixture gap:

The compile-time quote/splice reference includes simple `quote type { i32 }`
and item return-type splice coverage, but it does not include `quote type`
coverage for `() -> $(type_ast)` or any other zero-parameter function type with
a spliced return type.

### UV-AUDIT-0176: Undeclared `Self::Name` associated-type paths are accepted

Severity: Medium

Status: Agent verified; locally reviewed.

Spec anchors:

- `Docs\SPECIFICATION.md:12762`
- `Docs\SPECIFICATION.md:12900`
- `Docs\SPECIFICATION.md:13152`
- `Docs\SPECIFICATION.md:13163`
- `Docs\SPECIFICATION.md:13173`
- `Docs\SPECIFICATION.md:13177`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\typing\type_wf.cpp:132`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\type_wf.cpp:396`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\signature.cpp:289`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\signature.cpp:520`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\signature.cpp:540`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\class_decl.cpp:361`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\class_decl.cpp:452`

Observed behavior:

`TypeWF` treats any two-segment path beginning with `Self` as a well-formed
path. It does not require the second segment to name an associated type declared
by the surrounding class. Class method signature construction then lowers
parameter and return types through `LowerTypeWithWF`; `SubstSelfType` replaces
`Self::Name` only when an associated-type substitution map contains `Name`, and
otherwise leaves the raw `Self::Name` path in place.

Expected behavior:

`Self::Name` should be well formed only when `Name` is an associated-type member
declared by the active class and resolved through the associated-type binding
order: implementation binding first, then class default. A class or method
signature using `Self::Missing` must be rejected instead of admitted as an
ordinary path.

Impact:

Invalid class or implementation signatures can carry unresolved `Self::Name`
types into method tables, implementation signature matching, method-call typing,
and dynamic dispatch checks. This can turn a declaration error into later
unresolved-type behavior or compatibility mismatches that are detached from the
actual source of non-conformance.

HelloUltraviolet fixture gap:

The current associated-type specimens cover valid associated types and missing
implementation bindings, including
`HelloUltraviolet\Source\Reference\Polymorphism\AssociatedTypes.uv:3`,
`HelloUltraviolet\Fixtures\RejectedSource\Polymorphism\AssociatedTypeMissingBinding\Source\Main.uv:3`,
and
`HelloUltraviolet\Fixtures\RejectedSource\Polymorphism\AssociatedTypeUnboundMember\Source\Main.uv:3`.
They do not cover an undeclared `Self::Name` used in a class method parameter or
return type.

Verification note:

The delegated read-only check ran
`uvc build .\HelloUltraviolet --target-profile x86_64-win64 --build-progress off --color never --check`,
which exited `0` with existing warnings and infos.

### UV-AUDIT-0177: Quoted modal-state type splices leave stale modal references

Severity: High

Status: Locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:5375`
- `Docs\SPECIFICATION.md:5378`
- `Docs\SPECIFICATION.md:5380`
- `Docs\SPECIFICATION.md:5382`
- `Docs\SPECIFICATION.md:10622`
- `Docs\SPECIFICATION.md:10625`
- `Docs\SPECIFICATION.md:10644`
- `Docs\SPECIFICATION.md:10668`
- `Docs\SPECIFICATION.md:10670`
- `Docs\SPECIFICATION.md:25518`
- `Docs\SPECIFICATION.md:25524`
- `Docs\SPECIFICATION.md:25537`
- `Docs\SPECIFICATION.md:25542`

Implementation anchors:

- `Bootstrap\Ultraviolet\include\02_source\ast\nodes\ast_types.h:183`
- `Bootstrap\Ultraviolet\include\02_source\ast\nodes\ast_types.h:238`
- `Bootstrap\Ultraviolet\include\02_source\ast\nodes\ast_types.h:239`
- `Bootstrap\Ultraviolet\include\02_source\ast\nodes\ast_types.h:241`
- `Bootstrap\Ultraviolet\include\02_source\ast\nodes\ast_types.h:250`
- `Bootstrap\Ultraviolet\src\02_source\parser\type\state_specific_type.cpp:56`
- `Bootstrap\Ultraviolet\src\02_source\parser\type\state_specific_type.cpp:58`
- `Bootstrap\Ultraviolet\src\02_source\parser\type\state_specific_type.cpp:59`
- `Bootstrap\Ultraviolet\src\03_comptime\quote.cpp:589`
- `Bootstrap\Ultraviolet\src\03_comptime\quote.cpp:591`
- `Bootstrap\Ultraviolet\src\03_comptime\quote.cpp:596`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\type_expr.cpp:2299`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\type_expr.cpp:2300`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\type_lower.cpp:268`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\type_lower.cpp:269`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\type_lower.cpp:270`

Observed behavior:

The parsed AST for a modal-state type stores both `modal_ref` and the duplicated
`path` / `generic_args` fields. `ParseModalStateType` initializes the duplicated
fields and then calls `SyncTypeModalStateFromFields`. During compile-time quote
building, the `TypeModalState` branch rebuilds only `out.generic_args`; it does
not call `SyncTypeModalStateFromFields` afterward. Later lowering reads
`node.modal_ref` through `TypeModalRefPath` and `TypeModalRefArgs`, so it lowers
the original parsed generic arguments rather than the quote-built arguments.

Expected behavior:

For a quoted type payload, every rendered type splice must become part of the
returned `Ast::Type`. When a modal-state type's generic arguments are rewritten
during `QuoteBuild`, the canonical modal reference used by lowering must be
rebuilt from those rewritten arguments, or lowering must use the rebuilt
`generic_args` source of truth.

Impact:

`uvc` can statically validate a type splice inside a modal-state generic
argument and still emit or lower an `Ast::Type` whose `modal_ref` contains the
unresolved original splice node. This can make `GenericModalReference<$(ty)>@S`
behave as if the splice never happened when the quoted type is later emitted or
type-checked, even though the quote builder already produced a rewritten
`generic_args` vector.

HelloUltraviolet fixture gap:

The compile-time quote/splice surfaces cover simple `quote type { i32 }` and
item return-type splices, while the modal reference surface covers concrete
`GenericModalReference<i32>@Present`. There is no fixture that quotes or emits a
generic modal-state type whose generic argument is supplied by a type splice.

### UV-AUDIT-0178: Quoted multi-parameter function types evaluate return splices early

Severity: Medium

Status: Locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:572`
- `Docs\SPECIFICATION.md:12041`
- `Docs\SPECIFICATION.md:12044`
- `Docs\SPECIFICATION.md:25537`
- `Docs\SPECIFICATION.md:25542`

Implementation anchors:

- `Bootstrap\Ultraviolet\include\02_source\ast\nodes\ast_types.h:83`
- `Bootstrap\Ultraviolet\include\02_source\ast\nodes\ast_types.h:84`
- `Bootstrap\Ultraviolet\include\02_source\ast\nodes\ast_types.h:85`
- `Bootstrap\Ultraviolet\src\03_comptime\quote.cpp:529`
- `Bootstrap\Ultraviolet\src\03_comptime\quote.cpp:531`
- `Bootstrap\Ultraviolet\src\03_comptime\quote.cpp:532`
- `Bootstrap\Ultraviolet\src\03_comptime\quote.cpp:533`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\type_expr.cpp:2251`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\type_expr.cpp:2252`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\type_expr.cpp:2258`

Observed behavior:

`BuildType` rebuilds `ast::TypeFunc` by looping through `out.params` and
rebuilding `out.ret` inside the same loop body as each parameter type. For a
function type with two or more parameters, a splice in the return type is
evaluated after the first parameter is rebuilt and before later parameter types
are rebuilt. The static quote-splice checker traverses all parameters first and
then the return type, which matches the written source order.

Expected behavior:

The source order of `TypeFunc([params], ret)` places every parameter type before
the return type. `QuoteBuild` must evaluate and render type splices in that same
left-to-right source order: all parameter splices first, then the return-type
splice exactly once.

Impact:

Compile-time splice expressions in function-type quotes can observe or produce
effects in the wrong order. A quote such as a two-parameter function type with a
splice in the second parameter and another splice in the return type evaluates
the return splice before the second parameter splice, violating the ordered
substitution guarantee for returned `Ast::Type` payloads.

HelloUltraviolet fixture gap:

The compile-time quote/splice fixtures do not include a `quote type` specimen for
a multi-parameter function type with splices in both a later parameter and the
return type, so this ordering bug is not covered by the current test surface.

### UV-AUDIT-0179: Empty source-native test names are rejected despite valid string syntax

Severity: Low

Status: Locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:2237`
- `Docs\SPECIFICATION.md:7233`
- `Docs\SPECIFICATION.md:7248`
- `Docs\SPECIFICATION.md:7249`
- `Docs\SPECIFICATION.md:7260`
- `Docs\SPECIFICATION.md:7262`
- `Docs\SPECIFICATION.md:7349`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\02_source\attributes\attribute_registry.cpp:52`
- `Bootstrap\Ultraviolet\src\02_source\attributes\attribute_registry.cpp:56`
- `Bootstrap\Ultraviolet\src\02_source\attributes\attribute_registry.cpp:57`
- `Bootstrap\Ultraviolet\src\02_source\attributes\attribute_registry.cpp:269`
- `Bootstrap\Ultraviolet\src\02_source\attributes\attribute_registry.cpp:288`
- `Bootstrap\Ultraviolet\src\02_source\attributes\attribute_registry.cpp:290`
- `Bootstrap\Ultraviolet\src\02_source\attributes\attribute_registry.cpp:311`

Observed behavior:

`ValidateTestAttributeArgs` validates `name: ...` with
`IsNonEmptyStringLiteralToken`. That helper requires a `StringLiteral` token and
also rejects the token when `NormalizeAttrLiteral(tok.lexeme)` is empty. The
same helper is used for `covers(...)`, where non-empty text is required.

Expected behavior:

The string-literal grammar permits zero string characters. The source-native
test-attribute grammar and `AttrArgsOk(test, args)` rule require only
`name: string_literal` for a display name. The explicit non-empty requirement is
attached to `covers(string_literal)` coverage references, not to the optional
`name` argument.

Impact:

A valid source-native test with `#test(name: "")` is rejected as
`E-TST-0101` even though the spec defines the fully-qualified procedure path as
the fallback only when no `name` argument is present. This narrows the accepted
language beyond the specification and conflates display-label policy with
coverage-reference validity.

HelloUltraviolet fixture gap:

The source-native metadata fixtures cover omitted names, ordinary non-empty
names, coverage ordering, malformed unknown keys, duplicate names, and malformed
coverage references. They do not include an accepted source-native test with an
explicit empty display name.

### UV-AUDIT-0180: Sync resume panic lowers to a normal zero result

Severity: High

Status: Locally verified after delegated review.

Spec anchors:

- `Docs\SPECIFICATION.md:24342`
- `Docs\SPECIFICATION.md:24344`
- `Docs\SPECIFICATION.md:24348`
- `Docs\SPECIFICATION.md:24351`
- `Docs\SPECIFICATION.md:28742`
- `Docs\SPECIFICATION.md:28767`
- `Docs\SPECIFICATION.md:28806`
- `Docs\SPECIFICATION.md:28812`
- `Docs\SPECIFICATION.md:28814`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\sync.cpp:282`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\sync.cpp:382`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\sync.cpp:384`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\sync.cpp:404`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\sync.cpp:409`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\sync.cpp:416`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\sync.cpp:427`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\sync.cpp:483`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\sync.cpp:497`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\llvm_emit_helpers.cpp:1198`
- `Bootstrap\Ultraviolet\runtime\src\memory\async.c:137`
- `Bootstrap\Ultraviolet\runtime\src\memory\async.c:169`

Observed behavior:

`IRSync` loads the current panic-record pointer and passes it to
`EmitAsyncResumeRuntimeCall`. The runtime `async::resume` helper forwards that
pointer into the suspended frame's resume function. After the resume call,
`IRSync` branches to `sync.panic` when the panic flag is set, but that block
stores `Constant::getNullValue(expected)` into the result slot and then branches
to `sync.merge`. The merge block publishes the loaded/default result through
`emitter.SetTempValue`.

Expected behavior:

When resuming a suspended async computation records a panic, the `sync`
expression must continue as panic control. `PanicCheck` is specified to evaluate
to `Ctrl(Panic)` when the panic record is set, and `LowerPanic` writes panic
state while producing panic control. `SyncLoopIR` may return completed values or
failed-state error values, but it cannot turn an already-recorded panic into an
ordinary expression value.

Impact:

Code after a `sync` expression can execute with a fabricated zero/null result
even though the resumed async body panicked. That can produce side effects after
the specified panic point, disturb cleanup ordering, and make observable
behavior depend on some later boundary noticing the still-set panic record
instead of `sync` immediately transferring panic control.

HelloUltraviolet fixture gap:

The async composition references cover successful `sync`, failed-state `sync`,
and suspended-resume `sync` in
`HelloUltraviolet\Source\Reference\Async\CompositionForms.uv:183`,
`HelloUltraviolet\Source\Reference\Async\CompositionForms.uv:207`, and
`HelloUltraviolet\Source\Reference\Async\CompositionForms.uv:213`. The artifact
surface checks that async resume IR exists in
`HelloUltraviolet\Source\Audit\AsyncArtifactExecution.uv:338`, but no fixture
resumes a suspended async body that panics and asserts that `sync` propagates
panic control rather than merging with a zero result.

### UV-AUDIT-0181: Yield-from resume panic lowers to a normal zero result

Severity: High

Status: Locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:23374`
- `Docs\SPECIFICATION.md:23375`
- `Docs\SPECIFICATION.md:23377`
- `Docs\SPECIFICATION.md:23381`
- `Docs\SPECIFICATION.md:23390`
- `Docs\SPECIFICATION.md:23397`
- `Docs\SPECIFICATION.md:23404`
- `Docs\SPECIFICATION.md:23449`
- `Docs\SPECIFICATION.md:23453`
- `Docs\SPECIFICATION.md:28742`
- `Docs\SPECIFICATION.md:28812`
- `Docs\SPECIFICATION.md:28814`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\yield_from.cpp:802`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\yield_from.cpp:803`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\yield_from.cpp:896`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\yield_from.cpp:920`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\yield_from.cpp:925`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\yield_from.cpp:932`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\yield_from.cpp:943`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\yield_from.cpp:1063`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\yield_from.cpp:1066`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\yield_from.cpp:1084`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\llvm_emit_helpers.cpp:1198`

Observed behavior:

The resume body in `IRYieldFrom` loads the current panic-record pointer, passes
it to `EmitAsyncResumeRuntimeCall`, stores the resumed inner async value, and
then branches to `yield_from.panic` when the panic flag is set. That panic block
stores `Constant::getNullValue(expected)` into the yield-from result slot and
branches to `yield_from.cont`, where the result slot is loaded and published via
`emitter.SetTempValue(y.result, result_value)`.

Expected behavior:

`EvalSigma-YieldFrom-Resume` delegates to `EvalSigma(Resume(s, i))` and then
continues through `EvalYieldFromContinue` only for the resumed async value
outcome. A resume that records a panic must instead obey the cleanup lowering
interface: `PanicCheck` evaluates to `Ctrl(Panic)` when the panic record is set,
and `LowerPanic` produces panic control while writing the panic record. The
`YieldFromResumeIR` lowering cannot convert a recorded panic into an ordinary
value-producing continuation.

Impact:

An outer async frame resumed at a `yield from` continuation can continue after a
delegated inner async body panics, with the yield-from expression reading a
fabricated zero/null result. That allows side effects and state transitions to
occur after the specified panic point and can make panic propagation depend on a
later boundary observing the still-set panic record.

HelloUltraviolet fixture gap:

The async artifact fixture covers successful `yield from`, failed-state
`yield from`, and manual suspended resume in
`HelloUltraviolet\Fixtures\ArtifactProjects\AsyncObligations\Source\Main.uv:46`,
`HelloUltraviolet\Fixtures\ArtifactProjects\AsyncObligations\Source\Main.uv:77`,
and
`HelloUltraviolet\Fixtures\ArtifactProjects\AsyncObligations\Source\Main.uv:175`.
The artifact audit checks for `yield_from.loop` and the async resume runtime
call in `HelloUltraviolet\Source\Audit\AsyncArtifactExecution.uv:338`, but no
fixture resumes a delegated async body that panics and asserts panic control
rather than normal yield-from completion.

### UV-AUDIT-0182: Return-mode race resume panic lowers to a normal zero result

Severity: High

Status: Locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:23976`
- `Docs\SPECIFICATION.md:23979`
- `Docs\SPECIFICATION.md:23983`
- `Docs\SPECIFICATION.md:23996`
- `Docs\SPECIFICATION.md:23999`
- `Docs\SPECIFICATION.md:24000`
- `Docs\SPECIFICATION.md:24002`
- `Docs\SPECIFICATION.md:24005`
- `Docs\SPECIFICATION.md:24008`
- `Docs\SPECIFICATION.md:24353`
- `Docs\SPECIFICATION.md:24357`
- `Docs\SPECIFICATION.md:28742`
- `Docs\SPECIFICATION.md:28812`
- `Docs\SPECIFICATION.md:28814`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\race_return.cpp:41`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\race_return.cpp:42`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\race_return.cpp:43`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\race_return.cpp:44`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\race_return.cpp:257`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\race_return.cpp:284`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\race_return.cpp:285`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\race_return.cpp:406`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\race_return.cpp:411`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\race_return.cpp:418`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\race_return.cpp:429`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\race_return.cpp:444`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\race_return.cpp:445`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\race_return.cpp:454`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\llvm_emit_helpers.cpp:1198`

Observed behavior:

`IRRaceReturn` explicitly records the race return and resume spec rules, loads
the current panic-record pointer, and passes that pointer into
`EmitAsyncResumeRuntimeCall` for suspended race arms. When the panic flag is set
after an arm resume, lowering branches to `race.return.panic`, stores
`Constant::getNullValue(expected)` in the race result slot, and then falls
through `race.return.merge`, where the slot is loaded and published via
`emitter.SetTempValue(r.result, out)`.

Expected behavior:

`RaceStepReturn-Continue` resumes a suspended arm and recursively evaluates the
next race step only when `EvalSigma(Resume(a_j, ()))` produces `Val(a_j')`.
The panic framework requires a set panic record to become `Ctrl(Panic)` through
`PanicCheck`; `LowerPanic` likewise produces panic control after writing the
record. `RaceSelectIR(return)` therefore cannot translate a panic recorded by a
resumed arm into an ordinary race result value.

Impact:

A return-mode `race` whose selected suspended arm panics during resume can
produce a fabricated zero/null race result and continue execution. This skips
the required panic-control transfer at the race expression boundary, can run
handler or following-expression side effects after the panic point, and leaves
remaining-arm cancellation and cleanup behavior dependent on later control-flow
encounters rather than the race step that observed the panic.

HelloUltraviolet fixture gap:

Return-mode race coverage exercises completed values, failed-state values,
tuple-pattern handlers, and suspended unit-output operands in
`HelloUltraviolet\Source\Reference\Async\CompositionForms.uv:265`,
`HelloUltraviolet\Source\Reference\Async\CompositionForms.uv:279`, and
`HelloUltraviolet\Source\Reference\Async\CompositionForms.uv:288`, with matching
artifact fixtures in
`HelloUltraviolet\Fixtures\ArtifactProjects\AsyncObligations\Source\CompositionForms.uv:251`,
`HelloUltraviolet\Fixtures\ArtifactProjects\AsyncObligations\Source\CompositionForms.uv:264`,
and
`HelloUltraviolet\Fixtures\ArtifactProjects\AsyncObligations\Source\CompositionForms.uv:272`.
The artifact audit checks for `race.return.resume` in
`HelloUltraviolet\Source\Audit\AsyncArtifactCompositionExecution.uv:172`, but no
fixture resumes a return-mode race arm that records a panic and asserts panic
control rather than a normal race result.

### UV-AUDIT-0183: All-expression resume panic lowers to a normal zero result

Severity: High

Status: Locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:24119`
- `Docs\SPECIFICATION.md:24122`
- `Docs\SPECIFICATION.md:24130`
- `Docs\SPECIFICATION.md:24139`
- `Docs\SPECIFICATION.md:24142`
- `Docs\SPECIFICATION.md:24145`
- `Docs\SPECIFICATION.md:24166`
- `Docs\SPECIFICATION.md:24168`
- `Docs\SPECIFICATION.md:24174`
- `Docs\SPECIFICATION.md:24176`
- `Docs\SPECIFICATION.md:24342`
- `Docs\SPECIFICATION.md:24374`
- `Docs\SPECIFICATION.md:24376`
- `Docs\SPECIFICATION.md:24379`
- `Docs\SPECIFICATION.md:28742`
- `Docs\SPECIFICATION.md:28812`
- `Docs\SPECIFICATION.md:28814`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\all.cpp:99`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\all.cpp:100`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\all.cpp:101`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\all.cpp:243`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\all.cpp:268`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\all.cpp:269`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\all.cpp:384`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\all.cpp:389`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\all.cpp:396`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\all.cpp:407`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\all.cpp:460`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\all.cpp:461`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\all.cpp:474`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\llvm_emit_helpers.cpp:1198`

Observed behavior:

`IRAll` records `AllJoinIR`, `AllRuntimeSemantics`, and `EvalSigma-All`, then
loads the current panic-record pointer and passes it to
`EmitAsyncResumeRuntimeCall` while joining suspended operands. If the panic flag
is set after resume, lowering branches to `all.panic`, stores
`Constant::getNullValue(expected)` in the `all` result slot, branches to
`all.merge`, and publishes the loaded slot through
`emitter.SetTempValue(all.result, out)`.

Expected behavior:

`AllStep-Resume` advances the join only when `EvalSigma(Resume(a_j, ()))`
produces `Val(a_j')`; `AllLoop-Continue` recurses over that updated state.
When the resume records a panic instead, the cleanup lowering interface requires
`PanicCheck` to produce `Ctrl(Panic)` for the set panic record. `AllJoinIR`
cannot replace that panic control with a normal tuple/union result value.

Impact:

An `all` expression whose suspended operand panics during resume can continue
with a fabricated zero/null result. This hides the panic at the join boundary,
can allow following side effects to execute, and can skip the specified
first-failure cancellation behavior because the path is neither a normal
`@Failed` async value nor a successful completed operand.

HelloUltraviolet fixture gap:

The async composition reference covers successful `all`, failed-state `all`,
suspended operand resume, and single-result tuple construction in
`HelloUltraviolet\Source\Reference\Async\CompositionForms.uv:227`,
`HelloUltraviolet\Source\Reference\Async\CompositionForms.uv:241`, and
`HelloUltraviolet\Source\Reference\Async\CompositionForms.uv:250`, with matching
artifact fixtures in
`HelloUltraviolet\Fixtures\ArtifactProjects\AsyncObligations\Source\CompositionForms.uv:217`,
`HelloUltraviolet\Fixtures\ArtifactProjects\AsyncObligations\Source\CompositionForms.uv:230`,
and
`HelloUltraviolet\Fixtures\ArtifactProjects\AsyncObligations\Source\CompositionForms.uv:238`.
The artifact audit checks for `all.resume` in
`HelloUltraviolet\Source\Audit\AsyncArtifactCompositionExecution.uv:181`, but no
fixture resumes an `all` operand that records a panic and asserts panic control
rather than normal `all` completion.

### UV-AUDIT-0184: Async iterator resume panic exits as normal loop completion

Severity: High

Status: Locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:16970`
- `Docs\SPECIFICATION.md:17102`
- `Docs\SPECIFICATION.md:17107`
- `Docs\SPECIFICATION.md:17115`
- `Docs\SPECIFICATION.md:17118`
- `Docs\SPECIFICATION.md:17132`
- `Docs\SPECIFICATION.md:17135`
- `Docs\SPECIFICATION.md:23477`
- `Docs\SPECIFICATION.md:23642`
- `Docs\SPECIFICATION.md:23653`
- `Docs\SPECIFICATION.md:27269`
- `Docs\SPECIFICATION.md:27271`
- `Docs\SPECIFICATION.md:27301`
- `Docs\SPECIFICATION.md:27304`
- `Docs\SPECIFICATION.md:28742`
- `Docs\SPECIFICATION.md:28812`
- `Docs\SPECIFICATION.md:28814`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\control\loop.cpp:141`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\control\loop.cpp:462`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\control\loop.cpp:490`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\control\loop.cpp:511`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\control\loop.cpp:541`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\control\loop.cpp:546`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\control\loop.cpp:554`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\control\loop.cpp:566`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\control\loop.cpp:1172`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\control\loop.cpp:1189`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\control\loop.cpp:1194`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\llvm_emit_helpers.cpp:1198`

Observed behavior:

Async iterator lowering in `IRLoop` loads the current panic-record pointer and
passes it to `EmitAsyncResumeRuntimeCall` in the `loop.resume` block. If the
panic flag is set after resume, the generated branch targets `loop.end`.
`loop.end` then publishes the loop result from `loop_result_slot`, or
`DefaultFor(loop.result)` when there is no result slot, exactly like ordinary
iterator exhaustion or completed async state.

Expected behavior:

The async iterator case is the Chapter 21 form of ordinary loop iteration, and
the loop semantics preserve `Ctrl(Panic)` for loop-body or iterator control
paths. The IR loop execution rules likewise return `Ctrl(κ)` for panic and
abort control. When a resumed async iterator records a panic, the cleanup
framework requires `PanicCheck` to produce `Ctrl(Panic)`; it must not be treated
as `LoopIter-Done` or normal loop completion.

Impact:

A `loop value in async_stream` whose stream body panics during resume can exit
as if the async iterator completed normally. Code after the loop can execute
with the loop's default result value, and the observable panic boundary is lost
unless a later unrelated boundary notices the still-set panic record.

HelloUltraviolet fixture gap:

The async composition reference covers async iteration over a yielding stream
and failed-state propagation in
`HelloUltraviolet\Source\Reference\Async\CompositionForms.uv:66`,
`HelloUltraviolet\Source\Reference\Async\CompositionForms.uv:477`, and
`HelloUltraviolet\Source\Reference\Async\CompositionForms.uv:484`, with matching
artifact fixtures in
`HelloUltraviolet\Fixtures\ArtifactProjects\AsyncObligations\Source\CompositionForms.uv:66`
and
`HelloUltraviolet\Fixtures\ArtifactProjects\AsyncObligations\Source\IterationAndUntilForms.uv:31`.
The fixture set does not resume an async iterator whose body records a panic and
assert that the loop propagates panic control instead of completing normally.

### UV-AUDIT-0185: Async combinator wrappers ignore recorded resume and callback panics

Severity: High

Status: Locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:24206`
- `Docs\SPECIFICATION.md:24208`
- `Docs\SPECIFICATION.md:24234`
- `Docs\SPECIFICATION.md:24236`
- `Docs\SPECIFICATION.md:24241`
- `Docs\SPECIFICATION.md:24243`
- `Docs\SPECIFICATION.md:24263`
- `Docs\SPECIFICATION.md:24265`
- `Docs\SPECIFICATION.md:24291`
- `Docs\SPECIFICATION.md:24293`
- `Docs\SPECIFICATION.md:24319`
- `Docs\SPECIFICATION.md:24321`
- `Docs\SPECIFICATION.md:24326`
- `Docs\SPECIFICATION.md:24328`
- `Docs\SPECIFICATION.md:24381`
- `Docs\SPECIFICATION.md:24383`
- `Docs\SPECIFICATION.md:24388`
- `Docs\SPECIFICATION.md:24393`
- `Docs\SPECIFICATION.md:24398`
- `Docs\SPECIFICATION.md:24403`
- `Docs\SPECIFICATION.md:28742`
- `Docs\SPECIFICATION.md:28812`
- `Docs\SPECIFICATION.md:28814`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\call\direct.cpp:763`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\call\direct.cpp:778`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\call\direct.cpp:803`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\call\direct.cpp:817`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\call\direct.cpp:818`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\call\direct.cpp:994`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\call\direct.cpp:1015`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\call\direct.cpp:1019`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\call\direct.cpp:1061`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\call\direct.cpp:1066`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\call\direct.cpp:1110`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\call\direct.cpp:1115`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\call\direct.cpp:1118`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\call\direct.cpp:1185`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\call\direct.cpp:1192`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\call\direct.cpp:1194`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\call\direct.cpp:1302`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\call\direct.cpp:1322`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\call\direct.cpp:1326`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\call\direct.cpp:1331`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\call\direct.cpp:1398`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\call\direct.cpp:1403`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\call\direct.cpp:1408`
- `Bootstrap\Ultraviolet\runtime\src\memory\async.c:45`
- `Bootstrap\Ultraviolet\runtime\src\memory\async.c:66`
- `Bootstrap\Ultraviolet\runtime\src\memory\async.c:70`
- `Bootstrap\Ultraviolet\runtime\src\memory\async.c:71`
- `Bootstrap\Ultraviolet\runtime\src\memory\async.c:72`
- `Bootstrap\Ultraviolet\runtime\src\memory\async.c:74`
- `Bootstrap\Ultraviolet\runtime\src\memory\async.c:173`
- `Bootstrap\Ultraviolet\runtime\src\memory\async.c:177`
- `Bootstrap\Ultraviolet\runtime\src\memory\async.c:199`
- `Bootstrap\Ultraviolet\runtime\src\memory\async.c:203`

Observed behavior:

The shared combinator-lowering helper `invoke_callable` loads `kPanicOutName`,
passes it as the hidden panic argument to callback calls, lowers the call, and
immediately returns `inner.result`. No `PanicCheck` or equivalent flag branch is
emitted before the returned value is consumed. The shared `emit_resume_step`
helper likewise passes the same panic pointer to `EmitAsyncResumeRuntimeCall`,
materializes the returned async state, and stores it back into `async_slot`
without checking whether the resume recorded a panic.

That unchecked value is then used by each wrapper: `map` stores the callback
result into the suspended payload, `filter` branches on the predicate result,
`fold` stores the callback result as the next accumulator, and `chain` stores
the callback result as the next async value. `take` delegates to
`BuiltinSymAsyncTake`; the runtime wrapper passes `panic_out` into the source
resume, copies the returned state into `take_frame->source`, and returns or
stores that state without checking whether `panic_out` was set. The exported
`async::take` entry point also accepts `panic_out` but marks it unused.

Expected behavior:

The Chapter 21 combinator rules advance only when source resume or callback
application yields the stated normal value. `EvalSigma-Map-Resume-Yield`,
`EvalSigma-Filter-Resume-Pass`, `EvalSigma-Filter-Resume-Skip`,
`EvalSigma-Take-Resume-Yield`, `EvalSigma-Fold-Resume-Accumulate`, and
`EvalSigma-Chain-Resume-Source-Complete` all require ordinary `@Suspended`,
`@Completed`, or `Val(...)` premises before the wrapper produces its next async
state. Chapter 24 requires a set panic record to lower as `Ctrl(Panic)`.
Therefore combinator wrappers must not reinterpret a recorded panic as a bool,
payload, accumulator, chained async, or successful `take` state.

Impact:

Panics raised by a combinator source async or by a `map`, `filter`, `fold`, or
`chain` callback can be converted into ordinary wrapper output. Observable
effects include `filter` choosing skip/pass from a default predicate value,
`map` and `fold` publishing default payload or accumulator values, `chain`
continuing with a default async value, and `take` decrementing or completing as
though the source resumed normally.

HelloUltraviolet fixture gap:

`HelloUltraviolet` covers successful and failed combinator wrapper paths in
`HelloUltraviolet\Source\Reference\Async\CompositionForms.uv:407`,
`HelloUltraviolet\Source\Reference\Async\CompositionForms.uv:414`,
`HelloUltraviolet\Source\Reference\Async\CompositionForms.uv:421`,
`HelloUltraviolet\Source\Reference\Async\CompositionForms.uv:428`, and
`HelloUltraviolet\Source\Reference\Async\CompositionForms.uv:436`, with
runtime-edge coverage in
`HelloUltraviolet\Source\Reference\Async\CombinatorRuntimeForms.uv:34`,
`HelloUltraviolet\Source\Reference\Async\CombinatorRuntimeForms.uv:50`,
`HelloUltraviolet\Source\Reference\Async\CombinatorRuntimeForms.uv:64`,
`HelloUltraviolet\Source\Reference\Async\CombinatorRuntimeForms.uv:70`,
`HelloUltraviolet\Source\Reference\Async\CombinatorRuntimeForms.uv:104`,
`HelloUltraviolet\Source\Reference\Async\CombinatorRuntimeForms.uv:115`, and
`HelloUltraviolet\Source\Reference\Async\CombinatorRuntimeForms.uv:125`.
The artifact project mirrors those paths in
`HelloUltraviolet\Fixtures\ArtifactProjects\AsyncObligations\Source\CombinatorRuntimeForms.uv:34`,
`HelloUltraviolet\Fixtures\ArtifactProjects\AsyncObligations\Source\CombinatorRuntimeForms.uv:40`,
`HelloUltraviolet\Fixtures\ArtifactProjects\AsyncObligations\Source\CombinatorRuntimeForms.uv:48`,
`HelloUltraviolet\Fixtures\ArtifactProjects\AsyncObligations\Source\CombinatorRuntimeForms.uv:54`,
`HelloUltraviolet\Fixtures\ArtifactProjects\AsyncObligations\Source\CombinatorRuntimeForms.uv:64`, and
`HelloUltraviolet\Fixtures\ArtifactProjects\AsyncObligations\Source\CombinatorRuntimeForms.uv:74`.
There is no fixture where a combinator source resume or callback records a
panic and asserts that the wrapper propagates panic control instead of producing
ordinary output.

### UV-AUDIT-0186: `PanicSym` runtime ABI is emitted as pointer-to-code

Severity: High

Status: Locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:28615`
- `Docs\SPECIFICATION.md:28616`
- `Docs\SPECIFICATION.md:28617`
- `Docs\SPECIFICATION.md:28763`
- `Docs\SPECIFICATION.md:28765`
- `Docs\SPECIFICATION.md:29269`
- `Docs\SPECIFICATION.md:29281`
- `Docs\SPECIFICATION.md:29308`
- `Docs\SPECIFICATION.md:29309`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\05_codegen\intrinsics\intrinsics_interface.cpp:916`
- `Bootstrap\Ultraviolet\src\05_codegen\intrinsics\intrinsics_interface.cpp:917`
- `Bootstrap\Ultraviolet\src\05_codegen\intrinsics\intrinsics_interface.cpp:918`
- `Bootstrap\Ultraviolet\src\05_codegen\intrinsics\intrinsics_interface.cpp:919`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\checks\panic.cpp:122`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\checks\panic.cpp:126`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\checks\panic.cpp:128`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\checks\panic.cpp:143`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\checks\panic.cpp:145`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\checks\panic.cpp:147`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\module_emit.cpp:818`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\module_emit.cpp:822`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\module_emit.cpp:824`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\module_emit.cpp:836`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\module_emit.cpp:841`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\module_emit.cpp:844`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\hosted_emit.cpp:1474`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\hosted_emit.cpp:1478`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\hosted_emit.cpp:1479`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\hosted_emit.cpp:2207`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\hosted_emit.cpp:2209`
- `Bootstrap\Ultraviolet\runtime\include\uv_rt.h:332`
- `Bootstrap\Ultraviolet\runtime\src\core\panic.c:3`
- `Bootstrap\Ultraviolet\runtime\src\core\panic.c:4`
- `Bootstrap\Ultraviolet\runtime\src\core\panic.c:10`
- `Bootstrap\Ultraviolet\runtime\src\core\panic.c:13`
- `Bootstrap\Ultraviolet\runtime\src\core\panic.c:15`

Observed behavior:

The compiler's runtime-intrinsic table describes `RuntimePanicSym()` as a
noreturn runtime function with one `u32` parameter named `code` and return type
`!`, which matches the public specification. The actual generated panic calls do
not use that signature. Entry-stub panic lowering creates or reuses
`RuntimePanicSym()` as a `void` function whose single parameter is the backend
opaque pointer type, materializes the loaded panic code into a stack slot, and
passes the stack-slot address. Module-emission and hosted-boundary panic paths
repeat the same pointer-shaped call pattern. The runtime header and C
implementation also define the mangled panic symbol as taking `const uint32_t*`
and dereferencing it before exiting.

Expected behavior:

`EntryStubSpec` requires `CallIR(PanicSym, [c])` when the entry panic record is
set, and `RuntimeSig(PanicSym)` defines that symbol as `code: u32 -> !`.
`DeclAttrsOk` also requires the panic declaration to carry the never-return
runtime semantics. A conforming lowering must therefore call `PanicSym` with the
panic code value, not with the address of a temporary holding that value.

Impact:

Generated LLVM and runtime headers expose a panic ABI that contradicts both the
specification and the compiler's own runtime function metadata. Any path that
uses the metadata-derived declaration can collide with the ad hoc `void(ptr)`
declaration, and generated code that is inspected against `RuntimeSig(PanicSym)`
will report or execute the wrong call shape at the executable, module, and
hosted-boundary panic exits.

HelloUltraviolet fixture gap:

`HelloUltraviolet` has panic-exit artifacts such as
`HelloUltraviolet\Fixtures\ArtifactProjects\SliceRangeOOBPanic\Source\Main.uv:11`,
`HelloUltraviolet\Fixtures\ArtifactProjects\RawPointerNullWritePanic\Source\Main.uv:13`,
`HelloUltraviolet\Fixtures\ArtifactProjects\ReducedEmptyDispatchPanic\Expected.uv:3`,
and catalog coverage for panic rules in
`HelloUltraviolet\Source\Audit\Catalog\CommonLoweringProgramLifecycleAndBackend\CleanupDropAndUnwindingFramework.uv:83`,
`HelloUltraviolet\Source\Audit\Catalog\CommonLoweringProgramLifecycleAndBackend\BackendRequirements.uv:920`, and
`HelloUltraviolet\Source\Audit\Catalog\CommonLoweringProgramLifecycleAndBackend\BackendRequirements.uv:938`.
Those checks observe panic behavior and rule markers, but they do not assert
that the emitted `PanicSym` declaration and call use the specified `u32 -> !`
runtime ABI.

### UV-AUDIT-0187: `copy` is missing from expression-start gates

Severity: Medium

Status: Locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:17178`
- `Docs\SPECIFICATION.md:17203`
- `Docs\SPECIFICATION.md:19672`
- `Docs\SPECIFICATION.md:23061`
- `Docs\SPECIFICATION.md:30675`
- `Docs\SPECIFICATION.md:30715`
- `Docs\SPECIFICATION.md:30718`
- `Docs\SPECIFICATION.md:30978`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\02_source\parser\expr\unary.cpp:290`
- `Bootstrap\Ultraviolet\src\02_source\parser\expr\unary.cpp:291`
- `Bootstrap\Ultraviolet\src\02_source\parser\expr\unary.cpp:296`
- `Bootstrap\Ultraviolet\src\02_source\parser\expr\unary.cpp:297`
- `Bootstrap\Ultraviolet\src\02_source\parser\stmt\expr_stmt.cpp:206`
- `Bootstrap\Ultraviolet\src\02_source\parser\stmt\expr_stmt.cpp:213`
- `Bootstrap\Ultraviolet\src\02_source\parser\stmt\expr_stmt.cpp:218`
- `Bootstrap\Ultraviolet\src\02_source\parser\stmt\parse_stmt.cpp:309`
- `Bootstrap\Ultraviolet\src\02_source\parser\stmt\parse_stmt.cpp:314`
- `Bootstrap\Ultraviolet\src\02_source\parser\stmt\stmt_common.cpp:579`
- `Bootstrap\Ultraviolet\src\02_source\parser\stmt\stmt_common.cpp:583`
- `Bootstrap\Ultraviolet\src\02_source\parser\expr\expr_common.cpp:455`
- `Bootstrap\Ultraviolet\src\02_source\parser\expr\expr_common.cpp:468`
- `Bootstrap\Ultraviolet\src\02_source\parser\expr\range.cpp:63`
- `Bootstrap\Ultraviolet\src\02_source\parser\expr\range.cpp:129`
- `Bootstrap\Ultraviolet\src\02_source\parser\expr\wait_expr.cpp:77`

Observed behavior:

`ParseUnary` recognizes the reserved keyword `copy`, records
`Parse-Unary-Copy`, parses a unary operand, and constructs `CopyExpr`.
However, the expression-start predicates that decide whether to enter ordinary
expression parsing omit that keyword. `IsExprStartToken` rejects `copy` before
expression-statement parsing and before block-tail speculative parsing.
`IsExprStart` also rejects `copy`, so range-tail and `wait` operand lookahead
classify `copy` as absent before they delegate to the expression parser.

Expected behavior:

The grammar makes `copy_expr` a primary expression and defines it as
`"copy" unary_expr`. Expression statements, optional block tails, range bounds,
and `wait` operands all accept an `expression`, so their start-token gates must
admit `copy` and then delegate to the same `ParseUnary-Copy` path used in
initializer, return-value, and argument positions.

Impact:

Valid `copy` expressions can be rejected or split into a different parse shape
before semantic checking. That changes the owning diagnostic phase and can hide
the intended copy-specific typing or provenance diagnostics behind earlier
syntax recovery.

HelloUltraviolet fixture gap:

`HelloUltraviolet` covers `copy` in initializers, returns, call arguments, and
non-Bitcopy diagnostics, including
`HelloUltraviolet\Fixtures\AcceptedProjects\ExpressionSemantics\Source\Library.uv:425`,
`HelloUltraviolet\Fixtures\AcceptedProjects\ExpressionSemantics\Source\Library.uv:447`,
`HelloUltraviolet\Fixtures\RejectedSource\Expressions\CallCopyNonBitcopy\Source\Main.uv:13`,
and
`HelloUltraviolet\Fixtures\RejectedSource\Expressions\ValueUseNonBitcopyPlace\Source\Main.uv:5`.
It does not exercise `copy` as an expression statement, block-tail expression,
range bound, or `wait` operand.

### UV-AUDIT-0188: Quoted splice expressions are missing from range and `wait` lookahead

Severity: Medium

Status: Locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:9438`
- `Docs\SPECIFICATION.md:9451`
- `Docs\SPECIFICATION.md:9471`
- `Docs\SPECIFICATION.md:9476`
- `Docs\SPECIFICATION.md:23061`
- `Docs\SPECIFICATION.md:23076`
- `Docs\SPECIFICATION.md:25427`
- `Docs\SPECIFICATION.md:25515`
- `Docs\SPECIFICATION.md:30675`
- `Docs\SPECIFICATION.md:31004`
- `Docs\SPECIFICATION.md:31007`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\02_source\parser\expr\primary.cpp:249`
- `Bootstrap\Ultraviolet\src\02_source\parser\expr\primary.cpp:287`
- `Bootstrap\Ultraviolet\src\02_source\parser\expr\primary.cpp:381`
- `Bootstrap\Ultraviolet\src\02_source\parser\expr\primary.cpp:384`
- `Bootstrap\Ultraviolet\src\02_source\parser\expr\expr_common.cpp:455`
- `Bootstrap\Ultraviolet\src\02_source\parser\expr\expr_common.cpp:462`
- `Bootstrap\Ultraviolet\src\02_source\parser\expr\range.cpp:63`
- `Bootstrap\Ultraviolet\src\02_source\parser\expr\range.cpp:129`
- `Bootstrap\Ultraviolet\src\02_source\parser\expr\wait_expr.cpp:77`

Observed behavior:

`ParsePrimary` can parse `$(` as `SpliceExprNode` and `$ident` as an identifier
splice, but the shared `IsExprStart` predicate used by range and `wait`
lookahead does not include the `$` operator. A quoted range such as a to-bound
or exclusive upper bound beginning with a splice is therefore classified as a
full/from range before `ParsePrimary` can see the splice. A quoted `wait`
operand beginning with a splice is likewise rejected by `TryParseWaitExpr`
before the ordinary expression parser is invoked.

Expected behavior:

The quote rules state that quoted expression parsing is ordinary expression
parsing extended with `SpliceExprNode` and `SpliceIdentNode`, and the grammar
admits splice forms in primary-expression position. Range bounds and `wait`
operands are expressions, so in quoted expression bodies their lookahead must
accept splice-start tokens and then parse the bound or operand as the specified
expression.

Impact:

Valid quoted expression fragments with range-bound or `wait`-operand splices
are rejected or misparsed before splice compatibility and quote construction
run. Macro authors cannot reliably compose `Ast::Expr` fragments in these
nested positions even though the specification makes them ordinary expression
positions.

HelloUltraviolet fixture gap:

`HelloUltraviolet` has quote and splice fixtures such as
`HelloUltraviolet\Fixtures\AcceptedProjects\ComptimeConformance\Source\Library.uv:338`,
`HelloUltraviolet\Fixtures\AcceptedProjects\ComptimeConformance\Source\Library.uv:347`,
`HelloUltraviolet\Fixtures\AcceptedProjects\ComptimeConformance\Source\Library.uv:348`,
`HelloUltraviolet\Fixtures\AcceptedProjects\ComptimeConformance\Source\Library.uv:349`,
and diagnostic coverage in
`HelloUltraviolet\Source\Audit\Catalog\CompileTimeExecutionAndMetaprogramming\DiagnosticFixtureExecution.uv:279`.
Those cases do not place splice expressions as range bounds or as `wait`
operands inside quoted expression content.

### UV-AUDIT-0189: Simple-identifier `^` parses as region allocation before alias resolution

Severity: High

Status: Locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:16162`
- `Docs\SPECIFICATION.md:16192`
- `Docs\SPECIFICATION.md:16232`
- `Docs\SPECIFICATION.md:17184`
- `Docs\SPECIFICATION.md:17229`
- `Docs\SPECIFICATION.md:17231`
- `Docs\SPECIFICATION.md:30657`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\02_source\parser\expr\binary.cpp:247`
- `Bootstrap\Ultraviolet\src\02_source\parser\expr\primary.cpp:742`
- `Bootstrap\Ultraviolet\src\02_source\parser\expr\primary.cpp:744`
- `Bootstrap\Ultraviolet\src\02_source\parser\expr\primary.cpp:747`
- `Bootstrap\Ultraviolet\src\02_source\parser\expr\primary.cpp:748`
- `Bootstrap\Ultraviolet\src\02_source\parser\expr\alloc_expr.cpp:89`
- `Bootstrap\Ultraviolet\src\02_source\parser\expr\alloc_expr.cpp:92`
- `Bootstrap\Ultraviolet\src\02_source\parser\expr\alloc_expr.cpp:96`
- `Bootstrap\Ultraviolet\src\02_source\parser\expr\alloc_expr.cpp:97`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\resolve_expr.cpp:1469`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\resolve_expr.cpp:1479`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\alloc_expr.cpp:46`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\alloc_expr.cpp:53`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\alloc_expr.cpp:56`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\binary.cpp:615`

Observed behavior:

The binary parser gives `^` a bitwise-XOR precedence, and the resolver has a
spec-shaped rewrite for `Binary("^", Identifier(r), e)` only when the resolved
left identifier is a region alias. That path is bypassed for the common source
shape `x ^ y`: `ParsePrimary` first parses any identifier, checks whether the
next token is `^`, and immediately calls `ParseExplicitAllocExpr` without
knowing whether the identifier denotes a region alias. The resulting AST is an
`AllocExpr(region_opt = x, value = y)`, so type checking rejects a non-region
integer `x` with `E-MEM-1206` instead of treating the source as the specified
bitwise operator expression.

Expected behavior:

`^` is a `BitOps` operator in ordinary binary expressions. The region-allocation
rewrite is a name-resolution rule with a `RegionAlias(ent)` premise; it applies
only after `ResolveValueName(r)` proves that the left identifier is a region
alias. If that premise is not satisfied, `x ^ y` must remain a binary bitwise
operation and be checked by the ordinary operator rules.

Impact:

Valid integer XOR expressions whose left operand is a simple identifier are
misclassified as region allocation and can be rejected with an unrelated memory
diagnostic. The bug also changes precedence and AST shape before semantic
passes that expect a `BinaryExpr("^", ...)` can run.

HelloUltraviolet fixture gap:

`HelloUltraviolet` exercises implicit allocation and parenthesized explicit
allocation in
`HelloUltraviolet\Fixtures\AcceptedProjects\ExpressionSemantics\Source\Library.uv:180`
and
`HelloUltraviolet\Fixtures\AcceptedProjects\ExpressionSemantics\Source\Library.uv:185`,
and it exercises bitwise `&` in
`HelloUltraviolet\Fixtures\AcceptedProjects\ExpressionSemantics\Source\Library.uv:592`.
The accepted operator fixture does not include `left ^ right` with a simple
identifier left operand, and the rejected operator fixtures cover `true + 1`
and enum equality rather than XOR disambiguation.

### UV-AUDIT-0190: Wall-clock UTC runtime wraps non-representable POSIX timestamps

Severity: High

Status: Locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:3598`
- `Docs\SPECIFICATION.md:3601`
- `Docs\SPECIFICATION.md:3612`
- `Docs\SPECIFICATION.md:13578`
- `Docs\SPECIFICATION.md:13634`
- `Docs\SPECIFICATION.md:13637`

Implementation anchors:

- `Bootstrap\Ultraviolet\runtime\include\uv_rt.h:155`
- `Bootstrap\Ultraviolet\runtime\include\uv_rt.h:190`
- `Bootstrap\Ultraviolet\runtime\include\uv_rt.h:246`
- `Bootstrap\Ultraviolet\runtime\include\uv_rt.h:667`
- `Bootstrap\Ultraviolet\runtime\src\time\time.c:153`
- `Bootstrap\Ultraviolet\runtime\src\time\time.c:158`
- `Bootstrap\Ultraviolet\runtime\src\time\time.c:159`
- `Bootstrap\Ultraviolet\runtime\src\time\time.c:160`
- `Bootstrap\Ultraviolet\runtime\src\time\time.c:381`
- `Bootstrap\Ultraviolet\runtime\src\time\time.c:384`

Observed behavior:

`WallTimeNowUtc` lowers through
`ultraviolet::runtime::time::wall_now_utc`, whose POSIX implementation reads
`CLOCK_REALTIME` and writes `out->lo = ((uint64_t)ts.tv_sec *
1000000000ULL) + (uint64_t)ts.tv_nsec; out->hi = 0; return 1`. That converts
negative `tv_sec` values to a large unsigned integer, does not check
multiplication or addition overflow, and still returns success. The caller
then reports `Outcome<UtcInstant, TimeError>@Value` with the wrapped
`unix_nanoseconds` payload.

Expected behavior:

`UtcInstantVal(n)` stores UTC nanoseconds relative to the Unix epoch as an
`i128`, and `WallTimeNowUtc` may return a value only when the host wall-clock
value is representable. A pre-epoch clock reading is representable as a
negative `i128`; a reading whose nanosecond conversion is not representable
must be returned as a `TimeErr(..., OutOfRange)` or otherwise fail rather than
wrapping into a successful positive `UtcInstant`.

Impact:

Hosted programs can observe incorrect wall-clock values if the host clock is
set before 1970 or outside the range that the runtime's unsigned conversion can
represent without overflow. This violates the specified `UtcInstant` value
domain and converts an error-class case into a successful value.

HelloUltraviolet fixture gap:

`HelloUltraviolet` currently calls the wall-time runtime surface only as a
normal symbol/lowering check, for example
`HelloUltraviolet\Fixtures\ArtifactProjects\RuntimeInterfaceSymbols\Source\Library.uv:500`
and
`HelloUltraviolet\Fixtures\ArtifactProjects\RuntimeInterfaceSymbols\Source\Library.uv:506`.
Those fixtures do not force a pre-epoch wall clock, a conversion overflow, or
the `OutOfRange` error path for `WallTime::now_utc`.

### UV-AUDIT-0191: System process text is exposed without UTF-8 normalization

Severity: High

Status: Locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:28603`
- `Docs\SPECIFICATION.md:28604`
- `Docs\SPECIFICATION.md:28606`
- `Docs\SPECIFICATION.md:28607`
- `Docs\SPECIFICATION.md:28609`
- `Docs\SPECIFICATION.md:13659`
- `Docs\SPECIFICATION.md:13661`
- `Docs\SPECIFICATION.md:13662`

Implementation anchors:

- `Bootstrap\Ultraviolet\runtime\src\system\process.c:10`
- `Bootstrap\Ultraviolet\runtime\src\system\process.c:29`
- `Bootstrap\Ultraviolet\runtime\src\system\process.c:30`
- `Bootstrap\Ultraviolet\runtime\src\system\process.c:111`
- `Bootstrap\Ultraviolet\runtime\src\system\process.c:137`
- `Bootstrap\Ultraviolet\runtime\src\system\process.c:148`
- `Bootstrap\Ultraviolet\runtime\src\system\process.c:155`
- `Bootstrap\Ultraviolet\runtime\src\system\process.c:156`
- `Bootstrap\Ultraviolet\runtime\src\system\process.c:167`
- `Bootstrap\Ultraviolet\runtime\src\platform\linux\rt_platform_linux.c:3270`
- `Bootstrap\Ultraviolet\runtime\src\platform\linux\rt_platform_linux.c:3282`
- `Bootstrap\Ultraviolet\runtime\src\platform\linux\rt_platform_linux.c:3298`
- `Bootstrap\Ultraviolet\runtime\src\platform\macos\rt_platform_macos.c:3303`
- `Bootstrap\Ultraviolet\runtime\src\platform\macos\rt_platform_macos.c:3315`
- `Bootstrap\Ultraviolet\runtime\src\platform\macos\rt_platform_macos.c:3331`

Observed behavior:

`System::executable_path`, `System::argument`, and `System::current_directory`
all return `string@View`, but the runtime common layer exposes the bytes
returned by platform query functions directly as `UVStringView`. The shared
`uv_system_query_string_view` helper allocates a raw buffer, invokes the query,
and assigns the returned bytes to `out.data`/`out.len`; `System::argument`
does the same after `uv_rt_argument_query_utf8`. The POSIX adapters also copy
`argv[index + 1]` directly into the output buffer. No common validation,
normalization, transcoding, or failure path runs before these host bytes become
Ultraviolet string values.

Expected behavior:

`ProcessInvocationNormalization` requires the executable path, each command
argument after the executable path, and current directory to be normalized to
UTF-8 text, and source programs observe only the normalized `SystemInterface`
methods. The runtime must therefore reject, transcode, or otherwise normalize
host process/path text at the platform boundary before it is exposed as
`string@View`.

Impact:

On hosts where argv, current-directory bytes, or executable-path bytes are not
valid UTF-8, Ultraviolet programs can receive invalid string values through
ordinary `System` methods. This breaks the specified process-invocation
normalization contract and can make later string operations reason over byte
sequences that were never admitted as normalized text.

HelloUltraviolet fixture gap:

`HelloUltraviolet` currently exercises these methods as symbol/lowering and
positive runtime calls, for example
`HelloUltraviolet\Fixtures\ArtifactProjects\RuntimeInterfaceSymbols\Source\Library.uv:473`,
`HelloUltraviolet\Fixtures\ArtifactProjects\RuntimeInterfaceSymbols\Source\Library.uv:475`,
`HelloUltraviolet\Fixtures\ArtifactProjects\RuntimeInterfaceSymbols\Source\Library.uv:476`,
`HelloUltraviolet\Fixtures\ArtifactProjects\RuntimeInterfaceSymbols\Source\Library.uv:477`,
`HelloUltraviolet\Fixtures\ArtifactProjects\RuntimeInterfaceSymbols\Source\Library.uv:478`,
and `HelloUltraviolet\Source\Reference\Polymorphism\CapabilityClassRuntime.uv:191`.
Those fixtures check availability and length, not a process invocation whose
argv, executable path, or current directory requires normalization or rejection
at the runtime boundary.

### UV-AUDIT-0192: `string::slice` does not enforce UTF-8 byte boundaries

Severity: High

Status: Locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:11394`
- `Docs\SPECIFICATION.md:11441`
- `Docs\SPECIFICATION.md:11467`
- `Docs\SPECIFICATION.md:11468`
- `Docs\SPECIFICATION.md:11470`
- `Docs\SPECIFICATION.md:29203`
- `Docs\SPECIFICATION.md:29210`

Implementation anchors:

- `Bootstrap\Ultraviolet\runtime\include\uv_rt.h:487`
- `Bootstrap\Ultraviolet\runtime\include\uv_rt.h:488`
- `Bootstrap\Ultraviolet\runtime\include\uv_rt.h:489`
- `Bootstrap\Ultraviolet\runtime\include\uv_rt.h:490`
- `Bootstrap\Ultraviolet\runtime\src\memory\string_bytes.c:195`
- `Bootstrap\Ultraviolet\runtime\src\memory\string_bytes.c:199`
- `Bootstrap\Ultraviolet\runtime\src\memory\string_bytes.c:200`
- `Bootstrap\Ultraviolet\runtime\src\memory\string_bytes.c:201`
- `Bootstrap\Ultraviolet\runtime\src\memory\string_bytes.c:202`
- `Bootstrap\Ultraviolet\runtime\src\memory\string_bytes.c:203`
- `Bootstrap\Ultraviolet\runtime\src\memory\string_bytes.c:205`
- `Bootstrap\Ultraviolet\runtime\src\memory\string_bytes.c:209`
- `Bootstrap\Ultraviolet\runtime\src\memory\string_bytes.c:210`
- `Bootstrap\Ultraviolet\runtime\src\internal\rt_internal.h:456`
- `Bootstrap\Ultraviolet\runtime\src\io\io.c:355`
- `Bootstrap\Ultraviolet\runtime\src\io\io.c:557`
- `Bootstrap\Ultraviolet\runtime\src\io\io.c:576`

Observed behavior:

The runtime declaration exposes `string::slice` as a direct runtime entry point
returning `UVStringView`. Its implementation initializes an empty view, reads
`start` and `end`, rejects only a null receiver, reversed offsets, or `end`
beyond `self->len`, and otherwise returns `self->data + start_value` with
length `end_value - start_value`. It never checks whether either offset is a
valid UTF-8 boundary in the receiver byte sequence. The runtime already has
`uv_utf8_valid` and uses UTF-8 validation in the I/O string path, but that
validation is not applied to slice endpoints.

Expected behavior:

`StringSlice-Ok` permits a slice only when `0 <= start <= end <= ByteLen(SB,
self)` and both `start` and `end` are valid UTF-8 byte boundaries of
`ByteSeqOf(SB, self)`. The runtime implementation of the mapped
`BuiltinSym(string::slice)` must enforce that boundary precondition before
creating the resulting `string@View`.

Impact:

Slicing through a multi-byte scalar can manufacture a `string@View` whose bytes
are not valid UTF-8 even though the source string was valid. That violates the
string operation semantics and can propagate invalid string values through later
string, I/O, and capability interfaces.

HelloUltraviolet fixture gap:

`HelloUltraviolet` currently exercises `string::slice` with ASCII literals and
line-oriented text whose selected offsets are ordinary ASCII marker positions,
for example
`HelloUltraviolet\Fixtures\ArtifactProjects\RuntimeInterfaceSymbols\Source\Library.uv:164`,
`HelloUltraviolet\Source\Reference\ModalTypes\Strings.uv:68`,
`HelloUltraviolet\Source\Reference\ModalTypes\Strings.uv:88`,
`HelloUltraviolet\Source\Audit\AcceptedProjectExecution.uv:134`,
`HelloUltraviolet\Source\Audit\PatternLoweringConformanceExecution.uv:76`,
`HelloUltraviolet\Source\Audit\DiagnosticFixtureExecution.uv:274`,
`HelloUltraviolet\Source\Audit\DiagnosticFixtureExecution.uv:308`,
`HelloUltraviolet\Source\Audit\DiagnosticFixtureExecution.uv:430`,
`HelloUltraviolet\Source\Audit\DiagnosticFixtureExecution.uv:432`,
`HelloUltraviolet\Source\Audit\CatalogSourcePaths.uv:105`,
`HelloUltraviolet\Source\Audit\ArtifactProjectExecution.uv:2365`, and
`HelloUltraviolet\Source\Audit\ArtifactProjectExecution.uv:2434`. These calls
do not cover a valid string sliced at a non-boundary byte offset.

### UV-AUDIT-0193: Context initialization can return all-null root capabilities

Severity: High

Status: Locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:3232`
- `Docs\SPECIFICATION.md:3234`
- `Docs\SPECIFICATION.md:13764`
- `Docs\SPECIFICATION.md:13765`
- `Docs\SPECIFICATION.md:13766`
- `Docs\SPECIFICATION.md:13767`
- `Docs\SPECIFICATION.md:13768`
- `Docs\SPECIFICATION.md:13769`
- `Docs\SPECIFICATION.md:13789`
- `Docs\SPECIFICATION.md:13790`
- `Docs\SPECIFICATION.md:13792`
- `Docs\SPECIFICATION.md:26368`
- `Docs\SPECIFICATION.md:28597`
- `Docs\SPECIFICATION.md:28599`
- `Docs\SPECIFICATION.md:28615`
- `Docs\SPECIFICATION.md:28616`
- `Docs\SPECIFICATION.md:28688`
- `Docs\SPECIFICATION.md:28691`
- `Docs\SPECIFICATION.md:28692`
- `Docs\SPECIFICATION.md:28694`
- `Docs\SPECIFICATION.md:28708`
- `Docs\SPECIFICATION.md:28711`
- `Docs\SPECIFICATION.md:28712`
- `Docs\SPECIFICATION.md:28714`

Implementation anchors:

- `Bootstrap\Ultraviolet\runtime\include\uv_rt.h:440`
- `Bootstrap\Ultraviolet\runtime\src\context\context.c:3`
- `Bootstrap\Ultraviolet\runtime\src\context\context.c:8`
- `Bootstrap\Ultraviolet\runtime\src\context\context.c:12`
- `Bootstrap\Ultraviolet\runtime\src\context\context.c:18`
- `Bootstrap\Ultraviolet\runtime\src\context\context.c:21`
- `Bootstrap\Ultraviolet\runtime\src\context\context.c:22`
- `Bootstrap\Ultraviolet\runtime\src\context\context.c:23`
- `Bootstrap\Ultraviolet\runtime\src\context\context.c:30`
- `Bootstrap\Ultraviolet\runtime\src\context\context.c:31`
- `Bootstrap\Ultraviolet\runtime\src\context\context.c:33`
- `Bootstrap\Ultraviolet\runtime\src\context\context.c:41`
- `Bootstrap\Ultraviolet\runtime\src\context\context.c:42`
- `Bootstrap\Ultraviolet\runtime\src\context\context.c:45`
- `Bootstrap\Ultraviolet\runtime\src\context\context.c:51`
- `Bootstrap\Ultraviolet\runtime\src\context\context.c:52`
- `Bootstrap\Ultraviolet\runtime\src\context\context.c:56`
- `Bootstrap\Ultraviolet\runtime\src\context\context.c:60`
- `Bootstrap\Ultraviolet\runtime\src\context\context.c:61`
- `Bootstrap\Ultraviolet\runtime\src\context\context.c:66`
- `Bootstrap\Ultraviolet\runtime\src\context\context.c:74`
- `Bootstrap\Ultraviolet\runtime\src\context\context.c:78`
- `Bootstrap\Ultraviolet\runtime\src\context\context.c:82`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\entry_emit.cpp:658`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\entry_emit.cpp:660`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\entry_emit.cpp:662`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\entry_emit.cpp:713`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\entry_emit.cpp:714`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\entry_emit.cpp:716`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\entry_emit.cpp:804`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\entry_emit.cpp:807`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\entry_emit.cpp:1184`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\hosted_emit.cpp:844`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\hosted_emit.cpp:852`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\hosted_emit.cpp:854`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\hosted_emit.cpp:902`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\hosted_emit.cpp:903`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\hosted_emit.cpp:906`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\hosted_emit.cpp:965`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\hosted_emit.cpp:1258`
- `Bootstrap\Ultraviolet\runtime\src\internal\rt_internal.h:296`
- `Bootstrap\Ultraviolet\runtime\src\internal\rt_internal.h:302`
- `Bootstrap\Ultraviolet\runtime\src\internal\rt_internal.h:303`
- `Bootstrap\Ultraviolet\runtime\src\internal\rt_internal.h:334`
- `Bootstrap\Ultraviolet\runtime\src\internal\rt_internal.h:335`
- `Bootstrap\Ultraviolet\runtime\src\internal\rt_internal.h:336`

Observed behavior:

The runtime `context_init` entry point is a `void` routine that writes through
an output pointer. It clears every dynamic-object field in the `Context` to
null before allocating the provider state objects. If allocation of `IO`,
`Network`, `HeapAllocator`, `System`, or `Time` state fails, the function
returns immediately after freeing any provider state already allocated, leaving
the output `Context` with null data and vtable fields. The executable entry
emitter stores a null value into the context slot before calling
`context_init`, treats the call itself as successful, loads the context slot,
and later calls user `main`. The hosted-session create path likewise zeroes the
hosted environment, calls `context_init`, then proceeds to register and return
a nonzero session handle without checking that the context contains live root
providers.

Expected behavior:

`ContextInitSigma` has only a value-producing rule: it yields a `ContextValue`
used immediately by `EntryStubSpec`, `InterpretProject`, and
`HostSessionInitSigma`. `NAA-2` also makes those `Context` values the sole
explicit carrier of root capabilities, and hosted session creation must return
`0` iff it cannot establish a live hosted session. Therefore context
construction must either establish every required root provider or fail through
a specified abort/panic/session-create-failure path before source code can
observe the context.

Impact:

Under provider-state allocation failure, an executable can enter user code with
non-operational root capabilities, and a hosted library can publish a live
session whose `SessionContext` has null provider objects. Heap operations are a
secondary visible consequence: the internal heap accounting helpers treat a
null heap state as an unbounded allocation context and skip all used-count
mutation, so safe string/bytes allocation through a null `context.heap` can
escape the heap-state semantics instead of failing cleanly.

HelloUltraviolet fixture gap:

`HelloUltraviolet` exercises normal context and heap wiring, for example
`HelloUltraviolet\Source\Reference\Authority\Capabilities.uv:66`,
`HelloUltraviolet\Source\Reference\Authority\Capabilities.uv:71`,
`HelloUltraviolet\Source\Reference\Polymorphism\CapabilityClassRuntime.uv:144`,
`HelloUltraviolet\Source\Reference\Polymorphism\CapabilityClassRuntime.uv:146`,
`HelloUltraviolet\Fixtures\ArtifactProjects\RuntimeInterfaceSymbols\Source\Library.uv:360`,
`HelloUltraviolet\Fixtures\ArtifactProjects\RuntimeInterfaceSymbols\Source\Library.uv:362`,
and `HelloUltraviolet\Fixtures\ArtifactProjects\ExecutableOutput\Source\Main.uv:257`.
Those fixtures do not inject provider-state allocation failure, verify that
every context root has a live provider object before `main`, or require hosted
session creation to reject a context that could not be established.

### UV-AUDIT-0194: Monotonic time failures are exposed as null or zero successes

Severity: High

Status: Locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:3335`
- `Docs\SPECIFICATION.md:3594`
- `Docs\SPECIFICATION.md:3603`
- `Docs\SPECIFICATION.md:3606`
- `Docs\SPECIFICATION.md:3608`
- `Docs\SPECIFICATION.md:3609`
- `Docs\SPECIFICATION.md:3822`
- `Docs\SPECIFICATION.md:3823`
- `Docs\SPECIFICATION.md:3825`
- `Docs\SPECIFICATION.md:3827`
- `Docs\SPECIFICATION.md:3828`
- `Docs\SPECIFICATION.md:3830`
- `Docs\SPECIFICATION.md:13568`
- `Docs\SPECIFICATION.md:13570`
- `Docs\SPECIFICATION.md:13571`
- `Docs\SPECIFICATION.md:13614`
- `Docs\SPECIFICATION.md:13615`
- `Docs\SPECIFICATION.md:13616`
- `Docs\SPECIFICATION.md:13617`
- `Docs\SPECIFICATION.md:13618`
- `Docs\SPECIFICATION.md:13619`
- `Docs\SPECIFICATION.md:13699`
- `Docs\SPECIFICATION.md:13708`
- `Docs\SPECIFICATION.md:29383`
- `Docs\SPECIFICATION.md:29384`
- `Docs\SPECIFICATION.md:29386`
- `Docs\SPECIFICATION.md:29388`
- `Docs\SPECIFICATION.md:29389`
- `Docs\SPECIFICATION.md:29391`

Implementation anchors:

- `Bootstrap\Ultraviolet\runtime\include\uv_rt.h:181`
- `Bootstrap\Ultraviolet\runtime\include\uv_rt.h:182`
- `Bootstrap\Ultraviolet\runtime\include\uv_rt.h:185`
- `Bootstrap\Ultraviolet\runtime\include\uv_rt.h:186`
- `Bootstrap\Ultraviolet\runtime\include\uv_rt.h:188`
- `Bootstrap\Ultraviolet\runtime\include\uv_rt.h:648`
- `Bootstrap\Ultraviolet\runtime\include\uv_rt.h:649`
- `Bootstrap\Ultraviolet\runtime\include\uv_rt.h:650`
- `Bootstrap\Ultraviolet\runtime\include\uv_rt.h:652`
- `Bootstrap\Ultraviolet\runtime\include\uv_rt.h:653`
- `Bootstrap\Ultraviolet\runtime\include\uv_rt.h:654`
- `Bootstrap\Ultraviolet\runtime\src\internal\rt_internal.h:57`
- `Bootstrap\Ultraviolet\runtime\src\internal\rt_internal.h:58`
- `Bootstrap\Ultraviolet\runtime\src\internal\rt_internal.h:59`
- `Bootstrap\Ultraviolet\runtime\src\internal\rt_internal.h:63`
- `Bootstrap\Ultraviolet\runtime\src\internal\rt_internal.h:67`
- `Bootstrap\Ultraviolet\runtime\src\internal\rt_internal.h:68`
- `Bootstrap\Ultraviolet\runtime\src\time\time.c:66`
- `Bootstrap\Ultraviolet\runtime\src\time\time.c:70`
- `Bootstrap\Ultraviolet\runtime\src\time\time.c:73`
- `Bootstrap\Ultraviolet\runtime\src\time\time.c:75`
- `Bootstrap\Ultraviolet\runtime\src\time\time.c:76`
- `Bootstrap\Ultraviolet\runtime\src\time\time.c:80`
- `Bootstrap\Ultraviolet\runtime\src\time\time.c:83`
- `Bootstrap\Ultraviolet\runtime\src\time\time.c:84`
- `Bootstrap\Ultraviolet\runtime\src\time\time.c:85`
- `Bootstrap\Ultraviolet\runtime\src\time\time.c:88`
- `Bootstrap\Ultraviolet\runtime\src\time\time.c:89`
- `Bootstrap\Ultraviolet\runtime\src\time\time.c:90`
- `Bootstrap\Ultraviolet\runtime\src\time\time.c:94`
- `Bootstrap\Ultraviolet\runtime\src\time\time.c:100`
- `Bootstrap\Ultraviolet\runtime\src\time\time.c:101`
- `Bootstrap\Ultraviolet\runtime\src\time\time.c:105`
- `Bootstrap\Ultraviolet\runtime\src\time\time.c:108`
- `Bootstrap\Ultraviolet\runtime\src\time\time.c:109`
- `Bootstrap\Ultraviolet\runtime\src\time\time.c:116`
- `Bootstrap\Ultraviolet\runtime\src\time\time.c:117`
- `Bootstrap\Ultraviolet\runtime\src\time\time.c:121`
- `Bootstrap\Ultraviolet\runtime\src\time\time.c:122`
- `Bootstrap\Ultraviolet\runtime\src\time\time.c:126`
- `Bootstrap\Ultraviolet\runtime\src\time\time.c:132`
- `Bootstrap\Ultraviolet\runtime\src\time\time.c:133`
- `Bootstrap\Ultraviolet\runtime\src\time\time.c:136`
- `Bootstrap\Ultraviolet\runtime\src\time\time.c:137`
- `Bootstrap\Ultraviolet\runtime\src\time\time.c:266`
- `Bootstrap\Ultraviolet\runtime\src\time\time.c:268`
- `Bootstrap\Ultraviolet\runtime\src\time\time.c:271`
- `Bootstrap\Ultraviolet\runtime\src\time\time.c:272`
- `Bootstrap\Ultraviolet\runtime\src\time\time.c:274`
- `Bootstrap\Ultraviolet\runtime\src\time\time.c:275`
- `Bootstrap\Ultraviolet\runtime\src\time\time.c:277`
- `Bootstrap\Ultraviolet\runtime\src\time\time.c:279`
- `Bootstrap\Ultraviolet\runtime\src\time\time.c:295`
- `Bootstrap\Ultraviolet\runtime\src\time\time.c:298`
- `Bootstrap\Ultraviolet\runtime\src\time\time.c:304`
- `Bootstrap\Ultraviolet\runtime\src\time\time.c:305`
- `Bootstrap\Ultraviolet\runtime\src\time\time.c:306`
- `Bootstrap\Ultraviolet\runtime\src\time\time.c:307`
- `Bootstrap\Ultraviolet\runtime\src\time\time.c:308`
- `Bootstrap\Ultraviolet\runtime\src\time\time.c:312`
- `Bootstrap\Ultraviolet\runtime\src\time\time.c:313`
- `Bootstrap\Ultraviolet\runtime\src\time\time.c:316`
- `Bootstrap\Ultraviolet\runtime\src\time\time.c:319`
- `Bootstrap\Ultraviolet\runtime\src\time\time.c:320`
- `Bootstrap\Ultraviolet\runtime\src\time\time.c:321`
- `Bootstrap\Ultraviolet\runtime\src\time\time.c:322`
- `Bootstrap\Ultraviolet\runtime\src\time\time.c:324`

Observed behavior:

The runtime `Time::monotonic` attenuation returns a dynamic object with null
data when the receiver has no time state, and also returns a null dynamic object
when allocating the monotonic child state fails. The same implementation falls
back to an advertised resolution of `1` when the platform cannot report
monotonic-clock resolution.

The `MonotonicTime::now` runtime entry point is declared as a value-returning
out-parameter routine rather than an `Outcome`. If the receiver state is null,
has the wrong time kind, or the platform monotonic read fails, it writes
`domain = 0` and zero ticks and returns as though a `MonotonicInstant` had been
produced. The `MonotonicTime::resolution` entry point likewise writes a zero
`Duration` when the receiver state is null or has the wrong time kind.

Expected behavior:

`TimeMonotonic(v_time) ⇓ v_mono` must produce a monotonic-clock capability.
`MonotonicTime::now` and `MonotonicTime::resolution` are specified as direct
value-returning methods, not `Outcome` methods. Their primitive rules return
`Val(t)` and `Val(d)`, while the normative time relation requires a monotonic
clock read for `now` and requires `resolution` to return an advertised
monotonic-clock resolution with `n > 0`. Because these methods have no
source-level error result, the runtime must either provide the specified value
from a valid capability or fail before returning a fabricated successful value.

Impact:

Allocation failure while deriving a monotonic clock, a missing time root, or a
platform monotonic-clock read failure can be observed as a successful dynamic
capability or value return with null capability data, `domain = 0`, zero ticks,
or a zero duration. That violates the capability attenuation and time primitive
relations, and it can feed invalid monotonic instants or zero resolutions into
later elapsed/coarsen logic that was specified to operate on authorized
nonzero-resolution monotonic clock capabilities.

HelloUltraviolet fixture gap:

`HelloUltraviolet` covers the ordinary successful path through time
capabilities, for example
`HelloUltraviolet\Source\Tests\SourceNativeTests.uv:136`,
`HelloUltraviolet\Source\Tests\SourceNativeTests.uv:138`,
`HelloUltraviolet\Source\Tests\SourceNativeTests.uv:140`,
`HelloUltraviolet\Source\Tests\SourceNativeTests.uv:144`,
`HelloUltraviolet\Source\Reference\Polymorphism\CapabilityClassRuntime.uv:233`,
`HelloUltraviolet\Source\Reference\Polymorphism\CapabilityClassRuntime.uv:235`,
`HelloUltraviolet\Source\Reference\Polymorphism\CapabilityClassRuntime.uv:237`,
`HelloUltraviolet\Source\Reference\Polymorphism\CapabilityClassRuntime.uv:244`,
`HelloUltraviolet\Fixtures\ArtifactProjects\RuntimeInterfaceSymbols\Source\Library.uv:499`,
`HelloUltraviolet\Fixtures\ArtifactProjects\RuntimeInterfaceSymbols\Source\Library.uv:501`,
`HelloUltraviolet\Fixtures\ArtifactProjects\RuntimeInterfaceSymbols\Source\Library.uv:503`,
and
`HelloUltraviolet\Fixtures\ArtifactProjects\RuntimeInterfaceSymbols\Source\Library.uv:509`.
Those fixtures assert that the normal monotonic resolution is nonzero, but they
do not force monotonic child allocation failure, a missing time root, an
unsupported monotonic platform path, or a platform read failure after a
capability has been derived.

### UV-AUDIT-0195: Static and subplace assignment can write after old-value drop panic

Severity: High

Status: Agent-reported and locally corroborated.

Spec anchors:

- `Docs\SPECIFICATION.md:19171`
- `Docs\SPECIFICATION.md:19616`
- `Docs\SPECIFICATION.md:19619`
- `Docs\SPECIFICATION.md:19624`
- `Docs\SPECIFICATION.md:19635`
- `Docs\SPECIFICATION.md:19645`
- `Docs\SPECIFICATION.md:28824`
- `Docs\SPECIFICATION.md:28867`
- `Docs\SPECIFICATION.md:28872`
- `Docs\SPECIFICATION.md:29741`
- `Docs\SPECIFICATION.md:29882`
- `Docs\SPECIFICATION.md:30186`
- `Docs\SPECIFICATION.md:30188`
- `Docs\SPECIFICATION.md:30196`
- `Docs\SPECIFICATION.md:30211`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\expr_place.cpp:680`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\expr_place.cpp:681`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\expr_place.cpp:700`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\expr_place.cpp:703`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\expr_place.cpp:705`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\expr_place.cpp:706`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\expr_place.cpp:885`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\expr_place.cpp:886`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\expr_place.cpp:908`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\expr_place.cpp:910`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\expr_place.cpp:911`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\expr_place.cpp:944`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\expr_place.cpp:945`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\expr_place.cpp:968`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\expr_place.cpp:971`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\expr_place.cpp:972`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\expr_place.cpp:1000`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\expr_place.cpp:1001`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\expr_place.cpp:1013`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\expr_place.cpp:1016`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\expr_place.cpp:1017`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\expr_place.cpp:1154`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\expr_place.cpp:1155`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\expr_place.cpp:1173`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\expr_place.cpp:1176`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\expr_place.cpp:1177`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\control\seq.cpp:14`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\control\seq.cpp:579`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\control\seq.cpp:581`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\control\seq.cpp:620`

Observed behavior:

The static/path assignment lowering builds old-value cleanup as `drop_ir` and
appends it before the write, but only inserts `PanicFollowup(ctx)` after the
poison check. The same pattern appears in explicit path static assignment.
Field, tuple, and scalar-index subplace assignment also build a `drop_ir` by
reading the old subvalue and calling `EmitDrop`, then sequence the address
marker and `IRWritePtr` immediately after that cleanup. Scalar-index assignment
does add `PanicFollowup(ctx)` after the bounds check, but still has no
follow-up after `drop_ir`.

The LLVM IR sequence emitter iterates the `IRSeq` items and emits each item in
order until a block terminator exists. Since the old-value drop IR is not
followed by a panic propagation instruction in these assignment paths, a drop
method or child drop that records a panic can be followed by the global store
or pointer write for the replacement value.

Expected behavior:

`ExecSigma-Assign` and `ExecSigma-CompoundAssign` delegate mutation to
`WritePlaceSigma`. For subplaces, `DropSubvalue-Do` requires the old subvalue
to be dropped before the write when the root owns responsible immovable
storage. `DropValueOut-DropPanic` and `DropValueOut-ChildPanic` define a panic
result when the drop method or child drop panics. The assignment write is
therefore only permitted after the required old-value drop completes normally;
a panic from that drop must propagate instead of allowing the replacement write
to occur.

Impact:

An assignment into a responsible static, record field, tuple element, or array
element can mutate storage after destruction of the previous value has already
failed. That changes both the assignment outcome and the post-panic storage
state relative to the specified drop and write ordering.

HelloUltraviolet fixture gap:

`HelloUltraviolet` covers ordinary non-panicking assignment in
`HelloUltraviolet\Source\Reference\Statements\Assignment.uv:29`,
`HelloUltraviolet\Source\Reference\Statements\Assignment.uv:35`,
`HelloUltraviolet\Source\Reference\Statements\Assignment.uv:41`, and
`HelloUltraviolet\Source\Reference\Statements\Assignment.uv:47`. It also
covers destructor panic during cleanup in
`HelloUltraviolet\Source\Reference\Lowering\CleanupDropUnwinding.uv:51`,
`HelloUltraviolet\Source\Reference\Lowering\CleanupDropUnwinding.uv:65`,
`HelloUltraviolet\Source\Reference\Lowering\CleanupDropUnwinding.uv:97`,
`HelloUltraviolet\Source\Reference\Lowering\CleanupDropUnwinding.uv:103`,
`HelloUltraviolet\Source\Reference\Lowering\CleanupDropUnwinding.uv:118`,
`HelloUltraviolet\Source\Reference\Lowering\CleanupDropUnwinding.uv:125`,
`HelloUltraviolet\Source\Reference\Lowering\CleanupDropUnwinding.uv:142`,
and `HelloUltraviolet\Source\Reference\Lowering\CleanupDropUnwinding.uv:147`.
There is no fixture that assigns into a responsible static or subplace, makes
the old-value drop fail, and verifies that the replacement write is skipped.

### UV-AUDIT-0196: Identical assembly source roots are rejected as ambiguous

Severity: Medium

Status: Agent-reported and locally corroborated.

Specification anchors:

- `Docs\SPECIFICATION.md:1206`
- `Docs\SPECIFICATION.md:1209`
- `Docs\SPECIFICATION.md:1211`
- `Docs\SPECIFICATION.md:1213`
- `Docs\SPECIFICATION.md:1218`
- `Docs\SPECIFICATION.md:1740`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\01_project\load_project.cpp:232`
- `Bootstrap\Ultraviolet\src\01_project\load_project.cpp:239`
- `Bootstrap\Ultraviolet\src\01_project\load_project.cpp:240`
- `Bootstrap\Ultraviolet\src\01_project\load_project.cpp:241`
- `Bootstrap\Ultraviolet\src\01_project\load_project.cpp:246`
- `Bootstrap\Ultraviolet\src\01_project\load_project.cpp:247`
- `Bootstrap\Ultraviolet\src\01_project\load_project.cpp:248`
- `Bootstrap\Ultraviolet\src\01_project\load_project.cpp:249`
- `Bootstrap\Ultraviolet\src\01_project\load_project.cpp:253`
- `Bootstrap\Ultraviolet\src\01_project\load_project.cpp:254`
- `Bootstrap\Ultraviolet\src\01_project\load_project.cpp:255`
- `Bootstrap\Ultraviolet\src\01_project\load_project.cpp:256`
- `Bootstrap\Ultraviolet\src\01_project\load_project.cpp:269`
- `Bootstrap\Ultraviolet\src\01_project\load_project.cpp:276`
- `Bootstrap\Ultraviolet\src\01_project\load_project.cpp:280`

Observed behavior:

The specification defines `AssemblySourceRoots(As)` as the set of assembly
source-root path values. `OwnerRoot` is unique over root values, so two
assemblies with the same source-root path collapse to one root value. Equal
depth ambiguity exists only when two distinct root values both match at the
deepest depth.

`OwnerAssemblyForDir` instead tracks ownership by assembly index. For each
assembly it computes `core::Relative(dir, assemblies[i].source_root)`. If two
assemblies use the same `source_root`, both match with the same depth, and the
second match reports `WF-Assembly-Root-Owner-Ambiguous` / `E-PRJ-0206` because
the stored owner index differs.

Expected behavior:

Duplicate identical root paths must not create ambiguity under
`AssemblySourceRoots` set semantics. Ambiguity requires failure to choose a
unique deepest root path, not merely a second assembly index for the same root
path value.

Impact:

A manifest with distinct assembly names sharing the same source root can be
rejected before module ownership is applied, even though the specification's
root set still has a unique owner root.

HelloUltraviolet fixture gap:

`HelloUltraviolet\Source\Reference\Projects\ModuleDiscovery.uv:404` covers an
abstract non-unique owner case, but no fixture covers duplicate identical
assembly source roots or asserts the resulting `uvc` diagnostic behavior for
`E-PRJ-0206`.

### UV-AUDIT-0197: `Disc-Rel-Fail` emits the wrong project diagnostic code

Severity: Low

Status: Agent-reported and locally corroborated.

Specification anchors:

- `Docs\SPECIFICATION.md:1747`
- `Docs\SPECIFICATION.md:1748`
- `Docs\SPECIFICATION.md:8090`
- `Docs\SPECIFICATION.md:8268`
- `Docs\SPECIFICATION.md:8271`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\01_project\module_discovery.cpp:347`
- `Bootstrap\Ultraviolet\src\01_project\module_discovery.cpp:354`
- `Bootstrap\Ultraviolet\src\01_project\module_discovery.cpp:356`
- `Bootstrap\Ultraviolet\src\01_project\module_discovery.cpp:357`
- `Bootstrap\Ultraviolet\src\01_project\module_discovery.cpp:358`
- `Bootstrap\Ultraviolet\src\00_core\generated\diag_registry.inc:253`
- `Bootstrap\Ultraviolet\src\00_core\generated\diag_registry.inc:254`
- `Bootstrap\Ultraviolet\src\00_core\generated\diag_registry.inc:567`
- `Bootstrap\Ultraviolet\src\00_core\generated\diag_registry.inc:1171`

Observed behavior:

`ModulePathFor` calls `core::Relative(dir_str, root_str)`. When relative path
derivation fails, it records `Module-Path-Rel-Fail` and `Disc-Rel-Fail`, then
emits `E-PRJ-0304`.

The generated diagnostic registry maps `Disc-Rel-Fail` to `E-PRJ-0303`.
`E-PRJ-0304` is mapped to `Resolve-Canonical-Err`.

Expected behavior:

`Disc-Rel-Fail` and `Module-Path-Rel-Fail` are relative-path derivation
failures and must emit `E-PRJ-0303`. `E-PRJ-0304` is reserved for
canonicalization and resolve errors.

Impact:

`uvc` emits the wrong diagnostic code for a module-discovery relative-path
failure, weakening deterministic diagnostic conformance and selector coverage.

HelloUltraviolet fixture gap:

`HelloUltraviolet\Source\Reference\Projects\ModuleDiscovery.uv:366` models the
rule symbolically, and
`HelloUltraviolet\Source\Audit\Catalog\ModuleLevelForms\ModuleAndFileAggregation.uv:119`
and
`HelloUltraviolet\Source\Audit\Catalog\ModuleLevelForms\ModuleAndFileAggregation.uv:533`
credit the obligations. No executable fixture asserts the emitted `uvc`
diagnostic code for this path.

### UV-AUDIT-0198: Runtime attenuation can return null child capabilities

Severity: High

Status: Agent-reported and locally corroborated.

Specification anchors:

- `Docs\SPECIFICATION.md:3251`
- `Docs\SPECIFICATION.md:3252`
- `Docs\SPECIFICATION.md:3262`
- `Docs\SPECIFICATION.md:3264`
- `Docs\SPECIFICATION.md:3624`
- `Docs\SPECIFICATION.md:29317`
- `Docs\SPECIFICATION.md:29329`
- `Docs\SPECIFICATION.md:29345`

Implementation anchors:

- `Bootstrap\Ultraviolet\runtime\src\network\network.c:18`
- `Bootstrap\Ultraviolet\runtime\src\network\network.c:19`
- `Bootstrap\Ultraviolet\runtime\src\network\network.c:20`
- `Bootstrap\Ultraviolet\runtime\src\network\network.c:22`
- `Bootstrap\Ultraviolet\runtime\src\network\network.c:23`
- `Bootstrap\Ultraviolet\runtime\src\network\network.c:24`
- `Bootstrap\Ultraviolet\runtime\src\network\network.c:40`
- `Bootstrap\Ultraviolet\runtime\src\network\network.c:41`
- `Bootstrap\Ultraviolet\runtime\src\network\network.c:42`
- `Bootstrap\Ultraviolet\runtime\src\network\network.c:43`
- `Bootstrap\Ultraviolet\runtime\src\network\network.c:50`
- `Bootstrap\Ultraviolet\runtime\src\context\heap.c:14`
- `Bootstrap\Ultraviolet\runtime\src\context\heap.c:15`
- `Bootstrap\Ultraviolet\runtime\src\context\heap.c:16`
- `Bootstrap\Ultraviolet\runtime\src\context\heap.c:19`
- `Bootstrap\Ultraviolet\runtime\src\context\heap.c:20`
- `Bootstrap\Ultraviolet\runtime\src\context\heap.c:21`
- `Bootstrap\Ultraviolet\runtime\src\context\heap.c:23`
- `Bootstrap\Ultraviolet\runtime\src\context\heap.c:24`
- `Bootstrap\Ultraviolet\runtime\src\context\heap.c:25`
- `Bootstrap\Ultraviolet\runtime\src\context\heap.c:27`
- `Bootstrap\Ultraviolet\runtime\src\internal\rt_internal.h:302`
- `Bootstrap\Ultraviolet\runtime\src\internal\rt_internal.h:303`
- `Bootstrap\Ultraviolet\runtime\src\internal\rt_internal.h:335`
- `Bootstrap\Ultraviolet\runtime\src\internal\rt_internal.h:336`

Observed behavior:

`Network::restrict_to_host` initializes a dynamic object with `data = NULL`
and a copied vtable, then returns that object unchanged when either the
`UVNetState` allocation or host-copy allocation fails. `HeapAllocator::with_quota`
uses the same null-data successful return shape when `UVHeapState` allocation
fails.

The heap allocation path then treats a null heap state as allocation-permitted
and leaves usage accounting as a no-op. A quota child capability returned after
allocation failure therefore behaves like an unbounded, unaccounted heap
capability rather than a child state with `parent`, `quota`, and `used = 0`.

Expected behavior:

These attenuation operations are value-returning runtime primitives. A
successful `Network::restrict_to_host` return must be `Val(v_net')` where the
child network capability denotes authority no greater than the parent. A
successful `HeapAllocator::with_quota` return must be `Val(v_heap')` with
`HeapState(v_heap') = ⟨v_heap, q, 0⟩`. Allocation failure cannot be represented
as a successful dynamic capability with null runtime state.

Impact:

Source code can receive a value typed as `$Network` or `$HeapAllocator` that
does not carry the required child capability state. For heap attenuation, quota
checks can be silently erased after the failed child-state allocation.

HelloUltraviolet fixture gap:

Existing coverage exercises successful attenuation in
`HelloUltraviolet\Source\Reference\Authority\Capabilities.uv:54`,
`HelloUltraviolet\Source\Reference\Authority\Capabilities.uv:56`,
`HelloUltraviolet\Source\Reference\Authority\Capabilities.uv:64`,
`HelloUltraviolet\Source\Reference\Authority\Capabilities.uv:66`,
`HelloUltraviolet\Source\Reference\Authority\Capabilities.uv:67`,
`HelloUltraviolet\Source\Reference\Authority\Capabilities.uv:68`,
`HelloUltraviolet\Source\Reference\Authority\Network.uv:119`,
`HelloUltraviolet\Source\Reference\Authority\Network.uv:126`,
`HelloUltraviolet\Source\Reference\Authority\Network.uv:127`,
`HelloUltraviolet\Source\Reference\Authority\Network.uv:128`,
`HelloUltraviolet\Source\Reference\Authority\Network.uv:137`,
`HelloUltraviolet\Source\Reference\Polymorphism\CapabilityClassRuntime.uv:137`,
`HelloUltraviolet\Source\Reference\Polymorphism\CapabilityClassRuntime.uv:138`,
`HelloUltraviolet\Source\Reference\Polymorphism\CapabilityClassRuntime.uv:143`,
`HelloUltraviolet\Source\Reference\Polymorphism\CapabilityClassRuntime.uv:144`,
`HelloUltraviolet\Fixtures\ArtifactProjects\RuntimeInterfaceSymbols\Source\Library.uv:353`,
`HelloUltraviolet\Fixtures\ArtifactProjects\RuntimeInterfaceSymbols\Source\Library.uv:354`,
`HelloUltraviolet\Fixtures\ArtifactProjects\RuntimeInterfaceSymbols\Source\Library.uv:360`,
`HelloUltraviolet\Fixtures\ArtifactProjects\ExecutableOutput\Source\Main.uv:256`,
and `HelloUltraviolet\Fixtures\ArtifactProjects\ExecutableOutput\Source\Main.uv:257`.
No fixture forces attenuation allocation failure or asserts that child
capability data is non-null after attenuation.

### UV-AUDIT-0199: Directory iteration can fall back to raw-name ordering

Severity: Low

Status: Locally verified.

Specification anchors:

- `Docs\SPECIFICATION.md:1891`
- `Docs\SPECIFICATION.md:3518`
- `Docs\SPECIFICATION.md:3519`
- `Docs\SPECIFICATION.md:3520`
- `Docs\SPECIFICATION.md:3521`
- `Docs\SPECIFICATION.md:3522`
- `Docs\SPECIFICATION.md:3524`

Implementation anchors:

- `Bootstrap\Ultraviolet\runtime\src\io\io.c:1049`
- `Bootstrap\Ultraviolet\runtime\src\io\io.c:1069`
- `Bootstrap\Ultraviolet\runtime\src\io\io.c:1073`
- `Bootstrap\Ultraviolet\runtime\src\io\io.c:1075`
- `Bootstrap\Ultraviolet\runtime\src\io\io.c:1076`
- `Bootstrap\Ultraviolet\runtime\src\io\io.c:1077`
- `Bootstrap\Ultraviolet\runtime\src\io\io.c:1080`
- `Bootstrap\Ultraviolet\runtime\src\io\io.c:1081`
- `Bootstrap\Ultraviolet\runtime\src\io\io.c:1082`
- `Bootstrap\Ultraviolet\runtime\src\io\io.c:1097`
- `Bootstrap\Ultraviolet\runtime\src\io\io.c:1099`
- `Bootstrap\Ultraviolet\runtime\src\io\io.c:1100`
- `Bootstrap\Ultraviolet\runtime\src\io\io.c:1101`
- `Bootstrap\Ultraviolet\runtime\src\io\io.c:1117`
- `Bootstrap\Ultraviolet\runtime\src\io\io.c:1128`
- `Bootstrap\Ultraviolet\runtime\src\io\io.c:1129`
- `Bootstrap\Ultraviolet\runtime\src\io\io.c:1130`
- `Bootstrap\Ultraviolet\runtime\src\io\io.c:1131`
- `Bootstrap\Ultraviolet\runtime\src\io\io.c:1154`
- `Bootstrap\Ultraviolet\runtime\src\io\io.c:1155`
- `Bootstrap\Ultraviolet\runtime\src\io\io.c:1156`
- `Bootstrap\Ultraviolet\runtime\src\io\io.c:1157`
- `Bootstrap\Ultraviolet\runtime\src\io\io.c:1204`
- `Bootstrap\Ultraviolet\runtime\src\io\io.c:1205`
- `Bootstrap\Ultraviolet\runtime\src\io\io.c:1206`
- `Bootstrap\Ultraviolet\runtime\src\io\io.c:1210`
- `Bootstrap\Ultraviolet\runtime\src\io\io.c:1211`
- `Bootstrap\Ultraviolet\runtime\src\io\io.c:1351`
- `Bootstrap\Ultraviolet\runtime\src\io\io.c:1352`
- `Bootstrap\Ultraviolet\runtime\src\io\io.c:1353`
- `Bootstrap\Ultraviolet\runtime\src\io\io.c:1354`
- `Bootstrap\Ultraviolet\runtime\src\io\io.c:1355`
- `Bootstrap\Ultraviolet\runtime\src\io\io.c:1356`
- `Bootstrap\Ultraviolet\runtime\src\io\io.c:1400`
- `Bootstrap\Ultraviolet\runtime\src\io\io.c:1401`

Observed behavior:

The directory iterator builds each entry's sort key with `uv_entry_key_utf8`,
which performs ASCII case folding directly and otherwise requires ICU
configuration, NFC normalization, full case folding, and UTF-8 conversion. If
that key construction fails, `open_dir` does not report an IO error. It assigns
`key_utf8 = name_utf8` and continues. The later comparator sorts by `key_utf8`
first and raw `name_utf8` only as the tie-breaker.

Expected behavior:

The specification makes `NFC` and `CaseFold` total for scalar sequences and
defines `EntryKey(name) = CaseFold(NFC(name))`; `EntryOrder` must compare
`Utf8(EntryKey(...))` first and raw name bytes only when the entry keys are
equal. `DirEntries` must be sorted with that order. Runtime inability to build
the required key cannot silently replace the key with raw name bytes.

Impact:

When key construction fails for a non-ASCII entry, `DirIter@Open::next` can
observe directory entries in an order that differs from the specified
case-folded NFC order. This weakens deterministic runtime behavior across
platforms and resource states.

HelloUltraviolet fixture gap:

`HelloUltraviolet\Source\Reference\Authority\IO.uv:1538`,
`HelloUltraviolet\Source\Reference\Authority\IO.uv:1540`,
`HelloUltraviolet\Source\Reference\Authority\IO.uv:1542`, and
`HelloUltraviolet\Source\Reference\Authority\IO.uv:1545` cover ordinary
directory ordering with ASCII names. The catalog credits
`def.DirectoryEntryOrdering` at
`HelloUltraviolet\Source\Audit\Catalog\AbstractMachineObjectsResponsibilityAndAuthority\HostPrimitives.uv:425`.
No fixture covers non-ASCII names, ICU/key-construction failure, or the
required fallback behavior when directory-order key construction fails.

### UV-AUDIT-0200: Region allocation lowering stores through a nullable runtime pointer

Severity: High

Status: Locally verified.

Specification anchors:

- `Docs\SPECIFICATION.md:4598`
- `Docs\SPECIFICATION.md:4640`
- `Docs\SPECIFICATION.md:4642`
- `Docs\SPECIFICATION.md:10773`
- `Docs\SPECIFICATION.md:10786`
- `Docs\SPECIFICATION.md:29831`
- `Docs\SPECIFICATION.md:29833`
- `Docs\SPECIFICATION.md:30115`

Implementation anchors:

- `Bootstrap\Ultraviolet\runtime\src\memory\region.c:591`
- `Bootstrap\Ultraviolet\runtime\src\memory\region.c:596`
- `Bootstrap\Ultraviolet\runtime\src\memory\region.c:597`
- `Bootstrap\Ultraviolet\runtime\src\memory\region.c:598`
- `Bootstrap\Ultraviolet\runtime\src\memory\region.c:606`
- `Bootstrap\Ultraviolet\runtime\src\memory\region.c:607`
- `Bootstrap\Ultraviolet\runtime\src\memory\region.c:608`
- `Bootstrap\Ultraviolet\runtime\src\memory\region.c:609`
- `Bootstrap\Ultraviolet\runtime\src\memory\region.c:610`
- `Bootstrap\Ultraviolet\runtime\src\memory\region.c:613`
- `Bootstrap\Ultraviolet\runtime\src\memory\region.c:614`
- `Bootstrap\Ultraviolet\runtime\src\memory\region.c:615`
- `Bootstrap\Ultraviolet\runtime\src\memory\region.c:616`
- `Bootstrap\Ultraviolet\runtime\src\memory\region.c:617`
- `Bootstrap\Ultraviolet\runtime\src\memory\region.c:618`
- `Bootstrap\Ultraviolet\runtime\src\memory\region.c:619`
- `Bootstrap\Ultraviolet\runtime\src\memory\region.c:620`
- `Bootstrap\Ultraviolet\runtime\src\memory\region.c:625`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\alloc_expr.cpp:150`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\alloc_expr.cpp:153`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\alloc_expr.cpp:154`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\alloc_expr.cpp:155`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\alloc_expr.cpp:156`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\alloc_expr.cpp:157`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\alloc_expr.cpp:162`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\alloc_expr.cpp:163`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\alloc_expr.cpp:164`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\ops\alloc.cpp:230`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\ops\alloc.cpp:247`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\ops\alloc.cpp:248`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\ops\alloc.cpp:249`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\ops\alloc.cpp:250`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\ops\alloc.cpp:289`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\ops\alloc.cpp:290`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\ops\alloc.cpp:291`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\ops\alloc.cpp:292`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\ops\alloc.cpp:294`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\ops\alloc.cpp:295`

Observed behavior:

The runtime `Region::alloc` implementation returns a raw pointer with no error
channel. It can return null when the receiver handle is zero, when the arena or
region entry cannot be resolved, when arena allocation fails, or when allocation
tracking cannot reserve a slot. The normal allocation-expression lowering
registers the allocation result as a value loaded from that pointer. The LLVM
emitter checks only whether it obtained an LLVM value for the call instruction,
then bitcasts the returned pointer and stores the source value through it. It
does not branch on the runtime pointer value.

Expected behavior:

`RegionAlloc` is specified as producing a fresh address, writing the value,
appending that address to the arena, tagging it with the region tag, and yielding
the stored value. `Region::alloc` returns `T_π_Region(self)`, not an optional
pointer or an `Outcome`. The lowering rule likewise sequences `CallIR` followed
by `Store(p, v, T)` for the allocation pointer. A conforming implementation
therefore must not turn runtime allocation failure into a null pointer that is
then used as the destination for the required store.

Impact:

Region allocation can become a null-pointer store or a fabricated default
allocation result under allocation or tracking failure. That violates the
specified fresh-address and provenance semantics and can crash generated code
instead of preserving the language-level region allocation contract.

HelloUltraviolet fixture gap:

Existing fixtures exercise successful region allocation in
`HelloUltraviolet\Source\Reference\Statements\Bindings.uv:73`,
`HelloUltraviolet\Source\Reference\Statements\Bindings.uv:74`,
`HelloUltraviolet\Source\Reference\Statements\Bindings.uv:85`,
`HelloUltraviolet\Source\Reference\Statements\Bindings.uv:87`,
`HelloUltraviolet\Source\Reference\Authority\RegionsAndFrames.uv:66`,
`HelloUltraviolet\Source\Reference\Authority\RegionsAndFrames.uv:67`,
`HelloUltraviolet\Fixtures\ArtifactProjects\RuntimeInterfaceSymbols\Source\Library.uv:371`,
and `HelloUltraviolet\Fixtures\ArtifactProjects\RuntimeInterfaceSymbols\Source\Library.uv:372`.
Artifact checks confirm the runtime symbol declaration and call at
`HelloUltraviolet\Source\Audit\RuntimeInterfaceArtifactExecution.uv:105` and
`HelloUltraviolet\Source\Audit\RuntimeInterfaceArtifactExecution.uv:468`.
No fixture forces runtime region allocation failure or verifies that generated
code cannot store through a null region allocation pointer.

### UV-AUDIT-0201: `Region::new_scoped` can return a freed value for an active-region type

Severity: High

Status: Locally verified.

Specification anchors:

- `Docs\SPECIFICATION.md:4324`
- `Docs\SPECIFICATION.md:4634`
- `Docs\SPECIFICATION.md:4635`
- `Docs\SPECIFICATION.md:4637`
- `Docs\SPECIFICATION.md:10772`
- `Docs\SPECIFICATION.md:29019`

Implementation anchors:

- `Bootstrap\Ultraviolet\runtime\src\memory\region.c:840`
- `Bootstrap\Ultraviolet\runtime\src\memory\region.c:841`
- `Bootstrap\Ultraviolet\runtime\src\memory\region.c:844`
- `Bootstrap\Ultraviolet\runtime\src\memory\region.c:845`
- `Bootstrap\Ultraviolet\runtime\src\memory\region.c:846`
- `Bootstrap\Ultraviolet\runtime\src\memory\region.c:862`
- `Bootstrap\Ultraviolet\runtime\src\memory\region.c:863`
- `Bootstrap\Ultraviolet\runtime\src\memory\region.c:864`
- `Bootstrap\Ultraviolet\runtime\src\memory\region.c:870`
- `Bootstrap\Ultraviolet\runtime\src\memory\region.c:871`
- `Bootstrap\Ultraviolet\runtime\src\memory\region.c:872`
- `Bootstrap\Ultraviolet\runtime\src\memory\region.c:873`
- `Bootstrap\Ultraviolet\runtime\src\memory\region.c:877`

Observed behavior:

The runtime `Region::new_scoped` constructor returns `UV_REGION_FREED` with
handle `0` when arena allocation fails, or when inserting the arena or pushing
the region-stack entry fails. Only the successful path returns
`UV_REGION_ACTIVE` with the allocated handle.

Expected behavior:

The specification classifies `Region::new_scoped(...)` as a fresh
`Region@Active` producer. `Region-New-Scoped` constructs
`RegionValue(@Active, r)`, and the procedure signature returns
`unique Region@Active`. There is no `Outcome` or modal union in this surface
that can represent `Region@Freed` as a successful constructor result.

Impact:

Generated code can bind a value statically typed as `unique Region@Active`
while the runtime discriminant says `@Freed` and the handle is zero. Follow-up
region operations then operate on an impossible state for the static type,
including the null region-allocation path documented in `UV-AUDIT-0200`.

HelloUltraviolet fixture gap:

Existing coverage exercises only successful region construction in
`HelloUltraviolet\Source\Reference\Statements\Bindings.uv:73`,
`HelloUltraviolet\Source\Reference\Statements\Bindings.uv:85`,
`HelloUltraviolet\Source\Reference\Authority\RegionsAndFrames.uv:66`, and
`HelloUltraviolet\Fixtures\ArtifactProjects\RuntimeInterfaceSymbols\Source\Library.uv:371`.
No fixture forces constructor-state allocation or registry failure and asserts
that `Region::new_scoped` cannot return `@Freed` for a `Region@Active` result.

### UV-AUDIT-0202: Cancel-token construction can return the invalid sentinel as an active token

Severity: High

Status: Locally verified.

Specification anchors:

- `Docs\SPECIFICATION.md:22627`
- `Docs\SPECIFICATION.md:22629`
- `Docs\SPECIFICATION.md:22647`
- `Docs\SPECIFICATION.md:22653`
- `Docs\SPECIFICATION.md:22654`
- `Docs\SPECIFICATION.md:22656`
- `Docs\SPECIFICATION.md:22658`
- `Docs\SPECIFICATION.md:22659`
- `Docs\SPECIFICATION.md:22661`
- `Docs\SPECIFICATION.md:29286`
- `Docs\SPECIFICATION.md:29289`

Implementation anchors:

- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:32`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:248`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:449`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:463`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:464`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:465`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:466`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:467`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:483`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:484`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:485`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:486`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:489`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:490`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:491`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:492`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:3507`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:3508`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:3509`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:3510`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:3514`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:3516`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:3630`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:3631`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:3632`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:3633`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:3634`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:3638`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:3639`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:3642`

Observed behavior:

`CancelToken::new` returns `UV_CANCEL_INVALID_ID` if registry initialization
fails or if registry growth fails while creating the fresh entry. The
`CancelToken@Active::child` method also returns the same invalid sentinel when
the registry is unavailable, the parent id is invalid, or child-entry allocation
fails. Both functions expose the sentinel through return types that represent
`CancelToken@Active`.

Expected behavior:

`CancelToken::new` must return `CancelToken@Active` backed by a fresh cancel id
inserted into the cancellation map with parent `⊥` and status `Active`.
`CancelToken@Active::child()` must return a descendant `CancelToken@Active`
backed by a fresh id whose parent is the receiver id. The specified surface has
no error or sentinel result for these operations.

Impact:

Source code can hold a value statically typed as `CancelToken@Active` whose id
is not in the cancel map. Cancellation requests against that token are ignored,
`is_cancelled` reports false, child construction cannot establish the specified
parent relation, and wait behavior can diverge further as documented in
`UV-AUDIT-0203`.

HelloUltraviolet fixture gap:

Existing coverage exercises successful token creation and child cancellation in
`HelloUltraviolet\Source\Reference\Parallelism\Cancellation.uv:4`,
`HelloUltraviolet\Source\Reference\Parallelism\Cancellation.uv:5`,
`HelloUltraviolet\Source\Reference\Parallelism\Cancellation.uv:6`,
`HelloUltraviolet\Source\Reference\Parallelism\Cancellation.uv:8`,
`HelloUltraviolet\Source\Reference\Authority\CapabilityFormalModel.uv:388`,
`HelloUltraviolet\Source\Reference\Authority\CapabilityFormalModel.uv:389`,
`HelloUltraviolet\Source\Reference\Authority\CapabilityFormalModel.uv:390`,
and `HelloUltraviolet\Source\Reference\Authority\CapabilityFormalModel.uv:391`.
No fixture forces cancel-registry allocation failure or asserts that active
tokens cannot carry the invalid sentinel.

### UV-AUDIT-0203: Active cancel-token waits complete on wait-frame allocation failure

Severity: High

Status: Locally verified.

Specification anchors:

- `Docs\SPECIFICATION.md:22635`
- `Docs\SPECIFICATION.md:22673`
- `Docs\SPECIFICATION.md:22674`
- `Docs\SPECIFICATION.md:22676`
- `Docs\SPECIFICATION.md:22678`
- `Docs\SPECIFICATION.md:22679`
- `Docs\SPECIFICATION.md:22681`
- `Docs\SPECIFICATION.md:22704`
- `Docs\SPECIFICATION.md:22706`
- `Docs\SPECIFICATION.md:22708`
- `Docs\SPECIFICATION.md:22710`
- `Docs\SPECIFICATION.md:29290`

Implementation anchors:

- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:281`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:285`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:286`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:289`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:294`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:295`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:310`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:311`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:312`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:318`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:319`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:323`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:3645`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:3649`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:3650`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:3651`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:3652`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:3656`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:3657`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:3658`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:3661`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:3665`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:3666`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:3668`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:3669`

Observed behavior:

`CancelToken@Active::wait_cancelled` writes a completed async value when the
token id is invalid or already cancelled. For a valid active token, it allocates
a wait frame. If that allocation fails, it still writes a completed async value
instead of a suspended value. The resume helper also completes if it sees an
invalid token id.

Expected behavior:

`Cancel-WaitCancelled-Completed` applies when the token status is `Cancelled`.
`Cancel-WaitCancelled-Suspended` applies when the token status is `Active`.
Allocation failure while building the runtime wait frame cannot change an
active token's status to cancelled, and the specified result for an active token
is the suspended async state.

Impact:

Code waiting on an active, not-yet-cancelled token can observe immediate
completion when the runtime cannot allocate the wait frame. That can make
structured parallel cancellation checks and user async control flow proceed as
if cancellation occurred.

HelloUltraviolet fixture gap:

Existing fixtures exercise normal wait/cancel behavior in
`HelloUltraviolet\Source\Reference\Parallelism\Cancellation.uv:13`,
`HelloUltraviolet\Source\Reference\Parallelism\Cancellation.uv:14`,
`HelloUltraviolet\Source\Reference\Parallelism\Cancellation.uv:15`,
`HelloUltraviolet\Source\Reference\Parallelism\Cancellation.uv:17`,
`HelloUltraviolet\Source\Reference\Parallelism\Cancellation.uv:18`,
`HelloUltraviolet\Fixtures\ArtifactProjects\StructuredParallelLoweringEvidence\Source\Main.uv:20`,
`HelloUltraviolet\Fixtures\ArtifactProjects\StructuredParallelLoweringEvidence\Source\Main.uv:21`,
and `HelloUltraviolet\Fixtures\ArtifactProjects\StructuredParallelLoweringEvidence\Source\Main.uv:25`.
No fixture forces wait-frame allocation failure or asserts that an active token
wait remains suspended until cancellation.

### UV-AUDIT-0204: Dynamic foreign `@error` postconditions use the wrong classification

Severity: High

Status: Agent-reported and locally corroborated.

Spec anchors:

- `Docs\SPECIFICATION.md:26776`
- `Docs\SPECIFICATION.md:26782`
- `Docs\SPECIFICATION.md:26784`
- `Docs\SPECIFICATION.md:26788`
- `Docs\SPECIFICATION.md:26789`
- `Docs\SPECIFICATION.md:26834`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\call.cpp:546`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\call.cpp:552`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\call.cpp:564`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\call.cpp:565`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\call.cpp:568`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\call.cpp:569`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\call.cpp:587`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\call.cpp:615`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\call.cpp:656`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\call.cpp:658`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\call.cpp:659`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\call.cpp:662`

Observed behavior:

Dynamic foreign postcondition lowering evaluates every `@error` predicate into
`err_pred_values`, but combines multiple predicates with `&&` into
`foreign_err_cond_and`. It then derives `SuccessCond` from that conjunctive
condition. The helper that emits guarded checks is called for ordinary
postconditions with `success_cond` and for `@null_result` postconditions with
`null_cond`; it is never called for the `@error` predicate list guarded by
`ErrCond`.

Expected behavior:

The specification defines `ErrCond` as the disjunction of the `@error`
predicates and classifies the foreign call as an error iff `ErrCond` holds.
Dynamic lowering must enforce every foreign postcondition implication
immediately after the foreign call returns, including `ErrCond => P` for each
error predicate.

Impact:

A foreign call with multiple dynamic error predicates is treated as success
unless every error predicate is true, and the error-path predicate checks are
not inserted. Success postconditions can run on error results, while specified
error-path guarantees are not enforced.

HelloUltraviolet fixture gap:

`HelloUltraviolet\Source\Reference\FFI\ForeignContracts.uv:10` covers a dynamic
ordinary foreign postcondition, and
`HelloUltraviolet\Source\Reference\FFI\ForeignContracts.uv:15` covers a dynamic
`@null_result` postcondition. The rejected void-return `@error` fixture covers
well-formedness only. No accepted fixture combines multiple dynamic `@error`
predicates and asserts the runtime error classification/check behavior.

### UV-AUDIT-0205: Implicit exits run cleanup before dynamic postconditions

Severity: Medium

Status: Agent-reported and locally corroborated.

Spec anchors:

- `Docs\SPECIFICATION.md:14780`
- `Docs\SPECIFICATION.md:14880`
- `Docs\SPECIFICATION.md:14953`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\05_codegen\lower\lower_proc.cpp:1530`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\lower_proc.cpp:1551`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\lower_proc.cpp:1552`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\lower_proc.cpp:1553`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\lower_proc.cpp:1556`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\lower_proc.cpp:1558`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\lower_proc.cpp:1562`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\lower_proc.cpp:1566`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\stmt\return_stmt.cpp:263`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\stmt\return_stmt.cpp:276`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\stmt\return_stmt.cpp:279`

Observed behavior:

Explicit `return` lowering emits `EmitDynamicPostconditionCheckForReturn`
before function-root cleanup and `IRReturn`. Procedure lowering for tail
expressions and unit fallthrough computes `cleanup_ir`, appends it when the
body may fall through, and only then emits the dynamic postcondition check and
the implicit `IRReturn`.

Expected behavior:

Postconditions are evaluated at each return point with `@result` bound to the
returned value. The postcondition context includes the receiver, parameters,
`@result`, and `@entry`, so cleanup that destroys those values must not precede
the check on implicit return paths.

Impact:

Tail-expression and unit-fallthrough exits can evaluate dynamic
postconditions after relevant values have been cleaned up. This makes implicit
return semantics differ from explicit return semantics and can produce invalid
or missed contract observations.

HelloUltraviolet fixture gap:

Existing postcondition references use explicit returns, including
`HelloUltraviolet\Source\Reference\Procedures\Postconditions.uv:27` and
`HelloUltraviolet\Source\Reference\Procedures\Postconditions.uv:33`. There is
no contracted tail-expression or unit-fallthrough case that exercises this
lowering path.

### UV-AUDIT-0206: Raw catch-export SRet returns skip `ZeroValue(R)` storage

Severity: High

Status: Agent-reported and locally corroborated.

Spec anchors:

- `Docs\SPECIFICATION.md:26165`
- `Docs\SPECIFICATION.md:26171`
- `Docs\SPECIFICATION.md:30275`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\control\return.cpp:48`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\control\return.cpp:76`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\control\return.cpp:96`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\control\return.cpp:123`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\control\return.cpp:125`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\control\return.cpp:133`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\control\return.cpp:169`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\control\return.cpp:181`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\control\return.cpp:193`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\control\return.cpp:201`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\control\return.cpp:216`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\control\return.cpp:224`

Observed behavior:

`IRReturn` invokes `EmitCatchExportPanicReturn` before normal return lowering
for raw catch-export boundaries. On the panic branch, that helper clears the
panic record and emits `ret void` whenever the LLVM return type is `void`.
`LLVMRetLower-SRet` also lowers SRet returns to `void`, but the source return
value is represented by the hidden out-parameter. The panic branch does not
resolve or write that out-parameter before returning.

Expected behavior:

When `UnwindMode(proc) = "catch"`, a raw exported procedure must return
`ZeroValue(R)` for the source return type `R`. For SRet lowering, satisfying
that obligation requires writing the all-zero `R` value into the SRet storage
before the boundary returns `void`.

Impact:

A foreign caller of a raw catch export with an aggregate/SRet return can
observe stale or uninitialized caller-provided return storage after a panic at
the boundary.

HelloUltraviolet fixture gap:

`HelloUltraviolet\Source\Audit\ArtifactProjectExecution.uv:4283` and
`HelloUltraviolet\Source\Audit\ArtifactProjectExecution.uv:4287` assert the
scalar `i32` catch-export zero path. SRet lowering is covered separately at
`HelloUltraviolet\Source\Audit\ArtifactProjectExecution.uv:4648` and
`HelloUltraviolet\Source\Audit\ArtifactProjectExecution.uv:4686`, but no
fixture combines raw `#unwind("catch")` with an SRet return and verifies the
panic branch writes zero storage.

### UV-AUDIT-0207: Finite same-generic instantiation chains are rejected as infinite

Severity: Medium

Status: Agent-reported and locally corroborated.

Spec anchors:

- `Docs\SPECIFICATION.md:12638`
- `Docs\SPECIFICATION.md:12642`
- `Docs\SPECIFICATION.md:12643`
- `Docs\SPECIFICATION.md:12644`
- `Docs\SPECIFICATION.md:13949`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\call.cpp:1358`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\call.cpp:1361`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\call.cpp:1365`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\call.cpp:1366`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\call.cpp:1821`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\call.cpp:1823`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\call.cpp:1858`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\call.cpp:1861`

Observed behavior:

`GenericInstantiationWouldRecurse` scans the active generic declaration stack
by base symbol. If it finds the same base symbol with a non-equivalent concrete
argument vector, it returns true. `LowerCallExpr` maps that result directly to
`E-TYP-2307`, before the monomorphic symbol for the new argument vector can be
lowered as an independent instantiation.

Expected behavior:

Monomorphization produces a specialized declaration for each concrete
instantiation, each distinct instantiation lowers independently, and only
infinite monomorphization recursion is rejected. A same generic declaration
with a different finite type-argument vector is another instantiation node, not
recursion by itself.

Impact:

Valid finite generic programs can be rejected during `uvc` lowering when a
generic body calls the same generic declaration with a different concrete type
argument vector that would otherwise reach a finite set of instantiations.

HelloUltraviolet fixture gap:

`HelloUltraviolet\Fixtures\RejectedSource\Polymorphism\GenericInfiniteMonomorphization\Source\Main.uv:7`
and
`HelloUltraviolet\Fixtures\RejectedSource\Polymorphism\GenericInfiniteMonomorphization\Source\Main.uv:8`
cover a genuinely expanding instantiation, and
`HelloUltraviolet\Source\Audit\Catalog\AbstractionAndPolymorphism\GenericProceduresAndTypes.uv:227`
through
`HelloUltraviolet\Source\Audit\Catalog\AbstractionAndPolymorphism\GenericProceduresAndTypes.uv:231`
validate that rejected fixture. There is no accepted fixture for a finite
same-generic, different-type-argument instantiation chain.

### UV-AUDIT-0208: Unterminated modal bodies can loop at EOF

Severity: High

Status: Agent-reported and locally corroborated.

Spec anchors:

- `Docs\SPECIFICATION.md:2787`
- `Docs\SPECIFICATION.md:2789`
- `Docs\SPECIFICATION.md:2791`
- `Docs\SPECIFICATION.md:3169`
- `Docs\SPECIFICATION.md:3174`
- `Docs\SPECIFICATION.md:3184`
- `Docs\SPECIFICATION.md:10592`
- `Docs\SPECIFICATION.md:10593`
- `Docs\SPECIFICATION.md:10594`
- `Docs\SPECIFICATION.md:10608`
- `Docs\SPECIFICATION.md:10609`
- `Docs\SPECIFICATION.md:10613`
- `Docs\SPECIFICATION.md:10614`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\02_source\parser\item\modal_decl.cpp:254`
- `Bootstrap\Ultraviolet\src\02_source\parser\item\modal_decl.cpp:278`
- `Bootstrap\Ultraviolet\src\02_source\parser\item\modal_decl.cpp:280`
- `Bootstrap\Ultraviolet\src\02_source\parser\item\modal_decl.cpp:291`
- `Bootstrap\Ultraviolet\src\02_source\parser\item\modal_decl.cpp:297`
- `Bootstrap\Ultraviolet\src\02_source\parser\item\modal_decl.cpp:320`
- `Bootstrap\Ultraviolet\src\02_source\parser\item\modal_decl.cpp:324`
- `Bootstrap\Ultraviolet\src\02_source\parser\item\modal_decl.cpp:343`
- `Bootstrap\Ultraviolet\src\02_source\parser\item\modal_decl.cpp:355`
- `Bootstrap\Ultraviolet\src\02_source\parser\item\modal_decl.cpp:365`
- `Bootstrap\Ultraviolet\src\02_source\parser\item\modal_decl.cpp:366`
- `Bootstrap\Ultraviolet\src\02_source\parser\item\modal_decl.cpp:399`
- `Bootstrap\Ultraviolet\src\02_source\parser\parser.cpp:131`
- `Bootstrap\Ultraviolet\src\02_source\parser\parser.cpp:132`
- `Bootstrap\Ultraviolet\src\02_source\parser\parser.cpp:133`

Observed behavior:

`ParseStateMemberList` and `ParseStateBlockList` both loop until `}`. When the
input reaches EOF inside an unterminated modal body or state body,
`ParseStateBlock` can report a syntax error and call `AdvanceOrEOF`, but
`AdvanceOrEOF` returns the same parser state at EOF. `ParseStateBlockList` has
no progress guard, so it can continue parsing EOF as another state block
forever.

Expected behavior:

The modal-body grammar requires a closing `}` for both the modal body and each
state block, and parser diagnostics map generic syntax failure to `E-SRC-0520`.
Malformed modal bodies should therefore terminate parsing at EOF with a source
diagnostic instead of re-entering the same state-block parse.

Impact:

An unterminated modal declaration can hang `uvc` during parsing rather than
producing a deterministic source diagnostic.

HelloUltraviolet fixture gap:

`HelloUltraviolet\Source\Reference\ModalTypes\ModalDeclarations.uv:3` exercises
valid modal declarations. There is no rejected fixture for an unterminated modal
body or unterminated state block.

### UV-AUDIT-0209: Modal payload fields are incorrectly rejected for method or transition name reuse

Severity: Medium

Status: Agent-reported and locally corroborated.

Spec anchors:

- `Docs\SPECIFICATION.md:10638`
- `Docs\SPECIFICATION.md:10639`
- `Docs\SPECIFICATION.md:10640`
- `Docs\SPECIFICATION.md:10641`
- `Docs\SPECIFICATION.md:10654`
- `Docs\SPECIFICATION.md:10695`
- `Docs\SPECIFICATION.md:10698`
- `Docs\SPECIFICATION.md:11104`
- `Docs\SPECIFICATION.md:11105`
- `Docs\SPECIFICATION.md:11188`
- `Docs\SPECIFICATION.md:11189`
- `Docs\SPECIFICATION.md:11190`
- `Docs\SPECIFICATION.md:11209`
- `Docs\SPECIFICATION.md:11210`
- `Docs\SPECIFICATION.md:12265`
- `Docs\SPECIFICATION.md:12272`
- `Docs\SPECIFICATION.md:25329`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\modal_decl.cpp:107`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\modal_decl.cpp:207`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\modal_decl.cpp:226`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\modal_decl.cpp:232`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\modal_decl.cpp:617`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\modal_decl.cpp:618`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\modal_decl.cpp:651`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\modal_decl.cpp:652`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\modal_decl.cpp:653`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\modal_decl.cpp:1129`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\modal_decl.cpp:1130`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\modal_decl.cpp:1131`

Observed behavior:

`TypeModalDecl` first checks duplicate payload fields with
`DistinctStateFieldNames`, then also calls `StateMemberNamesDisjointAll`. That
second helper inserts payload fields, state methods, and transitions into a
single name set and emits `StateMember-Name-Conflict` when a field shares a name
with a method or transition.

Expected behavior:

Payload fields have the `Modal-Payload-DupField` rule, while
`StateMember-Name-Conflict` is defined only for the intersection of
`StateMethodNames(M, S)` and `TransitionNames(M, S)`. `ReflectStates` also
exposes fields, methods, and transitions as separate lists. A field name should
not be rejected solely because a method or transition in the same state uses the
same identifier.

Impact:

Valid modal declarations can be rejected with `E-TYP-2065` even though the
specified conflict applies only to method/transition name overlap.

HelloUltraviolet fixture gap:

`HelloUltraviolet\Source\Reference\ModalTypes\StateFields.uv:3`,
`HelloUltraviolet\Source\Reference\ModalTypes\StateMethods.uv:3`, and
`HelloUltraviolet\Source\Reference\ModalTypes\Transitions.uv:3` cover those
member categories independently. There is no fixture where a state field
intentionally shares a name with a state method or transition.

### UV-AUDIT-0210: Defaulted generic modal method calls skip modal-reference substitution

Severity: Medium

Status: Agent-reported and locally corroborated.

Spec anchors:

- `Docs\SPECIFICATION.md:10676`
- `Docs\SPECIFICATION.md:10678`
- `Docs\SPECIFICATION.md:10700`
- `Docs\SPECIFICATION.md:10701`
- `Docs\SPECIFICATION.md:11128`
- `Docs\SPECIFICATION.md:11129`
- `Docs\SPECIFICATION.md:11131`
- `Docs\SPECIFICATION.md:12463`
- `Docs\SPECIFICATION.md:12468`
- `Docs\SPECIFICATION.md:25825`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\method_call.cpp:1888`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\method_call.cpp:1897`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\method_call.cpp:1898`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\method_call.cpp:1899`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\method_call.cpp:1904`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\method_call.cpp:1910`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\method_call.cpp:1911`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\method_call.cpp:1931`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\method_call.cpp:1932`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\method_call.cpp:1951`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\method_call.cpp:1952`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\method_call.cpp:1966`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\method_call.cpp:1967`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\method_call.cpp:1977`
- `Bootstrap\Ultraviolet\src\04_analysis\generics\monomorphize.cpp:926`
- `Bootstrap\Ultraviolet\src\04_analysis\generics\monomorphize.cpp:932`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\record_literal.cpp:271`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\field_access.cpp:289`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\field_access.cpp:290`

Observed behavior:

Modal method-call typing builds `modal_subst` only when the modal declaration's
generic parameter count exactly equals the receiver modal state's stored
argument count. If a generic modal uses defaulted parameters and the receiver
omits those arguments, `modal_subst` remains empty. The method receiver,
parameter, transition parameter, and method return-type paths then lower through
`modal_lower_type` without substituting defaulted modal arguments.

Expected behavior:

`ModalRefSubst` is defined through `DefaultArgs(params_gen,
ModalRefArgs(modal_ref))`, so a well-formed modal state with omitted defaulted
arguments still has a complete substitution for state payloads, methods, and
transitions. The record-literal and field-access paths already call
`BuildModalRefSubstitution`; modal method-call typing should use the same
default-aware substitution shape.

Impact:

Valid receivers such as a generic modal state with supplied leading arguments
and omitted default arguments can leave method signatures containing
unsubstituted type parameters, producing bad argument checks or incorrect return
types.

HelloUltraviolet fixture gap:

`HelloUltraviolet\Source\Reference\ModalTypes\ModalDeclarations.uv:44` defines
a generic modal without defaulted parameters, and the generic modal calls at
`:88` and `:94` use explicit arguments. Generic defaults are covered for other
generic surfaces in
`HelloUltraviolet\Source\Reference\Polymorphism\GenericParameters.uv:5` and
`:16`, but not for modal method calls.

### UV-AUDIT-0211: Async combinator lowering does not build persistent wrapper state machines

Severity: High

Status: Locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:23834`
- `Docs\SPECIFICATION.md:23835`
- `Docs\SPECIFICATION.md:23836`
- `Docs\SPECIFICATION.md:24199`
- `Docs\SPECIFICATION.md:24227`
- `Docs\SPECIFICATION.md:24256`
- `Docs\SPECIFICATION.md:24284`
- `Docs\SPECIFICATION.md:24312`
- `Docs\SPECIFICATION.md:24381`
- `Docs\SPECIFICATION.md:24383`
- `Docs\SPECIFICATION.md:24388`
- `Docs\SPECIFICATION.md:24393`
- `Docs\SPECIFICATION.md:24398`
- `Docs\SPECIFICATION.md:24403`
- `Docs\SPECIFICATION.md:24408`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\method_call.cpp:1880`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\method_call.cpp:1883`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\method_call.cpp:1891`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\method_call.cpp:1931`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\method_call.cpp:1932`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\call\direct.cpp:620`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\call\direct.cpp:621`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\call\direct.cpp:1033`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\call\direct.cpp:1051`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\call\direct.cpp:1061`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\call\direct.cpp:1066`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\call\direct.cpp:1070`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\call\direct.cpp:1074`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\call\direct.cpp:1095`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\call\direct.cpp:1110`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\call\direct.cpp:1115`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\call\direct.cpp:1118`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\call\direct.cpp:1122`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\call\direct.cpp:1126`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\call\direct.cpp:1185`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\call\direct.cpp:1188`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\call\direct.cpp:1194`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\call\direct.cpp:1199`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\call\direct.cpp:1220`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\call\direct.cpp:1266`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\call\direct.cpp:1299`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\call\direct.cpp:1322`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\call\direct.cpp:1326`

Observed behavior:

`LowerMethodCall` lowers async combinators to pseudo-symbol calls, and LLVM
emission interprets those calls immediately. `map` mutates the current source
async value's suspended payload and returns that same source state, `filter`
loops and resumes the source until one value passes or the source exits, and
`fold` allocates an accumulator local in the current function and drains the
source loop during emission. Only `take` delegates to a runtime helper that
allocates a wrapper frame.

Expected behavior:

The lowering section requires `AsyncMapState`, `AsyncFilterState`,
`AsyncTakeState`, `AsyncFoldState`, and `AsyncChainState`. Each wrapper
lowering must delegate to the source async through `resume`, store local wrapper
state in the generated async frame, and preserve the Chapter 21 dynamic
semantics exactly. Construction of a combinator should produce a resumable
wrapper, not consume or transform the source with only current-function locals.

Impact:

Combinator state can be lost after the first returned suspension because later
`resume` calls still target the source async frame rather than a wrapper frame.
Multi-output `map` can map only the first observed output, `filter` can stop
filtering after the first passing output, and `fold`/`chain` can perform source
advancement at construction-time rather than wrapper-resume time.

HelloUltraviolet fixture gap:

`HelloUltraviolet\Source\Reference\Async\CompositionForms.uv:407`,
`:414`, `:421`, `:428`, and `:436` cover basic combinator forms, and
`HelloUltraviolet\Source\Reference\Async\CombinatorRuntimeForms.uv:34` through
`:129` cover selected runtime edges. The coverage does not resume a mapped
multi-output stream to verify the second output is transformed, does not resume
a filter with multiple passing outputs, and does not assert that `fold` or
`chain` construction itself is only wrapper creation.

### UV-AUDIT-0212: Unknown assembly keys emit the wrong diagnostic code

Severity: Low

Status: Agent-reported and locally corroborated.

Spec anchors:

- `Docs\SPECIFICATION.md:481`
- `Docs\SPECIFICATION.md:483`
- `Docs\SPECIFICATION.md:487`
- `Docs\SPECIFICATION.md:490`
- `Docs\SPECIFICATION.md:793`
- `Docs\SPECIFICATION.md:796`
- `Docs\SPECIFICATION.md:797`
- `Docs\SPECIFICATION.md:1730`
- `Docs\SPECIFICATION.md:1731`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\01_project\project_validate.cpp:653`
- `Bootstrap\Ultraviolet\src\01_project\project_validate.cpp:655`
- `Bootstrap\Ultraviolet\src\01_project\project_validate.cpp:656`
- `Bootstrap\Ultraviolet\src\01_project\project_validate.cpp:657`
- `Bootstrap\Ultraviolet\src\00_core\generated\diag_registry.inc:236`
- `Bootstrap\Ultraviolet\src\00_core\generated\diag_registry.inc:237`
- `Bootstrap\Ultraviolet\src\00_core\generated\diag_registry.inc:1252`
- `Bootstrap\Ultraviolet\src\00_core\generated\diag_registry.inc:1276`

Observed behavior:

Project validation records `WF-Assembly-Keys-Err` when an assembly table
contains an unknown key, but returns `fail("E-PRJ-0104",
"WF-Assembly-Keys-Err")`. The generated registry maps
`WF-Assembly-Keys-Err` to `E-PRJ-0103`; `E-PRJ-0104` is mapped to
`WF-TopKeys-Err`.

Expected behavior:

Diagnostic code selection requires an emitted diagnostic for an obligation id to
use `Code(id)`. Since the project diagnostics table assigns
`WF-Assembly-Keys-Err` to `E-PRJ-0103`, unknown assembly-table keys recorded
under that obligation should emit `E-PRJ-0103` unless the specification is
changed.

Impact:

`uvc` can emit a public diagnostic code that disagrees with the canonical
obligation-code mapping. Output diagnostic fixtures can pass or fail for the
wrong reason if they check only one side of the code/obligation pair.

HelloUltraviolet fixture gap:

`HelloUltraviolet\Source\Reference\Projects\ManifestModel.uv:224` models
`WF-Assembly-Keys-Err`, and the audit catalog credits it, but
`HelloUltraviolet\Fixtures\OutputDiagnostics\Projects` contains no executable
fixture for an unknown assembly-table key asserting the emitted `uvc` code.

### UV-AUDIT-0213: Diagnostic fixture checks do not bind codes and obligations to one event

Severity: Medium

Status: Agent-reported and locally corroborated.

Spec anchors:

- `Docs\SPECIFICATION.md:481`
- `Docs\SPECIFICATION.md:483`
- `Docs\SPECIFICATION.md:487`
- `Docs\SPECIFICATION.md:490`
- `Docs\SPECIFICATION.md:494`
- `Docs\SPECIFICATION.md:497`
- `Docs\SPECIFICATION.md:503`

Implementation anchors:

- `HelloUltraviolet\Source\Audit\DiagnosticFixtureExecution.uv:203`
- `HelloUltraviolet\Source\Audit\DiagnosticFixtureExecution.uv:217`
- `HelloUltraviolet\Source\Audit\DiagnosticFixtureExecution.uv:223`
- `HelloUltraviolet\Source\Audit\DiagnosticFixtureExecution.uv:225`
- `HelloUltraviolet\Source\Audit\DiagnosticFixtureExecution.uv:317`
- `HelloUltraviolet\Source\Audit\DiagnosticFixtureExecution.uv:329`
- `HelloUltraviolet\Source\Audit\DiagnosticFixtureExecution.uv:341`
- `HelloUltraviolet\Source\Audit\DiagnosticFixtureExecution.uv:429`
- `HelloUltraviolet\Source\Audit\DiagnosticFixtureExecution.uv:433`
- `HelloUltraviolet\Source\Audit\DiagnosticFixtureExecution.uv:441`
- `HelloUltraviolet\Source\Audit\DiagnosticFixtureExecution.uv:442`
- `HelloUltraviolet\Source\Audit\DiagnosticFixtureExecution.uv:696`

Observed behavior:

`expectedDiagnostic("CODE", "OBLIGATION")` checks whether `CODE` appears
anywhere in the `uvc` diagnostic output and, separately, whether `OBLIGATION`
appears anywhere in the conformance log. The two checks do not prove that the
same diagnostic event carried both fields, nor that the expected pair was the
ordered first failure.

Expected behavior:

The diagnostic-code rules define `Emit(Code(id))` for a diagnostic id, and the
ordering section defines first-failure behavior. A fixture expectation for a
diagnostic id should therefore verify that the relevant emitted diagnostic event
has both the expected public code and the expected obligation identity in the
required order.

Impact:

Crossed diagnostics can pass: one failure may emit the expected public code
while another records the expected obligation. This can mask diagnostic-code
selection defects and first-failure ordering defects in the HelloUltraviolet
testing surface.

HelloUltraviolet fixture gap:

There is no negative harness fixture that intentionally supplies a diagnostic
output and conformance log where the expected code and expected obligation are
present but attached to different diagnostic events or appear out of required
first-failure order.

### UV-AUDIT-0214: Spawned work can be queued without a completion event

Severity: High

Status: Agent-reported and locally corroborated.

Spec anchors:

- `Docs\SPECIFICATION.md:21487`
- `Docs\SPECIFICATION.md:21488`
- `Docs\SPECIFICATION.md:21679`
- `Docs\SPECIFICATION.md:21681`
- `Docs\SPECIFICATION.md:21682`
- `Docs\SPECIFICATION.md:21684`
- `Docs\SPECIFICATION.md:22253`
- `Docs\SPECIFICATION.md:22257`
- `Docs\SPECIFICATION.md:22265`
- `Docs\SPECIFICATION.md:22266`
- `Docs\SPECIFICATION.md:22268`
- `Docs\SPECIFICATION.md:22269`
- `Docs\SPECIFICATION.md:23220`
- `Docs\SPECIFICATION.md:23224`
- `Docs\SPECIFICATION.md:23226`
- `Docs\SPECIFICATION.md:23237`
- `Docs\SPECIFICATION.md:23238`

Implementation anchors:

- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:192`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:1518`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:1520`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:1627`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:1631`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:1633`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:1640`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:1705`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:1712`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:1714`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:2039`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:2040`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:2069`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:2070`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:2108`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:2110`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:2111`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:2112`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:2116`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:2117`

Observed behavior:

`uv_spawn_create` assigns `item->done_event` from `uv_rt_event_open` and then
continues without checking whether the event handle was created. If event
creation fails for pooled work, the item is still enqueued. `uv_spawn_wait`
then sees a pending item with no completion event and can run the same item
inline while it remains in the pool queue. If a worker is already running the
item, the missing event also means `wait` has no synchronization operation and
can return the result pointer before the worker reaches terminal state.

Expected behavior:

The spawn rules expose one pending handle for one submitted work item, and
`wait` on a pending `Spawned<T>` must block until the handle settles. Task
completion must synchronize with the consuming `wait`. A pending spawned handle
therefore needs a single execution path and a usable settlement synchronization
mechanism.

Impact:

Under completion-event allocation failure, a spawn body can execute twice, or
`wait` can observe result storage before the worker has completed it.

HelloUltraviolet fixture gap:

`HelloUltraviolet\Source\Reference\Parallelism\Spawn.uv:3`,
`HelloUltraviolet\Source\Reference\Parallelism\Spawn.uv:18`, and
`HelloUltraviolet\Fixtures\ArtifactProjects\AsyncFailedSpawnedWaitPanic`
exercise normal spawn/wait and panic paths, but no fixture injects completion
event allocation failure or asserts single execution under that condition.

### UV-AUDIT-0215: Worker-pool startup failure can leave pending work permanently unsettled

Severity: High

Status: Agent-reported and locally corroborated.

Spec anchors:

- `Docs\SPECIFICATION.md:21679`
- `Docs\SPECIFICATION.md:21681`
- `Docs\SPECIFICATION.md:21682`
- `Docs\SPECIFICATION.md:21684`
- `Docs\SPECIFICATION.md:22257`
- `Docs\SPECIFICATION.md:22265`
- `Docs\SPECIFICATION.md:22266`
- `Docs\SPECIFICATION.md:22268`
- `Docs\SPECIFICATION.md:22269`
- `Docs\SPECIFICATION.md:23237`
- `Docs\SPECIFICATION.md:23238`

Implementation anchors:

- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:1627`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:1631`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:1633`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:1640`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:1644`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:1735`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:1746`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:1747`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:1750`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:1756`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:1758`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:1894`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:1897`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:1898`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:2108`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:2116`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:2117`
- `Bootstrap\Ultraviolet\runtime\src\internal\rt_platform.h:407`
- `Bootstrap\Ultraviolet\runtime\src\internal\rt_platform.h:414`
- `Bootstrap\Ultraviolet\runtime\src\platform\linux\rt_platform_linux.c:2070`
- `Bootstrap\Ultraviolet\runtime\src\platform\linux\rt_platform_linux.c:2083`
- `Bootstrap\Ultraviolet\runtime\src\platform\linux\rt_platform_linux.c:2093`
- `Bootstrap\Ultraviolet\runtime\src\platform\linux\rt_platform_linux.c:2109`
- `Bootstrap\Ultraviolet\runtime\src\platform\linux\rt_platform_linux.c:2110`
- `Bootstrap\Ultraviolet\runtime\src\platform\linux\rt_platform_linux.c:2116`
- `Bootstrap\Ultraviolet\runtime\src\platform\windows\rt_platform_windows_api.h:545`
- `Bootstrap\Ultraviolet\runtime\src\platform\windows\rt_platform_windows_api.h:553`

Observed behavior:

`uv_start_worker_threads` returns no status. It marks the pool as started before
spawning worker threads, does not check the handle returned by each
`uv_rt_thread_spawn` call, and `uv_enqueue_item` proceeds to queue work and
increment pending counts regardless. If thread allocation or creation fails for
all workers, the pool can contain pending work with no worker able to settle it.

Expected behavior:

`EnqueueWork` must submit the work item to a domain that can make progress, and
`AwaitSpawned`/`wait` must be able to observe terminal settlement. If worker
startup fails, the runtime must fail before exposing a pending handle or use a
defined progress fallback.

Impact:

A `parallel` join or `wait` can block indefinitely after worker allocation or
thread creation failure, with pending counts that no worker can decrement.

HelloUltraviolet fixture gap:

`HelloUltraviolet\Source\Reference\Parallelism\ParallelBlocks.uv:32`,
`HelloUltraviolet\Source\Reference\Parallelism\ParallelBlocks.uv:38`,
`HelloUltraviolet\Source\Reference\Parallelism\Spawn.uv:3`, and
`HelloUltraviolet\Fixtures\ArtifactProjects\StructuredParallelLoweringEvidence\Source\Main.uv:8`
exercise normal inline and CPU execution. No fixture forces worker startup
failure or verifies a progress fallback.

### UV-AUDIT-0216: Spawn capture and result allocation failures still expose normal handles

Severity: High

Status: Agent-reported and locally corroborated.

Spec anchors:

- `Docs\SPECIFICATION.md:22085`
- `Docs\SPECIFICATION.md:22087`
- `Docs\SPECIFICATION.md:22089`
- `Docs\SPECIFICATION.md:22253`
- `Docs\SPECIFICATION.md:22255`
- `Docs\SPECIFICATION.md:22261`
- `Docs\SPECIFICATION.md:22262`
- `Docs\SPECIFICATION.md:22268`
- `Docs\SPECIFICATION.md:22269`
- `Docs\SPECIFICATION.md:23220`
- `Docs\SPECIFICATION.md:23224`
- `Docs\SPECIFICATION.md:23225`
- `Docs\SPECIFICATION.md:23226`
- `Docs\SPECIFICATION.md:23237`
- `Docs\SPECIFICATION.md:23238`

Implementation anchors:

- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:1518`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:1520`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:2020`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:2024`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:2030`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:2031`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:2032`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:2033`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:2034`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:2035`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:2048`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:2049`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:2058`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:2069`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:2070`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:2151`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\wait.cpp:158`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\wait.cpp:160`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\wait.cpp:164`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\wait.cpp:166`

Observed behavior:

`uv_spawn_create` copies the captured environment only if allocation succeeds;
on failure it leaves `item->captured_env` as null and still proceeds. It also
allocates `item->result` independently, leaves it null on allocation failure
while preserving a nonzero `item->result_size`, and still returns a normal
pending or ready handle. `uv_run_item` passes the possibly null captured
environment and result pointer to the body. `uv_spawn_wait` returns the result
pointer, and generated wait code casts and loads from that pointer for non-unit
non-pointer result types.

Expected behavior:

Spawn evaluation must preserve `CapturedEnv` and later retrieve a settled
`Ready(value)` result for `Spawned<T>`. If capture or result storage cannot be
represented, the runtime must not expose that state as a valid normal spawned
handle.

Impact:

Captured values can be lost before body execution, result writes can target a
null pointer, and `wait` lowering can load a non-pointer result from a null
address.

HelloUltraviolet fixture gap:

`HelloUltraviolet\Source\Reference\Parallelism\CaptureSemantics.uv:3`,
`:13`, and `:30` cover successful capture paths, while
`HelloUltraviolet\Source\Reference\Parallelism\Spawn.uv:54` covers successful
result retrieval. No fixture forces capture or result allocation failure.

### UV-AUDIT-0217: `Reactor::register` ignores the tracked result layout it computes

Severity: High

Status: Agent-reported and locally corroborated.

Spec anchors:

- `Docs\SPECIFICATION.md:10839`
- `Docs\SPECIFICATION.md:10842`
- `Docs\SPECIFICATION.md:10843`
- `Docs\SPECIFICATION.md:10846`
- `Docs\SPECIFICATION.md:10856`
- `Docs\SPECIFICATION.md:10858`
- `Docs\SPECIFICATION.md:13655`
- `Docs\SPECIFICATION.md:13663`
- `Docs\SPECIFICATION.md:23220`
- `Docs\SPECIFICATION.md:23234`
- `Docs\SPECIFICATION.md:23240`
- `Docs\SPECIFICATION.md:23241`
- `Docs\SPECIFICATION.md:23261`
- `Docs\SPECIFICATION.md:23263`
- `Docs\SPECIFICATION.md:23265`
- `Docs\SPECIFICATION.md:29133`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\method_call.cpp:1603`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\method_call.cpp:1607`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\method_call.cpp:1609`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\method_call.cpp:1655`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\method_call.cpp:1661`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\method_call.cpp:1663`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\spawn.cpp:154`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\spawn.cpp:164`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\spawn.cpp:165`
- `Bootstrap\Ultraviolet\runtime\include\uv_rt.h:727`
- `Bootstrap\Ultraviolet\runtime\include\uv_rt.h:728`
- `Bootstrap\Ultraviolet\runtime\include\uv_rt.h:729`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:2206`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:2207`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:2208`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:2212`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:2213`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:2218`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:2219`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:2225`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:2227`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:2232`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:2234`

Observed behavior:

`LowerReactorRegisterCall` computes the actual result size for the
`Tracked<T, E>` ready value and stores it in `spawn.result_size`. The runtime
symbol path then emits a call with only the reactor receiver and future pointer.
At the runtime boundary, `reactor::register` calls `uv_spawn_create` with a
hard-coded result size of 8, zeroes 8 bytes, copies 4 bytes of a completed
future payload into the success arm, and copies 1 byte of a failed future
payload into the error arm.

Expected behavior:

`Tracked<T, E>@Ready` carries `value: T | E`, and `wait` returns that full
ready value. `Reactor::register<T, E>` therefore has to allocate and populate
storage matching the actual `T | E` layout computed by lowering, not an
`i32 | bool`-shaped slot.

Impact:

Completed tracked futures with non-`i32 | bool` payloads can truncate or corrupt
ready values, and `wait tracked` can load beyond the allocated result storage.
This is separate from `UV-AUDIT-0137`, which covers pending-state loss.

HelloUltraviolet fixture gap:

`HelloUltraviolet\Fixtures\ArtifactProjects\AsyncObligations\Source\Main.uv:354`,
`HelloUltraviolet\Fixtures\ArtifactProjects\AsyncObligations\Source\Main.uv:361`,
`HelloUltraviolet\Fixtures\ArtifactProjects\RuntimeInterfaceSymbols\Source\Library.uv:464`,
and `HelloUltraviolet\Source\Reference\Polymorphism\CapabilityClassRuntime.uv:170`
cover `Tracked<i32, bool>`. There is no fixture registering completed futures
with wider scalar, aggregate, string, or non-bool error payloads.

### UV-AUDIT-0218: `System::get_env` truncates keys containing embedded zero bytes

Severity: Medium

Status: Agent-reported and locally corroborated.

Spec anchors:

- `Docs\SPECIFICATION.md:1762`
- `Docs\SPECIFICATION.md:1763`
- `Docs\SPECIFICATION.md:1764`
- `Docs\SPECIFICATION.md:1767`
- `Docs\SPECIFICATION.md:2253`
- `Docs\SPECIFICATION.md:2254`
- `Docs\SPECIFICATION.md:2255`
- `Docs\SPECIFICATION.md:2366`
- `Docs\SPECIFICATION.md:3572`
- `Docs\SPECIFICATION.md:3573`
- `Docs\SPECIFICATION.md:3575`
- `Docs\SPECIFICATION.md:3577`
- `Docs\SPECIFICATION.md:3578`
- `Docs\SPECIFICATION.md:3580`
- `Docs\SPECIFICATION.md:3777`
- `Docs\SPECIFICATION.md:3778`
- `Docs\SPECIFICATION.md:3780`
- `Docs\SPECIFICATION.md:13655`
- `Docs\SPECIFICATION.md:13658`

Implementation anchors:

- `Bootstrap\Ultraviolet\runtime\src\system\process.c:51`
- `Bootstrap\Ultraviolet\runtime\src\system\process.c:55`
- `Bootstrap\Ultraviolet\runtime\src\system\process.c:59`
- `Bootstrap\Ultraviolet\runtime\src\system\process.c:64`
- `Bootstrap\Ultraviolet\runtime\src\system\process.c:65`
- `Bootstrap\Ultraviolet\runtime\src\system\process.c:68`
- `Bootstrap\Ultraviolet\runtime\src\system\process.c:90`
- `Bootstrap\Ultraviolet\runtime\src\system\process.c:91`
- `Bootstrap\Ultraviolet\runtime\src\internal\rt_platform.h:174`
- `Bootstrap\Ultraviolet\runtime\src\internal\rt_platform.h:175`
- `Bootstrap\Ultraviolet\runtime\src\internal\rt_platform.h:178`

Observed behavior:

`System::get_env` copies `key->len` bytes into a heap buffer, appends a C-string
terminator, and passes that buffer to the platform environment query. A source
string key containing `\0` is therefore queried only through the prefix before
the first zero byte.

Expected behavior:

Ultraviolet strings are scalar sequences, and `\0` is a valid string escape.
`SystemGetEnv(key)` is specified over the exact source string key. If the host
environment boundary cannot represent embedded-zero keys, the runtime should
reject that key shape or return the specified missing-key value rather than
querying a different key.

Impact:

A program can request an environment variable named by one source string and
receive the value for a different host key sharing the same prefix before the
embedded zero byte.

HelloUltraviolet fixture gap:

`HelloUltraviolet\Source\Reference\Authority\System.uv:35`,
`HelloUltraviolet\Source\Reference\Authority\System.uv:153`, and
`HelloUltraviolet\Source\Reference\Polymorphism\CapabilityClassRuntime.uv:189`
cover ordinary missing and present environment-key paths. No fixture calls
`System::get_env` with an embedded-zero key.

### UV-AUDIT-0219: `uvc build test` is parsed as `uvc test`

Severity: Medium

Status: Agent-reported and locally corroborated.

Spec anchors:

- `Docs\SPECIFICATION.md:642`
- `Docs\SPECIFICATION.md:647`
- `Docs\SPECIFICATION.md:678`
- `Docs\SPECIFICATION.md:7295`
- `Docs\SPECIFICATION.md:7299`
- `Docs\SPECIFICATION.md:7300`
- `Docs\SPECIFICATION.md:7301`
- `Docs\SPECIFICATION.md:7302`
- `Docs\SPECIFICATION.md:7303`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\06_driver\cli.cpp:800`
- `Bootstrap\Ultraviolet\src\06_driver\cli.cpp:801`
- `Bootstrap\Ultraviolet\src\06_driver\cli.cpp:804`
- `Bootstrap\Ultraviolet\src\06_driver\cli.cpp:805`
- `Bootstrap\Ultraviolet\src\06_driver\cli.cpp:806`
- `Bootstrap\Ultraviolet\src\06_driver\cli.cpp:809`
- `Bootstrap\Ultraviolet\src\06_driver\cli.cpp:814`
- `Bootstrap\Ultraviolet\src\06_driver\cli.cpp:829`
- `Bootstrap\Ultraviolet\src\06_driver\cli.cpp:832`
- `Bootstrap\Ultraviolet\src\06_driver\cli.cpp:837`
- `Bootstrap\Ultraviolet\src\06_driver\cli.cpp:840`
- `Bootstrap\Ultraviolet\src\06_driver\cli.cpp:844`
- `Bootstrap\Ultraviolet\src\06_driver\compiler_main.cpp:3036`

Observed behavior:

After the parser sees `build`, it sets `command_selected` but continues to
interpret later command-shaped tokens as commands. `uvc build test` therefore
sets `do_test` and enters the test subcommand path instead of building the
input path named `test`. The same pattern applies to later `init` and `clean`
tokens after a command has already been selected.

Expected behavior:

The driver should select exactly one command mode. After `build` has selected
the build command, later positional tokens should be interpreted as that
command's input path or rejected with a command-line diagnostic, not reclassified
as another top-level command.

Impact:

A valid project path or file path whose spelling matches a command word can
silently invoke a different command surface. That makes command output and
project-root behavior depend on path spelling rather than on argument position.

HelloUltraviolet fixture gap:

`HelloUltraviolet\Fixtures\OutputDiagnostics\CommandLine` has fixtures for an
unknown command and an unknown test target. It has no fixture for repeated
command tokens or for a build input path named `test`, `init`, or `clean`.

### UV-AUDIT-0220: Duplicate assembly names record the wrong first-failure label

Severity: Low

Status: Agent-reported and locally corroborated.

Spec anchors:

- `Docs\SPECIFICATION.md:784`
- `Docs\SPECIFICATION.md:788`
- `Docs\SPECIFICATION.md:789`
- `Docs\SPECIFICATION.md:791`
- `Docs\SPECIFICATION.md:1014`
- `Docs\SPECIFICATION.md:1020`
- `Docs\SPECIFICATION.md:1021`
- `Docs\SPECIFICATION.md:1030`
- `Docs\SPECIFICATION.md:1032`
- `Docs\SPECIFICATION.md:1736`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\01_project\project_validate.cpp:384`
- `Bootstrap\Ultraviolet\src\01_project\project_validate.cpp:390`
- `Bootstrap\Ultraviolet\src\01_project\project_validate.cpp:394`
- `Bootstrap\Ultraviolet\src\01_project\project_validate.cpp:395`
- `Bootstrap\Ultraviolet\src\01_project\project_validate.cpp:638`
- `Bootstrap\Ultraviolet\src\01_project\project_validate.cpp:639`
- `Bootstrap\Ultraviolet\src\01_project\project_validate.cpp:643`
- `HelloUltraviolet\Source\Reference\Projects\ManifestModel.uv:221`
- `HelloUltraviolet\Source\Reference\Projects\ManifestModel.uv:222`
- `HelloUltraviolet\Source\Reference\Projects\ManifestModel.uv:450`

Observed behavior:

Duplicate assembly names emit `E-PRJ-0202`, but the manifest validator records
`def.FirstFail` with `first=WF-Assembly-Name-Dup-Err`. The passing rule for the
same check records `WF-Assembly-Name-Dup`, and the HelloUltraviolet manifest
reference model expects duplicate names to report `WF-Assembly-Name-Dup` as the
first failed manifest check.

Expected behavior:

The first-failure conformance payload should use the diagnostic owner expected
by the specification's duplicate-name diagnostic mapping and the project
reference model: `WF-Assembly-Name-Dup`.

Impact:

The diagnostic code can be correct while conformance logs disagree with the
reference model. A trace consumer that validates first-failure identity can mark
a conforming duplicate-name rejection as mismatched, or accept an implementation
that records the wrong manifest check identity.

HelloUltraviolet fixture gap:

The manifest reference model covers duplicate assembly-name first failure, but
there is no output-diagnostic fixture that runs `uvc` on a duplicate assembly
manifest and asserts both `E-PRJ-0202` and the exact `def.FirstFail` payload.

### UV-AUDIT-0221: Project-root discovery traces only the directory-input branch

Severity: Low

Status: Agent-reported and locally corroborated.

Spec anchors:

- `Docs\SPECIFICATION.md:677`
- `Docs\SPECIFICATION.md:678`
- `Docs\SPECIFICATION.md:7299`
- `Docs\SPECIFICATION.md:7300`
- `Docs\SPECIFICATION.md:7301`
- `Docs\SPECIFICATION.md:7302`
- `Docs\SPECIFICATION.md:7303`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\01_project\manifest.cpp:113`
- `Bootstrap\Ultraviolet\src\01_project\manifest.cpp:119`
- `Bootstrap\Ultraviolet\src\01_project\manifest.cpp:125`
- `Bootstrap\Ultraviolet\src\01_project\manifest.cpp:128`
- `Bootstrap\Ultraviolet\src\01_project\manifest.cpp:131`
- `Bootstrap\Ultraviolet\src\01_project\manifest.cpp:132`
- `Bootstrap\Ultraviolet\src\01_project\manifest.cpp:133`
- `Bootstrap\Ultraviolet\src\01_project\manifest.cpp:145`
- `Bootstrap\Ultraviolet\src\01_project\manifest.cpp:146`
- `Bootstrap\Ultraviolet\src\01_project\manifest.cpp:177`
- `Bootstrap\Ultraviolet\src\01_project\manifest.cpp:198`
- `Bootstrap\Ultraviolet\src\06_driver\compiler_main.cpp:3063`
- `Bootstrap\Ultraviolet\src\06_driver\compiler_main.cpp:3360`
- `Bootstrap\Ultraviolet\src\00_core\generated\static_rule_registry.inc:3673`
- `HelloUltraviolet\Source\Audit\OutputDiagnosticExecution.uv:868`

Observed behavior:

`StartDirForInput` implements the directory, file, and fallback start-directory
branches, but it emits `req.ProjectRootDirectoryInputStartsAtResolvedPath` only
when the input exists and is a directory. The source-file branch that searches
from the parent directory, the relative-input resolution step, and the
missing-path fallback branch have no corresponding runtime conformance trace in
the project-root implementation.

Expected behavior:

Conformance output should credit the resolved-input project-root semantics for
each branch the implementation executes, or the obligations should be split so
that directory, file, and missing-path inputs can be validated independently.

Impact:

Regressions in source-file parent selection or missing-path root selection can
pass conformance auditing when only directory-input evidence is present.

HelloUltraviolet fixture gap:

`HelloUltraviolet\Source\Audit\OutputDiagnosticExecution.uv:868` asserts the
directory-input project-root trace. The fixture surface does not run `uvc`
against a source-file command-line input to assert the parent-directory branch,
or against a missing path to assert the fallback start directory.

### UV-AUDIT-0222: `wait` can suspend after operand-created implicit key acquisition

Severity: High

Status: Agent-reported and locally corroborated.

Spec anchors:

- `Docs\SPECIFICATION.md:23286`
- `Docs\SPECIFICATION.md:23409`
- `Docs\SPECIFICATION.md:23415`
- `Docs\SPECIFICATION.md:23423`
- `Docs\SPECIFICATION.md:23430`
- `Docs\SPECIFICATION.md:24620`
- `Docs\SPECIFICATION.md:24670`
- `Docs\SPECIFICATION.md:24672`
- `Docs\SPECIFICATION.md:24674`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\wait_expr.cpp:66`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\wait_expr.cpp:67`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\wait_expr.cpp:68`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\wait_expr.cpp:97`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\wait_expr.cpp:98`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\wait_expr.cpp:112`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\expr_common.cpp:965`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\expr_common.cpp:1067`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\expr_common.cpp:1072`
- `HelloUltraviolet\Fixtures\RejectedSource\Keys\WaitWhileKeyHeldLowering\Source\Main.uv:6`

Observed behavior:

`LowerWaitExpr` checks `HasActiveHeldKeys(ctx)` before it lowers the wait
handle. That helper only reports non-implicit key scopes with acquired paths.
After the guard passes, `LowerExpr(*expr.handle, ctx)` may enter an implicit
key scope and append an acquired path, and the lowering then emits `IRWait`
with that newly held key still active.

Expected behavior:

`Lower-Wait-Key-Illegal` is defined at the program point where the wait occurs.
If operand lowering creates or exposes held keys before the suspension point,
the implementation must reject the wait or release/reacquire the held keys
according to the async key rules before emitting `WaitIR`.

Impact:

The backend can suspend while holding an implicit runtime key that was acquired
by the wait operand itself. This violates the async/key boundary and can keep a
protected resource held across a suspension point the specification forbids.

HelloUltraviolet fixture gap:

`HelloUltraviolet\Fixtures\RejectedSource\Keys\WaitWhileKeyHeldLowering\Source\Main.uv:6`
covers an explicit key block around `wait`. It does not cover a wait operand
that creates an implicit key acquisition during operand lowering.

### UV-AUDIT-0223: Method contract predicates are not type-checked as `bool`

Severity: High

Status: Agent-reported and locally corroborated.

Spec anchors:

- `Docs\SPECIFICATION.md:14418`
- `Docs\SPECIFICATION.md:14419`
- `Docs\SPECIFICATION.md:14648`
- `Docs\SPECIFICATION.md:14649`
- `Docs\SPECIFICATION.md:14764`
- `Docs\SPECIFICATION.md:15386`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\procedure_decl.cpp:2223`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\procedure_decl.cpp:2245`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\procedure_decl.cpp:2322`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\record_decl.cpp:1018`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\record_decl.cpp:1029`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\modal_decl.cpp:686`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\modal_decl.cpp:698`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\class_decl.cpp:418`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\class_decl.cpp:419`
- `Bootstrap\Ultraviolet\src\04_analysis\contracts\contract_check.cpp:2012`
- `Bootstrap\Ultraviolet\src\04_analysis\contracts\contract_check.cpp:2047`
- `Bootstrap\Ultraviolet\src\04_analysis\contracts\contract_check.cpp:2083`
- `HelloUltraviolet\Fixtures\RejectedSource\Procedures\ContractPredicateNonBool\Source\Main.uv:4`

Observed behavior:

Free procedures perform an additional type check that rejects a non-`bool`
contract predicate with `E-SEM-2808`. Record and modal methods build a
`ContractContext` and call `CheckContractWellFormed`, but that helper only
checks placement and purity. Class methods with default bodies retain
`type_ctx.contract` for body typing, but their declaration path does not run an
equivalent predicate type check for the method contract.

Expected behavior:

`WF-Contract` requires precondition and postcondition predicates to type as
`bool`. The same rule applies to method contracts as ordinary contract clauses,
so every method-contract predicate should be rejected with the contract
predicate diagnostic when it is not `bool`.

Impact:

Method declarations can accept integer, aggregate, or otherwise non-boolean
contract predicates that free procedures reject. Later contract lowering or
runtime checking then receives a predicate that was never admitted by the
specification's static contract rule.

HelloUltraviolet fixture gap:

`HelloUltraviolet\Fixtures\RejectedSource\Procedures\ContractPredicateNonBool\Source\Main.uv:4`
covers a free procedure. There is no matching rejected-source fixture for
record methods, modal state methods, or class default methods with non-`bool`
contract predicates.

### UV-AUDIT-0224: Type invariants accept non-boolean predicates

Severity: High

Status: Agent-reported and locally corroborated.

Spec anchors:

- `Docs\SPECIFICATION.md:14991`
- `Docs\SPECIFICATION.md:15007`
- `Docs\SPECIFICATION.md:15034`
- `Docs\SPECIFICATION.md:15050`
- `Docs\SPECIFICATION.md:15058`
- `Docs\SPECIFICATION.md:30492`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\record_decl.cpp:694`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\record_decl.cpp:715`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\enum_decl.cpp:382`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\enum_decl.cpp:389`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\modal_decl.cpp:896`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\modal_decl.cpp:903`
- `Bootstrap\Ultraviolet\src\04_analysis\contracts\contract_check.cpp:2131`
- `Bootstrap\Ultraviolet\src\04_analysis\contracts\contract_check.cpp:2140`
- `Bootstrap\Ultraviolet\src\04_analysis\contracts\contract_check.cpp:2149`
- `HelloUltraviolet\Fixtures\RejectedSource\Procedures\InvariantPublicMutableField\Source\Main.uv:4`
- `HelloUltraviolet\Fixtures\RejectedSource\Procedures\InvariantDiagnostics\Source\Main.uv:4`

Observed behavior:

Record, enum, and modal declarations call `CheckTypeInvariant` when an
invariant is present. The checker rejects `@result` and impure predicates, but
it does not type the invariant predicate and does not require it to be `bool`.

Expected behavior:

The invariant syntax is a `predicate_expr`, and invariant diagnostics are
defined for ill-formed predicates. Type invariants must therefore be checked
under the invariant context and rejected when their predicate does not have
type `bool`.

Impact:

A type can be admitted with an invariant such as an integer literal. Static
verification, dynamic invariant insertion, and downstream assumptions can then
treat a non-predicate expression as an invariant obligation.

HelloUltraviolet fixture gap:

`InvariantPublicMutableField` and `InvariantDiagnostics` cover public mutable
field rejection and existing invariant diagnostics, but there is no fixture for
a record, enum, or modal invariant whose predicate is not `bool`.

### UV-AUDIT-0225: Loop invariant non-boolean predicates use a foreign-contract diagnostic

Severity: Medium

Status: Agent-reported and locally corroborated.

Spec anchors:

- `Docs\SPECIFICATION.md:15007`
- `Docs\SPECIFICATION.md:15058`
- `Docs\SPECIFICATION.md:16953`
- `Docs\SPECIFICATION.md:16956`
- `Docs\SPECIFICATION.md:16961`
- `Docs\SPECIFICATION.md:16966`
- `Docs\SPECIFICATION.md:26845`
- `Docs\SPECIFICATION.md:30492`
- `Docs\SPECIFICATION.md:30522`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\loop_infinite.cpp:114`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\loop_infinite.cpp:119`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\loop_conditional.cpp:117`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\loop_conditional.cpp:124`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\loop_iter.cpp:215`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\loop_iter.cpp:220`
- `HelloUltraviolet\Fixtures\RejectedSource\Expressions\ControlExpressionDiagnosticOwnership\Expected.uv:3`
- `HelloUltraviolet\Fixtures\RejectedSource\FFI\ForeignPredicateContext\Expected.uv:3`

Observed behavior:

The infinite, conditional, and iterator loop expression checkers type the loop
invariant predicate and then return `E-SEM-2851` if it is not `bool`.
`E-SEM-2851` is registered as "Invalid predicate in foreign contract" in the
foreign-contract diagnostic table.

Expected behavior:

Loop invariants use `LoopInvOk` and their diagnostics are owned by the
procedure, contract, entry, and loop-invariant diagnostic surface, not by the
foreign-contract predicate surface. A non-`bool` loop invariant should report a
loop-invariant or contract-predicate diagnostic with the correct ownership.

Impact:

The compiler rejects the program, but it attributes an ordinary loop invariant
failure to the FFI contract subsystem. Diagnostic conformance can pass the wrong
owner, and users receive a misleading source category.

HelloUltraviolet fixture gap:

`ControlExpressionDiagnosticOwnership` covers an unprovable boolean loop
invariant, and `ForeignPredicateContext` covers `E-SEM-2851` for FFI. There is
no rejected-source fixture for a non-`bool` ordinary loop invariant.

### UV-AUDIT-0226: Generic nominal declaration bodies are checked outside `BindTypeParams`

Severity: High

Status: Agent-reported and locally corroborated.

Spec anchors:

- `Docs\SPECIFICATION.md:9827`
- `Docs\SPECIFICATION.md:10125`
- `Docs\SPECIFICATION.md:10538`
- `Docs\SPECIFICATION.md:10713`
- `Docs\SPECIFICATION.md:11124`
- `Docs\SPECIFICATION.md:12450`
- `Docs\SPECIFICATION.md:12471`
- `Docs\SPECIFICATION.md:12518`
- `Docs\SPECIFICATION.md:12900`
- `Docs\SPECIFICATION.md:14419`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\typing\typecheck.cpp:938`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\typecheck.cpp:948`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\typecheck.cpp:958`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\typecheck.cpp:966`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\typecheck.cpp:974`
- `Bootstrap\Ultraviolet\src\04_analysis\generics\generic_params.cpp:542`
- `Bootstrap\Ultraviolet\src\04_analysis\generics\generic_params.cpp:584`
- `Bootstrap\Ultraviolet\src\04_analysis\generics\generic_params.cpp:599`
- `Bootstrap\Ultraviolet\src\04_analysis\generics\generic_params.cpp:618`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\procedure_decl.cpp:2069`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\procedure_decl.cpp:2511`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\record_decl.cpp:494`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\record_decl.cpp:597`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\enum_decl.cpp:224`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\enum_decl.cpp:317`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\modal_decl.cpp:522`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\modal_decl.cpp:626`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\class_decl.cpp:280`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\class_decl.cpp:476`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\class_decl.cpp:524`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\type_alias_decl.cpp:222`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\type_alias_decl.cpp:248`
- `HelloUltraviolet\Source\Reference\Polymorphism\GenericParameters.uv:5`
- `HelloUltraviolet\Source\Reference\Comptime\Reflection.uv:4`

Observed behavior:

The declaration dispatcher invokes record, enum, modal, class, and type-alias
type checkers with the original `ctx`. Those checkers call
`ProcessGenericParams`, but their declaration-owned type positions are still
lowered with `LowerTypeWithWF(ctx, ...)`, and method signatures are built with
the same unbound context. Free procedures use `BindTypeParams` before checking
their signatures and bodies; the nominal declaration paths do not apply the
same declaration-local generic scope.

Expected behavior:

The record, enum, type-alias, modal, class-method, and record-method rules all
introduce a generic context with `BindTypeParams` before checking owned fields,
payloads, associated-type defaults, method parameters, returns, predicates, and
aliases. Type names such as `TValue` should resolve as declaration-local type
parameters across those positions.

Impact:

Generic nominal declarations can reject valid declarations whose fields,
payloads, method signatures, associated defaults, or aliases refer to their own
type parameters. Conversely, any path that happens to resolve a same-named
outer type can be checked against the wrong type identity.

HelloUltraviolet fixture gap:

`GenericParameters.uv` covers generic alias and record surfaces, and
`Reflection.uv` covers non-generic reflection surfaces. The current fixtures do
not exercise the full `BindTypeParams` matrix across enum payloads, modal state
fields, class associated defaults, record/modal/class method signatures, and
negative same-name shadowing.

### UV-AUDIT-0227: Static postcondition proof is skipped for tail-expression returns

Severity: High

Status: Agent-reported and locally corroborated.

Spec anchors:

- `Docs\SPECIFICATION.md:14882`
- `Docs\SPECIFICATION.md:14885`
- `Docs\SPECIFICATION.md:14887`
- `Docs\SPECIFICATION.md:14953`
- `Docs\SPECIFICATION.md:15097`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\return_stmt.cpp:995`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\return_stmt.cpp:1205`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\return_stmt.cpp:1207`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\return_stmt.cpp:1266`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\return_stmt.cpp:1278`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\return_stmt.cpp:1331`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\return_stmt.cpp:1375`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\return_stmt.cpp:1390`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\stmt_common.cpp:3659`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\stmt_common.cpp:3757`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\procedure_decl.cpp:2348`
- `HelloUltraviolet\Source\Reference\Procedures\Postconditions.uv:27`
- `HelloUltraviolet\Source\Reference\Procedures\Postconditions.uv:33`

Observed behavior:

Explicit `return` statements call `VerifyPostconditionAtReturn` and can emit
`E-SEM-2801` when the static postcondition proof fails. A procedure body that
returns through a block tail expression goes through `TypeBlock`, which checks
the tail expression against the expected return type and returns success under
`Chk-Block-Tail`. The procedure declaration path does not run a follow-up
static postcondition proof for that implicit return point.

Expected behavior:

Postconditions are verified at every return point with `@result` bound to the
returned value. Tail-expression procedure exits and unit fallthrough exits are
return points, so they must feed the same static postcondition proof path used
by explicit `return`.

Impact:

A contracted procedure can satisfy all type checks while its tail expression
does not prove the stated postcondition. The static contract mode then accepts
an implementation that should fail with `Contract-Static-Fail`.

HelloUltraviolet fixture gap:

`Postconditions.uv` exercises explicit returns. There is no contracted
tail-expression or unit-fallthrough fixture that requires static postcondition
proof at an implicit return point.

### UV-AUDIT-0228: Nested `@entry` can reach lowering before the inner capture exists

Severity: Medium

Status: Agent-reported and locally corroborated.

Spec anchors:

- `Docs\SPECIFICATION.md:14778`
- `Docs\SPECIFICATION.md:14780`
- `Docs\SPECIFICATION.md:14939`
- `Docs\SPECIFICATION.md:14946`
- `Docs\SPECIFICATION.md:14949`
- `Docs\SPECIFICATION.md:14953`
- `Docs\SPECIFICATION.md:14955`
- `Docs\SPECIFICATION.md:14965`
- `Docs\SPECIFICATION.md:14971`
- `Docs\SPECIFICATION.md:14980`
- `Docs\SPECIFICATION.md:14984`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\contract_entry.cpp:394`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\contract_entry.cpp:468`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\contract_entry.cpp:485`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\contract_entry.cpp:518`
- `Bootstrap\Ultraviolet\src\04_analysis\contracts\contract_intrinsics.cpp:610`
- `Bootstrap\Ultraviolet\src\04_analysis\contracts\contract_intrinsics.cpp:819`
- `Bootstrap\Ultraviolet\src\04_analysis\contracts\contract_intrinsics.cpp:838`
- `Bootstrap\Ultraviolet\src\04_analysis\contracts\contract_intrinsics.cpp:845`
- `Bootstrap\Ultraviolet\src\04_analysis\contracts\contract_intrinsics.cpp:853`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\lower_proc.cpp:431`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\lower_proc.cpp:636`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\lower_proc.cpp:670`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\contract_entry.cpp:34`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\contract_entry.cpp:38`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\contract_entry.cpp:43`
- `HelloUltraviolet\Fixtures\RejectedSource\Procedures\ContractEntryOutsidePostcondition\Source\Main.uv:4`
- `HelloUltraviolet\Fixtures\RejectedSource\Procedures\ContractEntryNonBitcopy\Source\Main.uv:4`
- `HelloUltraviolet\Fixtures\RejectedSource\Procedures\ContractEntrySideEffect\Source\Main.uv:4`

Observed behavior:

`ExprUsesOnlyEntryEnvBindings` does not special-case `EntryExpr`, so an inner
`@entry(...)` is treated like an acceptable leaf while the outer `@entry` is
typed. `ValidateEntryIntrinsic` collects entry expressions but its checks for
capabilities, side effects, and determinism also do not reject a nested
`EntryExpr` as such. The lowering entry collector visits the outer entry before
the inner entry and lowers the outer capture expression; when that expression
contains the inner `@entry`, `LowerEntryExpr` cannot find a captured value for
the inner node and reports a codegen failure.

Expected behavior:

Nested `@entry` must either be rejected during contract intrinsic validation or
captured in an order that guarantees every `@entry` node has a pre-body capture
before postcondition lowering can reference it. The runtime representation rule
requires captured entry values to be preserved only as inputs to postcondition
checks, not discovered as missing values during expression lowering.

Impact:

A malformed contract can pass semantic validation and fail in the backend, or
produce fallback entry values after reporting an internal lowering failure,
rather than receiving a source-level contract diagnostic.

HelloUltraviolet fixture gap:

Existing `ContractEntry*` fixtures cover outside-postcondition use, non-Bitcopy
entry values, side effects, capability use, and moved parameters. There is no
fixture for `@entry(@entry(value))` or another nested-entry expression.

### UV-AUDIT-0229: Contract intrinsics are missing from statement expression-start gates

Severity: Medium

Status: Agent-reported and locally corroborated.

Spec anchors:

- `Docs\SPECIFICATION.md:14851`
- `Docs\SPECIFICATION.md:14853`
- `Docs\SPECIFICATION.md:14854`
- `Docs\SPECIFICATION.md:30675`
- `Docs\SPECIFICATION.md:30859`
- `Docs\SPECIFICATION.md:30861`
- `Docs\SPECIFICATION.md:30862`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\02_source\parser\expr\primary.cpp:400`
- `Bootstrap\Ultraviolet\src\02_source\parser\expr\primary.cpp:405`
- `Bootstrap\Ultraviolet\src\02_source\parser\expr\expr_common.cpp:455`
- `Bootstrap\Ultraviolet\src\02_source\parser\expr\expr_common.cpp:463`
- `Bootstrap\Ultraviolet\src\02_source\parser\stmt\expr_stmt.cpp:206`
- `Bootstrap\Ultraviolet\src\02_source\parser\stmt\expr_stmt.cpp:214`
- `Bootstrap\Ultraviolet\src\02_source\parser\stmt\expr_stmt.cpp:284`
- `Bootstrap\Ultraviolet\src\02_source\parser\stmt\parse_stmt.cpp:309`
- `Bootstrap\Ultraviolet\src\02_source\parser\stmt\stmt_common.cpp:579`
- `HelloUltraviolet\Fixtures\RejectedSource\Procedures\PostconditionsDiagnostic\Source\Main.uv:4`
- `HelloUltraviolet\Fixtures\RejectedSource\Procedures\ContractEntryOutsidePostcondition\Source\Main.uv:4`

Observed behavior:

The expression parser accepts contract intrinsics as primary expressions when
parsing has already entered expression mode: `primary.cpp` recognizes `@result`
and `@entry(...)`, and the expression-level start predicate includes the `@`
operator. The statement-level expression-start helper omits `@`, and
`parse_stmt.cpp` and block-tail parsing use that statement helper before trying
an expression. A standalone `@result` or `@entry(...)` statement therefore
reports a generic source error before the semantic contract-intrinsic diagnostic
can run.

Expected behavior:

The grammar lists `contract_intrinsic` as a `primary_expr` and says contract
intrinsics parse in any primary-expression position. Statement expressions and
block tail expressions that begin with `@result` or `@entry(...)` should parse
as expressions and then be rejected semantically when the intrinsic is used
outside its permitted contract context.

Impact:

Invalid contract-intrinsic expressions receive syntax diagnostics instead of
the specified contract diagnostics. The parser and typer also disagree about
which tokens can begin an expression.

HelloUltraviolet fixture gap:

`PostconditionsDiagnostic` and `ContractEntryOutsidePostcondition` exercise
contract-intrinsic semantic diagnostics in contexts that already parse as
expressions. There is no fixture for a standalone statement expression or block
tail expression beginning with `@result` or `@entry(...)`.

### UV-AUDIT-0230: Wall-time resolution failure is reported as a one-nanosecond success

Severity: High

Status: Locally corroborated.

Spec anchors:

- `Docs\SPECIFICATION.md:3335`
- `Docs\SPECIFICATION.md:3594`
- `Docs\SPECIFICATION.md:3613`
- `Docs\SPECIFICATION.md:3848`
- `Docs\SPECIFICATION.md:13616`
- `Docs\SPECIFICATION.md:29165`
- `Docs\SPECIFICATION.md:29267`
- `Docs\SPECIFICATION.md:29409`

Implementation anchors:

- `Bootstrap\Ultraviolet\runtime\src\time\time.c:189`
- `Bootstrap\Ultraviolet\runtime\src\time\time.c:193`
- `Bootstrap\Ultraviolet\runtime\src\time\time.c:203`
- `Bootstrap\Ultraviolet\runtime\src\time\time.c:287`
- `Bootstrap\Ultraviolet\runtime\src\time\time.c:290`
- `Bootstrap\Ultraviolet\runtime\src\time\time.c:396`
- `Bootstrap\Ultraviolet\runtime\include\uv_rt.h:671`
- `Bootstrap\Ultraviolet\runtime\include\uv_rt.h:672`
- `Bootstrap\Ultraviolet\src\04_analysis\caps\cap_time.cpp:187`
- `Bootstrap\Ultraviolet\src\04_analysis\caps\cap_time.cpp:192`
- `HelloUltraviolet\Source\Reference\Authority\Time.uv:152`
- `HelloUltraviolet\Source\Tests\SourceNativeTests.uv:143`

Observed behavior:

The wall-clock resolution helper can fail when the host cannot report a wall
resolution. `Time::wall` initializes `resolution` to one nanosecond, calls
`uv_platform_wall_resolution_ns(&resolution)`, discards the failure result, and
stores the default value in the wall-time state. `WallTime::resolution` later
checks only that the receiver is a wall-time state, then returns
`Outcome<Duration, TimeError>@Value` with that stored resolution.

Expected behavior:

`WallTimeResolution(v_wall)` must return an advertised wall-clock resolution
with `n > 0`, or return `TimeErr(Duration, ClockUnavailable)` when the host
cannot report the wall-clock resolution. A synthetic one-nanosecond value is
not the host-advertised resolution for the failure path.

Impact:

Hosts that cannot report wall-clock resolution are exposed to Ultraviolet code
as if they had a valid one-nanosecond wall-clock resolution. That hides the
specified `ClockUnavailable` error branch and can make coarsening, timing
policy, and capability-reference tests depend on a fabricated precision.

HelloUltraviolet fixture gap:

`Authority\Time.uv` and `SourceNativeTests.uv` cover the ordinary
`WallTime::resolution` success path. Existing runtime-interface fixtures check
that the symbol is present and called, but none inject a platform resolution
failure or assert the `ClockUnavailable` branch for `WallTime::resolution`.

### UV-AUDIT-0231: Configured CPU domains can degrade into fabricated defaults

Severity: High

Status: Locally corroborated.

Spec anchors:

- `Docs\SPECIFICATION.md:3254`
- `Docs\SPECIFICATION.md:21625`
- `Docs\SPECIFICATION.md:21631`
- `Docs\SPECIFICATION.md:21637`
- `Docs\SPECIFICATION.md:21809`
- `Docs\SPECIFICATION.md:21811`
- `Docs\SPECIFICATION.md:21818`
- `Docs\SPECIFICATION.md:21820`
- `Docs\SPECIFICATION.md:21822`
- `Docs\SPECIFICATION.md:21824`
- `Docs\SPECIFICATION.md:21935`

Implementation anchors:

- `Bootstrap\Ultraviolet\runtime\include\uv_rt.h:713`
- `Bootstrap\Ultraviolet\runtime\src\context\context.c:109`
- `Bootstrap\Ultraviolet\runtime\src\context\context.c:121`
- `Bootstrap\Ultraviolet\runtime\src\context\context.c:124`
- `Bootstrap\Ultraviolet\runtime\src\context\context.c:126`
- `Bootstrap\Ultraviolet\runtime\src\context\context.c:127`
- `Bootstrap\Ultraviolet\runtime\src\context\context.c:130`
- `Bootstrap\Ultraviolet\runtime\src\context\context.c:135`
- `Bootstrap\Ultraviolet\runtime\src\context\context.c:139`
- `Bootstrap\Ultraviolet\runtime\src\context\context.c:160`
- `Bootstrap\Ultraviolet\runtime\src\context\context.c:164`
- `Bootstrap\Ultraviolet\runtime\src\context\context.c:187`
- `Bootstrap\Ultraviolet\runtime\src\context\context.c:191`
- `Bootstrap\Ultraviolet\runtime\src\context\context.c:192`
- `Bootstrap\Ultraviolet\src\04_analysis\caps\cap_system.cpp:226`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\method_call.cpp:2000`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\method_call.cpp:2025`
- `Bootstrap\Ultraviolet\src\05_codegen\intrinsics\intrinsics_interface.cpp:1322`
- `Bootstrap\Ultraviolet\src\05_codegen\intrinsics\intrinsics_interface.cpp:1327`

Observed behavior:

`Context::cpu(mask)` and `Context::cpu(mask, priority)` lower to the
`context::cpu_configured` runtime symbol. That runtime allocates a fresh
`UVExecutionDomain`, initializes the return dynamic object to null, and returns
the null object unchanged when allocation fails. The `ExecutionDomain::name`
runtime method then treats a null `self.data` as `"cpu"`, while
`ExecutionDomain::max_concurrency` treats the same null receiver as `1`.

Expected behavior:

The configured CPU constructors are specified as successful domain constructors:
`ctx.cpu(mask)` selects a CPU domain restricted to the given `CpuSet`, and
`ctx.cpu(mask, priority)` selects a CPU domain restricted to the mask and
default task priority. A `parallel` domain expression must evaluate to a value
implementing `ExecutionDomain`, and the `ExecutionDomain` methods report
properties of that domain value. Allocation failure cannot be represented as a
successful null dynamic object whose methods fabricate default values.

Impact:

When configured-domain allocation fails, source code receives a value typed as
`$ExecutionDomain` that has no domain state, no recorded affinity mask, and no
recorded priority. Parallel-domain selection and direct calls to
`name()`/`max_concurrency()` can then proceed as if a valid default CPU domain
had been produced, erasing the requested restriction and hiding the failure.

HelloUltraviolet fixture gap:

`HelloUltraviolet` covers successful execution-domain construction and
observation in `HelloUltraviolet\Source\Reference\Parallelism\ExecutionDomains.uv:183`,
`HelloUltraviolet\Source\Reference\Parallelism\ExecutionDomains.uv:184`,
`HelloUltraviolet\Source\Reference\Parallelism\ExecutionDomains.uv:185`,
`HelloUltraviolet\Source\Reference\Polymorphism\CapabilityClassRuntime.uv:219`,
`HelloUltraviolet\Source\Reference\Polymorphism\CapabilityClassRuntime.uv:220`,
`HelloUltraviolet\Source\Reference\Polymorphism\CapabilityClassRuntime.uv:221`,
`HelloUltraviolet\Source\Reference\Polymorphism\CapabilityClassRuntime.uv:222`,
and `HelloUltraviolet\Source\Reference\Polymorphism\CapabilityClassRuntime.uv:223`.
Those fixtures do not force configured-domain allocation failure, verify that a
configured CPU domain retains the requested mask and priority, or assert that
execution-domain methods reject null dynamic receiver data.

### UV-AUDIT-0232: No-tail blocks require exact unit instead of unit subtyping

Severity: Medium

Status: Agent-reported and locally corroborated.

Spec anchors:

- `Docs\SPECIFICATION.md:6218`
- `Docs\SPECIFICATION.md:6223`
- `Docs\SPECIFICATION.md:10271`
- `Docs\SPECIFICATION.md:19102`
- `Docs\SPECIFICATION.md:19126`
- `Docs\SPECIFICATION.md:19132`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\stmt_common.cpp:3686`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\stmt_common.cpp:3718`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\stmt_common.cpp:3754`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\stmt_common.cpp:3782`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\stmt_common.cpp:3783`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\stmt_common.cpp:3788`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\block_expr.cpp:48`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\unsafe_block_expr.cpp:84`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\subtyping.cpp:781`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\subtyping.cpp:1325`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\subtyping.cpp:1337`

Observed behavior:

`CheckBlock` accepts the no-tail/no-result checking-mode block case only when
the expected type is exactly `TypePrim("()")`. The same routine already uses
`Subtyping(ctx, result_type, expected)` for block result-flow paths, and tail
expressions delegate to expression checking against the expected type. Ordinary
and unsafe block expressions both route checking mode through this `CheckBlock`
path. The subtyping implementation itself supports member and width subtyping
for unions, so a unit value can satisfy an expected union containing unit; the
no-tail block path simply never asks that question.

Expected behavior:

`Chk-Block-Unit` accepts a block with no result type, no tail expression, and no
return tail when `TypePrim("()") <: T`. It is not restricted to the single case
where `T` is syntactically the unit type. Union introduction is semantic, and
the subtyping rules define membership and width subtyping for union targets.

Impact:

Valid checking-mode block expressions can be rejected when the expected type
admits unit through subtyping. A block such as a no-tail branch checked against
`() | i32` can fail with "block has no tail expression or explicit return for
expected ..." even though `Chk-Block-Unit` permits the unit result under the
expected type.

HelloUltraviolet fixture gap:

`HelloUltraviolet\Source\Reference\Statements\Blocks.uv:140` and
`HelloUltraviolet\Source\Audit\Catalog\StatementsAndBlocks\Blocks.uv:263`
cover ordinary `BlockInfo-Unit`. Existing fixtures do not check a no-tail block
expression against a non-unit expected type that admits unit, such as a union
containing `()`.

### UV-AUDIT-0233: Ordinary block statement sequences can loop at EOF

Severity: High

Status: Agent-reported and locally corroborated.

Spec anchors:

- `Docs\SPECIFICATION.md:19020`
- `Docs\SPECIFICATION.md:19021`
- `Docs\SPECIFICATION.md:19032`
- `Docs\SPECIFICATION.md:19035`
- `Docs\SPECIFICATION.md:19037`
- `Docs\SPECIFICATION.md:19040`
- `Docs\SPECIFICATION.md:19042`
- `Docs\SPECIFICATION.md:19043`
- `Docs\SPECIFICATION.md:19045`
- `Docs\SPECIFICATION.md:19047`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\02_source\parser\stmt\stmt_common.cpp:563`
- `Bootstrap\Ultraviolet\src\02_source\parser\stmt\stmt_common.cpp:569`
- `Bootstrap\Ultraviolet\src\02_source\parser\stmt\stmt_common.cpp:605`
- `Bootstrap\Ultraviolet\src\02_source\parser\stmt\stmt_common.cpp:607`
- `Bootstrap\Ultraviolet\src\02_source\parser\stmt\parse_stmt.cpp:381`
- `Bootstrap\Ultraviolet\src\02_source\parser\stmt\parse_stmt.cpp:388`
- `Bootstrap\Ultraviolet\src\02_source\parser\stmt\parse_stmt.cpp:390`
- `Bootstrap\Ultraviolet\src\02_source\parser\parser.cpp:131`
- `Bootstrap\Ultraviolet\src\02_source\parser\parser_recovery.cpp:143`

Observed behavior:

`ParseStmtSeq` loops until it sees `}`. It skips newlines, probes for a tail
expression only if the current token starts an expression, then calls
`ParseStmt(cur)` and assigns `cur = head.parser`. Inside an unterminated
ordinary block at EOF, `ParseStmtCore` fails, `ParseStmt` reports a syntax
error, `AdvanceOrEOF` returns the same parser at EOF, and `SyncStmt` also
returns at EOF. `ParseStmtSeq` then repeats with the same parser state.

Expected behavior:

`ParseBlock` requires `ParseStmtSeq` to return a parser positioned at `}`. EOF
inside a block is a malformed block and should terminate parsing with the
missing-body or unexpected-EOF source diagnostic path, not re-enter statement
parsing without consuming input.

Impact:

An unterminated ordinary block or procedure body can leave `uvc` in the parser
instead of producing deterministic diagnostics. This is distinct from
`UV-AUDIT-0208`, which covers modal and state bodies.

HelloUltraviolet fixture gap:

Existing block and procedure fixtures cover valid statement sequences and
ordinary missing-return cases. There is no rejected fixture for an unterminated
ordinary block or procedure body that reaches EOF.

### UV-AUDIT-0234: Block parsing accepts retained newlines before `{`

Severity: Medium

Status: Agent-reported and locally corroborated.

Spec anchors:

- `Docs\SPECIFICATION.md:1921`
- `Docs\SPECIFICATION.md:1924`
- `Docs\SPECIFICATION.md:1930`
- `Docs\SPECIFICATION.md:1935`
- `Docs\SPECIFICATION.md:19020`
- `Docs\SPECIFICATION.md:19021`
- `Docs\SPECIFICATION.md:16858`
- `Docs\SPECIFICATION.md:16888`
- `Docs\SPECIFICATION.md:19723`
- `Docs\SPECIFICATION.md:19822`
- `Docs\SPECIFICATION.md:19917`
- `Docs\SPECIFICATION.md:20208`
- `Docs\SPECIFICATION.md:24726`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\02_source\parser\stmt\stmt_common.cpp:617`
- `Bootstrap\Ultraviolet\src\02_source\parser\stmt\stmt_common.cpp:618`
- `Bootstrap\Ultraviolet\src\02_source\parser\stmt\stmt_common.cpp:619`
- `Bootstrap\Ultraviolet\src\02_source\parser\stmt\parse_stmt.cpp:219`
- `Bootstrap\Ultraviolet\src\02_source\parser\stmt\parse_stmt.cpp:231`
- `Bootstrap\Ultraviolet\src\02_source\parser\stmt\parse_stmt.cpp:246`
- `Bootstrap\Ultraviolet\src\02_source\parser\stmt\parse_stmt.cpp:260`
- `Bootstrap\Ultraviolet\src\02_source\parser\stmt\parse_stmt.cpp:298`
- `Bootstrap\Ultraviolet\src\02_source\parser\expr\if_expr.cpp:306`
- `Bootstrap\Ultraviolet\src\02_source\parser\expr\if_expr.cpp:341`
- `Bootstrap\Ultraviolet\src\02_source\parser\expr\loop_conditional.cpp:203`
- `Bootstrap\Ultraviolet\src\02_source\parser\expr\loop_infinite.cpp:261`
- `Bootstrap\Ultraviolet\src\02_source\parser\expr\loop_iter.cpp:334`

Observed behavior:

`ParseBlock` skips leading newline tokens before checking for `{`. Any caller
that reaches `ParseBlock` after a head token can therefore accept a retained
newline between the head and the body. This applies through statement callers
such as `unsafe`, `defer`, `region`, `frame`, and explicit `name.frame`, and
through expression callers such as `if`, `else`, and loop bodies.

Expected behavior:

`ParseBlock` is specified with `IsPunc(Tok(P), "{")`: the parser state entering
the rule must already be at the opening brace. Newline retention is controlled
by `Filter(K)` and `Continue(K, i)`. If a newline remains before `{`, the block
head and the block body are separated by a terminator and the block parser
should not silently discard that token.

Impact:

The parser accepts grammar-impossible split-head forms and can weaken or move
missing-body diagnostics for control expressions and statement block forms.

HelloUltraviolet fixture gap:

Existing fixtures cover valid same-line block heads. There is no rejected
fixture asserting that retained-newline block heads are rejected for `if`,
`else`, loops, `defer`, `unsafe`, `region`, `frame`, or `comptime` statements.

### UV-AUDIT-0235: Statement parsing does not concatenate newline-separated attribute lists

Severity: Medium

Status: Agent-reported and locally corroborated.

Spec anchors:

- `Docs\SPECIFICATION.md:6677`
- `Docs\SPECIFICATION.md:6688`
- `Docs\SPECIFICATION.md:6697`
- `Docs\SPECIFICATION.md:6885`
- `Docs\SPECIFICATION.md:6887`
- `Docs\SPECIFICATION.md:19011`
- `Docs\SPECIFICATION.md:24726`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\02_source\parser\item\attribute_list.cpp:614`
- `Bootstrap\Ultraviolet\src\02_source\parser\item\attribute_list.cpp:619`
- `Bootstrap\Ultraviolet\src\02_source\parser\item\attribute_list.cpp:622`
- `Bootstrap\Ultraviolet\src\02_source\parser\item\attribute_list.cpp:650`
- `Bootstrap\Ultraviolet\src\02_source\parser\item\attribute_list.cpp:656`
- `Bootstrap\Ultraviolet\src\02_source\parser\stmt\parse_stmt.cpp:371`
- `Bootstrap\Ultraviolet\src\02_source\parser\stmt\parse_stmt.cpp:375`
- `Bootstrap\Ultraviolet\src\02_source\parser\stmt\parse_stmt.cpp:381`

Observed behavior:

`ParseStmt` calls `ParseAttributeListOpt` exactly once, then skips newlines if
attributes were present. If the next physical line starts another attribute
list for the same statement target, the second `#` is passed to
`ParseStmtCore`, which does not treat it as the continuation of the attribute
list. The statement then receives a generic syntax error at the second
attribute marker.

Expected behavior:

The spec defines `attribute_list` as one or more attributes and states that
multiple attribute lists on the same target are equivalent to a single
concatenated list in source order. Statement parsing must therefore collect
newline-separated lists that target the same statement before applying
`AttachStmtAttrs`.

Impact:

Valid attributed statement forms can be rejected or diagnosed at the second
attribute token. Statement-level metadata such as `#emit` and `#files` becomes
less composable than declaration and member metadata.

HelloUltraviolet fixture gap:

Existing attribute coverage exercises single statement attribute lists and
stacked top-level declaration attributes. There is no fixture with two
newline-separated attribute lists on one statement target.

### UV-AUDIT-0236: Statement attributes are reassigned to subexpressions or dropped

Severity: Medium

Status: Agent-reported and locally corroborated.

Spec anchors:

- `Docs\SPECIFICATION.md:6697`
- `Docs\SPECIFICATION.md:6822`
- `Docs\SPECIFICATION.md:6838`
- `Docs\SPECIFICATION.md:6855`
- `Docs\SPECIFICATION.md:6856`
- `Docs\SPECIFICATION.md:6865`
- `Docs\SPECIFICATION.md:6875`
- `Docs\SPECIFICATION.md:6883`
- `Docs\SPECIFICATION.md:19011`
- `Docs\SPECIFICATION.md:19723`
- `Docs\SPECIFICATION.md:19822`
- `Docs\SPECIFICATION.md:19917`
- `Docs\SPECIFICATION.md:20208`
- `Docs\SPECIFICATION.md:24726`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\02_source\parser\stmt\parse_stmt.cpp:371`
- `Bootstrap\Ultraviolet\src\02_source\parser\stmt\parse_stmt.cpp:395`
- `Bootstrap\Ultraviolet\src\02_source\parser\stmt\parse_stmt.cpp:396`
- `Bootstrap\Ultraviolet\src\02_source\parser\stmt\stmt_common.cpp:651`
- `Bootstrap\Ultraviolet\src\02_source\parser\stmt\stmt_common.cpp:663`
- `Bootstrap\Ultraviolet\src\02_source\parser\stmt\stmt_common.cpp:668`
- `Bootstrap\Ultraviolet\src\02_source\parser\stmt\stmt_common.cpp:669`
- `Bootstrap\Ultraviolet\src\02_source\parser\stmt\stmt_common.cpp:673`
- `Bootstrap\Ultraviolet\src\02_source\parser\stmt\stmt_common.cpp:674`
- `Bootstrap\Ultraviolet\src\02_source\parser\stmt\stmt_common.cpp:678`
- `Bootstrap\Ultraviolet\src\02_source\parser\stmt\stmt_common.cpp:682`
- `Bootstrap\Ultraviolet\src\02_source\parser\stmt\stmt_common.cpp:686`
- `Bootstrap\Ultraviolet\src\02_source\parser\stmt\stmt_common.cpp:694`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\let_stmt.cpp:140`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\let_stmt.cpp:251`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\var_stmt.cpp:139`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\var_stmt.cpp:250`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\type_expr.cpp:3630`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\key_block_stmt.cpp:1662`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\key_block_stmt.cpp:1671`

Observed behavior:

`ParseStmt` accepts attributes before every statement and then calls
`ApplyStmtAttrs`. That helper attaches binding attributes to `let` and `var`,
but local `using` has no AST field and the attributes are silently dropped.
Assignments and compound assignments copy the same list onto both the place
and value expressions. Expression statements, `return`, and `break` wrap only
their value expression. Key blocks receive their own attribute slot, while
other block statements such as `defer`, `region`, `frame`, `unsafe`, and
`continue` have no preserved statement attribute target.

Expected behavior:

`Parse-Statement` uses `AttachStmtAttrs(attrs_opt, s_0)`, and the attribute
rules define distinct `Statement`, `Expression`, `Binding`, and `KeyBlock`
targets. Attributes attached to a statement must be preserved on that statement
target and validated against the correct `AttrTarget`; they must not be
duplicated onto child expressions or silently erased.

Impact:

Attribute target diagnostics can be lost or reported for the wrong target.
Statement-only attributes may disappear on statement forms that lack storage,
and expression/key-block attributes can be accepted on child nodes that were
not the syntactic target.

HelloUltraviolet fixture gap:

Existing fixtures cover binding attributes, expression attributes, and key-block
memory-order attributes. There is no fixture checking statement attributes on
local `using`, assignment, compound assignment, `return`, `break`, `defer`,
`region`, `frame`, `unsafe`, `continue`, or expression statement forms for
correct target preservation and rejection behavior.

### UV-AUDIT-0237: Normal region exits drop the registered region cleanup

Severity: High

Status: Agent-reported and locally corroborated.

Spec anchors:

- `Docs\SPECIFICATION.md:4629`
- `Docs\SPECIFICATION.md:10788`
- `Docs\SPECIFICATION.md:19153`
- `Docs\SPECIFICATION.md:19869`
- `Docs\SPECIFICATION.md:28986`
- `Docs\SPECIFICATION.md:29026`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\05_codegen\lower\stmt\region_stmt.cpp:62`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\stmt\region_stmt.cpp:89`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\stmt\region_stmt.cpp:107`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\stmt\region_stmt.cpp:120`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\stmt\region_stmt.cpp:141`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\stmt\region_stmt.cpp:148`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\block_expr.cpp:185`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\block_expr.cpp:191`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\block_expr.cpp:197`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\control\region.cpp:278`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\control\region.cpp:279`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\control\region.cpp:280`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\control\region.cpp:300`
- `Bootstrap\Ultraviolet\src\05_codegen\cleanup\cleanup.cpp:778`
- `Bootstrap\Ultraviolet\src\05_codegen\cleanup\cleanup.cpp:794`
- `Bootstrap\Ultraviolet\src\05_codegen\cleanup\cleanup.cpp:878`
- `Bootstrap\Ultraviolet\src\05_codegen\cleanup\cleanup.cpp:881`

Observed behavior:

`LowerRegionStmt` pushes a cleanup-capable scope, registers runtime scope exit,
registers a region release action, lowers the region body, and pops the scope.
The returned IR sequence contains the lowered options expression, runtime scope
enter, and `IRRegion`. It does not compute or append the cleanup plan before
the scope is popped. The LLVM region emitter then emits the region body and
restores active-region bookkeeping without invoking the cleanup actions.

Expected behavior:

The spec requires region release and frame reset paths to execute
`CleanupScope` before arena operations, requires `Region::free_unchecked` to be
invoked exactly once for an active or frozen scoped region at scope exit, and
defines block exit cleanup as the mechanism for registered cleanups. A normal
region statement exit must therefore emit the registered region-release and
runtime-scope-exit cleanup actions.

Impact:

Normal completion of a `region` statement can leave runtime region scope state
and active/frozen region storage unreleased. The cleanup registration exists,
but the normal-exit lowering path drops it, so behavior depends on abnormal
control-flow paths instead of the specified scope-exit semantics.

HelloUltraviolet fixture gap:

Existing region references and IR-lowering fixtures exercise region values and
catalog coverage. There is no fixture that checks emitted normal-exit cleanup
for a `region` statement or the required runtime scope release sequence.

### UV-AUDIT-0238: Cancel-token method lowering can pass null for unbound token temporaries

Severity: High

Status: Agent-reported and locally corroborated.

Spec anchors:

- `Docs\SPECIFICATION.md:22627`
- `Docs\SPECIFICATION.md:22629`
- `Docs\SPECIFICATION.md:22631`
- `Docs\SPECIFICATION.md:22633`
- `Docs\SPECIFICATION.md:22635`
- `Docs\SPECIFICATION.md:22649`
- `Docs\SPECIFICATION.md:22651`
- `Docs\SPECIFICATION.md:22698`
- `Docs\SPECIFICATION.md:22706`
- `Docs\SPECIFICATION.md:22708`
- `Docs\SPECIFICATION.md:22710`
- `Docs\SPECIFICATION.md:29286`
- `Docs\SPECIFICATION.md:29288`
- `Docs\SPECIFICATION.md:29290`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\call.cpp:2267`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\method_call.cpp:2454`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\method_call.cpp:2465`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\method_call.cpp:2476`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\cancellation.cpp:11`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\cancellation.cpp:15`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\cancellation.cpp:126`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\cancellation.cpp:139`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\cancellation.cpp:144`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\cancellation.cpp:153`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\cancellation.cpp:159`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\cancellation.cpp:168`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\cancellation.cpp:174`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir_storage_emit.cpp:78`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:3614`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:3621`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:3626`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:3631`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:3650`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:3651`

Observed behavior:

`CancelToken::new` lowers to an `IRCancelCreate` result value. Built-in
methods on `CancelToken@Active` lower to cancellation IR using the receiver
value directly. The LLVM cancellation helper first asks for addressable storage
for that receiver, then tries to evaluate the receiver value; if neither yields
a pointer to token storage, it reports codegen failure and uses a null pointer.
At the runtime boundary, a null self-reference is converted to the invalid
cancel-token id.

Expected behavior:

The spec models cancel tokens as records whose id field is read by `CancelId`
and requires `CancelCheckIR`, `CancelWaitIR`, and cancellation requests to act
on the receiver token value. A valid token temporary must be materialized or
passed in a representation that preserves its id; it must not silently degrade
into the invalid sentinel because the receiver was not first bound to a local.

Impact:

Code such as a method call on a freshly constructed or otherwise unbound token
can lose the actual cancel id at the codegen boundary. Cancellation requests can
be ignored, checks can observe the wrong state, and waits can complete as
invalid-token waits instead of using the active token specified by the source.

HelloUltraviolet fixture gap:

Existing cancellation fixtures bind tokens to locals before invoking methods.
There is no fixture with a non-local token receiver or an emitted-code check
that the cancellation runtime receives non-null addressable token storage.

### UV-AUDIT-0239: POSIX IO path resolution does not implement WinSep and AbsPath

Severity: High

Status: Agent-reported and locally corroborated.

Spec anchors:

- `Docs\SPECIFICATION.md:901`
- `Docs\SPECIFICATION.md:903`
- `Docs\SPECIFICATION.md:916`
- `Docs\SPECIFICATION.md:930`
- `Docs\SPECIFICATION.md:3451`
- `Docs\SPECIFICATION.md:3452`
- `Docs\SPECIFICATION.md:3455`
- `Docs\SPECIFICATION.md:3456`
- `Docs\SPECIFICATION.md:3461`
- `Docs\SPECIFICATION.md:3467`
- `Docs\SPECIFICATION.md:3468`
- `Docs\SPECIFICATION.md:3501`
- `Docs\SPECIFICATION.md:3502`

Implementation anchors:

- `Bootstrap\Ultraviolet\runtime\src\internal\rt_path.h:19`
- `Bootstrap\Ultraviolet\runtime\src\platform\linux\rt_path_linux.c:4`
- `Bootstrap\Ultraviolet\runtime\src\platform\linux\rt_path_linux.c:12`
- `Bootstrap\Ultraviolet\runtime\src\platform\linux\rt_path_linux.c:47`
- `Bootstrap\Ultraviolet\runtime\src\platform\linux\rt_path_linux.c:54`
- `Bootstrap\Ultraviolet\runtime\src\platform\linux\rt_path_linux.c:72`
- `Bootstrap\Ultraviolet\runtime\src\platform\macos\rt_path_macos.c:4`
- `Bootstrap\Ultraviolet\runtime\src\platform\macos\rt_path_macos.c:12`
- `Bootstrap\Ultraviolet\runtime\src\platform\macos\rt_path_macos.c:47`
- `Bootstrap\Ultraviolet\runtime\src\platform\macos\rt_path_macos.c:54`
- `Bootstrap\Ultraviolet\runtime\src\platform\macos\rt_path_macos.c:72`
- `Bootstrap\Ultraviolet\runtime\src\io\io.c:339`
- `Bootstrap\Ultraviolet\runtime\src\io\io.c:366`
- `Bootstrap\Ultraviolet\runtime\src\io\io.c:596`
- `Bootstrap\Ultraviolet\runtime\src\io\io.c:643`
- `Bootstrap\Ultraviolet\runtime\src\io\io.c:689`
- `Bootstrap\Ultraviolet\runtime\src\io\io.c:743`
- `Bootstrap\Ultraviolet\runtime\src\io\io.c:835`
- `Bootstrap\Ultraviolet\runtime\src\io\io.c:998`
- `Bootstrap\Ultraviolet\runtime\src\io\io.c:1022`
- `Bootstrap\Ultraviolet\runtime\src\io\io.c:1233`
- `Bootstrap\Ultraviolet\runtime\src\io\io.c:1489`
- `Bootstrap\Ultraviolet\runtime\src\io\io.c:1515`
- `Bootstrap\Ultraviolet\runtime\src\io\io.c:1616`
- `Bootstrap\Ultraviolet\runtime\src\io\io.c:1658`

Observed behavior:

The runtime routes every IO path operation through `uv_fs_resolve_path`.
Restricted roots reject paths only when `uv_rt_path_is_absolute_utf8` reports
absolute, then join and canonicalize. On Linux, path canonicalization treats
both `/` and `\` as separators, but absolute-path detection only treats leading
`/` as absolute. On macOS, both absolute-path detection and canonicalization
only treat `/` as special, so backslash-separated parent traversal is parsed as
ordinary segment text.

Expected behavior:

The spec defines `WinSep` as both `\` and `/`, defines `Segs` over `WinSep`,
and defines `AbsPath` to include drive-rooted, UNC, and root-relative paths.
`RestrictPath`, `PathInvalid`, and `IOExists` semantics are expressed in terms
of that shared canonical path model. POSIX runtime adapters must therefore use
the same separator and absolute-path grammar for IO authority checks and error
classification.

Impact:

Restricted IO can classify backslash-rooted or drive-rooted paths as relative
on POSIX hosts, and macOS can fail to recognize `..` traversal when the path
uses backslashes. That weakens the specified path authority boundary and can
return successful or false results where the spec requires invalid-path
handling through the canonical model.

HelloUltraviolet fixture gap:

Existing IO references include parent traversal and embedded-zero invalid path
cases. There is no POSIX-oriented fixture for drive-rooted paths,
backslash-rooted paths, UNC-shaped paths, or backslash-separated `..`
segments through restricted IO.

### UV-AUDIT-0240: `Context.reactor` is null and reactor hooks ignore the receiver

Severity: High

Status: Agent-reported and locally corroborated.

Spec anchors:

- `Docs\SPECIFICATION.md:3334`
- `Docs\SPECIFICATION.md:13644`
- `Docs\SPECIFICATION.md:13685`
- `Docs\SPECIFICATION.md:13690`
- `Docs\SPECIFICATION.md:13768`
- `Docs\SPECIFICATION.md:13781`
- `Docs\SPECIFICATION.md:29129`
- `Docs\SPECIFICATION.md:29133`
- `Docs\SPECIFICATION.md:29357`
- `Docs\SPECIFICATION.md:29359`
- `Docs\SPECIFICATION.md:29362`
- `Docs\SPECIFICATION.md:29364`
- `Docs\SPECIFICATION.md:29367`
- `Docs\SPECIFICATION.md:29369`

Implementation anchors:

- `Bootstrap\Ultraviolet\runtime\include\uv_rt.h:52`
- `Bootstrap\Ultraviolet\runtime\include\uv_rt.h:57`
- `Bootstrap\Ultraviolet\runtime\include\uv_rt.h:723`
- `Bootstrap\Ultraviolet\runtime\include\uv_rt.h:727`
- `Bootstrap\Ultraviolet\runtime\src\context\context.c:3`
- `Bootstrap\Ultraviolet\runtime\src\context\context.c:16`
- `Bootstrap\Ultraviolet\runtime\src\context\context.c:17`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:2194`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:2195`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:2197`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:2199`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:2206`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:2207`
- `Bootstrap\Ultraviolet\runtime\src\concurrency\parallel.c:2209`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\llvm_emit_helpers.cpp:727`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\llvm_emit_helpers.cpp:739`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\llvm_emit_helpers.cpp:761`

Observed behavior:

`UVContext` contains a dynamic `reactor` field, and codegen loads that field
when lowering `context.reactor`. Runtime context initialization sets the
reactor dynamic object data and vtable to null and does not later install a
reactor capability. The runtime implementations of `reactor::run` and
`reactor::register` accept the receiver pointer but explicitly ignore it.

Expected behavior:

The spec declares `Context.reactor` as a dynamic `Reactor` capability, defines
reactor methods on the `Reactor` class, and models `ReactorRun` and
`ReactorRegister` as host-primitive relations over `v_reactor`. The context
field value must therefore denote a usable reactor capability whose receiver
identity participates in those primitive relations.

Impact:

Source code can pass `context.reactor` as an authority-bearing capability while
the runtime field is a null dynamic object. Reactor operations can succeed or
register work without consulting the receiver, so the capability value does not
carry the authority or host-event-loop identity specified by the language
model.

HelloUltraviolet fixture gap:

Existing async and capability references call `context.reactor~>run` and
`context.reactor~>register`, and pass reactor capabilities through records and
parameters. There is no fixture that asserts the runtime context field is
non-null or that reactor host primitives are receiver-dependent.

### UV-AUDIT-0241: Parent-segment manifest paths are classified before `Resolve`

Severity: Medium

Status: Agent-reported and locally corroborated.

Spec anchors:

- `Docs\SPECIFICATION.md:938`
- `Docs\SPECIFICATION.md:939`
- `Docs\SPECIFICATION.md:954`
- `Docs\SPECIFICATION.md:959`
- `Docs\SPECIFICATION.md:960`
- `Docs\SPECIFICATION.md:962`
- `Docs\SPECIFICATION.md:964`
- `Docs\SPECIFICATION.md:969`
- `Docs\SPECIFICATION.md:970`
- `Docs\SPECIFICATION.md:1013`
- `Docs\SPECIFICATION.md:1745`
- `Docs\SPECIFICATION.md:1748`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\00_core\path.cpp:257`
- `Bootstrap\Ultraviolet\src\00_core\path.cpp:270`
- `Bootstrap\Ultraviolet\src\00_core\path.cpp:274`
- `Bootstrap\Ultraviolet\src\00_core\path.cpp:298`
- `Bootstrap\Ultraviolet\src\00_core\path.cpp:299`
- `Bootstrap\Ultraviolet\src\00_core\path.cpp:300`
- `Bootstrap\Ultraviolet\src\00_core\path.cpp:301`
- `Bootstrap\Ultraviolet\src\01_project\project_validate.cpp:410`
- `Bootstrap\Ultraviolet\src\01_project\project_validate.cpp:415`
- `Bootstrap\Ultraviolet\src\01_project\project_validate.cpp:416`
- `Bootstrap\Ultraviolet\src\01_project\project_validate.cpp:417`
- `Bootstrap\Ultraviolet\src\01_project\project_validate.cpp:756`
- `Bootstrap\Ultraviolet\src\01_project\project_validate.cpp:758`
- `Bootstrap\Ultraviolet\src\01_project\project_validate.cpp:759`
- `Bootstrap\Ultraviolet\src\01_project\project_validate.cpp:760`
- `Bootstrap\Ultraviolet\src\01_project\project_validate.cpp:762`
- `Bootstrap\Ultraviolet\src\01_project\project_validate.cpp:763`
- `HelloUltraviolet\Fixtures\OutputDiagnostics\Projects\ManifestOutDirPathInvalid\Ultraviolet.toml:5`
- `HelloUltraviolet\Fixtures\OutputDiagnostics\Projects\ManifestOutDirPathInvalid\Expected.uv:3`

Observed behavior:

`CheckRelPath` rejects a manifest path as `RelPathErr` when `core::Canon(p)`
fails before it calls `core::Resolve(root, p)`. The existing
`ManifestOutDirPathInvalid` fixture uses `out_dir = "../Escapes"` and expects
`E-PRJ-0301` with `WF-Assembly-OutDir-Path-Err`. That path still contains a
`..` component after normalization, so the implementation classifies it through
the relational-path precheck rather than through the `Resolve` judgment.

Expected behavior:

The spec defines `Canon(p) = bottom` when normalized path components contain
`..`, defines `Resolve-Canonical-Err` when `Canon(Normalize(Join(R, p)))` is
bottom, and maps that rule to `E-PRJ-0304`. Assembly `out_dir` checking is part
of `ChecksAsm`, so parent-segment paths that fail during `Resolve(R, p)` should
preserve the `Resolve-Canonical-Err` first failure instead of being preempted by
`WF-Assembly-OutDir-Path-Err`.

Impact:

Diagnostics and conformance logs attribute parent-segment manifest paths to the
wrong rule and code. Tooling that checks rule-specific first-failure behavior
cannot distinguish canonicalization failure from relative paths that resolve
successfully but fall outside the project root.

HelloUltraviolet fixture gap:

The existing output-diagnostic fixture locks the current `E-PRJ-0301`
classification for `../Escapes`. There is no companion fixture that expects
`Resolve-Canonical-Err` / `E-PRJ-0304` for a parent-segment `out_dir` path.

### UV-AUDIT-0242: `uvc test` can bypass required assembly selection

Severity: Medium

Status: Agent-reported and locally corroborated.

Spec anchors:

- `Docs\SPECIFICATION.md:1050`
- `Docs\SPECIFICATION.md:1060`
- `Docs\SPECIFICATION.md:1066`
- `Docs\SPECIFICATION.md:1069`
- `Docs\SPECIFICATION.md:1071`
- `Docs\SPECIFICATION.md:1074`
- `Docs\SPECIFICATION.md:1081`
- `Docs\SPECIFICATION.md:1082`
- `Docs\SPECIFICATION.md:1084`
- `Docs\SPECIFICATION.md:1111`
- `Docs\SPECIFICATION.md:1113`
- `Docs\SPECIFICATION.md:1739`
- `Docs\SPECIFICATION.md:7307`
- `Docs\SPECIFICATION.md:7311`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\01_project\load_project.cpp:291`
- `Bootstrap\Ultraviolet\src\01_project\load_project.cpp:303`
- `Bootstrap\Ultraviolet\src\01_project\load_project.cpp:317`
- `Bootstrap\Ultraviolet\src\01_project\load_project.cpp:321`
- `Bootstrap\Ultraviolet\src\01_project\load_project.cpp:327`
- `Bootstrap\Ultraviolet\src\01_project\load_project.cpp:409`
- `Bootstrap\Ultraviolet\src\01_project\load_project.cpp:411`
- `Bootstrap\Ultraviolet\src\01_project\load_project.cpp:485`
- `Bootstrap\Ultraviolet\src\01_project\load_project.cpp:501`
- `Bootstrap\Ultraviolet\src\06_driver\compiler_main.cpp:3064`
- `Bootstrap\Ultraviolet\src\06_driver\compiler_main.cpp:3073`
- `Bootstrap\Ultraviolet\src\06_driver\compiler_main.cpp:3123`
- `Bootstrap\Ultraviolet\src\06_driver\compiler_main.cpp:3152`
- `HelloUltraviolet\Source\Audit\Catalog\ProjectAndCompilationModel\AssembliesAndProjectLoading.uv:191`
- `HelloUltraviolet\Source\Audit\Catalog\ProjectAndCompilationModel\AssembliesAndProjectLoading.uv:200`
- `HelloUltraviolet\Source\Audit\Catalog\ProjectAndCompilationModel\AssembliesAndProjectLoading.uv:218`

Observed behavior:

The ordinary project loader calls `SelectAssembly` when selection is required.
The `uvc test` path instead calls `LoadProjectAllAssemblies`, whose internal
`require_selected_assembly` flag is false. With no `--assembly` target, that
path assigns `assemblies.front()` as the selected assembly instead of running
`SelectAssembly`. The driver then discovers tests across
`project_result.project->assemblies` and filters them by the test target.

Expected behavior:

The spec defines `LoadProject(R, target)` through `SelectAssembly(As', target)`
and defines `Select-Err` for no target when there is neither exactly one
assembly nor exactly one executable assembly. `uvc test` still consumes a
`Project` value, and `ResolveTestTarget(P, bottom) = AllTests` operates over
that project. Loading the project for tests must therefore preserve the
required assembly-selection failure instead of fabricating a selected assembly.

Impact:

A multi-assembly project that should fail with `E-PRJ-0205` can proceed under
`uvc test`, with `P.assembly`, `P.source_root`, and `P.outputs` silently taken
from the first manifest assembly. Test discovery may still scan all assemblies,
but project-level fields and conformance records no longer reflect the
spec-defined selected assembly.

HelloUltraviolet fixture gap:

The accepted catalog includes symbolic coverage for `Select-Only`,
`Select-Only-Exe`, and `Select-Err`. There is no driver-level fixture invoking
`uvc test` on an ambiguous multi-assembly project with no `--assembly` target
and expecting `E-PRJ-0205`.

### UV-AUDIT-0243: Unknown test targets emit the final code instead of `Test-Target-Err`

Severity: Medium

Status: Agent-reported and locally corroborated.

Spec anchors:

- `Docs\SPECIFICATION.md:7322`
- `Docs\SPECIFICATION.md:7356`
- `Docs\SPECIFICATION.md:30479`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\06_driver\compiler_main.cpp:357`
- `Bootstrap\Ultraviolet\src\06_driver\compiler_main.cpp:368`
- `Bootstrap\Ultraviolet\src\00_core\diagnostic_messages.cpp:146`
- `Bootstrap\Ultraviolet\src\00_core\diagnostic_messages.cpp:159`
- `Bootstrap\Ultraviolet\src\00_core\diagnostics.cpp:224`
- `Bootstrap\Ultraviolet\src\00_core\diagnostics.cpp:659`
- `Bootstrap\Ultraviolet\src\00_core\diagnostics.cpp:662`
- `Bootstrap\Ultraviolet\src\00_core\generated\diag_registry.inc:355`
- `Bootstrap\Ultraviolet\src\00_core\generated\diag_registry.inc:907`
- `Bootstrap\Ultraviolet\src\00_core\generated\diag_registry.inc:1193`
- `HelloUltraviolet\Fixtures\OutputDiagnostics\CommandLine\UnknownTestTarget\Expected.uv:3`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\OutputDiagnostics\CommandLine.uv:3`
- `HelloUltraviolet\Source\Audit\OutputDiagnosticExecution.uv:3273`

Observed behavior:

Unknown test targets are emitted with `MakeDiagnosticById("E-TST-0108")`.
`MakeDiagnosticById` records an explicit obligation id only when the requested
id differs from the resolved diagnostic code, so the rule alias
`Test-Target-Err` is not attached. The table-driven diagnostic obligation path
then records `diagnostics.TestAttributes` for `E-TST-0108`, and the
HelloUltraviolet expected output also checks only `diagnostics.TestAttributes`.

Expected behavior:

The spec defines unknown test-target resolution as
`ResolveTestTarget(P, s) => Code(Test-Target-Err)` and maps
`Test-Target-Err` to `E-TST-0108`. The emitted diagnostic should therefore
preserve the rule-specific obligation id, either by constructing the diagnostic
from `Test-Target-Err` or by otherwise recording that rule on the diagnostic.

Impact:

The user-visible code is correct, but conformance output loses the normative
first-failure rule. Fixture checks can pass while never proving that the
unknown-target path is the specified `Test-Target-Err` path.

HelloUltraviolet fixture gap:

The command-line fixture catalog currently enumerates only the unknown-command
fixture, and the unknown-test-target expected output checks only the
chapter-level test-attribute obligation. There is no fixture assertion that the
`uvc test` unknown-target path emits `Test-Target-Err`.

### UV-AUDIT-0244: Line-continuation state erases blank-line terminators

Severity: High

Status: Agent-reported and locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:1911`
- `Docs\SPECIFICATION.md:1924`
- `Docs\SPECIFICATION.md:1930`
- `Docs\SPECIFICATION.md:1939`
- `Docs\SPECIFICATION.md:1940`
- `Docs\SPECIFICATION.md:1943`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\02_source\lexer\lexer.cpp:254`
- `Bootstrap\Ultraviolet\src\02_source\lexer\lexer.cpp:271`
- `Bootstrap\Ultraviolet\src\02_source\lexer\lexer.cpp:273`
- `Bootstrap\Ultraviolet\src\02_source\lexer\lexer.cpp:275`
- `Bootstrap\Ultraviolet\src\02_source\lexer\lexer.cpp:350`
- `Bootstrap\Ultraviolet\src\02_source\lexer\lexer.cpp:363`
- `Bootstrap\Ultraviolet\src\02_source\lexer\lexer.cpp:417`
- `Bootstrap\Ultraviolet\src\02_source\lexer\lexer.cpp:600`
- `HelloUltraviolet\Source\Api.uv:3342`
- `HelloUltraviolet\Source\Api.uv:3372`
- `HelloUltraviolet\Source\Reference\SourceText\LogicalLines.uv:20`
- `HelloUltraviolet\Source\Reference\Parsing\Terminators.uv:31`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\RejectedSource\Parsing.uv:11`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\RejectedSource\Statements.uv:164`

Observed behavior:

`BuildNewlineContext` stores the previous non-newline token in
`ctx.prev_index[i]` for every token and only updates that state when the current
token is not a newline. A token sequence shaped like `a +`, newline, newline,
`b` therefore gives both newline tokens the same previous non-newline token
`+`. `ContinuesLineImpl` can classify both newlines as continued through the
operator-continuation branch, and `FilterNewlines` removes every newline that is
not a required terminator.

Expected behavior:

`Prev(K, i)` exists only when there is no intervening newline between the
previous non-newline token and `i`. For the second newline in a blank-line run,
`Prev(K, i)` must be bottom, so the operator/comma continuation disjunct cannot
apply. That second newline must remain a required terminator in the filtered
token stream.

Impact:

`uvc` can join source across a blank line after an operator or comma. That
changes the parser input before statement or expression parsing, can accept a
layout the spec treats as terminated, and can move the first diagnostic away
from the required-terminator boundary.

HelloUltraviolet fixture gap:

HelloUltraviolet runs logical-line and terminator reference checks, plus
general missing-terminator rejected fixtures, but there is no `uvc` fixture with
a blank line after a continuation-introducing operator or comma that proves the
second newline remains a required terminator.

### UV-AUDIT-0245: Generic class-bound applications are stored without arity validation

Severity: High

Status: Agent-reported and locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:12291`
- `Docs\SPECIFICATION.md:12373`
- `Docs\SPECIFICATION.md:12458`
- `Docs\SPECIFICATION.md:12608`
- `Docs\SPECIFICATION.md:12613`
- `Docs\SPECIFICATION.md:13945`
- `Docs\SPECIFICATION.md:13946`
- `Docs\SPECIFICATION.md:13948`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\generic_params.cpp:50`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\generic_params.cpp:89`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\generic_params.cpp:91`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\generic_params.cpp:101`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\generic_params.cpp:108`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\generic_params.cpp:191`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\generic_params.cpp:192`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\type_wf.cpp:444`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\type_wf.cpp:446`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\type_wf.cpp:449`
- `HelloUltraviolet\Fixtures\RejectedSource\Polymorphism\GenericTypeApplyArgCount\Source\Main.uv:7`
- `HelloUltraviolet\Fixtures\RejectedSource\Polymorphism\GenericClassBoundNonClass\Source\Main.uv:7`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\RejectedSource\Polymorphism.uv:164`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\RejectedSource\Polymorphism.uv:249`

Observed behavior:

`ProcessGenericParams` accepts each inline class bound once
`ClassBoundPathExists` finds the bound path. It then lowers each
`bound.generic_args` member only as a standalone type and stores the original
bound with `info.class_bounds.push_back(bound)`. The declaration path never
constructs a `TypeApply` for the bound itself and never runs the
`TypeWFImpl` branch that checks `RequiredParamCount` and `TotalParamCount`.
Later generic-argument checking also tests only `bound.class_path` with
`TypeImplementsClass`, so the bound application's own arity is not validated
there either.

Expected behavior:

The spec parses inline bounds as `class_bound` values with optional generic
arguments, and `WF-Apply` requires applied generic types to satisfy default
argument, arity, argument well-formedness, class-bound, and predicate-clause
rules. A declaration such as `TValue <: Box<i32, bool, i32>` for a two-parameter
class must therefore be rejected through the `WF-Apply-ArgCount-Err` path
instead of being stored as a valid generic parameter bound.

Impact:

Malformed class-bound applications can enter the semantic model as constraints.
That lets later type checking and generic satisfaction operate from impossible
or undervalidated bound requirements, while user-visible diagnostics miss the
specified arity and bound failures at the declaration site.

HelloUltraviolet fixture gap:

HelloUltraviolet has a normal type-use arity fixture and a non-class generic
bound fixture, but no rejected-source fixture where the malformed application
appears inside a generic parameter class bound and is checked with `uvc test`.

### UV-AUDIT-0246: Hosted-state lowering can substitute global storage for session slots

Severity: High

Status: Agent-reported and locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:26363`
- `Docs\SPECIFICATION.md:28468`
- `Docs\SPECIFICATION.md:28474`
- `Docs\SPECIFICATION.md:28478`
- `Docs\SPECIFICATION.md:28685`
- `Docs\SPECIFICATION.md:29769`
- `Docs\SPECIFICATION.md:29770`
- `Docs\SPECIFICATION.md:29773`
- `Docs\SPECIFICATION.md:29775`
- `Docs\SPECIFICATION.md:29823`
- `Docs\SPECIFICATION.md:29853`
- `Docs\SPECIFICATION.md:29858`
- `Docs\SPECIFICATION.md:29898`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\hosted_emit.cpp:234`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\hosted_emit.cpp:305`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\hosted_emit.cpp:318`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\hosted_emit.cpp:321`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\hosted_emit.cpp:344`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\hosted_emit.cpp:354`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\value\evaluate.cpp:606`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\value\evaluate.cpp:624`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\value\evaluate.cpp:2418`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\value\evaluate.cpp:2421`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\value\evaluate.cpp:2425`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\value\evaluate.cpp:2439`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\value\evaluate.cpp:2440`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\memory\addr_of.cpp:60`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\memory\addr_of.cpp:64`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\memory\addr_of.cpp:67`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\memory\store_global.cpp:84`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\memory\store_global.cpp:108`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\memory\store_global.cpp:113`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\memory\store_global.cpp:115`
- `HelloUltraviolet\Fixtures\AcceptedProjects\HostedExportLibrary\Source\Library.uv:31`
- `HelloUltraviolet\Fixtures\AcceptedProjects\HostedExportLibrary\Source\Library.uv:108`
- `HelloUltraviolet\Fixtures\AcceptedProjects\HostedExportLibrary\Source\Library.uv:113`
- `HelloUltraviolet\Source\Audit\AcceptedProjectExecution.uv:2900`

Observed behavior:

`GetHostedStatePtr` accepts a `fallback_ptr`. When the current hosted
environment pointer cannot be materialized, it returns the coerced fallback.
When an environment pointer exists but can be null at runtime and fallback
storage exists, it emits a `hosted.state.fallback` branch and a PHI that selects
the fallback pointer. Static reads, address-of lowering, derived address
materialization, and global stores all pass LLVM globals or newly declared
globals as that fallback for symbols that also have hosted-state slots.

Expected behavior:

For `HostedStateSym(Project(Γ), sym)`, `StateRef(sym)` must lower to
`SessionStateSlot(sym)` in the active hosted session. The emitted
`GlobalConst` or `GlobalZero` object is only the initializer template for that
per-session slot, and runtime loads or stores routed through `StateRef(sym)`
must observe the distinct live-session cell. Hosted thunk and body emission
also must not substitute a default or other fallback value for failed hosted
state materialization.

Impact:

A hosted export can read, take the address of, or store to the image/global
template storage instead of the active session slot whenever the helper takes
one of its fallback paths. That breaks per-session isolation for user statics
and poison-flag state, and can make two hosted sessions share state the spec
requires to be distinct.

HelloUltraviolet fixture gap:

`HostedExportLibrary` declares `_HOSTED_STATE_REFERENCE` and a hosted export
that mutates it, and the accepted-project execution checks for the hosted state
export symbol. It does not inspect the emitted hosted-state IR for absence of
fallback global storage, and no `uvc` fixture forces session-state
materialization failure or a null hosted environment to prove lowering fails or
stays session-indexed instead of selecting template storage.

### UV-AUDIT-0247: Raw pointer invalid access is lowered as a null-only check

Severity: High

Status: Locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:4714`
- `Docs\SPECIFICATION.md:11989`
- `Docs\SPECIFICATION.md:11990`
- `Docs\SPECIFICATION.md:11992`
- `Docs\SPECIFICATION.md:11994`
- `Docs\SPECIFICATION.md:11995`
- `Docs\SPECIFICATION.md:11997`
- `Docs\SPECIFICATION.md:11999`
- `Docs\SPECIFICATION.md:12000`
- `Docs\SPECIFICATION.md:12002`
- `Docs\SPECIFICATION.md:12009`
- `Docs\SPECIFICATION.md:12010`
- `Docs\SPECIFICATION.md:12012`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\05_codegen\checks\checks.cpp:250`
- `Bootstrap\Ultraviolet\src\05_codegen\checks\checks.cpp:258`
- `Bootstrap\Ultraviolet\src\05_codegen\checks\checks.cpp:261`
- `Bootstrap\Ultraviolet\src\05_codegen\checks\checks.cpp:867`
- `Bootstrap\Ultraviolet\src\05_codegen\checks\checks.cpp:868`
- `Bootstrap\Ultraviolet\src\05_codegen\checks\checks.cpp:873`
- `Bootstrap\Ultraviolet\src\05_codegen\checks\checks.cpp:874`
- `Bootstrap\Ultraviolet\src\05_codegen\checks\checks.cpp:880`
- `Bootstrap\Ultraviolet\src\05_codegen\checks\checks.cpp:884`
- `Bootstrap\Ultraviolet\src\05_codegen\checks\checks.cpp:992`
- `Bootstrap\Ultraviolet\src\05_codegen\checks\checks.cpp:999`
- `Bootstrap\Ultraviolet\src\05_codegen\checks\checks.cpp:1005`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\expr_place.cpp:172`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\expr_place.cpp:180`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\expr_place.cpp:183`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\expr_place.cpp:1286`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\expr_place.cpp:1290`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\expr_place.cpp:1292`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\expr_place.cpp:1293`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\expr_place.cpp:1297`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\expr_place.cpp:1304`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\memory\ptr.cpp:717`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\memory\ptr.cpp:766`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\memory\ptr.cpp:770`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\memory\ptr.cpp:980`
- `HelloUltraviolet\Source\Audit\Catalog\ModalAndSpecialTypes\RawPointerTypes.uv:110`
- `HelloUltraviolet\Source\Audit\Catalog\ModalAndSpecialTypes\RawPointerTypes.uv:114`
- `HelloUltraviolet\Source\Audit\Catalog\ModalAndSpecialTypes\RawPointerTypes.uv:128`
- `HelloUltraviolet\Source\Audit\Catalog\ModalAndSpecialTypes\RawPointerTypes.uv:132`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\ArtifactProjects\Coverage.uv:273`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\ArtifactProjects\Coverage.uv:279`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\ArtifactProjects\Coverage.uv:294`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\ArtifactProjects\Coverage.uv:300`
- `HelloUltraviolet\Source\Audit\ArtifactProjectExecution.uv:6195`
- `HelloUltraviolet\Source\Audit\ArtifactProjectExecution.uv:6199`
- `HelloUltraviolet\Source\Audit\ArtifactProjectExecution.uv:6203`
- `HelloUltraviolet\Source\Audit\ArtifactProjectExecution.uv:6207`
- `HelloUltraviolet\Source\Audit\ArtifactProjectExecution.uv:6211`
- `HelloUltraviolet\Source\Audit\ArtifactProjectExecution.uv:7419`
- `HelloUltraviolet\Source\Audit\ArtifactProjectExecution.uv:7445`
- `HelloUltraviolet\Fixtures\ArtifactProjects\RawPointerNullReadPanic\Source\Main.uv:1`
- `HelloUltraviolet\Fixtures\ArtifactProjects\RawPointerNullReadPanic\Source\Main.uv:12`
- `HelloUltraviolet\Fixtures\ArtifactProjects\RawPointerNullWritePanic\Source\Main.uv:1`
- `HelloUltraviolet\Fixtures\ArtifactProjects\RawPointerNullWritePanic\Source\Main.uv:15`

Observed behavior:

Raw pointer read lowering records `rule.13.ReadPtr-Raw-Invalid` with
`check=nonnull`, `panic=NullDeref`, `ir=IRReadPtr`, and `state_metadata=none`,
then emits only a `nonnull` `IRCheckOp` before `IRReadPtr`. The raw write path
does the same for `rule.13.WritePtr-Raw-Invalid` before `IRWritePtr`. The LLVM
IR visitor then turns those instructions into direct loads and stores. There is
no emitted check that the non-null raw pointer address is actually readable or
writable in the current execution state.

Expected behavior:

`ReadPtr-Raw-Invalid` must panic whenever `ReadAddr` is undefined for the raw
pointer address, and `WritePtr-Raw-Invalid` must panic whenever no valid
`WriteAddr` transition exists for the raw mutable pointer address. That includes
non-null addresses that are dangling, fabricated, stale, or otherwise outside
the readable or writable address relation. A null-only guard does not implement
those state-dependent invalid-address rules.

Impact:

A non-null invalid raw pointer can pass the recorded conformance check and reach
an LLVM load or store. That can produce host undefined behavior, a process
fault, or silent memory corruption instead of the specified `Ctrl(Panic)`, while
the conformance ledger still credits the raw-invalid rules.

HelloUltraviolet fixture gap:

HelloUltraviolet maps both raw-invalid obligations to null-pointer panic
fixtures. The catalog and artifact execution then assert the same
`check=nonnull;panic=NullDeref` payloads that expose the narrowing. The fixtures
only construct `null` raw pointers and do not include any `uvc` artifact that
exercises a non-null raw pointer whose `ReadAddr` or `WriteAddr` relation is
undefined.

### UV-AUDIT-0248: Runtime cast checks skip invalid `u32` to `char` values

Severity: High

Status: Locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:16570`
- `Docs\SPECIFICATION.md:16571`
- `Docs\SPECIFICATION.md:16573`
- `Docs\SPECIFICATION.md:16580`
- `Docs\SPECIFICATION.md:16581`
- `Docs\SPECIFICATION.md:16583`
- `Docs\SPECIFICATION.md:16611`
- `Docs\SPECIFICATION.md:16618`
- `Docs\SPECIFICATION.md:16619`
- `Docs\SPECIFICATION.md:16621`
- `Docs\SPECIFICATION.md:16635`
- `Docs\SPECIFICATION.md:28789`
- `Docs\SPECIFICATION.md:28794`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\cast.cpp:57`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\cast.cpp:59`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\cast.cpp:129`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\cast.cpp:131`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\cast.cpp:137`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\cast.cpp:140`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\checks\check_cast.cpp:9`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\checks\check_cast.cpp:13`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\checks\check_cast.cpp:19`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\checks\check_cast.cpp:21`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\checks\check_cast.cpp:29`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\checks\check_cast.cpp:32`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\ops\cast.cpp:64`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\ops\cast.cpp:66`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\ops\cast.cpp:87`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\ops\cast.cpp:89`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\llvm_types.cpp:471`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\llvm_types.cpp:472`
- `HelloUltraviolet\Fixtures\ArtifactProjects\CastPanic\Source\Main.uv:1`
- `HelloUltraviolet\Fixtures\ArtifactProjects\CastPanic\Source\Main.uv:3`
- `HelloUltraviolet\Fixtures\ArtifactProjects\CastPanic\Source\Main.uv:4`
- `HelloUltraviolet\Fixtures\ArtifactProjects\CastPanic\Source\Main.uv:9`
- `HelloUltraviolet\Source\Audit\Catalog\Expressions\CastAndTransmuteExpressions.uv:299`
- `HelloUltraviolet\Source\Audit\Catalog\Expressions\CastAndTransmuteExpressions.uv:303`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\ArtifactProjects\Coverage.uv:405`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\ArtifactProjects\Coverage.uv:411`
- `HelloUltraviolet\Source\Audit\ExpressionsWorkstreamArtifactExecution.uv:191`
- `HelloUltraviolet\Source\Audit\ExpressionsWorkstreamArtifactExecution.uv:205`
- `HelloUltraviolet\Source\Audit\ExpressionsWorkstreamArtifactExecution.uv:213`

Observed behavior:

`LowerCast` emits `IRCheckCast`, `PanicFollowup`, and then `IRCast` for
non-dynamic casts. The LLVM `IRCheckCast` visitor only handles integer-to-integer
casts where the destination bit width is smaller than the source bit width. It
returns without emitting a check when the destination is not an integer, when the
source is not an integer, or when the destination width is greater than or equal
to the source width. Because `char` lowers to `i32`, a `u32` to `char` cast has
equal source and destination widths and bypasses the scalar-value check. The
subsequent `IRCast` integer path then forwards the value with `CreateIntCast`.

Expected behavior:

The spec says `CastVal-U32-Char` is defined only when the `u32` value is a
Unicode scalar. `CastNeedsCheck` explicitly includes `u32` to `char`, and
`CheckCastIR` must panic with reason `Cast` exactly when `CastVal` is undefined.
Invalid code points such as `1114112u32` therefore must take the
`EvalSigma-Cast-Panic` path before the cast result is used.

Impact:

Invalid Unicode scalar values can become `char` values in generated code instead
of raising the specified runtime cast panic. That breaks `char` validity
invariants, can contaminate downstream layout and FFI assumptions, and can make
the emitted program return normally where the spec requires `Ctrl(Panic)`.

HelloUltraviolet coverage status:

`HelloUltraviolet\Fixtures\ArtifactProjects\CastPanic\Source\Main.uv` already
uses `1114112u32 as char` as the runtime panic witness, and the expressions
workstream test expects `rule.16.EvalSigma-Cast-Panic` from that artifact. A
`uvc test` run covering `expressions workstream cast panic exercise` should
therefore detect this backend gap rather than needing a new fixture.

### UV-AUDIT-0249: Transmute invalid-pattern lowering emits no runtime check

Severity: High

Status: Locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:16623`
- `Docs\SPECIFICATION.md:16625`
- `Docs\SPECIFICATION.md:16626`
- `Docs\SPECIFICATION.md:16628`
- `Docs\SPECIFICATION.md:16630`
- `Docs\SPECIFICATION.md:16631`
- `Docs\SPECIFICATION.md:16633`
- `Docs\SPECIFICATION.md:16635`
- `Docs\SPECIFICATION.md:18015`
- `Docs\SPECIFICATION.md:27600`
- `Docs\SPECIFICATION.md:27603`
- `Docs\SPECIFICATION.md:28789`
- `Docs\SPECIFICATION.md:28794`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\05_codegen\checks\checks.cpp:777`
- `Bootstrap\Ultraviolet\src\05_codegen\checks\checks.cpp:781`
- `Bootstrap\Ultraviolet\src\05_codegen\checks\checks.cpp:782`
- `Bootstrap\Ultraviolet\src\05_codegen\checks\checks.cpp:795`
- `Bootstrap\Ultraviolet\src\05_codegen\checks\checks.cpp:805`
- `Bootstrap\Ultraviolet\src\05_codegen\checks\checks.cpp:807`
- `Bootstrap\Ultraviolet\src\05_codegen\checks\checks.cpp:812`
- `Bootstrap\Ultraviolet\src\05_codegen\checks\checks.cpp:818`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir_instruction_visitor.h:121`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir_instruction_visitor.h:135`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\ops\transmute.cpp:9`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\ops\transmute.cpp:17`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\ops\transmute.cpp:25`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\ops\transmute.cpp:43`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\ops\transmute.cpp:56`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\ops\transmute.cpp:62`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\ops\transmute.cpp:67`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\ops\transmute.cpp:76`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\transmute_expr.cpp:109`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\transmute_expr.cpp:123`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\transmute_expr.cpp:375`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\transmute_expr.cpp:377`
- `HelloUltraviolet\Fixtures\DiagnosticSource\Expressions\ValidTransmuteTarget\Source\Main.uv:1`
- `HelloUltraviolet\Fixtures\DiagnosticSource\Expressions\ValidTransmuteTarget\Source\Main.uv:4`
- `HelloUltraviolet\Fixtures\DiagnosticSource\Expressions\ValidTransmuteTarget\Expected.uv:3`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\DiagnosticSource\Expressions.uv:11`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\DiagnosticSource\Expressions.uv:15`
- `HelloUltraviolet\Source\Audit\Catalog\Expressions\CastAndTransmuteExpressions.uv:155`
- `HelloUltraviolet\Source\Audit\Catalog\Expressions\CastAndTransmuteExpressions.uv:159`
- `HelloUltraviolet\Source\Audit\Catalog\Expressions\CastAndTransmuteExpressions.uv:307`
- `HelloUltraviolet\Source\Audit\Catalog\Expressions\CastAndTransmuteExpressions.uv:308`

Observed behavior:

`LowerTransmute` records both `Lower-Transmute` and `Lower-Transmute-Err`, lowers
the operand, checks only source and target size equality, and then emits
`IRTransmute` directly. It does not test `InvalidPatterns(T_2)`, does not emit a
`CheckTransmuteIR` equivalent, and does not insert a `PanicFollowup` before the
transmute result is used. The IR visitor table has `IRCheckCast` and
`IRTransmute` handlers, but no `IRCheckTransmute` handler. The `IRTransmute`
visitor simply bitcasts or memory-reinterprets equal-sized values and stores the
result.

Expected behavior:

When the target type admits invalid bit patterns, `Lower-Transmute-Err` requires
`SeqIR(IR_e, CheckTransmuteIR(T_2, v), PanicCheckIR, TransmuteIR(...))`.
`CheckTransmuteIR` must panic with reason `Cast` when the source bits are not a
`ValidValue` for the target. For example, `bool` only admits byte values `0x00`
and `0x01`, and `char` only admits Unicode scalar values, so transmutes into
those targets need a runtime validity check after the warning path.

Impact:

Unsafe code can transmute invalid bit patterns into types whose validity the
rest of the compiler assumes, such as `bool` or `char`. That can make later
optimizations, layout reasoning, FFI calls, and control-flow decisions observe
values the spec says must be rejected by a runtime `Cast` panic.

HelloUltraviolet fixture gap:

HelloUltraviolet has a diagnostic-source fixture for
`transmute<u8, bool>(2u8)` and expects `W-SAF-0100`, which proves the warning
path. The catalog also credits `TransmuteVal` from accepted references that use
valid transmutes. There is no artifact project or `uvc` runtime exercise proving
that the same invalid-pattern transmute emits and trips the required
`CheckTransmuteIR` panic path.

### UV-AUDIT-0250: Rejected `#test` arguments credit `AttrArgsOk(test, args)`

Severity: Medium

Status: Agent-reported and locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:7258`
- `Docs\SPECIFICATION.md:7260`
- `Docs\SPECIFICATION.md:7261`
- `Docs\SPECIFICATION.md:7262`
- `Docs\SPECIFICATION.md:7263`
- `Docs\SPECIFICATION.md:7264`
- `Docs\SPECIFICATION.md:7348`
- `Docs\SPECIFICATION.md:7349`
- `Docs\SPECIFICATION.md:7350`
- `Docs\SPECIFICATION.md:7351`
- `Docs\SPECIFICATION.md:7355`

Implementation anchors:

- `Bootstrap\Ultraviolet\include\00_core\assert_spec.h:6`
- `Bootstrap\Ultraviolet\include\00_core\assert_spec.h:10`
- `Bootstrap\Ultraviolet\src\02_source\attributes\attribute_registry.cpp:269`
- `Bootstrap\Ultraviolet\src\02_source\attributes\attribute_registry.cpp:274`
- `Bootstrap\Ultraviolet\src\02_source\attributes\attribute_registry.cpp:279`
- `Bootstrap\Ultraviolet\src\02_source\attributes\attribute_registry.cpp:281`
- `Bootstrap\Ultraviolet\src\02_source\attributes\attribute_registry.cpp:288`
- `Bootstrap\Ultraviolet\src\02_source\attributes\attribute_registry.cpp:290`
- `Bootstrap\Ultraviolet\src\02_source\attributes\attribute_registry.cpp:299`
- `Bootstrap\Ultraviolet\src\02_source\attributes\attribute_registry.cpp:304`
- `Bootstrap\Ultraviolet\src\02_source\attributes\attribute_registry.cpp:311`
- `Bootstrap\Ultraviolet\src\02_source\attributes\attribute_registry.cpp:313`
- `Bootstrap\Ultraviolet\src\02_source\attributes\attribute_registry.cpp:320`
- `Bootstrap\Ultraviolet\src\02_source\attributes\attribute_registry.cpp:322`
- `Bootstrap\Ultraviolet\src\02_source\attributes\attribute_registry.cpp:327`
- `Bootstrap\Ultraviolet\src\02_source\attributes\attribute_registry.cpp:329`
- `Bootstrap\Ultraviolet\src\02_source\attributes\attribute_registry.cpp:337`
- `Bootstrap\Ultraviolet\src\02_source\attributes\attribute_registry.cpp:338`
- `HelloUltraviolet\Fixtures\RejectedSource\Attributes\TestMalformedArgument\Expected.uv:4`
- `HelloUltraviolet\Fixtures\RejectedSource\Attributes\TestDuplicateName\Expected.uv:4`
- `HelloUltraviolet\Fixtures\RejectedSource\Attributes\TestMalformedCoverage\Expected.uv:4`
- `HelloUltraviolet\Fixtures\RejectedSource\Attributes\TestUnknownCoverageReference\Expected.uv:4`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\RejectedSource\Attributes.uv:84`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\RejectedSource\Attributes.uv:90`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\RejectedSource\Attributes.uv:104`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\RejectedSource\Attributes.uv:110`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\RejectedSource\Attributes.uv:124`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\RejectedSource\Attributes.uv:130`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\RejectedSource\Attributes.uv:204`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\RejectedSource\Attributes.uv:210`
- `HelloUltraviolet\Source\Audit\AttributesMetadataArtifactExecution.uv:799`
- `HelloUltraviolet\Source\Audit\AttributesMetadataArtifactExecution.uv:804`
- `HelloUltraviolet\Source\Audit\AttributesMetadataArtifactExecution.uv:808`
- `HelloUltraviolet\Source\Audit\AttributesMetadataArtifactExecution.uv:812`
- `HelloUltraviolet\Source\Audit\AttributesMetadataArtifactExecution.uv:816`

Observed behavior:

`ValidateTestAttributeArgs` records `def.TestAttributeArgsOk` as soon as it sees
a `#test` attribute. The subsequent branches can then reject malformed
arguments, duplicate `name` arguments, malformed `covers(...)` arguments, or
unknown coverage references. The rejected-source fixtures for those cases all
expect the public diagnostic code and the broad `def.TestAttributeArgsOk`
obligation in their expected metadata.

Expected behavior:

`AttrArgsOk(test, args)` holds exactly when every listed argument condition is
satisfied. Rejection cases should credit the violated diagnostic path or a
clause-specific obligation, not the whole accepted-argument predicate before the
predicate is known to hold.

Impact:

The HelloUltraviolet rejected-source surface can report coverage for
source-native test argument validity while exercising only invalid argument
branches. That weakens the obligation ledger evidence for accepted `#test`
argument semantics and can hide missing accepted-case coverage.

Duplicate review:

This is separate from `UV-AUDIT-0077`, which covers duplicate `name` arguments
split across multiple `#test` attributes, and from `UV-AUDIT-0179`, which covers
empty display-name acceptance. It is also narrower than `UV-AUDIT-0213`, which
covers diagnostic-code and obligation pairing; here the same implementation
records an accepted predicate before the predicate has succeeded.

### UV-AUDIT-0251: Diagnostic sync validation scans strings instead of normative diagnostic rows

Severity: Medium

Status: Agent-reported and locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:466`
- `Docs\SPECIFICATION.md:467`
- `Docs\SPECIFICATION.md:474`
- `Docs\SPECIFICATION.md:475`
- `Docs\SPECIFICATION.md:483`
- `Docs\SPECIFICATION.md:484`
- `Docs\SPECIFICATION.md:488`
- `Docs\SPECIFICATION.md:492`
- `Docs\SPECIFICATION.md:30461`
- `Docs\SPECIFICATION.md:30463`

Implementation anchors:

- `Bootstrap\Ultraviolet\tools\validate_diagnostic_spec_sync.py:13`
- `Bootstrap\Ultraviolet\tools\validate_diagnostic_spec_sync.py:18`
- `Bootstrap\Ultraviolet\tools\validate_diagnostic_spec_sync.py:19`
- `Bootstrap\Ultraviolet\tools\validate_diagnostic_spec_sync.py:20`
- `Bootstrap\Ultraviolet\tools\validate_diagnostic_spec_sync.py:21`
- `Bootstrap\Ultraviolet\tools\validate_diagnostic_spec_sync.py:22`
- `Bootstrap\Ultraviolet\tools\validate_diagnostic_spec_sync.py:62`
- `Bootstrap\Ultraviolet\tools\validate_diagnostic_spec_sync.py:63`
- `Bootstrap\Ultraviolet\tools\validate_diagnostic_spec_sync.py:65`
- `Bootstrap\Ultraviolet\tools\validate_diagnostic_spec_sync.py:78`
- `Bootstrap\Ultraviolet\tools\validate_diagnostic_spec_sync.py:89`
- `Bootstrap\Ultraviolet\tools\validate_diagnostic_spec_sync.py:90`
- `Bootstrap\Ultraviolet\tools\validate_diagnostic_spec_sync.py:91`
- `Bootstrap\Ultraviolet\tools\validate_diagnostic_spec_sync.py:95`

Observed behavior:

`validate_diagnostic_spec_sync.py` builds `spec_codes` by matching any diagnostic
code-shaped string anywhere in the full specification text. It builds
`source_codes` from a small set of source string patterns, fails only when a
source code is absent from that broad spec string set, and checks only that the
generated diagnostic registry contains at least one row. The command currently
passes with `source_codes=51`, `spec_codes=502`, and `registry_rows=1004`.

Expected behavior:

Diagnostic synchronization must be based on the normative diagnostic tables that
define `SeverityColumn`, `ConditionColumn`, and `SpecCode`. Appendix A is
informative only and must not define those mappings. A sync gate should compare
the generated registry and rule-code mappings to the owning construct-section
rows, including code, severity, and condition ownership, rather than treating
arbitrary code-shaped strings as proof of synchronization.

Impact:

The green diagnostic sync check can coexist with missing, extra, stale, or
mis-mapped diagnostics in the generated registry or compiler sources. It gives
release and audit tooling weaker evidence than the spec's diagnostic-code model
requires, and can let diagnostic drift pass CI.

Duplicate review:

Existing audit entries cover specific diagnostic-code mismatches and unreachable
diagnostics. This finding is about the validation gate itself: it can pass
without proving that the compiler and generated registry match the normative
diagnostic tables.

### UV-AUDIT-0252: Current-module imports are rejected as missing modules

Severity: Medium

Status: Agent-reported and locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:7604`
- `Docs\SPECIFICATION.md:7605`
- `Docs\SPECIFICATION.md:7627`
- `Docs\SPECIFICATION.md:7629`
- `Docs\SPECIFICATION.md:7630`
- `Docs\SPECIFICATION.md:7632`
- `Docs\SPECIFICATION.md:7655`
- `Docs\SPECIFICATION.md:7657`
- `Docs\SPECIFICATION.md:8376`
- `Docs\SPECIFICATION.md:8377`
- `Docs\SPECIFICATION.md:8379`
- `Docs\SPECIFICATION.md:8386`
- `Docs\SPECIFICATION.md:8387`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\resolve\resolve_imports.cpp:43`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\resolve_imports.cpp:54`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\resolve_imports.cpp:55`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\resolve_imports.cpp:58`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\resolve_imports.cpp:62`
- `Bootstrap\Ultraviolet\src\02_source\module_paths.cpp:13`
- `Bootstrap\Ultraviolet\src\02_source\module_paths.cpp:24`
- `Bootstrap\Ultraviolet\src\02_source\module_paths.cpp:25`
- `Bootstrap\Ultraviolet\src\02_source\module_paths.cpp:26`
- `Bootstrap\Ultraviolet\src\02_source\module_paths.cpp:38`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\collect_toplevel.cpp:755`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\collect_toplevel.cpp:762`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\collect_toplevel.cpp:766`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\collect_toplevel.cpp:769`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\resolve_module.cpp:219`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\resolve_module.cpp:220`
- `HelloUltraviolet\Source\Reference\Modules\Imports.uv:3`
- `HelloUltraviolet\Source\Reference\Modules\Imports.uv:4`
- `HelloUltraviolet\Source\Reference\Modules\Imports.uv:54`
- `HelloUltraviolet\Source\Reference\Names\Imports.uv:27`
- `HelloUltraviolet\Fixtures\RejectedSource\Modules\MissingImport\Source\Main.uv:1`
- `HelloUltraviolet\Fixtures\RejectedSource\Modules\MissingImport\Source\Main.uv:3`
- `HelloUltraviolet\Fixtures\RejectedSource\Modules\MissingImport\Expected.uv:3`
- `HelloUltraviolet\Fixtures\RejectedSource\Modules\MissingImport\Expected.uv:5`

Observed behavior:

`ValidateImportDecl` rejects an import immediately when `import.path` equals the
current module path and returns `Resolve-Import-Err`. That bypasses the canonical
`ResolveImportModulePath` helper, whose direct-resolution branch accepts any path
present in `module_names`. Later top-level binding also uses
`ResolveImportModulePath` and would create a module alias for a resolved import
path, but the earlier validation rejects the current-module case first. The
diagnostic mapping reports that rejection as `E-MOD-1202`, the non-existent
module diagnostic.

Expected behavior:

The spec's `Resolve-Import-Direct` rule succeeds when `StringOfPath(path)` is in
`AllModuleNames`. It does not exclude the current module path. A current-module
import should therefore resolve and bind the requested module alias under
`ImportNames`, or the specification needs an explicit self-import prohibition
and a diagnostic that does not claim the existing module is missing.

Impact:

Conforming source can be rejected, and the emitted diagnostic says the import
names a non-existent module even when it names the module currently being
compiled. This also leaves current-module alias behavior outside the verified
name-resolution surface.

HelloUltraviolet fixture gap:

The module import reference imports a sibling module with plain, internal
aliased, and private aliased imports. The names import reference uses abstract
counter values rather than real import resolution. The rejected `MissingImport`
fixture covers a genuinely absent module path. No `uvc` fixture imports the
current module and then uses the resulting alias.

### UV-AUDIT-0253: Link flag conformance is claimed without matching `LinkFlagsFor`

Severity: Medium

Status: Subagent-reported and locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:1503`
- `Docs\SPECIFICATION.md:1505`
- `Docs\SPECIFICATION.md:1509`
- `Docs\SPECIFICATION.md:1529`
- `Docs\SPECIFICATION.md:27745`
- `Docs\SPECIFICATION.md:27746`
- `Docs\SPECIFICATION.md:27747`
- `Docs\SPECIFICATION.md:27748`
- `Docs\SPECIFICATION.md:27749`
- `Docs\SPECIFICATION.md:27750`
- `Docs\SPECIFICATION.md:27751`
- `Docs\SPECIFICATION.md:27752`
- `Docs\SPECIFICATION.md:27753`
- `Docs\SPECIFICATION.md:27754`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\01_project\targets\target_platform.cpp:590`
- `Bootstrap\Ultraviolet\src\01_project\targets\target_platform.cpp:600`
- `Bootstrap\Ultraviolet\src\01_project\targets\target_platform.cpp:620`
- `Bootstrap\Ultraviolet\src\01_project\targets\target_platform.cpp:627`
- `Bootstrap\Ultraviolet\src\01_project\targets\target_platform.cpp:630`
- `Bootstrap\Ultraviolet\src\01_project\targets\target_platform.cpp:638`
- `Bootstrap\Ultraviolet\src\01_project\targets\target_platform.cpp:694`
- `Bootstrap\Ultraviolet\src\01_project\targets\target_platform.cpp:718`
- `Bootstrap\Ultraviolet\src\01_project\targets\target_platform.cpp:722`
- `Bootstrap\Ultraviolet\src\01_project\targets\target_platform.cpp:727`
- `Bootstrap\Ultraviolet\src\01_project\targets\target_platform.cpp:801`
- `Bootstrap\Ultraviolet\src\01_project\link.cpp:1852`
- `Bootstrap\Ultraviolet\src\01_project\link.cpp:1854`
- `Bootstrap\Ultraviolet\src\01_project\link.cpp:1858`
- `Bootstrap\Ultraviolet\src\01_project\link.cpp:1863`
- `Bootstrap\Ultraviolet\src\01_project\link.cpp:2282`
- `HelloUltraviolet\Source\Audit\ArtifactProjectExecution.uv:1502`
- `HelloUltraviolet\Source\Audit\ArtifactProjectExecution.uv:1508`
- `HelloUltraviolet\Source\Audit\ArtifactProjectExecution.uv:1514`
- `HelloUltraviolet\Source\Audit\ArtifactProjectExecution.uv:1798`
- `HelloUltraviolet\Source\Audit\ArtifactProjectExecution.uv:1812`

Observed behavior:

`BuildTargetLinkArgs` constructs linker argument vectors with target-specific
flags outside the spec's `LinkFlagsFor` tables. Windows links add `/NOLOGO`,
manifest flags, stack flags, `/MAP:...`, `/LIBPATH:...`, and exports. ELF links
add `--undefined=_start`, `-rpath=$ORIGIN`, optional `--version-script=...`, and
toolchain library search behavior. Darwin shared links add exported-symbol
arguments. The `def.LinkFlags` conformance record stores only summary fields
such as `arg_count`, `has_out`, and `has_import_lib`, then `def.LinkArgsOk` is
recorded for the link without comparing the actual flag vector to
`LinkFlagsFor`.

Expected behavior:

`LinkArgsOk(P, L, out, imp)` requires `LinkFlags(P) =
LinkFlagsFor(SelectedTargetProfile, LinkMode(P), LinkOutputPath(P),
LinkImportLibOpt(P))`. The linker invocation should use exactly the specified
target flag relation, or the specification should define each additional flag
before `def.LinkArgsOk` is recorded for the real command.

Impact:

Conformance logs can claim a spec-valid linker invocation while artifact shape,
loader behavior, export behavior, and diagnostics are controlled by flags that
are not part of the normative relation. This is separate from `UV-AUDIT-0116`,
which covers extra linker inputs rather than unmodeled linker flags.

HelloUltraviolet fixture gap:

`ArtifactProjectExecution.uv` checks that link artifacts and conformance records
exist, but it does not compare the actual linker argument vector against
`LinkFlagsFor` or reject target-only flags absent from the specification.

### UV-AUDIT-0254: Generated map artifacts are outside the spec artifact set

Severity: Medium

Status: Subagent-reported and locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:621`
- `Docs\SPECIFICATION.md:628`
- `Docs\SPECIFICATION.md:630`
- `Docs\SPECIFICATION.md:631`
- `Docs\SPECIFICATION.md:1269`
- `Docs\SPECIFICATION.md:1270`
- `Docs\SPECIFICATION.md:1271`
- `Docs\SPECIFICATION.md:1273`
- `Docs\SPECIFICATION.md:1276`
- `Docs\SPECIFICATION.md:1286`
- `Docs\SPECIFICATION.md:1503`
- `Docs\SPECIFICATION.md:1505`
- `Docs\SPECIFICATION.md:26991`
- `Docs\SPECIFICATION.md:27747`
- `Docs\SPECIFICATION.md:27748`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\01_project\targets\target_platform.cpp:340`
- `Bootstrap\Ultraviolet\src\01_project\targets\target_platform.cpp:348`
- `Bootstrap\Ultraviolet\src\01_project\targets\target_platform.cpp:603`
- `Bootstrap\Ultraviolet\src\01_project\targets\target_platform.cpp:604`
- `Bootstrap\Ultraviolet\src\01_project\targets\target_platform.cpp:627`
- `Bootstrap\Ultraviolet\src\01_project\targets\target_platform.cpp:724`
- `Bootstrap\Ultraviolet\src\01_project\targets\target_platform.cpp:727`
- `Bootstrap\Ultraviolet\src\01_project\outputs.cpp:395`
- `Bootstrap\Ultraviolet\src\01_project\outputs.cpp:408`
- `Bootstrap\Ultraviolet\src\01_project\outputs.cpp:550`
- `Bootstrap\Ultraviolet\src\01_project\outputs.cpp:561`
- `Bootstrap\Ultraviolet\src\01_project\outputs.cpp:565`
- `Bootstrap\Ultraviolet\src\06_driver\output_pipeline.cpp:2193`
- `Bootstrap\Ultraviolet\src\06_driver\output_pipeline.cpp:2198`
- `Bootstrap\Ultraviolet\src\06_driver\output_pipeline.cpp:2401`
- `HelloUltraviolet\Source\Reference\Projects\OutputArtifacts.uv:9`
- `HelloUltraviolet\Source\Audit\ArtifactProjectExecution.uv:38`
- `HelloUltraviolet\Source\Audit\ArtifactProjectExecution.uv:6873`
- `HelloUltraviolet\Source\Audit\ArtifactProjectExecution.uv:6876`
- `HelloUltraviolet\Source\Audit\AcceptedProjectExecution.uv:2651`
- `HelloUltraviolet\Source\Audit\AcceptedProjectExecution.uv:2656`

Observed behavior:

Windows executable and shared-library links always request a `.map` file through
`/MAP:...`; POSIX shared-library export filtering writes an `.exports.map`
version-script file. `OutputArtifacts` carries a `map_file`, and incremental
reuse requires it to exist when `MapPath` is present. HelloUltraviolet asserts
map-file presence and contents. However, `RequiredOutputs` includes only object
paths, optional IR paths, the primary artifact, and the optional import library.
`ArtifactsOf(P)` likewise contains only objects, IRs, and the primary artifact.

Expected behavior:

If these generated files are required artifacts, the specification should define
their paths and membership in `RequiredOutputs`, output hygiene,
`ArtifactsOf(P)`, output summaries, incremental reuse, and target link flags. If
they are only implementation temporaries, `uvc` should not expose them through
the output artifact contract or require their presence for artifact reuse.

Impact:

The compiler emits and depends on files the specification does not model.
Missing or stale map artifacts can affect output-pipeline reuse even though the
spec artifact set has no map member. The HelloUltraviolet surface currently
locks in that non-spec artifact behavior.

HelloUltraviolet fixture gap:

Project output references and artifact executions assert `.map` behavior, but
there is no fixture that reconciles those files with `RequiredOutputs(P)` or
`ArtifactsOf(P)`.

### UV-AUDIT-0255: `ValidValue` fallback rejects spec-defined composite value bits

Severity: Medium

Status: Subagent-reported and locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:9922`
- `Docs\SPECIFICATION.md:10233`
- `Docs\SPECIFICATION.md:10442`
- `Docs\SPECIFICATION.md:10567`
- `Docs\SPECIFICATION.md:10951`
- `Docs\SPECIFICATION.md:27615`
- `Docs\SPECIFICATION.md:27616`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\layout\layout_value_bits.cpp:1556`
- `Bootstrap\Ultraviolet\src\04_analysis\layout\layout_value_bits.cpp:1560`
- `Bootstrap\Ultraviolet\src\04_analysis\layout\layout_value_bits.cpp:1579`
- `Bootstrap\Ultraviolet\src\04_analysis\layout\layout_value_bits.cpp:1644`
- `Bootstrap\Ultraviolet\src\04_analysis\layout\layout_value_bits.cpp:1649`
- `Bootstrap\Ultraviolet\src\04_analysis\layout\layout_value_bits.cpp:1650`
- `Bootstrap\Ultraviolet\src\04_analysis\layout\layout_value_bits.cpp:1656`
- `Bootstrap\Ultraviolet\src\04_analysis\layout\layout_value_bits.cpp:2042`
- `Bootstrap\Ultraviolet\src\04_analysis\layout\layout_value_bits.cpp:2052`
- `Bootstrap\Ultraviolet\src\04_analysis\layout\layout_value_bits.cpp:2058`
- `HelloUltraviolet\Source\Audit\Catalog\CommonLoweringProgramLifecycleAndBackend\LayoutAndAbiFramework.uv:182`
- `HelloUltraviolet\Source\Audit\Catalog\ConcreteDataTypes\Records.uv:461`
- `HelloUltraviolet\Source\Audit\Catalog\ConcreteDataTypes\Enums.uv:515`
- `HelloUltraviolet\Source\Audit\Catalog\ConcreteDataTypes\UnionTypes.uv:290`
- `HelloUltraviolet\Source\Audit\Catalog\ConcreteDataTypes\TypeAliases.uv:182`
- `HelloUltraviolet\Source\Audit\Catalog\ModalAndSpecialTypes\ModalDeclarations.uv:740`

Observed behavior:

`ValidValue` manually accepts primitives, permissions, pointers, raw pointers,
tuples, arrays, slices, ranges, dynamic values, strings, and bytes. Remaining
type forms then fall through to `false`. That rejects `TypePath` records,
`TypePath` aliases, union values, and modal-state records even when their bytes
are produced by spec-defined `ValueBits` rules. The public `ValueBits` path in
this area reaches enum `TypePath` lookup, while the record helper exists but is
not called before `ValidValue` rejects the composite type.

Expected behavior:

For every non-primitive, non-pointer, and non-raw-pointer type, `ValidValue(T,
bits)` holds exactly when some value has `ValueBits(T, v) = bits`. The spec
defines those bits for records, enums, aliases, unions, and modal states.

Impact:

The layout validity judgment is incomplete for composite types. Conformance
checks or safety paths that rely on `ValidValue(T, bits)` can reject valid
composite layouts or fail to model invalid-pattern behavior consistently. This
is distinct from `UV-AUDIT-0249`, which covers missing runtime transmute checks.

HelloUltraviolet fixture gap:

The catalog records `def.24.ValidValueFallback` and separate composite
`ValueBits` obligations for records, enums, unions, aliases, and modal states,
but it does not exercise `ValidValue` over those composite bit patterns.

### UV-AUDIT-0256: Static binding lists drop enum, modal, and range pattern names

Severity: High

Status: Locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:5005`
- `Docs\SPECIFICATION.md:5019`
- `Docs\SPECIFICATION.md:5029`
- `Docs\SPECIFICATION.md:5033`
- `Docs\SPECIFICATION.md:5037`
- `Docs\SPECIFICATION.md:7882`
- `Docs\SPECIFICATION.md:7902`
- `Docs\SPECIFICATION.md:7912`
- `Docs\SPECIFICATION.md:7914`
- `Docs\SPECIFICATION.md:7920`
- `Docs\SPECIFICATION.md:28340`
- `Docs\SPECIFICATION.md:28359`
- `Docs\SPECIFICATION.md:28379`
- `Docs\SPECIFICATION.md:28441`
- `Docs\SPECIFICATION.md:28468`
- `Docs\SPECIFICATION.md:28469`
- `Docs\SPECIFICATION.md:28512`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\resolve\collect_toplevel.cpp:582`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\collect_toplevel.cpp:621`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\collect_toplevel.cpp:629`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\collect_toplevel.cpp:654`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\collect_toplevel.cpp:668`
- `Bootstrap\Ultraviolet\src\05_codegen\globals\globals.cpp:140`
- `Bootstrap\Ultraviolet\src\05_codegen\globals\globals.cpp:161`
- `Bootstrap\Ultraviolet\src\05_codegen\globals\globals.cpp:168`
- `Bootstrap\Ultraviolet\src\05_codegen\globals\globals.cpp:180`
- `Bootstrap\Ultraviolet\src\05_codegen\globals\globals.cpp:195`
- `Bootstrap\Ultraviolet\src\05_codegen\globals\globals.cpp:328`
- `Bootstrap\Ultraviolet\src\05_codegen\globals\globals.cpp:416`
- `Bootstrap\Ultraviolet\src\05_codegen\globals\globals.cpp:587`
- `Bootstrap\Ultraviolet\src\05_codegen\globals\globals.cpp:628`
- `Bootstrap\Ultraviolet\src\05_codegen\globals\init.cpp:188`
- `Bootstrap\Ultraviolet\src\05_codegen\globals\init.cpp:321`
- `Bootstrap\Ultraviolet\src\05_codegen\globals\init.cpp:324`
- `Bootstrap\Ultraviolet\src\05_codegen\globals\init.cpp:432`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\pattern\pattern_common.cpp:683`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\pattern\pattern_common.cpp:752`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\pattern\pattern_common.cpp:886`

Observed behavior:

The semantic `PatNames` implementation descends enum tuple payloads, enum record
payloads, modal record payloads, and range bounds. The globals implementation
defines a separate `CollectPatternNames` for `StaticBindList`; that collector
only handles identifiers, typed identifiers, tuple patterns, and record fields,
then treats wildcard and all other pattern variants as non-binding.

This means a module-scope static such as an enum payload pattern, modal payload
pattern, or range pattern whose bound names survive semantic analysis can lower
bindings through `PatternBindingValuesInOrder` and `StaticStoreIR`, while
`StaticBindList` reports no matching names for the same binding form.

Expected behavior:

`StaticBindList(binding)` must be exactly `PatNames(pat)` for every
`StaticDecl` binding pattern. The same name set is then used by `StaticItemOf`,
`StaticType`, `StaticBindInfo`, `Emit-Static-Multi`, `StaticStoreIR`, static
deinit order, `StaticBindOrder`, and hosted/shared-library state-symbol
classification.

Impact:

Static destructuring through enum, modal, or range patterns can lose storage
declarations, symbol resolution, type metadata, deinitialization, initialization
ordering, and hosted/shared-state classification for names the language
semantics already introduced. The initializer path may still produce stores for
those bindings, but the corresponding globals and lookup metadata are omitted
because the global output path enumerates `StaticBindList` independently.

HelloUltraviolet fixture gap:

Pattern fixtures exercise local tuple/record destructuring and case-pattern
coverage, while static output fixtures focus on simple static bindings. No
fixture covers module-scope enum, modal, or range-pattern destructuring and then
checks the emitted globals, initialization stores, and deinitialization order.

### UV-AUDIT-0257: Invalid UTF-8 scalar-domain sequences can throw before `E-SRC-0101`

Severity: Medium

Status: Subagent-reported and locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:1765`
- `Docs\SPECIFICATION.md:1802`
- `Docs\SPECIFICATION.md:1965`
- `Docs\SPECIFICATION.md:1968`
- `Docs\SPECIFICATION.md:2007`
- `Docs\SPECIFICATION.md:2044`
- `Docs\SPECIFICATION.md:2046`
- `Docs\SPECIFICATION.md:2675`
- `Docs\SPECIFICATION.md:8169`
- `Docs\SPECIFICATION.md:8174`

Implementation anchors:

- `Bootstrap\Ultraviolet\include\00_core\source_text.h:17`
- `Bootstrap\Ultraviolet\include\00_core\source_text.h:19`
- `Bootstrap\Ultraviolet\include\00_core\source_text.h:33`
- `Bootstrap\Ultraviolet\src\00_core\source_load.cpp:115`
- `Bootstrap\Ultraviolet\src\00_core\source_load.cpp:160`
- `Bootstrap\Ultraviolet\src\00_core\source_load.cpp:180`
- `Bootstrap\Ultraviolet\src\00_core\source_load.cpp:297`
- `Bootstrap\Ultraviolet\src\00_core\source_load.cpp:361`
- `Bootstrap\Ultraviolet\src\00_core\source_load.cpp:449`
- `Bootstrap\Ultraviolet\src\00_core\source_load.cpp:450`
- `Bootstrap\Ultraviolet\src\00_core\source_load.cpp:454`
- `Bootstrap\Ultraviolet\src\02_source\parser\parse_modules.cpp:976`
- `HelloUltraviolet\Fixtures\RejectedSource\SourceText\InvalidUTF8\Expected.uv:3`

Observed behavior:

`DecodeUtf8Internal` constructs a `UnicodeScalar` before validating the decoded
scalar domain in the three-byte and four-byte branches. Encoded surrogate bytes
such as `ED A0 80` and out-of-range scalar bytes such as `F4 90 80 80` therefore
reach `UnicodeScalar::Validate`, which throws `"invalid Unicode scalar"` before
`Decode` can return `ok = false`.

Expected behavior:

All invalid UTF-8 byte sequences, including sequences that numerically decode to
surrogate or out-of-range scalar values, must follow `Step-Decode-Err`,
`NoSpan-Decode`, `LoadSource-Err`, and the `ParseModule` load-source
short-circuit. The emitted diagnostic must be `E-SRC-0101`.

Impact:

Specific malformed source bytes can take an unclassified exception path instead
of producing the required source-loading diagnostic. That bypasses the structured
diagnostic stream and the `ParseModule` short-circuit proof point for those
inputs.

HelloUltraviolet fixture gap:

`HelloUltraviolet\Fixtures\RejectedSource\SourceText\InvalidUTF8` exercises the
generic invalid-UTF-8 diagnostic with an invalid leading byte. It does not cover
encoded surrogate scalars or scalar values greater than `0x10FFFF`.

### UV-AUDIT-0258: Enum-pattern payload locals are missed by closure and spawn capture lowering

Severity: High

Status: Locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:5029`
- `Docs\SPECIFICATION.md:5033`
- `Docs\SPECIFICATION.md:17638`
- `Docs\SPECIFICATION.md:17805`
- `Docs\SPECIFICATION.md:17807`
- `Docs\SPECIFICATION.md:17820`
- `Docs\SPECIFICATION.md:17902`
- `Docs\SPECIFICATION.md:17932`
- `Docs\SPECIFICATION.md:17937`
- `Docs\SPECIFICATION.md:17942`
- `Docs\SPECIFICATION.md:18397`
- `Docs\SPECIFICATION.md:18402`
- `Docs\SPECIFICATION.md:18416`
- `Docs\SPECIFICATION.md:18426`
- `Docs\SPECIFICATION.md:18431`
- `Docs\SPECIFICATION.md:22278`

Implementation anchors:

- `Bootstrap\Ultraviolet\include\02_source\ast\nodes\ast_patterns.h:76`
- `Bootstrap\Ultraviolet\include\02_source\ast\nodes\ast_patterns.h:81`
- `Bootstrap\Ultraviolet\include\02_source\ast\nodes\ast_patterns.h:86`
- `Bootstrap\Ultraviolet\include\02_source\ast\ast_common.h:146`
- `Bootstrap\Ultraviolet\include\02_source\ast\ast_common.h:151`
- `Bootstrap\Ultraviolet\include\02_source\ast\ast_common.h:155`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\stmt_common.cpp:681`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\stmt_common.cpp:709`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\closure_expr.cpp:139`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\closure_expr.cpp:161`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\closure_expr.cpp:166`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\closure_expr.cpp:172`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\closure_expr.cpp:350`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\closure_expr.cpp:473`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\spawn_expr.cpp:193`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\spawn_expr.cpp:215`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\spawn_expr.cpp:220`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\spawn_expr.cpp:226`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\spawn_expr.cpp:437`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\spawn_expr.cpp:567`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\spawn_expr.cpp:1023`

Observed behavior:

The AST uses `TuplePayloadPattern` and `RecordPayloadPattern` for enum patterns.
The semantic pattern-name collector matches those payload types. The closure and
spawn lowering capture collectors define their own `CollectPatternNames` helpers,
but those helpers test enum pattern payloads against the expression payload
types `EnumPayloadParen` and `EnumPayloadBrace`. Those branches cannot match an
`EnumPattern` payload.

When a closure or spawn body contains a case, if-is, loop, or local binding whose
pattern is an enum payload pattern, the lowering capture collector does not add
the payload-bound names to its local-name scope before visiting the nested body.

Expected behavior:

Enum payload pattern names introduced by `PatNames` are local bindings and must
be excluded from closure/spawn `CaptureSet`. Closure environment fields,
capture offsets, `StoreCapture`, captured-body binding, and spawn captured
environment construction must all use the same `CaptureSet` semantics.

Impact:

Lowering can treat enum-payload locals as free variables. If an outer binding has
the same name, the generated closure or spawned work item can capture the outer
binding instead of the local enum payload. If no outer binding exists, lowering
can disagree with the already-accepted semantic scope and omit an environment
entry that the nested body expects.

HelloUltraviolet fixture gap:

Pattern fixtures exercise enum patterns and closure/spawn fixtures exercise
captures, but there is no fixture where an enum-payload pattern introduces a name
inside a closure or spawn body and then verifies that the nested body uses the
payload-local binding rather than an outer binding with the same name.

### UV-AUDIT-0259: `--dump` project summary reports emitted modules as the selected module list

Severity: Low

Status: Locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:622`
- `Docs\SPECIFICATION.md:624`
- `Docs\SPECIFICATION.md:626`
- `Docs\SPECIFICATION.md:628`
- `Docs\SPECIFICATION.md:1255`
- `Docs\SPECIFICATION.md:1264`
- `Docs\SPECIFICATION.md:1267`
- `Docs\SPECIFICATION.md:1332`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\06_driver\compiler_main.cpp:4211`
- `Bootstrap\Ultraviolet\src\06_driver\compiler_main.cpp:4221`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\assembly_import_graph.cpp:554`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\assembly_import_graph.cpp:649`
- `Bootstrap\Ultraviolet\src\01_project\outputs.cpp:245`
- `Bootstrap\Ultraviolet\src\01_project\outputs.cpp:673`
- `Bootstrap\Ultraviolet\src\01_project\outputs.cpp:708`
- `Bootstrap\Ultraviolet\src\01_project\outputs.cpp:721`
- `HelloUltraviolet\Fixtures\AcceptedProjects\CrossAssemblyImplementation\Ultraviolet.toml:2`
- `HelloUltraviolet\Fixtures\AcceptedProjects\CrossAssemblyImplementation\Ultraviolet.toml:7`
- `HelloUltraviolet\Fixtures\AcceptedProjects\CrossAssemblyImplementation\Ultraviolet.toml:8`
- `HelloUltraviolet\Fixtures\AcceptedProjects\CrossAssemblyImplementation\Source\CrossAssemblyImplementation\Library.uv:3`

Observed behavior:

The `--dump` path builds an output-project projection with
`BuildOutputProjectForAssembly` and passes that projection to `DumpProject`.
That projection starts from `AssemblyProject`, then replaces `modules` with
`ComputeEmitModules`. `DumpProject` uses `project.modules` for both the
`module_list` project-summary field and the output-summary rows.

Running
`.\\Bootstrap\\Ultraviolet\\build\\windows\\out\\uvc.exe build HelloUltraviolet\\Fixtures\\AcceptedProjects\\CrossAssemblyImplementation --assembly CrossAssemblyImplementation --dump --build-progress off`
prints `<module_list, [CrossAssemblyImplementation, ExternalContracts]>`.
`ExternalContracts` is a dependency assembly imported by the selected library.

Expected behavior:

`ProjectSummary(P)` must report `module_list = ModuleList(P)`, while
`OutputSummary(P)` and dump-file expansion use `EmitModuleList(P)` and
`EmitAssemblies(P)`. Dependency modules folded into the selected linkable
assembly therefore belong in output-summary rows and file rows, not in the
selected assembly's `module_list` project-summary field.

Impact:

The command-output contract conflates the selected assembly's declared module
list with the emitted-module set. Tooling that reads `--dump` cannot distinguish
which modules belong to the selected assembly from dependency modules emitted
into that assembly's artifact.

HelloUltraviolet fixture gap:

`CrossAssemblyImplementation` already demonstrates the condition with a
dependency assembly imported by a selected library, but there is no dump-output
fixture asserting that `module_list` stays equal to the selected assembly's
`ModuleList(P)` while dependency modules appear only in output and file rows.

### UV-AUDIT-0260: Rootless executable `main` can pass without an emitted entry stub

Severity: High

Status: Subagent-reported and locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:1381`
- `Docs\SPECIFICATION.md:1383`
- `Docs\SPECIFICATION.md:1393`
- `Docs\SPECIFICATION.md:14120`
- `Docs\SPECIFICATION.md:14128`
- `Docs\SPECIFICATION.md:28615`
- `Docs\SPECIFICATION.md:28717`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\typing\typecheck.cpp:1056`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\typecheck.cpp:1066`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\typecheck.cpp:1126`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\typecheck.cpp:1144`
- `Bootstrap\Ultraviolet\src\06_driver\pipeline.cpp:531`
- `Bootstrap\Ultraviolet\src\06_driver\pipeline.cpp:544`
- `Bootstrap\Ultraviolet\src\06_driver\pipeline.cpp:562`
- `Bootstrap\Ultraviolet\src\06_driver\pipeline.cpp:611`
- `Bootstrap\Ultraviolet\src\06_driver\pipeline.cpp:912`
- `Bootstrap\Ultraviolet\src\06_driver\pipeline.cpp:914`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\lower_module.cpp:211`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\lower_module.cpp:2040`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\lower_module.cpp:2061`
- `HelloUltraviolet\Fixtures\AcceptedProjects\ExecutableMain\Source\Main.uv:3`
- `HelloUltraviolet\Source\Main.uv:3`

Observed behavior:

`MainCheckProject` scans all project modules and accepts exactly one valid
procedure named `main`, regardless of whether that procedure is in the root
module named by the executable assembly. The backend selects a project entry
module only by root-module path. If the executable has no root module,
`project_entry_module` remains empty.

The emission path has a rootless fallback, but that fallback depends on
`module.main_symbol`. Lowering only assigns `ctx.main_symbol` when the procedure
named `main` is in `IsProjectEntryModule(module, ctx)`, and that predicate is
false when `project_entry_module` is empty. A valid submodule `main` can
therefore pass semantic checking without ever producing the symbol needed for
`WithEntry`.

Expected behavior:

An accepted executable with `MainDecls(P) = [d]` and `MainSigOk(d)` must emit
an `EntryStub(P)` that calls the accepted main declaration and runs the required
context initialization, project initialization, main call, deinitialization, and
panic propagation sequence. If the implementation requires executable `main` to
reside in the root module, that restriction must be diagnosed before codegen by
a spec-owned rule.

Impact:

`uvc` can semantically accept an executable project while omitting the native
entry stub for the only accepted `main`. The final artifact can then lack the
required context construction, lifecycle calls, and panic-propagation entry
semantics.

HelloUltraviolet fixture gap:

Executable fixtures keep `main` in the root module. There is no fixture for an
executable whose assembly has no root module and whose only valid `main` is in a
submodule.

### UV-AUDIT-0261: Shared-library destructor rewrites deinit panic into `ForeignPost`

Severity: High

Status: Subagent-reported and locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:15072`
- `Docs\SPECIFICATION.md:26173`
- `Docs\SPECIFICATION.md:26175`
- `Docs\SPECIFICATION.md:26834`
- `Docs\SPECIFICATION.md:28584`
- `Docs\SPECIFICATION.md:28639`
- `Docs\SPECIFICATION.md:28645`
- `Docs\SPECIFICATION.md:28653`
- `Docs\SPECIFICATION.md:28658`
- `Docs\SPECIFICATION.md:28670`
- `Docs\SPECIFICATION.md:28673`
- `Docs\SPECIFICATION.md:28767`
- `Docs\SPECIFICATION.md:28783`
- `Docs\SPECIFICATION.md:28805`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\entry_emit.cpp:1725`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\entry_emit.cpp:1749`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\entry_emit.cpp:1790`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\entry_emit.cpp:1802`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\entry_emit.cpp:1912`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\entry_emit.cpp:1923`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\entry_emit.cpp:1931`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\entry_emit.cpp:1939`
- `HelloUltraviolet\Source\Audit\ArtifactProjectExecution.uv:2055`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\ArtifactProjects\Coverage.uv:78`
- `HelloUltraviolet\Fixtures\ArtifactProjects\SharedLibraryImageLifecycle\Source\SharedLibraryImageLifecycleHostHarness\Main.uv:262`

Observed behavior:

The generated loader detach path clears the image panic record, calls deinit
procedures with a panic-out slot, captures the first deinit panic code, restores
that captured code to the image panic record on detach failure, and returns
failure. The generated destructor hook then calls the loader detach path. If the
detach call fails, the destructor ignores the preserved image panic code and
unconditionally calls the runtime panic entry with `PanicReason::ForeignPost`.

Expected behavior:

`LibraryImageDestroySigma(P, i, sigma)` is the shared-library unload operation,
and its boundary panic-record operations are interpreted through
`ImagePanicRecordOf(_, i)`. A panic from `Deinit(P, sigma)` must preserve the
cleanup panic identity through the image panic record. `ForeignPost` is reserved
for failed dynamic foreign postcondition checks, not shared-library deinit
failure.

Impact:

Unload-time deinitialization failures in shared libraries can be reported as
foreign postcondition failures. Runtime handlers, conformance traces, and
diagnostic tooling then lose the actual cleanup panic code that the detach path
already preserved.

HelloUltraviolet fixture gap:

`SharedLibraryImageLifecycle` exercises successful image init/destroy and linked
shared-library reuse. It does not force a shared-library deinit panic on unload
and assert that the destructor hook preserves the image panic code instead of
emitting `ForeignPost`.

### UV-AUDIT-0262: Qualified generic procedure calls are rejected before postfix parsing

Severity: Medium

Status: Locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:5706`
- `Docs\SPECIFICATION.md:5709`
- `Docs\SPECIFICATION.md:12524`
- `Docs\SPECIFICATION.md:12526`
- `Docs\SPECIFICATION.md:15921`
- `Docs\SPECIFICATION.md:15936`
- `Docs\SPECIFICATION.md:15939`
- `Docs\SPECIFICATION.md:15949`
- `Docs\SPECIFICATION.md:15957`
- `Docs\SPECIFICATION.md:16062`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\02_source\parser\expr\primary.cpp:762`
- `Bootstrap\Ultraviolet\src\02_source\parser\expr\primary.cpp:766`
- `Bootstrap\Ultraviolet\src\02_source\parser\expr\identifier.cpp:84`
- `Bootstrap\Ultraviolet\src\02_source\parser\expr\identifier.cpp:95`
- `Bootstrap\Ultraviolet\src\02_source\parser\parser_paths.cpp:338`
- `Bootstrap\Ultraviolet\src\02_source\parser\parser_paths.cpp:356`
- `Bootstrap\Ultraviolet\src\02_source\parser\parser_paths.cpp:374`
- `Bootstrap\Ultraviolet\src\02_source\parser\expr\path.cpp:149`
- `Bootstrap\Ultraviolet\src\02_source\parser\expr\path.cpp:153`
- `Bootstrap\Ultraviolet\src\02_source\parser\expr\path.cpp:155`
- `Bootstrap\Ultraviolet\src\02_source\parser\expr\qualified_apply.cpp:53`
- `Bootstrap\Ultraviolet\src\02_source\parser\expr\qualified_apply.cpp:62`
- `Bootstrap\Ultraviolet\src\02_source\parser\expr\qualified_apply.cpp:66`
- `Bootstrap\Ultraviolet\src\02_source\parser\expr\qualified_apply.cpp:68`
- `Bootstrap\Ultraviolet\src\02_source\parser\expr\postfix.cpp:107`
- `Bootstrap\Ultraviolet\src\02_source\parser\expr\postfix.cpp:108`
- `Bootstrap\Ultraviolet\src\02_source\parser\expr\postfix.cpp:287`
- `Bootstrap\Ultraviolet\src\02_source\parser\expr\postfix.cpp:288`
- `Bootstrap\Ultraviolet\src\02_source\parser\expr\call_type_args.cpp:86`
- `Bootstrap\Ultraviolet\src\02_source\parser\expr\call_type_args.cpp:100`
- `HelloUltraviolet\Source\Reference\Polymorphism\GenericProcedures.uv:29`
- `HelloUltraviolet\Source\Reference\Polymorphism\GenericProcedures.uv:34`
- `HelloUltraviolet\Source\Reference\Names\QualifiedResolution.uv:128`
- `HelloUltraviolet\Source\Reference\Names\QualifiedResolution.uv:130`

Observed behavior:

For an identifier followed by `::`, primary parsing refuses the simple
identifier path and calls `ParseQualifiedApply`. That parser consumes the full
qualified head through `ParseQualifiedHead`; when the next token is `<`, it emits
a syntax error, skips the angle list, synchronizes the statement, and returns an
error expression. `TryParseQualifiedExpr` has the same rejection branch.

The generic-call parser is implemented as a postfix step: `CallTypeArgsStart`
recognizes `<...>(...)`, and `ParsePostfixTail` can apply
`ParseCallTypeArgsStep` after a primary expression. However, a callee such as a
qualified name never reaches that postfix step with `<` intact, because the
qualified-primary parser rejects it first.

Expected behavior:

`generic_call_expr ::= postfix_expr generic_args "(" argument_list? ")"` and
`Postfix-Call-TypeArgs` make explicit type-argument calls a postfix operation
over any parsed `postfix_expr`. A qualified callee expression therefore must be
able to parse as the postfix callee and then elaborate to
`CallTypeArgs(callee, type_args, args)` when followed by `<...>(...)`.

Impact:

Explicit type-argument calls to generic procedures through a qualified path are
unusable even though the grammar, AST, resolver, and type-checking rules define
them through the ordinary postfix call-type-argument path. Users can only cover
the generic-call path through unqualified procedure names, which weakens public
module API usage and hides the parser mismatch behind imports.

HelloUltraviolet fixture gap:

The generic procedure reference exercises an explicit generic call only through
an unqualified callee. The qualified-resolution reference covers qualified
generic type application and qualified enum constructors, but it does not call a
generic procedure through a qualified path with explicit type arguments.

### UV-AUDIT-0263: Foreign contract predicates accept non-boolean expressions

Severity: Medium

Status: Agent-reported and locally corroborated.

Spec anchors:

- `Docs\SPECIFICATION.md:14648`
- `Docs\SPECIFICATION.md:14649`
- `Docs\SPECIFICATION.md:26637`
- `Docs\SPECIFICATION.md:26638`
- `Docs\SPECIFICATION.md:26640`
- `Docs\SPECIFICATION.md:26641`
- `Docs\SPECIFICATION.md:26642`
- `Docs\SPECIFICATION.md:26731`
- `Docs\SPECIFICATION.md:26755`
- `Docs\SPECIFICATION.md:26759`
- `Docs\SPECIFICATION.md:26834`
- `Docs\SPECIFICATION.md:26838`
- `Docs\SPECIFICATION.md:26845`
- `Docs\SPECIFICATION.md:26847`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\extern_block.cpp:219`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\extern_block.cpp:248`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\extern_block.cpp:253`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\extern_block.cpp:256`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\extern_block.cpp:259`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\extern_block.cpp:646`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\extern_block.cpp:650`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\extern_block.cpp:652`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\extern_block.cpp:658`
- `Bootstrap\Ultraviolet\src\04_analysis\contracts\contract_intrinsics.cpp:932`
- `Bootstrap\Ultraviolet\src\04_analysis\contracts\contract_intrinsics.cpp:958`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\call.cpp:506`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\call.cpp:508`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\call.cpp:622`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\call.cpp:624`

Observed behavior:

Foreign predicate validation is structural only. `ValidateForeignPredicateExpr`
accepts literals, null pointers, tuples, in-scope identifiers, unary
expressions, binary expressions, and field accesses without typing the
predicate or requiring a `bool` result. `BuildExternProcInfo` then records those
expressions through `ResolveForeignAssumes` or `ResolveForeignEnsures`. In the
dynamic lowering path, the lowered predicate result is registered as `bool` and
used as a contract-check condition.

Expected behavior:

Foreign `predicate_expr` clauses are contract predicates. The contract predicate
rules require predicate expressions to type as `bool`, and foreign contracts use
the same predicate values as `StaticProof` obligations or `ContractCheck`
conditions. A non-boolean `@foreign_assumes` predicate should be rejected with
`E-SEM-2851`; a non-boolean `@foreign_ensures`, `@error`, or `@null_result`
predicate should be rejected with `E-SEM-2853`.

Impact:

`uvc` can accept invalid FFI contracts when the predicate expression is
well-scoped but not boolean. Static verification may defer the malformed
predicate to later proof behavior or miss it when the foreign procedure is not
called. Dynamic verification can lower a malformed predicate value as if it were
a boolean contract check, weakening both diagnostic conformance and runtime
contract semantics.

HelloUltraviolet fixture gap:

Existing FFI fixtures cover impure or out-of-scope foreign predicates, invalid
postcondition bindings, and `@error` or `@null_result` well-formedness. The
accepted FFI reference covers boolean foreign predicates. There is no rejected
fixture for a well-scoped but non-boolean foreign predicate.

### UV-AUDIT-0264: Emitted item well-formedness and type-error diagnostics are not wired

Severity: Medium

Status: Agent-reported and locally corroborated.

Spec anchors:

- `Docs\SPECIFICATION.md:25149`
- `Docs\SPECIFICATION.md:25161`
- `Docs\SPECIFICATION.md:25162`
- `Docs\SPECIFICATION.md:25511`
- `Docs\SPECIFICATION.md:25523`
- `Docs\SPECIFICATION.md:25542`
- `Docs\SPECIFICATION.md:25746`
- `Docs\SPECIFICATION.md:25747`
- `Docs\SPECIFICATION.md:25748`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\03_comptime\eval.cpp:583`
- `Bootstrap\Ultraviolet\src\03_comptime\eval.cpp:594`
- `Bootstrap\Ultraviolet\src\03_comptime\rewrite.cpp:850`
- `Bootstrap\Ultraviolet\src\03_comptime\rewrite.cpp:853`
- `Bootstrap\Ultraviolet\src\03_comptime\rewrite.cpp:854`
- `Bootstrap\Ultraviolet\src\03_comptime\rewrite.cpp:855`
- `Bootstrap\Ultraviolet\src\00_core\generated\diag_registry.inc:149`
- `Bootstrap\Ultraviolet\src\00_core\generated\diag_registry.inc:150`
- `Bootstrap\Ultraviolet\src\00_core\generated\diag_registry.inc:151`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\typecheck_diag_map.inc:184`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\typecheck_diag_map.inc:185`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\typecheck_diag_map.inc:186`
- `HelloUltraviolet\Fixtures\RejectedSource\Comptime\EmitterEmitNonItem\Expected.uv:3`
- `HelloUltraviolet\Fixtures\AcceptedProjects\ComptimeConformance\Source\Library.uv:328`
- `HelloUltraviolet\Fixtures\AcceptedProjects\ComptimeConformance\Source\Library.uv:386`

Observed behavior:

`emitter~>emit` rejects non-item AST values with `E-CTE-0251`, hygienizes item
AST values, and appends the resulting item payload to `pending_emits`. The
module expansion path then inserts those emitted items into the module queue and
the same-module visible item list. Searches for `E-CTE-0252` and `E-CTE-0253`
find only generated diagnostic registries and the type-check diagnostic map, not
a source path that emits those diagnostics for an inserted item that later fails
well-formedness or type checking.

Expected behavior:

An emitted AST that is not an item must report `E-CTE-0251`. After an item AST is
inserted into the expanded module set, failures of item well-formedness must be
reported as `E-CTE-0252`, and type errors in emitted code must be reported as
`E-CTE-0253` or otherwise attached to that emitted-code diagnostic path.

Impact:

Invalid generated declarations can fall through to ordinary later-phase
diagnostics instead of the compile-time emitted-item taxonomy required by the
specification. Users and fixture metadata then cannot distinguish failures in
generated code from equivalent source-authored failures, and the registered
compile-time diagnostics remain effectively unreachable.

HelloUltraviolet fixture gap:

`EmitterEmitNonItem` covers only the non-item AST diagnostic path. The accepted
`ComptimeConformance` project emits valid items, including the multi-item
emission surface, but there is no rejected fixture for an emitted item that
fails well-formedness after insertion or type checking after insertion.

### UV-AUDIT-0265: `diagnostics.error` continues Phase 2 execution as unit

Severity: Medium

Status: Locally corroborated.

Spec anchors:

- `Docs\SPECIFICATION.md:24917`
- `Docs\SPECIFICATION.md:24957`
- `Docs\SPECIFICATION.md:25120`
- `Docs\SPECIFICATION.md:25152`
- `Docs\SPECIFICATION.md:25153`
- `Docs\SPECIFICATION.md:25211`
- `Docs\SPECIFICATION.md:25212`
- `Docs\SPECIFICATION.md:25214`
- `Docs\SPECIFICATION.md:25690`
- `Docs\SPECIFICATION.md:25730`
- `Docs\SPECIFICATION.md:25768`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\03_comptime\eval.cpp:523`
- `Bootstrap\Ultraviolet\src\03_comptime\eval.cpp:526`
- `Bootstrap\Ultraviolet\src\03_comptime\eval.cpp:527`
- `Bootstrap\Ultraviolet\src\03_comptime\eval.cpp:610`
- `Bootstrap\Ultraviolet\src\03_comptime\eval.cpp:649`
- `Bootstrap\Ultraviolet\src\03_comptime\eval.cpp:654`
- `Bootstrap\Ultraviolet\src\03_comptime\eval.cpp:674`
- `Bootstrap\Ultraviolet\src\03_comptime\eval.cpp:675`
- `Bootstrap\Ultraviolet\src\03_comptime\rewrite.cpp:527`
- `Bootstrap\Ultraviolet\src\03_comptime\rewrite.cpp:530`
- `HelloUltraviolet\Fixtures\RejectedSource\Comptime\UserDiagnosticError\Source\Main.uv:5`
- `HelloUltraviolet\Fixtures\RejectedSource\Comptime\UserDiagnosticError\Expected.uv:3`
- `HelloUltraviolet\Fixtures\RejectedSource\Comptime\DeriveTargetUserError\Source\Main.uv:5`
- `HelloUltraviolet\Fixtures\RejectedSource\Comptime\DeriveTargetUserError\Expected.uv:3`

Observed behavior:

The `diagnostics.error` method appends `E-CTE-0070`, then returns a successful
compile-time unit value. `EvalBlock` treats an expression statement with a
successful unit result as complete and continues to later statements. For
`#emit` compile-time blocks, `RewriteStmt` transfers `pending_emits` whenever
`EvalBlock(...).ok` is true, so declarations emitted after a user diagnostic
error can still be appended to the Phase 2 item stream.

Expected behavior:

The `ComptimeDiagnostics` interface gives `diagnostics.error(message)` result
type `!`, and `CtBuiltin-Diagnostics-Error` appends the diagnostic with a
failure judgment rather than returning unit. A compile-time user error should
stop the current Phase 2 execution path after appending `E-CTE-0070`; later
statements and later declaration-emission side effects in that path must not
continue as if the call returned `()`.

Impact:

Phase 2 side effects after a user error can leak into the expanded module set
before the compilation is rejected. That can produce extra diagnostics,
unexpected emitted declarations, or different derive/emission behavior after a
program point that the specification classifies as divergent.

HelloUltraviolet fixture gap:

Existing user-diagnostic fixtures assert that `E-CTE-0070` is emitted for a
plain compile-time block and for a derive target. They do not place a
side-effecting compile-time operation after `diagnostics.error` to assert that
Phase 2 execution stops rather than continuing with unit.

### UV-AUDIT-0266: Integer narrowing casts are checked as panicking conversions

Severity: High

Status: Agent-reported and locally corroborated.

Spec anchors:

- `Docs\SPECIFICATION.md:16502`
- `Docs\SPECIFICATION.md:16503`
- `Docs\SPECIFICATION.md:16524`
- `Docs\SPECIFICATION.md:16525`
- `Docs\SPECIFICATION.md:16529`
- `Docs\SPECIFICATION.md:16530`
- `Docs\SPECIFICATION.md:16611`
- `Docs\SPECIFICATION.md:16613`
- `Docs\SPECIFICATION.md:16614`
- `Docs\SPECIFICATION.md:16618`
- `Docs\SPECIFICATION.md:16619`
- `Docs\SPECIFICATION.md:16621`
- `Docs\SPECIFICATION.md:16635`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\cast.cpp:131`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\cast.cpp:132`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\cast.cpp:137`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\cast.cpp:140`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\checks\check_cast.cpp:9`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\checks\check_cast.cpp:13`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\checks\check_cast.cpp:19`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\checks\check_cast.cpp:29`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\checks\check_cast.cpp:30`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\checks\check_cast.cpp:32`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\ops\cast.cpp:64`
- `HelloUltraviolet\Source\Reference\Expressions\CastsAndTransmutes.uv:7`
- `HelloUltraviolet\Source\Reference\Expressions\CastsAndTransmutes.uv:8`
- `HelloUltraviolet\Source\Reference\Expressions\CastsAndTransmutes.uv:77`
- `HelloUltraviolet\Source\Reference\Expressions\CastsAndTransmutes.uv:78`

Observed behavior:

`LowerCast` emits `IRCheckCast`, a panic followup, and `IRCast` for every
non-dynamic cast instead of selecting `Lower-Cast` for casts where
`CastNeedsCheck` is false. The LLVM `IRCheckCast` implementation then checks
narrowing integer casts by truncating to the target width, widening back to the
source width, and panicking unless the widened value equals the original value.

Expected behavior:

Integer-to-integer casts are defined by `ToSigned` and `ToUnsigned` over the
target width. They are wrapping conversions, not checked conversions. Because
`CastNeedsCheck` includes only float-to-int casts and `u32` to `char`, ordinary
integer-to-integer casts must lower through `CastIR` without `CheckCastIR`.

Impact:

Legal integer casts can panic at runtime. For example, a cast such as
`256u16 as u8` should produce `0u8` through target-width unsigned conversion,
but the current round-trip check classifies the truncation as a cast failure.

HelloUltraviolet fixture gap:

The cast reference covers sign reinterpretation examples such as `(-1) as u8`
and `255u8 as i8`, which happen to round-trip under the current check. It does
not include a narrowing integer cast whose valid result is not
round-trip-preserving in the source width.

### UV-AUDIT-0267: Float-to-integer casts skip required runtime range checks

Severity: High

Status: Agent-reported and locally corroborated.

Spec anchors:

- `Docs\SPECIFICATION.md:16546`
- `Docs\SPECIFICATION.md:16547`
- `Docs\SPECIFICATION.md:16580`
- `Docs\SPECIFICATION.md:16611`
- `Docs\SPECIFICATION.md:16618`
- `Docs\SPECIFICATION.md:16619`
- `Docs\SPECIFICATION.md:16621`
- `Docs\SPECIFICATION.md:16635`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\cast.cpp:131`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\cast.cpp:137`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\checks\check_cast.cpp:9`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\checks\check_cast.cpp:13`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\ops\cast.cpp:87`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\ops\cast.cpp:90`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\ops\cast.cpp:91`
- `HelloUltraviolet\Source\Reference\Expressions\CastsAndTransmutes.uv:23`
- `HelloUltraviolet\Source\Reference\Expressions\CastsAndTransmutes.uv:24`
- `HelloUltraviolet\Source\Reference\Expressions\CastsAndTransmutes.uv:83`
- `HelloUltraviolet\Source\Reference\Expressions\CastsAndTransmutes.uv:84`

Observed behavior:

`LowerCast` emits an `IRCheckCast` before float-to-int casts, but the LLVM
visitor for that instruction returns immediately unless both the target type and
the source value type are integers. The subsequent `IRCast` then lowers
float-to-int with raw `CreateFPToSI` or `CreateFPToUI`.

Expected behavior:

`CastNeedsCheck` explicitly includes float-to-integer casts. `CheckCastIR` must
panic with reason `Cast` exactly when `CastVal` is undefined, which includes
float-to-int conversions whose truncated value is outside the target integer
range or is otherwise not representable.

Impact:

Out-of-range or invalid float-to-int conversions bypass the specified runtime
panic path and reach the backend conversion directly. That can produce
target-dependent values or LLVM undefined behavior instead of the required
`EvalSigma-Cast-Panic` result.

HelloUltraviolet fixture gap:

The cast reference covers in-range truncation from `3.75f64` and `-3.75f64` to
`i32`. It does not include an artifact fixture for out-of-range float-to-int,
negative float-to-unsigned, or non-finite float-to-int behavior.

### UV-AUDIT-0268: Dependency emission is assigned to one owner instead of each qualifying selected path

Severity: Medium

Status: Agent-reported and locally corroborated.

Spec anchors:

- `Docs\SPECIFICATION.md:624`
- `Docs\SPECIFICATION.md:628`
- `Docs\SPECIFICATION.md:1126`
- `Docs\SPECIFICATION.md:1255`
- `Docs\SPECIFICATION.md:1264`
- `Docs\SPECIFICATION.md:1265`
- `Docs\SPECIFICATION.md:1267`
- `Docs\SPECIFICATION.md:1269`
- `Docs\SPECIFICATION.md:1489`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\resolve\assembly_import_graph.cpp:189`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\assembly_import_graph.cpp:225`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\assembly_import_graph.cpp:228`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\assembly_import_graph.cpp:230`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\assembly_import_graph.cpp:246`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\assembly_import_graph.cpp:257`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\assembly_import_graph.cpp:554`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\assembly_import_graph.cpp:567`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\assembly_import_graph.cpp:568`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\assembly_import_graph.cpp:575`
- `Bootstrap\Ultraviolet\src\04_analysis\resolve\assembly_import_graph.cpp:659`
- `HelloUltraviolet\Fixtures\AcceptedProjects\CrossAssemblyImplementation\Ultraviolet.toml:2`
- `HelloUltraviolet\Fixtures\AcceptedProjects\CrossAssemblyImplementation\Ultraviolet.toml:7`
- `HelloUltraviolet\Fixtures\AcceptedProjects\CrossAssemblyImplementation\Source\CrossAssemblyImplementation\Library.uv:3`

Observed behavior:

`ComputeDependencyOwners` assigns each dependency assembly to a single linkable
owner by reverse search from the dependency and lexicographic tie-breaking among
nearest linkable importers. `ComputeEmitModules` then includes dependency
modules only when that owner is the selected assembly currently being emitted.
If a selected executable directly imports dependency `SharedDep`, and also
imports library `AaaLib` that imports `SharedDep`, the dependency can be owned
by `AaaLib` instead of the selected executable when `AaaLib` wins the owner
tie-break.

Expected behavior:

For a selected project `P`, `EmitAssemblies(P)` includes `P.assembly` and every
dependency reachable from `P.assembly` by at least one path with no library
interior. A direct selected-assembly-to-dependency path qualifies even when a
linked library also imports the same dependency. `EmitModuleList(P)`,
`RequiredOutputs(P)`, `OutputSummary(P)`, and `LinkObjs(P)` must therefore be
computed from the selected assembly path, not from a single global dependency
owner assignment.

Impact:

`uvc` can omit object or IR emission for a dependency directly imported by the
selected linkable assembly. The selected artifact can then miss dependency
symbols or state, while another library artifact receives the dependency modules
even though the selected assembly also has a qualifying emission path.

HelloUltraviolet fixture gap:

`CrossAssemblyImplementation` covers a library importing one dependency. It does
not cover a selected linkable assembly that directly imports a dependency also
imported by a linked library, and it does not assert `EmitModuleList(P)`
membership for that dependency under the selected assembly.

### UV-AUDIT-0269: Dynamic `ValidValue` accepts arbitrary vtable pointer encodings

Severity: High

Status: Agent-reported and locally corroborated.

Spec anchors:

- `Docs\SPECIFICATION.md:13328`
- `Docs\SPECIFICATION.md:27615`
- `Docs\SPECIFICATION.md:27616`
- `Docs\SPECIFICATION.md:29683`
- `Docs\SPECIFICATION.md:29686`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\layout\layout_value_bits.cpp:1634`
- `Bootstrap\Ultraviolet\src\04_analysis\layout\layout_value_bits.cpp:1636`
- `Bootstrap\Ultraviolet\src\04_analysis\layout\layout_value_bits.cpp:1637`
- `Bootstrap\Ultraviolet\src\04_analysis\layout\layout_value_bits.cpp:2122`
- `Bootstrap\Ultraviolet\src\04_analysis\layout\layout_value_bits.cpp:2124`
- `Bootstrap\Ultraviolet\src\04_analysis\layout\layout_value_bits.cpp:2131`
- `Bootstrap\Ultraviolet\src\04_analysis\layout\layout_value_bits.cpp:2132`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\llvm_types.cpp:1719`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\llvm_types.cpp:1721`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\llvm_types.cpp:1722`

Observed behavior:

`ValidValue` for `TypeDynamic` accepts any byte sequence whose length equals the
dynamic layout size. `ValueBits` for the same type serializes `DynamicVal` by
copying its `data` and `vtable` pointer values into the two dynamic fields, with
no check that the vtable pointer denotes the class vtable symbol required by the
dynamic value.

Expected behavior:

The specification defines `ValueBits(TypeDynamic(Cl), v)` only for a dynamic
value whose data field is an immutable raw pointer and whose vtable field is
`AddrOfSym(ScopedSym(VTableDecl(T, Cl)))`. Because non-primitive `ValidValue`
requires the existence of such a `v`, a dynamic value bit pattern is valid only
when the vtable pointer is the class vtable address for some concrete `T`
implementing `Cl`, paired with the corresponding data pointer representation.

Impact:

Validity-dependent operations can accept fabricated dynamic class objects whose
vtable pointer is arbitrary but correctly sized. A later dynamic call or
boundary transfer can then preserve or consume a dynamic value that the
specification never admits as valid.

HelloUltraviolet fixture gap:

The reference fixtures cover dynamic lowering and dynamic method calls through
ordinary construction paths. They do not include an artifact fixture that feeds
an invalid dynamic vtable bit pattern through a `ValidValue`-dependent path.

### UV-AUDIT-0270: Pipeline `=>` is tokenized outside the lexical operator set

Severity: Medium

Status: Agent-reported and locally corroborated.

Spec anchors:

- `Docs\SPECIFICATION.md:2159`
- `Docs\SPECIFICATION.md:2162`
- `Docs\SPECIFICATION.md:2166`
- `Docs\SPECIFICATION.md:2467`
- `Docs\SPECIFICATION.md:2503`
- `Docs\SPECIFICATION.md:17527`
- `Docs\SPECIFICATION.md:17548`
- `Docs\SPECIFICATION.md:17553`
- `Docs\SPECIFICATION.md:30670`

Implementation anchors:

- `Bootstrap\Ultraviolet\include\00_core\keywords.h:111`
- `Bootstrap\Ultraviolet\include\00_core\keywords.h:148`
- `Bootstrap\Ultraviolet\src\02_source\lexer\lexer.cpp:443`
- `Bootstrap\Ultraviolet\src\02_source\lexer\lexer.cpp:446`
- `Bootstrap\Ultraviolet\src\02_source\lexer\lexer.cpp:536`
- `HelloUltraviolet\Fixtures\RejectedSource\Expressions\PipelineNotCallable\Source\Main.uv:4`
- `HelloUltraviolet\Fixtures\ArtifactProjects\ExecutableOutput\Source\Main.uv:106`

Observed behavior:

The lexer operator table includes `=>`, and the tokenization path builds
operator candidates directly from that table. Pipeline fixtures therefore parse
and reach semantic or lowering behavior through `=>`.

Expected behavior:

The Chapter 4 lexical token relation builds operator candidates from
`OperatorSet`, but that set omits `=>`. Chapter 16 and Appendix B then require
pipeline parsing via `IsOp(Tok(P), "=>")`. Until the lexical operator set and
pipeline grammar are reconciled, `uvc` accepts an operator token that the
lexical specification does not admit.

Impact:

The implementation-only operator table can drift from the normative token set,
and any tool that derives lexing behavior from `OperatorSet` will disagree with
the compiler and the pipeline grammar.

HelloUltraviolet fixture gap:

Pipeline expression fixtures exercise the implementation behavior, but there is
no lexer/token-set conformance fixture asserting that `kUltravioletOperators`
matches `OperatorSet` plus any spec-authorized grammar operators.

### UV-AUDIT-0271: Diagnostic-source fixtures omit the `W-SRC-0308` unsafe-span warning

Severity: Low

Status: Agent-reported and locally corroborated.

Spec anchors:

- `Docs\SPECIFICATION.md:2558`
- `Docs\SPECIFICATION.md:2577`
- `Docs\SPECIFICATION.md:2691`
- `Docs\SPECIFICATION.md:30469`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\02_source\lexer\lexer_security.cpp:296`
- `Bootstrap\Ultraviolet\src\02_source\lexer\lexer_security.cpp:300`
- `Bootstrap\Ultraviolet\src\02_source\lexer\lexer_security.cpp:323`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\DiagnosticSource\SourceText.uv:3`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\DiagnosticSource\SourceText.uv:4`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\DiagnosticSource\SourceText.uv:32`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\DiagnosticSource\SourceText.uv:39`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\DiagnosticSource\SourceText.uv:49`

Observed behavior:

`LexSecure` records `LexSecure-Warn` and emits `W-SRC-0308` for sensitive
Unicode inside a token-level unsafe span. The HelloUltraviolet diagnostic-source
catalog for source text has two specimens, covering `W-SRC-0101` and
`W-SRC-0301`, but not `W-SRC-0308`.

Expected behavior:

The diagnostic-source surface should include a fixture that places a sensitive
Unicode scalar inside an unsafe span and asserts `W-SRC-0308` with the
`LexSecure-Warn` obligation.

Impact:

The warning half of lexical security can regress while the source-text
diagnostic-source catalog remains green. This is distinct from `UV-AUDIT-0136`,
which covers rejected-source fixture gaps for lexical errors such as
`E-SRC-0308`.

HelloUltraviolet fixture gap:

No `Fixtures\DiagnosticSource\SourceText` specimen currently covers the
lexically sensitive Unicode warning path.

### UV-AUDIT-0272: Float literal constants are encoded through host rounding

Severity: High

Status: Agent-reported and locally corroborated.

Spec anchors:

- `Docs\SPECIFICATION.md:4672`
- `Docs\SPECIFICATION.md:8919`
- `Docs\SPECIFICATION.md:8921`
- `Docs\SPECIFICATION.md:8922`
- `Docs\SPECIFICATION.md:15477`
- `Docs\SPECIFICATION.md:15478`
- `Docs\SPECIFICATION.md:15479`
- `Docs\SPECIFICATION.md:15578`
- `Docs\SPECIFICATION.md:27577`
- `Docs\SPECIFICATION.md:27578`
- `Docs\SPECIFICATION.md:27580`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\typing\literals.cpp:501`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\literals.cpp:506`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\literals.cpp:509`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\literals.cpp:510`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\literals.cpp:511`
- `Bootstrap\Ultraviolet\src\04_analysis\layout\layout_value_bits.cpp:611`
- `Bootstrap\Ultraviolet\src\04_analysis\layout\layout_value_bits.cpp:637`
- `Bootstrap\Ultraviolet\src\04_analysis\layout\layout_value_bits.cpp:642`
- `Bootstrap\Ultraviolet\src\04_analysis\layout\layout_value_bits.cpp:1470`
- `Bootstrap\Ultraviolet\src\04_analysis\layout\layout_value_bits.cpp:1472`
- `Bootstrap\Ultraviolet\src\04_analysis\layout\layout_value_bits.cpp:1479`
- `Bootstrap\Ultraviolet\src\04_analysis\layout\layout_value_bits.cpp:1485`
- `Bootstrap\Ultraviolet\src\04_analysis\layout\layout_value_bits.cpp:1491`
- `Bootstrap\Ultraviolet\src\04_analysis\layout\layout_value_bits.cpp:1492`

Observed behavior:

Float literal checking accepts unsuffixed and `f`-suffixed literals for any
expected float type without validating that the exact decimal value is in the
target `FloatValueSet`. Constant encoding then parses the literal with
`std::strtod` and casts the resulting host `double` to the target float width or
half encoder, with no overflow or underflow status check.

Expected behavior:

`LiteralValue(lit, TypePrim(t))` forms `FloatVal(t, v)` only when the exact
decimal `FloatValue(lit)` is representable in `FloatValueSet(t)`.
`Encode-Float` consumes that value and encodes `FloatValValue(v)`. It does not
permit replacing a non-representable exact decimal with a host-rounded,
overflowed, or underflowed value.

Impact:

Compile-time constants, emitted data bytes, comparisons, ABI-visible constants,
and validity reasoning can observe rounded or infinite host values that are not
the specified literal value.

HelloUltraviolet fixture gap:

The literal fixtures cover ordinary float spellings and suffix mismatch paths,
but they do not exercise non-representable decimal literals, `strtod` overflow,
or target-width underflow before constant emission.

### UV-AUDIT-0273: Integer literal range failures fall through to return-type diagnostics

Severity: Medium

Status: Locally corroborated.

Spec anchors:

- `Docs\SPECIFICATION.md:15524`
- `Docs\SPECIFICATION.md:15525`
- `Docs\SPECIFICATION.md:15527`
- `Docs\SPECIFICATION.md:15659`
- `Docs\SPECIFICATION.md:20090`
- `Docs\SPECIFICATION.md:20091`
- `Docs\SPECIFICATION.md:20259`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\typing\literals.cpp:479`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\literals.cpp:484`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\literals.cpp:491`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\literals.cpp:492`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\literals.cpp:493`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\literals.cpp:494`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\type_infer.cpp:1457`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\type_infer.cpp:1462`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\type_infer.cpp:1466`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\return_stmt.cpp:1341`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\return_stmt.cpp:1343`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\return_stmt.cpp:1347`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\return_stmt.cpp:1348`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\stmt\return_stmt.cpp:1352`
- `Bootstrap\Ultraviolet\src\00_core\generated\diag_registry.inc:312`
- `HelloUltraviolet\Fixtures\RejectedSource\Expressions\IntLiteralCheckRange\Source\Main.uv:3`
- `HelloUltraviolet\Fixtures\RejectedSource\Expressions\IntLiteralCheckRange\Source\Main.uv:4`
- `HelloUltraviolet\Fixtures\RejectedSource\Expressions\IntLiteralCheckRange\Expected.uv:3`
- `HelloUltraviolet\Fixtures\RejectedSource\Expressions\IntLiteralCheckRange\Expected.uv:4`

Observed behavior:

`CheckLiteralExpr` detects an integer literal whose value is outside the
expected primitive integer range and records `rule.16.Chk-Int-Literal` plus
`diag.16.LiteralAndNameExpressions`, but returns a failed check with no
diagnostic id. `CheckExprAgainst` only propagates literal-check failures that
carry a diagnostic id, so the failure falls through as an ordinary expected-type
failure. In a return expression, `TypeReturnStmt` then emits `E-SEM-3161` under
`Return-Type-Err`.

Expected behavior:

Chapter 16 owns diagnostics for literal values that do not fit the required
primitive type. A failed `Chk-Int-Literal` should therefore surface as the
literal-range diagnostic for that use site, not as the Chapter 18 return-type
mismatch rule whose premise is a successfully typed return destination with a
non-subtype result.

Impact:

Out-of-range integer literals can be reported as return type mismatches or other
enclosing expected-type failures. This misclassifies first-failure ownership and
lets fixtures credit the literal obligations while asserting the return-type
diagnostic code.

HelloUltraviolet fixture gap:

`IntLiteralCheckRange` places `return 128` in a procedure returning `i8`, but
its expected file asserts `E-SEM-3161` for the literal obligations. There is no
fixture that verifies the literal-range diagnostic independently of an enclosing
return mismatch.

### UV-AUDIT-0274: Non-suffix generic defaults reuse the duplicate-name diagnostic

Severity: Medium

Status: Locally corroborated.

Spec anchors:

- `Docs\SPECIFICATION.md:12454`
- `Docs\SPECIFICATION.md:12458`
- `Docs\SPECIFICATION.md:12459`
- `Docs\SPECIFICATION.md:12506`
- `Docs\SPECIFICATION.md:13947`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\generic_params.cpp:125`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\generic_params.cpp:126`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\generic_params.cpp:130`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\generic_params.cpp:131`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\generic_params.cpp:132`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\generic_params.cpp:135`
- `Bootstrap\Ultraviolet\src\00_core\generated\diag_registry.inc:430`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\typecheck_diag_map.inc:465`
- `HelloUltraviolet\Fixtures\RejectedSource\Polymorphism\DuplicateTypeParameterName\Expected.uv:3`

Observed behavior:

`ProcessGenericParams` checks `DefaultSuffix` after collecting generic
parameters. When a parameter without a default follows one with a default, it
records `GenDefault-Order-Err` and `rule.14.WF-Generic-Param`, but sets the
diagnostic id to `E-TYP-2304`. That diagnostic is registered and documented as
"Duplicate type parameter name in generic declaration".

Expected behavior:

The specification lists non-suffix defaults as their own generic-parameter
diagnostic condition. A declaration that violates `DefaultSuffix` should be
reported as a default-order failure, not as a duplicate type-parameter name.

Impact:

`uvc` rejects the invalid declaration but reports a misleading diagnostic code
and message. Tooling and fixtures cannot distinguish duplicate parameter names
from default-order violations even though the specification gives them separate
failure causes.

HelloUltraviolet fixture gap:

HelloUltraviolet has a duplicate-type-parameter fixture for `E-TYP-2304`, but no
rejected-source fixture for a generic parameter list where a defaulted parameter
is followed by a non-defaulted parameter.

### UV-AUDIT-0275: Test environment can force compilation-unit relative-path failure

Severity: Medium

Status: Agent-reported and locally corroborated.

Spec anchors:

- `Docs\SPECIFICATION.md:1187`
- `Docs\SPECIFICATION.md:1189`
- `Docs\SPECIFICATION.md:1190`
- `Docs\SPECIFICATION.md:1192`
- `Docs\SPECIFICATION.md:1747`
- `Docs\SPECIFICATION.md:8141`
- `Docs\SPECIFICATION.md:8142`
- `Docs\SPECIFICATION.md:8176`
- `Docs\SPECIFICATION.md:8177`
- `Docs\SPECIFICATION.md:8179`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\01_project\module_discovery.cpp:421`
- `Bootstrap\Ultraviolet\src\01_project\module_discovery.cpp:425`
- `Bootstrap\Ultraviolet\src\01_project\module_discovery.cpp:428`
- `Bootstrap\Ultraviolet\src\01_project\module_discovery.cpp:430`
- `Bootstrap\Ultraviolet\src\01_project\module_discovery.cpp:431`
- `Bootstrap\Ultraviolet\src\01_project\module_discovery.cpp:432`
- `Bootstrap\Ultraviolet\src\01_project\module_discovery.cpp:434`
- `Bootstrap\Ultraviolet\src\01_project\module_discovery.cpp:469`
- `Bootstrap\Ultraviolet\src\01_project\module_discovery.cpp:470`
- `Bootstrap\Ultraviolet\src\01_project\module_discovery.cpp:471`
- `HelloUltraviolet\Fixtures\AcceptedProjects\ExecutableMain\Ultraviolet.toml:1`
- `HelloUltraviolet\Fixtures\AcceptedProjects\ExecutableMain\Ultraviolet.toml:3`
- `HelloUltraviolet\Fixtures\AcceptedProjects\ExecutableMain\Ultraviolet.toml:4`
- `HelloUltraviolet\Fixtures\AcceptedProjects\ExecutableMain\Source\Main.uv:3`

Observed behavior:

`CompilationUnit(module_dir, language)` checks the process environment variable
`UV_TEST_COMPILATION_UNIT_FAIL` before enumerating source files. When it is set
to a non-empty value, the function records `CompilationUnit-Rel-Fail`, emits
`E-PRJ-0303`, and returns without evaluating the actual files under the module
directory.

Expected behavior:

`CompilationUnit(d)` is defined from `Files(d)` and deterministic file ordering.
`CompilationUnit-Rel-Fail` applies only when `relative(f, d)` fails for some
actual file in `Files(d)`, and `ParseModule-Err-Unit` only propagates that real
compilation-unit failure. Ambient test environment state is not part of the
specified relation.

Impact:

A valid project can fail with `E-PRJ-0303` for a reason unrelated to relative
path derivation. That makes project loading nondeterministic with respect to the
process environment and weakens the diagnostic's meaning for users and audit
fixtures.

HelloUltraviolet fixture gap:

`ExecutableMain` is a valid accepted project, and the agent verified it succeeds
normally but fails under `UV_TEST_COMPILATION_UNIT_FAIL=1`. No fixture or driver
guard asserts that test-only failure injection is unavailable in ordinary `uvc`
execution.

### UV-AUDIT-0276: Test environment can force source read failure before file IO

Severity: Medium

Status: Locally corroborated.

Spec anchors:

- `Docs\SPECIFICATION.md:2676`
- `Docs\SPECIFICATION.md:8144`
- `Docs\SPECIFICATION.md:8146`
- `Docs\SPECIFICATION.md:8149`
- `Docs\SPECIFICATION.md:8151`
- `Docs\SPECIFICATION.md:8152`
- `Docs\SPECIFICATION.md:8154`
- `Docs\SPECIFICATION.md:8163`
- `Docs\SPECIFICATION.md:8164`
- `Docs\SPECIFICATION.md:8165`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\02_source\parser\parse_modules.cpp:61`
- `Bootstrap\Ultraviolet\src\02_source\parser\parse_modules.cpp:63`
- `Bootstrap\Ultraviolet\src\02_source\parser\parse_modules.cpp:65`
- `Bootstrap\Ultraviolet\src\02_source\parser\parse_modules.cpp:66`
- `Bootstrap\Ultraviolet\src\02_source\parser\parse_modules.cpp:67`
- `Bootstrap\Ultraviolet\src\02_source\parser\parse_modules.cpp:68`
- `Bootstrap\Ultraviolet\src\02_source\parser\parse_modules.cpp:70`
- `Bootstrap\Ultraviolet\src\02_source\parser\parse_modules.cpp:72`
- `Bootstrap\Ultraviolet\src\02_source\parser\parse_modules.cpp:100`
- `Bootstrap\Ultraviolet\src\02_source\parser\parse_modules.cpp:957`
- `Bootstrap\Ultraviolet\src\02_source\parser\parse_modules.cpp:963`
- `Bootstrap\Ultraviolet\src\00_core\generated\diag_registry.inc:317`
- `Bootstrap\Ultraviolet\src\00_core\generated\diag_registry.inc:1161`
- `HelloUltraviolet\Fixtures\AcceptedProjects\ExecutableMain\Source\Main.uv:3`

Observed behavior:

`ReadBytesDefault` checks the process environment variable
`UV_TEST_READ_BYTES_FAIL` before opening the requested source path. When it is
set to a non-empty value, the function records `ReadBytes-Err`, marks the
`ReadBytes` host primitive as failed, emits `E-SRC-0102`, and returns without
attempting to open or read the file.

Expected behavior:

The source-loading rules define `ReadBytes(f)` as a partial operation over the
file path. `ReadBytes-Err` applies when `read_ok(f)` fails for that source file,
and `ParseModule-Err-Read` propagates that read failure. A test environment
variable is not part of the specified `ReadBytes` relation and should not make a
readable source file fail before file IO occurs.

Impact:

A valid source file can fail with `E-SRC-0102` for a non-spec process
environment reason. This breaks determinism of source parsing and makes
`ReadBytes-Err` no longer prove that the host file read actually failed.

HelloUltraviolet fixture gap:

Accepted project fixtures such as `ExecutableMain` cover ordinary source reads,
but no fixture or driver guard asserts that test-only read failure injection is
unavailable during ordinary `uvc` execution.

### UV-AUDIT-0277: Async `E = !` removes `@Failed` from modal exhaustiveness

Severity: Medium

Status: Agent-reported and locally corroborated.

Spec anchors:

- `Docs\SPECIFICATION.md:10650`
- `Docs\SPECIFICATION.md:18849`
- `Docs\SPECIFICATION.md:18867`
- `Docs\SPECIFICATION.md:18871`
- `Docs\SPECIFICATION.md:22874`
- `Docs\SPECIFICATION.md:23032`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\typing\if_case_check.cpp:183`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\if_case_check.cpp:188`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\if_case_check.cpp:189`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\if_case_check.cpp:1952`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\if_case_check.cpp:1954`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\if_case_check.cpp:2165`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\if_case_check.cpp:2167`
- `HelloUltraviolet\Fixtures\RejectedSource\Patterns\IfCaseModalNonExhaustive\Source\Main.uv:13`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\RejectedSource\Patterns.uv:128`
- `HelloUltraviolet\Source\Reference\Async\AsyncType.uv:54`

Observed behavior:

`ModalExhaustiveStatesForScrutinee` starts from the modal declaration's state
set, then detects an `Async` scrutinee whose error type is `!` and erases
`Failed` before the synth and check paths compare `arm_states` with
`decl_states`. As a result, an `if case` over `Async<Out, In, Result, !>` can
typecheck without `else` after covering only `@Suspended` and `@Completed`.

Expected behavior:

Chapter 17 defines `States(M) = StateNames(M)`, and `T-IfCase-Modal` /
`IfCase-Modal-NonExhaustive` compare modal coverage against `States(M)`. Chapter
21 defines `States(Async) = { @Suspended, @Completed, @Failed }`. The async
lowering rule for `E = !` omits `@Failed` only from concrete storage layouts and
resume-result tag space; it explicitly leaves semantic `@Failed` uninhabited.
That lowering exception does not change the Chapter 17 modal state set used for
exhaustiveness.

Impact:

The type checker can suppress `E-TYP-2060` for a pattern set that is
non-exhaustive by the semantic modal rules. This conflates storage-lowered async
states with source-language modal states and makes Chapter 17 conformance depend
on a Chapter 21 layout optimization.

HelloUltraviolet fixture gap:

`IfCaseModalNonExhaustive` covers ordinary modal missing-state rejection, and
the async reference source names all three async states. No rejected fixture
checks that an `Async<..., !>` case analysis still requires semantic coverage of
`@Failed` when there is no `else` or irrefutable arm.

### UV-AUDIT-0278: Output-stage preparation failures emit code-less diagnostics

Severity: Medium

Status: Locally corroborated.

Spec anchors:

- `Docs\SPECIFICATION.md:450`
- `Docs\SPECIFICATION.md:451`
- `Docs\SPECIFICATION.md:453`
- `Docs\SPECIFICATION.md:1573`
- `Docs\SPECIFICATION.md:1593`
- `Docs\SPECIFICATION.md:1596`
- `Docs\SPECIFICATION.md:1608`
- `Docs\SPECIFICATION.md:1611`
- `Docs\SPECIFICATION.md:1642`
- `Docs\SPECIFICATION.md:1646`
- `Docs\SPECIFICATION.md:30442`
- `Docs\SPECIFICATION.md:30459`
- `Docs\SPECIFICATION.md:30524`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\06_driver\output_pipeline.cpp:506`
- `Bootstrap\Ultraviolet\src\06_driver\output_pipeline.cpp:508`
- `Bootstrap\Ultraviolet\src\06_driver\output_pipeline.cpp:510`
- `Bootstrap\Ultraviolet\src\06_driver\output_pipeline.cpp:797`
- `Bootstrap\Ultraviolet\src\06_driver\output_pipeline.cpp:806`
- `Bootstrap\Ultraviolet\src\06_driver\output_pipeline.cpp:1203`
- `Bootstrap\Ultraviolet\src\06_driver\output_pipeline.cpp:2462`
- `Bootstrap\Ultraviolet\src\06_driver\compiler_main.cpp:229`
- `Bootstrap\Ultraviolet\src\06_driver\compiler_main.cpp:233`
- `Bootstrap\Ultraviolet\src\06_driver\compiler_main.cpp:236`
- `Bootstrap\Ultraviolet\src\06_driver\compiler_main.cpp:4216`
- `Bootstrap\Ultraviolet\src\06_driver\compiler_main.cpp:4688`
- `Bootstrap\Ultraviolet\src\06_driver\compiler_main.cpp:4912`
- `Bootstrap\Ultraviolet\src\00_core\generated\diag_registry.inc:216`
- `Bootstrap\Ultraviolet\src\00_core\generated\diag_registry.inc:233`
- `Bootstrap\Ultraviolet\src\00_core\generated\diag_registry.inc:1134`
- `Bootstrap\Ultraviolet\src\00_core\generated\diag_registry.inc:1142`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\OutputDiagnostics\OutputPipeline.uv:3`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\OutputDiagnostics\OutputPipeline.uv:21`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\OutputDiagnostics\OutputPipeline.uv:55`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\OutputDiagnostics\OutputPipeline.uv:89`

Observed behavior:

Several ordinary Phase 4 output-preparation failures synthesize
`core::Diagnostic` directly with `severity = Error` and a message, but without a
diagnostic code. These paths include missing or failed shared-library export
resolution, project codegen context preparation failure, output-project
construction failure in both `OutputPipeline` and the CLI driver, and codegen
cache construction failure. Some of these branches return from the pipeline
without recording an `Output-Pipeline-Err` conformance marker.

Expected behavior:

Chapter 2 defines diagnostics with optional codes, but states that normative
diagnostic tables define code-owned diagnostics and that `d.code = ⊥` auxiliary
diagnostics are admitted only when a feature section explicitly defines them.
The output pipeline rules route directory, object, and IR failures through
`Output-Pipeline-Err`, `Out-Dirs-Err`, `Out-Obj-Err`, and `Out-IR-Err`; section
24.8 owns output/backend failures with `E-OUT-0401` through `E-OUT-0418`.
Output-stage failures in this space should therefore be represented by the
applicable `E-OUT-*` code and owning rule rather than an unowned message-only
diagnostic.

Impact:

`uvc` can fail during Phase 4 with an `Error` diagnostic that has no normative
diagnostic id and, for some early-return branches, no matching output-pipeline
rule record. Machine consumers and fixtures cannot assert the specified
`E-OUT-*` condition, and an implementation failure becomes indistinguishable
from an explicitly admitted auxiliary diagnostic even though section 24.8 owns
the output/backend diagnostic space.

HelloUltraviolet fixture gap:

`OutputPipeline.uv` enumerates only the directory, object, and IR write
diagnostic paths for `E-OUT-0401`, `E-OUT-0402`, and `E-OUT-0403`. No fixture
covers output-project construction failure, shared-library export resolution or
preparation failure, or codegen-cache construction failure, so the no-code
Phase 4 branches can regress without a fixture detecting the missing `E-OUT-*`
diagnostic.

### UV-AUDIT-0279: Recursive compile-time evaluation crashes instead of reporting nontermination

Severity: High

Status: Confirmed with `uvc`.

Spec anchors:

- `Docs\SPECIFICATION.md:24807`
- `Docs\SPECIFICATION.md:24913`
- `Docs\SPECIFICATION.md:24921`
- `Docs\SPECIFICATION.md:24979`
- `Docs\SPECIFICATION.md:24984`
- `Docs\SPECIFICATION.md:25700`
- `Docs\SPECIFICATION.md:25711`
- `Docs\SPECIFICATION.md:30515`

Implementation anchors:

- `Bootstrap\Ultraviolet\include\03_comptime\comptime_internal.h:152`
- `Bootstrap\Ultraviolet\include\03_comptime\comptime_internal.h:190`
- `Bootstrap\Ultraviolet\include\03_comptime\comptime_internal.h:231`
- `Bootstrap\Ultraviolet\src\03_comptime\eval.cpp:395`
- `Bootstrap\Ultraviolet\src\03_comptime\eval.cpp:401`
- `Bootstrap\Ultraviolet\src\03_comptime\eval.cpp:446`
- `Bootstrap\Ultraviolet\src\03_comptime\eval.cpp:1129`
- `Bootstrap\Ultraviolet\src\03_comptime\rewrite.cpp:814`
- `Bootstrap\Ultraviolet\src\03_comptime\rewrite.cpp:820`
- `Bootstrap\Ultraviolet\src\00_core\generated\diag_registry.inc:114`
- `Bootstrap\Ultraviolet\src\00_core\generated\diag_registry.inc:666`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\typecheck_diag_map.inc:149`
- `HelloUltraviolet\Fixtures\AcceptedProjects\ComptimeConformance\Source\Library.uv:65`
- `HelloUltraviolet\Fixtures\AcceptedProjects\ComptimeConformance\Source\Library.uv:74`
- `HelloUltraviolet\Fixtures\AcceptedProjects\ComptimeConformance\Source\Library.uv:266`
- `HelloUltraviolet\Fixtures\AcceptedProjects\ComptimeConformance\Source\Library.uv:293`
- `HelloUltraviolet\Fixtures\RejectedSource\Comptime\ComptimeProcedureContractFailure\Expected.uv:3`
- `HelloUltraviolet\Fixtures\RejectedSource\Comptime\RuntimeCallsComptimeProcedure\Expected.uv:3`

Observed behavior:

A bounded `uvc` confirmation using a library with one compile-time procedure that
recursively returns its own result, then a `comptime` expression that calls it,
terminated the compiler with `fatal: Unhandled stack overflow` instead of
emitting a compile-time diagnostic. The command was:
`Bootstrap\Ultraviolet\build\windows\out\uvc.exe build __audit_tmp_ct_termination --check --target-profile x86_64-win64 --build-progress off --color never`.

The implementation path explains the crash. `CtEnv` stores compile-time
procedures in `procs`, `BindCtProc` records the declaration as a Phase 2 binding,
and `EvalCall` resolves a callee by looking it up in `env.procs`. Once found,
`EvalCall` evaluates the body with `EvalBlock(*it->second.body, proc_env)`.
There is no active-call set, step budget, recursion depth limit, or diagnostic
failure kind in `EvalResult`, and `rg` finds `E-CTE-0022` only in generated
diagnostic registries and maps rather than in a non-generated emission path.

Expected behavior:

Section 22 defines `CtEval` and `CtExec` as the Phase 2 execution judgments,
requires compile-time sites to execute as part of the compile-time pass, and
section 22.6 owns `E-CTE-0022` for "Compile-time evaluation did not terminate".
For recursive or otherwise nonterminating compile-time evaluation, the compiler
must leave the compile-time execution model through that diagnostic boundary,
not through unbounded host recursion and a process-level stack overflow.

Impact:

Valid source syntax can terminate the compiler process during Phase 2 instead of
producing the specified diagnostic stream. Build tooling receives a compiler
crash report rather than `E-CTE-0022`, and the generated diagnostic row is
effectively unreachable for the most direct source-level nontermination case.

HelloUltraviolet fixture gap:

HelloUltraviolet exercises successful compile-time procedures, compile-time
procedure contracts, and runtime calls to compile-time procedures. It has no
rejected fixture expecting `E-CTE-0022` for direct or mutual recursive
compile-time evaluation, so this crash path is not covered by the current
fixture surface.

### UV-AUDIT-0280: Quoted local using aliases reject permitted identifier splices

Severity: Medium

Status: Confirmed with `uvc`.

Spec anchors:

- `Docs\SPECIFICATION.md:19479`
- `Docs\SPECIFICATION.md:19484`
- `Docs\SPECIFICATION.md:25493`
- `Docs\SPECIFICATION.md:25503`
- `Docs\SPECIFICATION.md:25505`
- `Docs\SPECIFICATION.md:25507`
- `Docs\SPECIFICATION.md:25516`
- `Docs\SPECIFICATION.md:25526`
- `Docs\SPECIFICATION.md:25538`
- `Docs\SPECIFICATION.md:25542`
- `Docs\SPECIFICATION.md:25737`

Implementation anchors:

- `Bootstrap\Ultraviolet\include\02_source\ast\nodes\ast_stmts.h:89`
- `Bootstrap\Ultraviolet\include\02_source\ast\nodes\ast_stmts.h:91`
- `Bootstrap\Ultraviolet\src\02_source\parser\parser_paths.cpp:219`
- `Bootstrap\Ultraviolet\src\02_source\parser\parser_paths.cpp:241`
- `Bootstrap\Ultraviolet\src\02_source\parser\parser_paths.cpp:277`
- `Bootstrap\Ultraviolet\src\02_source\parser\parser_paths.cpp:283`
- `Bootstrap\Ultraviolet\src\02_source\parser\stmt\parse_stmt.cpp:68`
- `Bootstrap\Ultraviolet\src\02_source\parser\stmt\parse_stmt.cpp:175`
- `Bootstrap\Ultraviolet\src\02_source\parser\stmt\using_local_stmt.cpp:21`
- `Bootstrap\Ultraviolet\src\02_source\parser\stmt\using_local_stmt.cpp:24`
- `Bootstrap\Ultraviolet\src\02_source\parser\stmt\using_local_stmt.cpp:36`
- `Bootstrap\Ultraviolet\src\02_source\parser\stmt\using_local_stmt.cpp:48`
- `Bootstrap\Ultraviolet\src\02_source\parser\stmt\using_local_stmt.cpp:50`
- `Bootstrap\Ultraviolet\src\03_comptime\quote.cpp:759`
- `Bootstrap\Ultraviolet\src\03_comptime\quote.cpp:760`
- `Bootstrap\Ultraviolet\src\03_comptime\quote.cpp:1800`
- `Bootstrap\Ultraviolet\src\03_comptime\quote.cpp:1941`
- `Bootstrap\Ultraviolet\src\00_core\generated\diag_registry.inc:140`
- `HelloUltraviolet\Fixtures\AcceptedProjects\ComptimeConformance\Source\Library.uv:347`
- `HelloUltraviolet\Fixtures\AcceptedProjects\ComptimeConformance\Source\Library.uv:349`
- `HelloUltraviolet\Fixtures\AcceptedProjects\ComptimeConformance\Source\Library.uv:383`

Observed behavior:

A temporary `uvc` confirmation using a compile-time block with:
`let stmt_ast: Ast::Stmt = quote { using source_name as $alias_name }`
failed with `E-CTE-0220`, "Quoted content is syntactically invalid or
category-ambiguous", at the opening quote body. The command was:
`Bootstrap\Ultraviolet\build\windows\out\uvc.exe build __audit_tmp_quote_using --check --target-profile x86_64-win64 --build-progress off --color never`.

The parser path explains the failure. `ParseStmtCore` dispatches local using
statements to `ParseUsingLocalStmt`, but `ParseUsingLocalStmt` declares and calls
only structural `ParseIdent` for both `source` and `alias`. In quote mode,
`ParseIdent` records a restricted splice and fails instead of producing the
`SpliceIdentNode` that `ParseLocalIdent` can produce. The AST and quote builder
already have `UsingLocalStmt.source_splice_opt` and `alias_splice_opt`, and
`BuildStmt` tries to render both fields, but the active parser never populates
them for quoted local using statements.

Expected behavior:

Section 22.4 admits `SpliceIdentNode` in `using ... as` alias names, requires
statement quoted bodies to parse with `SpliceExprNode` and `SpliceIdentNode`
extensions, and defines string-valued identifier splices through
`RenderSplice(Identifier, ...)`. A quoted local using statement with a
string-valued alias splice should therefore parse as one statement and defer
alias-name materialization to `QuoteBuild` rather than being classified as
invalid quoted content.

Impact:

Metaprograms cannot generate local using aliases from string-valued identifier
splices, even though the AST and quote builder are shaped to support that
position. The compiler reports a generic quote syntax/category diagnostic for a
spec-permitted identifier-splice position.

HelloUltraviolet fixture gap:

The fixture surface covers local using statements, quoted identifier splices in
patterns and expressions, parameter identifier splices, and a non-spliced
top-level using alias. It does not cover a quoted local `using ... as $alias`
statement, so this parser regression is not exercised.

### UV-AUDIT-0281: Quoted region aliases reject permitted identifier splices

Severity: Medium

Status: Confirmed with `uvc`.

Spec anchors:

- `Docs\SPECIFICATION.md:19795`
- `Docs\SPECIFICATION.md:19812`
- `Docs\SPECIFICATION.md:19817`
- `Docs\SPECIFICATION.md:19820`
- `Docs\SPECIFICATION.md:19823`
- `Docs\SPECIFICATION.md:19825`
- `Docs\SPECIFICATION.md:25493`
- `Docs\SPECIFICATION.md:25503`
- `Docs\SPECIFICATION.md:25505`
- `Docs\SPECIFICATION.md:25507`
- `Docs\SPECIFICATION.md:25516`
- `Docs\SPECIFICATION.md:25526`
- `Docs\SPECIFICATION.md:25538`
- `Docs\SPECIFICATION.md:25542`
- `Docs\SPECIFICATION.md:25737`

Implementation anchors:

- `Bootstrap\Ultraviolet\include\02_source\ast\nodes\ast_stmts.h:154`
- `Bootstrap\Ultraviolet\src\02_source\parser\parser_paths.cpp:241`
- `Bootstrap\Ultraviolet\src\02_source\parser\parser_paths.cpp:277`
- `Bootstrap\Ultraviolet\src\02_source\parser\stmt\parse_stmt.cpp:127`
- `Bootstrap\Ultraviolet\src\02_source\parser\stmt\parse_stmt.cpp:134`
- `Bootstrap\Ultraviolet\src\02_source\parser\stmt\parse_stmt.cpp:245`
- `Bootstrap\Ultraviolet\src\02_source\parser\stmt\parse_stmt.cpp:247`
- `Bootstrap\Ultraviolet\src\02_source\parser\stmt\region_stmt.cpp:169`
- `Bootstrap\Ultraviolet\src\02_source\parser\stmt\region_stmt.cpp:209`
- `Bootstrap\Ultraviolet\src\02_source\parser\stmt\region_stmt.cpp:218`
- `Bootstrap\Ultraviolet\src\02_source\parser\stmt\region_stmt.cpp:242`
- `Bootstrap\Ultraviolet\src\03_comptime\quote.cpp:805`
- `Bootstrap\Ultraviolet\src\03_comptime\quote.cpp:807`
- `Bootstrap\Ultraviolet\src\03_comptime\quote.cpp:1800`
- `Bootstrap\Ultraviolet\src\03_comptime\quote.cpp:1941`
- `Bootstrap\Ultraviolet\src\00_core\generated\diag_registry.inc:140`
- `HelloUltraviolet\Source\Reference\Statements\Region.uv:13`
- `HelloUltraviolet\Source\Audit\Catalog\StatementsAndBlocks\Region.uv:47`

Observed behavior:

A temporary `uvc` confirmation using a compile-time block with:
`let stmt_ast: Ast::Stmt = quote { region as $alias_name { } }`
failed with `E-CTE-0220`, "Quoted content is syntactically invalid or
category-ambiguous", at the opening quote body. The command was:
`Bootstrap\Ultraviolet\build\windows\out\uvc.exe build __audit_tmp_quote_region --check --target-profile x86_64-win64 --build-progress off --color never`.

There are two region parsers. `region_stmt.cpp` contains a splice-aware
`ParseRegionAliasOpt` that calls `ParseLocalIdent` and moves
`alias_splice_opt` into the `RegionStmt`. The active statement dispatcher in
`parse_stmt.cpp` instead parses `region` inline with its private
`ParseRegionAliasOpt`, which returns only `std::optional<Identifier>` and calls
structural `ParseIdent`. As a result, quoted `region as $alias` reaches the
restricted-splice parse path and `RegionStmt.alias_splice_opt` is never
populated, even though `BuildStmt` knows how to render it.

Expected behavior:

Section 22.4 explicitly admits `SpliceIdentNode` in `region as` aliases, and
`ParseQuotedBody(Stmt, ...)` is the ordinary statement parser extended with
identifier splices. A quoted region statement with a string-valued alias splice
should parse as a statement, preserve the alias splice in `RegionStmt`, and let
`QuoteBuild` render it into the final alias identifier.

Impact:

Spec-valid quoted region statements with generated aliases are rejected as
invalid quoted content. Compile-time code cannot generate named region
statements from string-valued identifier splices despite dedicated AST and quote
builder support for that field.

HelloUltraviolet fixture gap:

HelloUltraviolet covers ordinary `region as name` parsing and the generic
quote/splice order obligation, but it has no accepted fixture for quoted
`region as $alias` and no catalog entry tying the region alias splice position
to the quote/splice conformance surface.

### UV-AUDIT-0282: Applied generic move parameters skip required drop cleanup

Severity: High

Status: Confirmed with `uvc`.

Spec anchors:

- `Docs\SPECIFICATION.md:2743`
- `Docs\SPECIFICATION.md:2744`
- `Docs\SPECIFICATION.md:12609`
- `Docs\SPECIFICATION.md:12632`
- `Docs\SPECIFICATION.md:28749`
- `Docs\SPECIFICATION.md:28750`
- `Docs\SPECIFICATION.md:28824`
- `Docs\SPECIFICATION.md:28829`
- `Docs\SPECIFICATION.md:28834`
- `Docs\SPECIFICATION.md:28843`
- `Docs\SPECIFICATION.md:28856`
- `Docs\SPECIFICATION.md:28878`
- `Docs\SPECIFICATION.md:29242`

Implementation anchors:

- `Bootstrap\Ultraviolet\include\04_analysis\typing\types.h:146`
- `Bootstrap\Ultraviolet\include\04_analysis\typing\types.h:223`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\type_lower.cpp:299`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\type_lower.cpp:314`
- `Bootstrap\Ultraviolet\src\05_codegen\cleanup\drop_hooks.cpp:477`
- `Bootstrap\Ultraviolet\src\05_codegen\cleanup\drop_hooks.cpp:549`
- `Bootstrap\Ultraviolet\src\05_codegen\cleanup\drop_hooks.cpp:550`
- `Bootstrap\Ultraviolet\src\05_codegen\cleanup\cleanup.cpp:1211`
- `Bootstrap\Ultraviolet\src\05_codegen\cleanup\cleanup.cpp:1220`
- `Bootstrap\Ultraviolet\src\05_codegen\cleanup\cleanup.cpp:1273`
- `Bootstrap\Ultraviolet\src\05_codegen\cleanup\cleanup.cpp:1667`
- `HelloUltraviolet\Source\Reference\Polymorphism\GenericParameters.uv:5`
- `HelloUltraviolet\Source\Reference\Polymorphism\GenericParameters.uv:72`
- `HelloUltraviolet\Fixtures\ArtifactProjects\EmitLlLibrary\Source\Library.uv:29`
- `HelloUltraviolet\Fixtures\ArtifactProjects\EmitLlLibrary\Source\Library.uv:847`

Observed behavior:

A temporary `uvc` confirmation declared `AuditBox<TValue>` and a procedure
with a move parameter `box: AuditBox<unique string@Managed>`. The command was:
`Bootstrap\Ultraviolet\build\windows\out\uvc.exe build __audit_tmp_generic_drop_typeapply --emit-ir --target-profile x86_64-win64 --build-progress off --color never`.

The emitted procedure body for `auditDropApplied` contains only no-op/block
cleanup scaffolding and returns. It emits neither the specialized
`AuditBox<unique string@Managed>` drop glue call nor the managed-string runtime
drop call.

The implementation path matches the output. Generic type syntax lowers to
`TypeApply`, and drop-need analysis explicitly handles `TypeApply` by applying
the nominal declaration's generic arguments before checking fields. `EmitDropImpl`
does not have a `TypeApply` branch; after the positive `TypeNeedsDrop` result,
execution falls through to the final empty IR return. Therefore a responsible
binding or move parameter whose type is represented as `TypeApply` can be known
to require cleanup but still lower to no cleanup IR.

Expected behavior:

`TypeApply(path, args)` denotes the specialized declaration named by `path`.
`EmitDrop(T, v)` must satisfy `EmitDropSpec`, and `DropValueOut` must perform
the applicable drop call and child cleanup. For `AuditBox<unique string@Managed>`,
the specialized field type is `unique string@Managed`, so dropping the applied
record must eventually call `StringDropSym` for that field.

Impact:

Move parameters, locals, and other responsible values typed through applied
generic nominal syntax can leak managed strings, managed bytes, and user-owned
children. Any child drop panic is also skipped, so cleanup ordering and panic
observability diverge from the abstract cleanup rules.

HelloUltraviolet fixture gap:

HelloUltraviolet exercises generic records with `i32` and `bool` payloads and an
FFI generic box with an `i32` payload. The cleanup catalog covers `EmitDropSpec`
and managed string drop symbols, but there is no fixture that drops a
`TypeApply`-represented generic value whose applied argument requires cleanup.

### UV-AUDIT-0283: Generic record and modal-state drop glue uses unsubstituted fields

Severity: High

Status: Confirmed with `uvc`.

Spec anchors:

- `Docs\SPECIFICATION.md:9853`
- `Docs\SPECIFICATION.md:10644`
- `Docs\SPECIFICATION.md:10673`
- `Docs\SPECIFICATION.md:10678`
- `Docs\SPECIFICATION.md:12609`
- `Docs\SPECIFICATION.md:12632`
- `Docs\SPECIFICATION.md:25316`
- `Docs\SPECIFICATION.md:25810`
- `Docs\SPECIFICATION.md:25812`
- `Docs\SPECIFICATION.md:25824`
- `Docs\SPECIFICATION.md:25825`
- `Docs\SPECIFICATION.md:28749`
- `Docs\SPECIFICATION.md:28750`
- `Docs\SPECIFICATION.md:28829`
- `Docs\SPECIFICATION.md:28834`
- `Docs\SPECIFICATION.md:28835`
- `Docs\SPECIFICATION.md:28840`
- `Docs\SPECIFICATION.md:28843`
- `Docs\SPECIFICATION.md:28878`
- `Docs\SPECIFICATION.md:29242`
- `Docs\SPECIFICATION.md:30327`
- `Docs\SPECIFICATION.md:30328`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\typing\expr\record_literal.cpp:473`
- `Bootstrap\Ultraviolet\src\05_codegen\cleanup\drop_hooks.cpp:192`
- `Bootstrap\Ultraviolet\src\05_codegen\cleanup\drop_hooks.cpp:477`
- `Bootstrap\Ultraviolet\src\05_codegen\cleanup\cleanup.cpp:492`
- `Bootstrap\Ultraviolet\src\05_codegen\cleanup\cleanup.cpp:499`
- `Bootstrap\Ultraviolet\src\05_codegen\cleanup\cleanup.cpp:1426`
- `Bootstrap\Ultraviolet\src\05_codegen\cleanup\cleanup.cpp:1450`
- `Bootstrap\Ultraviolet\src\05_codegen\cleanup\cleanup.cpp:1497`
- `Bootstrap\Ultraviolet\src\05_codegen\cleanup\cleanup.cpp:1614`
- `Bootstrap\Ultraviolet\src\05_codegen\cleanup\cleanup.cpp:1617`
- `Bootstrap\Ultraviolet\src\05_codegen\cleanup\cleanup.cpp:1638`
- `Bootstrap\Ultraviolet\src\05_codegen\cleanup\cleanup.cpp:1692`
- `Bootstrap\Ultraviolet\src\05_codegen\cleanup\cleanup.cpp:1703`
- `Bootstrap\Ultraviolet\src\05_codegen\cleanup\cleanup.cpp:1769`
- `Bootstrap\Ultraviolet\src\05_codegen\cleanup\cleanup.cpp:1775`
- `Bootstrap\Ultraviolet\src\05_codegen\cleanup\cleanup.cpp:1786`
- `Bootstrap\Ultraviolet\src\05_codegen\cleanup\cleanup.cpp:1823`
- `Bootstrap\Ultraviolet\src\05_codegen\cleanup\cleanup.cpp:1844`
- `Bootstrap\Ultraviolet\src\05_codegen\cleanup\cleanup.cpp:1863`
- `HelloUltraviolet\Source\Reference\Polymorphism\GenericParameters.uv:72`
- `HelloUltraviolet\Source\Reference\Polymorphism\GenericParameters.uv:74`
- `HelloUltraviolet\Source\Reference\Lowering\CleanupDropUnwinding.uv:153`
- `HelloUltraviolet\Source\Audit\Catalog\CommonLoweringProgramLifecycleAndBackend\CleanupDropAndUnwindingFramework.uv:29`
- `HelloUltraviolet\Source\Audit\Catalog\CommonLoweringProgramLifecycleAndBackend\CleanupDropAndUnwindingFramework.uv:254`
- `HelloUltraviolet\Source\Audit\Catalog\CommonLoweringProgramLifecycleAndBackend\RuntimeInterface.uv:398`

Observed behavior:

A temporary record fixture declared `AuditBox<TValue>` and assigned:
`let box: AuditBox<unique string@Managed> = AuditBox { value: move text }`.
The command was:
`Bootstrap\Ultraviolet\build\windows\out\uvc.exe build __audit_tmp_generic_drop_record_literal --emit-ir --target-profile x86_64-win64 --build-progress off --color never`.
The caller emits a call to specialized `AuditBox<unique string@Managed>` drop
glue, but the generated drop procedure body only loads `%data` and then emits a
no-op. It does not call the managed-string drop runtime symbol.

A temporary modal-state fixture declared `AuditState<TValue>@Holding { value:
TValue }` and assigned an `AuditState<unique string@Managed>@Holding` value.
The command was:
`Bootstrap\Ultraviolet\build\windows\out\uvc.exe build __audit_tmp_generic_drop_modal --emit-ir --target-profile x86_64-win64 --build-progress off --color never`.
The caller emits a call to specialized
`AuditState<unique string@Managed>@Holding` drop glue, but that generated drop
procedure body also loads `%data` and then no-ops.

The implementation explains both outputs. Record literals can synthesize
`MakeTypePath(type_path, record_generic_args)`, and `TypeNeedsDropImpl`
substitutes the generic arguments when deciding whether record or modal fields
need cleanup. Once drop glue is emitted, `DropGlueIR` calls `EmitDropImpl` with
`allow_drop_glue = false`. The record path delegates to `EmitDropFields`, but
`EmitDropFields` lowers each declaration field type directly and never applies
the record's `generic_args`. The modal-state path calls `CollectModalFields`,
which also lowers raw declaration field types, and general-modal cleanup builds
state types with `MakeTypeModalState(type_path.path, state.name)` without
preserving the generic arguments.

Expected behavior:

Applied record and modal payload fields must be cleaned up under the same
substitution used for type checking, layout, and drop-need analysis. The
generated `DropGlueIR(T)` for a specialized generic record or modal state must
satisfy `DropGlueSpec(T, IR)`, so child cleanup must use `TypeSubst(theta,
field_type)` and must call `StringDropSym` for a specialized
`unique string@Managed` field.

Impact:

The compiler can correctly decide that a specialized generic record or modal
state requires cleanup, emit and call specialized drop glue, then generate an
empty drop body for its substituted owning fields. This leaks owned payloads and
suppresses child drop panics for generic record and modal-state values.

HelloUltraviolet fixture gap:

The test surface has generic record layout checks and broad cleanup catalog
markers, but it lacks a fixture that inspects emitted cleanup for a generic
record or modal-state payload instantiated with `unique string@Managed` or
`unique bytes@Managed`.

### UV-AUDIT-0284: Quoted generic class-bound type arguments are not built

Severity: Medium

Status: Confirmed by implementation inspection and a temporary `uvc` accepted
fixture.

Spec anchors:

- `Docs\SPECIFICATION.md:12290`
- `Docs\SPECIFICATION.md:12291`
- `Docs\SPECIFICATION.md:12292`
- `Docs\SPECIFICATION.md:12373`
- `Docs\SPECIFICATION.md:25517`
- `Docs\SPECIFICATION.md:25524`
- `Docs\SPECIFICATION.md:25538`
- `Docs\SPECIFICATION.md:25542`
- `Docs\SPECIFICATION.md:30622`
- `Docs\SPECIFICATION.md:30623`
- `Docs\SPECIFICATION.md:30625`
- `Docs\SPECIFICATION.md:30626`
- `Docs\SPECIFICATION.md:30627`

Implementation anchors:

- `Bootstrap\Ultraviolet\include\02_source\ast\nodes\ast_items.h:151`
- `Bootstrap\Ultraviolet\include\02_source\ast\nodes\ast_items.h:154`
- `Bootstrap\Ultraviolet\src\02_source\parser\item\generic_params.cpp:163`
- `Bootstrap\Ultraviolet\src\02_source\parser\item\generic_params.cpp:170`
- `Bootstrap\Ultraviolet\src\02_source\parser\item\generic_params.cpp:172`
- `Bootstrap\Ultraviolet\src\02_source\parser\item\generic_params.cpp:174`
- `Bootstrap\Ultraviolet\src\02_source\parser\item\generic_params.cpp:175`
- `Bootstrap\Ultraviolet\src\03_comptime\quote.cpp:591`
- `Bootstrap\Ultraviolet\src\03_comptime\quote.cpp:599`
- `Bootstrap\Ultraviolet\src\03_comptime\quote.cpp:910`
- `Bootstrap\Ultraviolet\src\03_comptime\quote.cpp:966`
- `Bootstrap\Ultraviolet\src\03_comptime\quote.cpp:1013`
- `Bootstrap\Ultraviolet\src\03_comptime\quote.cpp:1056`
- `Bootstrap\Ultraviolet\src\03_comptime\quote.cpp:1108`
- `Bootstrap\Ultraviolet\src\03_comptime\quote.cpp:1143`
- `Bootstrap\Ultraviolet\src\03_comptime\quote.cpp:1164`
- `Bootstrap\Ultraviolet\src\03_comptime\quote.cpp:1184`
- `Bootstrap\Ultraviolet\src\03_comptime\quote.cpp:1197`
- `Bootstrap\Ultraviolet\src\03_comptime\quote.cpp:1232`
- `Bootstrap\Ultraviolet\src\03_comptime\quote.cpp:1247`
- `Bootstrap\Ultraviolet\src\03_comptime\quote.cpp:1259`
- `HelloUltraviolet\Source\Reference\Comptime\QuoteSpliceEmission.uv:11`
- `HelloUltraviolet\Source\Reference\Comptime\QuoteSpliceEmission.uv:13`
- `HelloUltraviolet\Source\Reference\Comptime\QuoteSpliceEmission.uv:21`
- `HelloUltraviolet\Source\Reference\Comptime\QuoteSpliceEmission.uv:24`
- `HelloUltraviolet\Fixtures\AcceptedProjects\ComptimeConformance\Source\Library.uv:337`
- `HelloUltraviolet\Fixtures\AcceptedProjects\ComptimeConformance\Source\Library.uv:339`
- `HelloUltraviolet\Fixtures\AcceptedProjects\ComptimeConformance\Source\Library.uv:347`
- `HelloUltraviolet\Fixtures\AcceptedProjects\ComptimeConformance\Source\Library.uv:350`
- `HelloUltraviolet\Fixtures\AcceptedProjects\ComptimeConformance\Source\Library.uv:383`

Observed behavior:

`ParseTypeBounds` parses a class bound's optional generic arguments and stores
them in `TypeBound.generic_args`. `BuildType` recursively quote-builds generic
arguments on ordinary type path and modal-state type nodes, but
`BuildGenericParamsInPlace` only visits `param.default_type`. It never visits
any `param.bounds[*].generic_args`.

A temporary confirmation project emitted a quoted item containing:
`public procedure emittedQuoteBound<TValue <: AuditBound<$(bound_arg)>>() -> ()`.
The command
`Bootstrap\Ultraviolet\build\windows\out\uvc.exe build __audit_tmp_quote_bound --check --conformance quote_bound --target-profile x86_64-win64 --build-progress off --color never`
succeeded, and the conformance log recorded `Parse-ClassBound` with
`args_opt=some;arg_count=1` for the quoted item. There was no quote-build path in
`BuildGenericParamsInPlace` that could have rendered the type splice contained
inside that bound argument.

Expected behavior:

The item quote parser admits ordinary item syntax extended with splice nodes,
and `class_bound` admits `generic_args`, whose elements are types. `QuoteBuild`
must evaluate every splice in source order and render type-position splices
through `RenderSplice(Type, ...)`. A type splice inside a generic class-bound
argument should therefore be built before the quoted item is emitted.

Impact:

Quoted declarations can carry unrendered type splices inside generic
class-bound arguments. The issue applies to every declaration kind that carries
`generic_params`: procedures, methods, records, enums, modals, classes, extern
procedures, and type aliases. Those declarations can be emitted with invalid or
stale generic constraints even though the visible quote expression succeeds.

HelloUltraviolet fixture gap:

HelloUltraviolet covers quoted return-type, expression, pattern, statement,
parameter, and top-level using alias splices. It does not cover a quoted item
whose generic parameter class bound contains a spliced type argument.

### UV-AUDIT-0285: User generic parameters are inferred variant instead of invariant

Severity: High

Status: Confirmed with `uvc`.

Spec anchors:

- `Docs\SPECIFICATION.md:6230`
- `Docs\SPECIFICATION.md:6232`
- `Docs\SPECIFICATION.md:6239`
- `Docs\SPECIFICATION.md:12437`
- `Docs\SPECIFICATION.md:12618`
- `Docs\SPECIFICATION.md:12619`
- `Docs\SPECIFICATION.md:12624`
- `Docs\SPECIFICATION.md:13942`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\typing\subtyping.cpp:252`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\subtyping.cpp:902`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\subtyping.cpp:903`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\subtyping.cpp:908`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\subtyping.cpp:910`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\subtyping.cpp:919`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\subtyping.cpp:952`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\variance.cpp:121`
- `HelloUltraviolet\Fixtures\RejectedSource\DataTypes\GenericCovariantSubtyping\Source\Main.uv:5`
- `HelloUltraviolet\Fixtures\RejectedSource\DataTypes\GenericCovariantSubtyping\Source\Main.uv:10`
- `HelloUltraviolet\Fixtures\RejectedSource\DataTypes\GenericInvariantSubtyping\Source\Main.uv:3`
- `HelloUltraviolet\Fixtures\RejectedSource\DataTypes\GenericInvariantSubtyping\Source\Main.uv:9`
- `HelloUltraviolet\Source\Reference\Types\Subtyping.uv:35`
- `HelloUltraviolet\Source\Reference\Types\Subtyping.uv:236`

Observed behavior:

A temporary `uvc` confirmation declared:
`type NarrowValue = i32`, `type WideValue = i32 | bool`, and
`record AuditBox<TValue> { value: TValue }`. It then assigned
`AuditBox<NarrowValue>` to `AuditBox<WideValue>`. The command was:
`Bootstrap\Ultraviolet\build\windows\out\uvc.exe build __audit_tmp_generic_invariance --check --target-profile x86_64-win64 --build-progress off --color never`.
The build succeeded.

The subtyping code derives a parameter's effective variance from member type
positions. A direct field occurrence of `TValue` is treated as covariant by
`VarianceOf`, a function-parameter occurrence is treated contravariantly, and
invariance arises only from mixed or invariant member positions.

Expected behavior:

The specification states that user-declared generic parameters default to
invariant variance. `Sub-Generic` should use `VarianceOf(path, i)` from the
declared generic parameter metadata, and a default user parameter should require
exact argument equivalence. The temporary assignment should therefore fail with
the invariant generic diagnostic.

Impact:

The compiler accepts non-equivalent applied user-defined nominal types as
subtypes when a generic parameter appears only in covariant or contravariant
positions. This can admit assignments and argument passing that the spec's
default invariant semantics reject.

HelloUltraviolet fixture gap:

The existing rejected fixtures encode the implementation's inferred-variance
model: direct-field generics are treated as covariant, function-parameter
generics as contravariant, and arrays as invariant. There is no fixture
asserting that user-defined generic parameters are invariant by default.

### UV-AUDIT-0286: Default-completed type applications compare as different types

Severity: Medium

Status: Confirmed with `uvc`.

Spec anchors:

- `Docs\SPECIFICATION.md:12463`
- `Docs\SPECIFICATION.md:12468`
- `Docs\SPECIFICATION.md:12609`
- `Docs\SPECIFICATION.md:12632`
- `Docs\SPECIFICATION.md:25316`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\typing\type_equiv.cpp:345`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\type_equiv.cpp:366`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\subtyping.cpp:902`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\subtyping.cpp:908`
- `HelloUltraviolet\Source\Reference\Polymorphism\GenericParameters.uv:5`
- `HelloUltraviolet\Source\Reference\Polymorphism\GenericParameters.uv:72`
- `HelloUltraviolet\Source\Reference\Polymorphism\GenericParameters.uv:74`
- `HelloUltraviolet\Source\Reference\Polymorphism\GenericParameters.uv:77`
- `HelloUltraviolet\Source\Reference\Polymorphism\GenericParameters.uv:80`

Observed behavior:

A temporary `uvc` confirmation declared
`record AuditDefaultBox<TValue; TMirror = TValue>` and then assigned
`AuditDefaultBox<i32>` to `AuditDefaultBox<i32, i32>`. The command was:
`Bootstrap\Ultraviolet\build\windows\out\uvc.exe build __audit_tmp_generic_defaults --check --target-profile x86_64-win64 --build-progress off --color never`.
The compiler rejected the assignment with `E-MOD-2402`,
"Type annotation incompatible with inferred type".

The type equivalence and generic subtyping implementations compare the written
generic argument vector lengths directly. They do not complete omitted defaults
with `DefaultArgs` before comparing argument lists.

Expected behavior:

`TypeApply(path, args)` denotes the specialized declaration after default
argument completion and substitution. Applications whose completed argument
lists are equivalent should be treated as the same specialization for type
equivalence and subtyping.

Impact:

Valid code can be rejected whenever one surface writes omitted default generic
arguments and another writes the equivalent explicit defaulted form. The
compiler can also treat the same monomorphic specialization as two distinct
nominal applications.

HelloUltraviolet fixture gap:

HelloUltraviolet declares a defaulted generic record and exercises omitted and
explicit forms separately, but it does not compare assignment, argument passing,
or subtyping between the omitted-default and explicit-default forms.

### UV-AUDIT-0287: Async liveness misses address-of and static-store uses after suspension

Severity: High

Status: Static implementation evidence.

Spec anchors:

- `Docs\SPECIFICATION.md:24544`
- `Docs\SPECIFICATION.md:24548`
- `Docs\SPECIFICATION.md:24554`
- `Docs\SPECIFICATION.md:24562`

Implementation anchors:

- `Bootstrap\Ultraviolet\include\05_codegen\ir\ir_model.h:189`
- `Bootstrap\Ultraviolet\include\05_codegen\ir\ir_model.h:232`
- `Bootstrap\Ultraviolet\include\05_codegen\ir\ir_model.h:674`
- `Bootstrap\Ultraviolet\include\05_codegen\ir\ir_model.h:682`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\lower_proc.cpp:720`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\lower_proc.cpp:876`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\lower_proc.cpp:881`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\lower_proc.cpp:883`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\lower_proc.cpp:885`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\lower_proc.cpp:886`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\lower_proc.cpp:950`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\lower_proc.cpp:951`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\lower_proc.cpp:1218`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\memory\addr_of.cpp:9`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\memory\addr_of.cpp:43`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\memory\addr_of.cpp:140`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\memory\store_global.cpp:66`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\memory\store_global.cpp:92`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\proc_emit.cpp:577`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\proc_emit.cpp:603`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\proc_emit.cpp:619`
- `HelloUltraviolet\Fixtures\ArtifactProjects\AsyncObligations\Source\Main.uv:31`
- `HelloUltraviolet\Fixtures\ArtifactProjects\AsyncObligations\Source\Main.uv:43`

Observed behavior:

`CollectUsesAfterSuspension` handles many post-suspension IR node types, but it
has no branch for `IRStoreGlobal` and no branch for `IRAddrOf`. `IRStoreGlobal`
contains a value operand, and `IRAddrOf` contains an `IRPlace` root. The LLVM
emitters later read those operands through normal local storage and binding
state. If a local binding is used after a suspension only through a static store
RHS or an address-of root, it is omitted from `uses_after_suspension`, so no
async frame slot is created or restored for it.

Expected behavior:

`FrameSlots(proc)` includes every binding live across suspension. A binding used
by post-suspension address-of or static-store IR is live on a path from a
suspension point and must be included in `LiveAcrossSuspend(proc)`.

Impact:

Valid async code can resume without storage for a pre-suspension local whose
only later use is address-of or static-store assignment. Depending on the
lowered form, the generated code can write a default/missing value or fail
codegen when address-of finds binding state but no local storage.

HelloUltraviolet fixture gap:

The async artifact fixture covers scalar yield and resume paths. It does not
cover an async procedure that uses a pre-suspension local only through
post-suspension address-of or static assignment.

### UV-AUDIT-0288: Async frame allocation failure is dereferenced during suspension

Severity: High

Status: Static implementation evidence.

Spec anchors:

- `Docs\SPECIFICATION.md:24520`
- `Docs\SPECIFICATION.md:24544`
- `Docs\SPECIFICATION.md:24554`
- `Docs\SPECIFICATION.md:24562`

Implementation anchors:

- `Bootstrap\Ultraviolet\runtime\src\memory\async.c:83`
- `Bootstrap\Ultraviolet\runtime\src\memory\async.c:86`
- `Bootstrap\Ultraviolet\runtime\src\memory\async.c:100`
- `Bootstrap\Ultraviolet\runtime\src\memory\async.c:105`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\yield.cpp:218`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\yield.cpp:224`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\yield.cpp:229`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\yield.cpp:238`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\yield.cpp:243`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\yield.cpp:258`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\yield.cpp:263`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\yield.cpp:269`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\yield.cpp:274`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\yield_from.cpp:404`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\yield_from.cpp:410`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\yield_from.cpp:415`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\yield_from.cpp:424`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\yield_from.cpp:429`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\yield_from.cpp:444`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\yield_from.cpp:449`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\yield_from.cpp:455`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\yield_from.cpp:460`

Observed behavior:

The runtime `async::alloc_frame` returns `NULL` for zero size, size overflow, and
raw heap allocation failure. The `yield` and `yield from` emitters call the
allocator and then only check whether an LLVM value object exists. They do not
emit a runtime null check on the returned pointer before computing frame-header
addresses and storing resume state, resume function, hosted environment, and key
snapshot fields through that pointer.

Expected behavior:

Async frame initialization must either obtain a valid frame before initializing
the frame and resumption point, or convert allocation failure into a defined
non-normal outcome. Generated code must not store through a null async frame.

Impact:

Allocation failure or frame-size overflow can crash generated async code during
suspension, or produce an invalid suspended async value without a valid
resumption frame.

HelloUltraviolet fixture gap:

No fixture forces `runtime::async::alloc_frame` failure or validates that
suspension cannot expose an invalid frame.

### UV-AUDIT-0289: Async failure state materializes `@Failed` without frame-slot cleanup

Severity: High

Status: Static implementation evidence.

Spec anchors:

- `Docs\SPECIFICATION.md:24527`
- `Docs\SPECIFICATION.md:24531`
- `Docs\SPECIFICATION.md:24539`
- `Docs\SPECIFICATION.md:24544`
- `Docs\SPECIFICATION.md:24548`
- `Docs\SPECIFICATION.md:24593`
- `Docs\SPECIFICATION.md:24596`

Implementation anchors:

- `Bootstrap\Ultraviolet\include\05_codegen\ir\ir_model.h:660`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\async_fail.cpp:10`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\async_fail.cpp:15`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\async_fail.cpp:54`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\async_fail.cpp:68`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\async_fail.cpp:72`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\async_fail.cpp:119`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\async\async_fail.cpp:150`
- `Bootstrap\Ultraviolet\runtime\src\memory\async.c:119`
- `Bootstrap\Ultraviolet\runtime\src\memory\async.c:137`
- `Bootstrap\Ultraviolet\runtime\src\memory\async.c:168`
- `HelloUltraviolet\Fixtures\ArtifactProjects\AsyncObligations\Source\Main.uv:72`
- `HelloUltraviolet\Fixtures\ArtifactProjects\AsyncObligations\Source\Main.uv:89`

Observed behavior:

`IRAsyncFail` constructs an `@Failed` async modal value by writing the failed
discriminator and copying the error payload. The implementation records
`requirement.21.AsyncFailStateIRSemantics`, but it does not emit any defer
execution, frame-slot drop sequence, or frame release as part of the failure
state construction. The runtime `async::resume` calls the resume function and
returns the settled value without invoking `async::free_frame`.

Expected behavior:

`AsyncFailStateIR(f)` must execute defer blocks and drop live frame slots in
reverse cleanup order before materializing `@Failed { error }`. A resumed async
frame that fails must not leave non-bitcopy frame slots or key snapshots alive
after the failure state is returned.

Impact:

Non-bitcopy state preserved across suspension can remain in the async frame
after failure. Child cleanup and child drop panics are skipped, and the async
frame itself can remain allocated after the resumed computation settles as
failed.

HelloUltraviolet fixture gap:

The async obligations fixture covers failed async paths with scalar values. It
does not cover an async procedure that suspends with a non-bitcopy live binding
and then fails after resume while asserting frame-slot cleanup.

### UV-AUDIT-0290: Catch-mode FFI imports can continue with zero result after foreign unwind

Severity: High

Status: Static implementation evidence.

Spec anchors:

- `Docs\SPECIFICATION.md:26118`
- `Docs\SPECIFICATION.md:26922`
- `Docs\SPECIFICATION.md:26923`
- `Docs\SPECIFICATION.md:26924`
- `Docs\SPECIFICATION.md:26948`
- `Docs\SPECIFICATION.md:26952`
- `Docs\SPECIFICATION.md:28812`
- `Docs\SPECIFICATION.md:30003`
- `Docs\SPECIFICATION.md:30004`
- `Docs\SPECIFICATION.md:30006`

Implementation anchors:

- `Bootstrap\Ultraviolet\src\05_codegen\lower\lower_module.cpp:2212`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\lower_module.cpp:2224`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\expr_context.cpp:1714`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\expr_context.cpp:1742`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\call.cpp:2296`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\call.cpp:2375`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\llvm_call.cpp:2133`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\llvm_call.cpp:2151`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\llvm_call.cpp:2203`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\llvm_call.cpp:2205`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\llvm_call.cpp:2214`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\llvm_call.cpp:2250`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\llvm_call.cpp:2252`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\checks\panic.cpp:178`
- `HelloUltraviolet\Fixtures\ArtifactProjects\EmitLlLibrary\Source\Library.uv:785`
- `HelloUltraviolet\Fixtures\ArtifactProjects\EmitLlLibrary\Source\Library.uv:787`
- `HelloUltraviolet\Fixtures\ArtifactProjects\EmitLlLibrary\Source\Library.uv:809`
- `HelloUltraviolet\Source\Reference\FFI\BoundaryUnwinding.uv:4`
- `HelloUltraviolet\Source\Reference\FFI\BoundaryUnwinding.uv:10`
- `HelloUltraviolet\Source\Reference\FFI\BoundaryUnwinding.uv:29`
- `HelloUltraviolet\Source\Audit\ArtifactProjectExecution.uv:2806`
- `HelloUltraviolet\Source\Audit\ArtifactProjectExecution.uv:2834`
- `HelloUltraviolet\Source\Audit\ArtifactProjectExecution.uv:2844`
- `HelloUltraviolet\Source\Audit\ArtifactProjectExecution.uv:2845`

Observed behavior:

Extern import signatures are registered with only their source-visible
parameters and return type. `NeedsPanicOutForSymbol` returns true only when the
registered signature's last parameter is `__panic`, so ordinary extern imports
do not get a panic-out argument or `PanicFollowup(ctx)` in call lowering.

The LLVM catch-import path emits an `invoke`. In the unwind block, it stores a
panic record, writes zero to SRet storage when present, and branches to the
normal continuation. In the normal continuation, non-void imports get a PHI that
selects the real invoke result on the normal edge and a zero/null value on the
unwind edge. Because the source call had no panic follow-up, the pending panic
record is not consumed before source code observes the result value.

Expected behavior:

Catch-mode imported procedures convert foreign unwinds to Ultraviolet panic
control. The zero-return behavior is specified for raw and hosted export
boundaries, not imported procedure calls. After the catch landing pad writes a
panic record, lowering must route through `PanicCheck` semantics rather than
continuing as an ordinary zero-valued result.

Impact:

A foreign unwind through a catch-mode import can be observed by source code as
an ordinary zero or null result. That hides the unwind, bypasses expected panic
control and cleanup behavior, and makes import-boundary behavior match the
export zero-return rule instead of the import panic-conversion rule.

HelloUltraviolet fixture gap:

The EmitLl artifact fixture declares abort and catch imports and checks emitted
IR markers such as `ffi.invoke.unwind` and `ffi_invoke_result`. The reference
FFI surface covers normal return through a catch import. No fixture makes the
imported foreign boundary actually unwind and asserts panic control rather than
zero-result continuation.

### UV-AUDIT-0291: Source-native `covers(...)` claims can be constant-body traces

Severity: High

Status: Static HelloUltraviolet evidence.

Scope:

This is a HelloUltraviolet test-surface finding.

Spec anchors:

- `Docs\SPECIFICATION.md:7251`
- `Docs\SPECIFICATION.md:7258`
- `Docs\SPECIFICATION.md:7266`
- `Docs\SPECIFICATION.md:7276`
- `Docs\SPECIFICATION.md:7284`

Implementation/test anchors:

- `HelloUltraviolet\Source\Tests\SourceNativeTests.uv:117`
- `HelloUltraviolet\Source\Tests\SourceNativeTests.uv:124`
- `HelloUltraviolet\Source\Tests\SourceNativeTests.uv:131`
- `HelloUltraviolet\Source\Tests\SourceNativeTests.uv:150`
- `HelloUltraviolet\Source\Tests\SourceNativeTests.uv:157`
- `HelloUltraviolet\Fixtures\ArtifactProjects\AttributeMetadataDiscovery\Source\Tests\Metadata.uv:17`
- `HelloUltraviolet\Fixtures\ArtifactProjects\AttributeTestAuthorityAndShape\Source\Tests\AuthorityAndShape.uv:80`
- `HelloUltraviolet\Source\Audit\AttributesMetadataArtifactExecution.uv:1346`
- `HelloUltraviolet\Source\Audit\AttributesMetadataArtifactExecution.uv:1418`

Observed behavior:

Several source-native test procedures claim obligations through `covers(...)`
while the selected procedure body returns a constant success value. Examples
include `grammar.TestAttribute@L28723`, `def.TestName@L28776`,
`def.TestCoverage@L28791`, `req.TestProcedureShape@L28842`, and
`conformance.TestAttributeDynamicSemantics@L28884`.

The stronger artifact wrapper for metadata discovery checks generated
conformance-log payloads such as coverage count/order, display names, selected
test counts, and stable identities. That proves discovery metadata was recorded,
but it does not make the constant-body `#test` procedures exercise the grammar,
shape, or dynamic semantics obligations themselves.

Expected behavior:

A `covers(...)` reference should be treated as a trace link only. A source-native
test should not satisfy an obligation unless its body, expected diagnostic,
runtime result, artifact assertion, or reference-model comparison demonstrates
the required behavior.

Impact:

HelloUltraviolet can present valid-looking source-native obligation claims where
the executed code performs no obligation-specific check. That allows a trace row
to be mistaken for actual exercise of source-native test semantics.

### UV-AUDIT-0292: Accepted exercise quality gate validates execution, not obligation semantics

Severity: High

Status: Static HelloUltraviolet evidence with local inventory.

Scope:

This is a HelloUltraviolet test-surface and conformance-accounting finding.

Spec/test anchors:

- `Docs\SPECIFICATION.md:7284`
- `HelloUltraviolet\Source\Audit\CatalogExerciseQuality.uv:36`
- `HelloUltraviolet\Source\Audit\CatalogExerciseQuality.uv:52`
- `HelloUltraviolet\Source\Audit\CatalogExerciseQuality.uv:76`
- `HelloUltraviolet\Source\Audit\CatalogExerciseQuality.uv:119`
- `HelloUltraviolet\Source\Audit\CatalogExerciseQuality.uv:132`
- `HelloUltraviolet\Source\Api.uv:775`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:2`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:5`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:24`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:27`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:29`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:66`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:313`

Observed behavior:

`catalogExerciseQualityEntryPasses` rejects only three conditions:
`is_constant_literal_result`, `!is_executed`, and a narrow appendix grammar
shape. It does not compare an obligation's normative premises with the target
procedure's actual assertions. The public gate then requires the validated count
to equal `5305` and checks only the byte count, line count, and digest of
`HelloUltraviolet/Audit/ExerciseQualityManifest.csv`.

A local inventory of the generated entries found 5,305 rows mapped to 552 unique
targets. The largest single target,
`artifactProjectEmitLlExercise`, receives 500 obligation rows. Other broad
targets include `runNamesQualifiedResolutionReference` with 261 rows,
`runModulesAggregationReference` with 172 rows, and
`artifactProjectRuntimeInterfaceSymbolsExercise` with 114 rows.

Expected behavior:

The exercise-quality gate should distinguish "target executed" from "obligation
exercised." Rows should remain unverified unless the target contains
obligation-specific evidence: source behavior, diagnostics, runtime assertions,
IR/artifact checks, or an oracle comparison matching the obligation.

Impact:

The current gate can certify that every accepted-source manifest row has a
non-constant, executed target while still leaving many individual obligations
untested. This is the mechanism that lets a broad reference or artifact runner
look like complete exercise of thousands of obligations.

### UV-AUDIT-0293: Pattern lowering obligations are credited from conformance-log row presence

Severity: Medium

Status: Static HelloUltraviolet evidence.

Scope:

This is a HelloUltraviolet test-surface finding.

Spec anchors:

- `Docs\SPECIFICATION.md:18757`
- `Docs\SPECIFICATION.md:18763`
- `Docs\SPECIFICATION.md:18783`
- `Docs\SPECIFICATION.md:18787`
- `Docs\SPECIFICATION.md:18792`
- `Docs\SPECIFICATION.md:18802`

Implementation/test anchors:

- `HelloUltraviolet\Source\Audit\PatternLoweringConformanceExecution.uv:51`
- `HelloUltraviolet\Source\Audit\PatternLoweringConformanceExecution.uv:140`
- `HelloUltraviolet\Source\Audit\PatternLoweringConformanceExecution.uv:166`
- `HelloUltraviolet\Source\Audit\PatternLoweringConformanceExecution.uv:182`
- `HelloUltraviolet\Source\Audit\PatternLoweringConformanceExecution.uv:197`
- `HelloUltraviolet\Source\Audit\PatternLoweringConformanceExecution.uv:212`
- `HelloUltraviolet\Source\Audit\PatternLoweringConformanceExecution.uv:303`
- `HelloUltraviolet\Source\Audit\PatternLoweringConformanceExecution.uv:318`
- `HelloUltraviolet\Source\Audit\PatternLoweringConformanceExecution.uv:333`
- `HelloUltraviolet\Source\Audit\PatternLoweringConformanceExecution.uv:348`
- `HelloUltraviolet\Source\Audit\PatternLoweringConformanceExecution.uv:363`
- `HelloUltraviolet\Source\Audit\PatternLoweringConformanceExecution.uv:378`
- `HelloUltraviolet\Source\Audit\PatternLoweringConformanceExecution.uv:393`
- `HelloUltraviolet\Source\Audit\CatalogExerciseQualityGroup019.uv:261`
- `HelloUltraviolet\Source\Audit\CatalogExerciseQualityGroup019.uv:387`
- `HelloUltraviolet\Source\Audit\CatalogExerciseQualityGroup019.uv:415`
- `HelloUltraviolet\Source\Audit\CatalogExerciseQualityGroup019.uv:457`
- `HelloUltraviolet\Source\Audit\CatalogExerciseQualityGroup019.uv:723`
- `HelloUltraviolet\Source\Audit\Catalog\Patterns\BasicPatterns.uv:203`
- `HelloUltraviolet\Source\Audit\Catalog\Patterns\BasicPatterns.uv:266`
- `HelloUltraviolet\Source\Audit\Catalog\Patterns\EnumAndModalPatterns.uv:302`
- `HelloUltraviolet\Source\Audit\Catalog\Patterns\RangePatterns.uv:122`
- `HelloUltraviolet\Source\Audit\Catalog\Patterns\TupleAndRecordPatterns.uv:284`

Observed behavior:

Ten pattern catalog entries are tied to procedures whose names and bodies check
that an exact conformance row appears in `pattern-lowering.conformance.log`.
Several use `patternLoweringProjectContainsMarker`, which tests only for the
obligation row string. The more detailed variants still match self-reported
payload fragments such as `emits_ifcase_ir=true`, `has_else=`, or
`bind_count=`, without inspecting the actual lowered IR/control-flow structure
against the pattern-lowering rule.

Expected behavior:

Pattern lowering exercise should prove the rule's emitted structure or runtime
effect. A log row can support traceability, but should not be the only evidence
for `Lower-Pat`, `Lower-IfCases`, `Lower-BindList`, or shared lowering
obligations.

Impact:

Pattern lowering can be reported as exercised when the only checked fact is that
the compiler wrote the row claiming it was exercised. That can hide missing
fallback arms, binding-order errors, or incorrect pattern dispatch structure.

### UV-AUDIT-0294: Compile-time erasure artifact checks token absence instead of structural erasure

Severity: Medium

Status: Static HelloUltraviolet evidence.

Scope:

This is a HelloUltraviolet test-surface finding.

Spec anchors:

- `Docs\SPECIFICATION.md:356`
- `Docs\SPECIFICATION.md:7214`
- `Docs\SPECIFICATION.md:25026`
- `Docs\SPECIFICATION.md:25031`
- `Docs\SPECIFICATION.md:25409`

Implementation/test anchors:

- `HelloUltraviolet\Source\Audit\Catalog\CompileTimeExecutionAndMetaprogramming\LoweringErasureArtifactExecution.uv:1`
- `HelloUltraviolet\Source\Audit\Catalog\CompileTimeExecutionAndMetaprogramming\LoweringErasureArtifactExecution.uv:68`
- `HelloUltraviolet\Source\Audit\Catalog\CompileTimeExecutionAndMetaprogramming\LoweringErasureArtifactExecution.uv:80`
- `HelloUltraviolet\Source\Audit\Catalog\CompileTimeExecutionAndMetaprogramming\LoweringErasureArtifactExecution.uv:88`
- `HelloUltraviolet\Source\Audit\Catalog\CompileTimeExecutionAndMetaprogramming\LoweringErasureArtifactExecution.uv:116`
- `HelloUltraviolet\Source\Audit\Catalog\CompileTimeExecutionAndMetaprogramming\LoweringErasureArtifactExecution.uv:132`
- `HelloUltraviolet\Source\Audit\Catalog\CompileTimeExecutionAndMetaprogramming\LoweringErasureArtifactExecution.uv:158`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:4432`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:4462`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:4486`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:4509`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:4535`

Observed behavior:

`comptimeRuntimeErasureArtifactEvidenceMatches` builds the runtime-erasure
fixture, checks that final IR contains several emitted runtime names, excludes
literal text such as `TypeEmitter`, `Introspect`, `ComptimeDiagnostics`,
`ProjectFiles`, `Ast::`, `QuoteExpr`, `ComptimeExpr`, and `TypeLiteralExpr`,
then checks conformance-log rows and `phase4_input_ct_meta_free=true`.

The five manifest rows for compile-time forms, capabilities, reflection,
quote/splice emission, and derive target lowering are therefore credited from
string absence and a self-reported Phase 4 payload. The test does not inspect a
typed/lowered representation proving that all Chapter 22 nodes and compile-time
capabilities have been structurally erased before Phase 4.

Expected behavior:

Compile-time erasure evidence should be structural: Phase 4 input and emitted IR
should be checked for absence of compile-time forms by compiler IR/AST shape, or
by exhaustive artifact assertions that cannot be satisfied by renamed symbols or
missing debug strings.

Impact:

Compile-time lowering can regress by retaining a Chapter 22 form or capability
under a different spelling while the fixture still passes. The current evidence
shows selected tokens are absent, not that Phase 4 is compile-time-form-free.

### UV-AUDIT-0295: Rejected fixtures credit accepted predicates and broad ownership rows

Severity: Medium

Status: Static HelloUltraviolet evidence; subagent command checks reported the
same first-diagnostic classes for sampled fixtures.

Scope:

This is a HelloUltraviolet rejected-fixture accounting finding.

Spec anchors:

- `Docs\SPECIFICATION.md:3183`
- `Docs\SPECIFICATION.md:3184`
- `Docs\SPECIFICATION.md:6558`
- `Docs\SPECIFICATION.md:6566`
- `Docs\SPECIFICATION.md:14080`
- `Docs\SPECIFICATION.md:14083`
- `Docs\SPECIFICATION.md:14107`
- `Docs\SPECIFICATION.md:16453`
- `Docs\SPECIFICATION.md:16458`
- `Docs\SPECIFICATION.md:16459`
- `Docs\SPECIFICATION.md:18000`

Implementation/test anchors:

- `HelloUltraviolet\Source\Audit\FixtureCatalog\RejectedSource\Procedures.uv:474`
- `HelloUltraviolet\Fixtures\RejectedSource\Procedures\ReturnAnnotationPresence\Source\Main.uv:3`
- `HelloUltraviolet\Fixtures\RejectedSource\Procedures\ReturnAnnotationPresence\Expected.uv:3`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\RejectedSource\Expressions.uv:1596`
- `HelloUltraviolet\Fixtures\RejectedSource\Expressions\PtrNullExpectedRequirement\Source\Main.uv:4`
- `HelloUltraviolet\Fixtures\RejectedSource\Expressions\PtrNullExpectedRequirement\Expected.uv:3`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\RejectedSource\Expressions.uv:1698`
- `HelloUltraviolet\Fixtures\RejectedSource\Expressions\CastValidity\Source\Main.uv:4`
- `HelloUltraviolet\Fixtures\RejectedSource\Expressions\CastValidity\Expected.uv:3`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\RejectedSource\Statements.uv:185`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\RejectedSource\Statements.uv:457`

Observed behavior:

Several rejected-source fixtures assign accepted or broad obligations to
negative specimens:

- `ReturnAnnotationPresence` expects `def.15.ReturnAnnOk`, but the source omits
  the return annotation and exercises `¬ ReturnAnnOk`.
- `PtrNullExpectedRequirement` expects `def.16.PtrNullExpected`, but the source
  assigns `Ptr::null()` to `i32`, exercising the failed expected-type branch.
- `CastValidity` expects `def.16.CastValidity`, but the source casts
  `true as f32`, exercising invalid-cast rejection.
- Statement parser fixtures tie missing terminator or stray `else` specimens to
  `req.16.ControlExpressionDiagnosticOwnership`, even though those specimens
  exercise generic statement parse errors rather than control-expression
  ownership.

Expected behavior:

Rejected fixtures should credit the violated diagnostic rule or a negative
predicate branch. Accepted predicates and broad diagnostic-ownership rows need
separate evidence that exercises the accepted or owned behavior directly.

Impact:

The rejected-source catalog can make invalid branches appear to exercise the
positive predicate or a whole diagnostic family. That weakens the fixture surface
and masks missing accepted-case and priority-order checks.

### UV-AUDIT-0296: Broad accepted references map failure and provenance rows to happy-path values

Severity: High

Status: Static HelloUltraviolet evidence.

Scope:

This is a HelloUltraviolet accepted-reference accounting finding.

Spec anchors:

- `Docs\SPECIFICATION.md:4246`
- `Docs\SPECIFICATION.md:4381`
- `Docs\SPECIFICATION.md:4589`
- `Docs\SPECIFICATION.md:4629`
- `Docs\SPECIFICATION.md:6306`
- `Docs\SPECIFICATION.md:6316`
- `Docs\SPECIFICATION.md:6341`
- `Docs\SPECIFICATION.md:6499`
- `Docs\SPECIFICATION.md:6538`
- `Docs\SPECIFICATION.md:6565`
- `Docs\SPECIFICATION.md:7459`
- `Docs\SPECIFICATION.md:19962`
- `Docs\SPECIFICATION.md:19973`
- `Docs\SPECIFICATION.md:19985`

Implementation/test anchors:

- `HelloUltraviolet\Source\Reference\Types\Inference.uv:9`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:1380`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:1396`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:1398`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:1403`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:1424`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:1432`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:1438`
- `HelloUltraviolet\Source\Reference\Permissions\AliasExclusivity.uv:48`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:1586`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:1590`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:1592`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:1594`
- `HelloUltraviolet\Source\Reference\Statements\Frame.uv:86`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:3822`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:3831`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:3832`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:3837`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:3842`

Observed behavior:

Accepted reference functions are credited for broad obligation families that
their bodies do not exercise:

- `runTypesInferenceReference` checks a primitive integer, a boolean comparison,
  a tuple destructure, and one inferred-return helper, while the manifest maps it
  to unification failure rows such as occurs-check, tuple/array/permission
  failures, `Unify-Err`, `Syn-Call-Err`, and `Chk-PtrNull-Err`.
- `runPermissionsAliasExclusivityReference` sums legal const/shared/unique
  accesses to `52`, while the manifest maps it to the permission coexistence
  matrix, dynamic exclusivity semantics, and diagnostic ownership.
- `runStatementsFrameReference` checks integer outcomes for valid implicit and
  explicit frame uses, while the manifest maps it to frame binding state,
  provenance, reset, and control-exit rules.

Expected behavior:

Accepted references should either contain obligation-specific branches for each
credited rule or the manifest should mark the unmatched rows as only trace
links. Failure, diagnostic, provenance, cleanup, and control-exit obligations
need direct accepted/rejected/runtime/artifact evidence.

Impact:

HelloUltraviolet can count happy-path reference functions as exercising failure
and lifecycle semantics they never reach. This creates false confidence in broad
sections such as type inference, permissions, and frame execution.

### UV-AUDIT-0297: Generated coverage now has a blocked ledger for non-exercised HUV rows

Severity: High

Status: Partially corrected in generated HelloUltraviolet catalog accounting.

Scope:

This is a HelloUltraviolet conformance-accounting finding.

Implementation/test anchors:

- `Tools\GenerateHelloCatalog.py:32`
- `Tools\GenerateHelloCatalog.py:845`
- `Tools\GenerateHelloCatalog.py:1011`
- `Tools\GenerateHelloCatalog.py:1152`
- `Tools\GenerateHelloCatalog.py:1180`
- `Tools\GenerateHelloCatalog.py:2531`
- `HelloUltraviolet\Source\Audit\Catalog.uv:3`
- `HelloUltraviolet\Source\Audit\Catalog.uv:5`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:1`

Observed behavior:

The generated HUV catalog previously counted marker-only rows, positive
reference rows, conformance-log row-presence checks, and broad reference helpers
as direct obligation exercise. The corrected generator now filters fixture
catalog locations out of preserved targets, treats fixture expectation metadata
as blocked unless a real non-fixture target exists, and rewrites zero-entry
catalog files so stale generated rows are removed from disk. The latest catalog
generation records `5019` counted obligations and `1033` blocked obligations.
Blocked rows are kept in the generator ledger instead of appearing in the
accepted exercise manifest.

The corrected rows include:

- source-native marker rows that only named `covers(...)` claims;
- all remaining `FixtureCatalog::*` and `validated...FixtureCount` target rows
  that previously counted fixture metadata functions as obligation exercise;
- rejected fixtures that credited accepted predicates;
- broad conformance, diagnostics, type inference, source text, module, manifest,
  name-resolution, modal, bytes/string, backend, async, comptime, FFI, pattern,
  statement, key-system, authority, attribute, procedure, and lifecycle rows that
  were not exercised by the referenced HUV source;
- emit-LL lifecycle and backend rows that were backed by successful static
  library artifacts or conformance-log row text rather than the required
  failure, panic, poison, vtable, or invalid-lowering branch.

One class was corrected by retargeting instead of blocking: async combinator
branch rows now point to concrete runtime-reference functions in
`CombinatorRuntimeForms.uv` instead of the generic `CompositionForms.uv` surface.

Expected behavior:

Rows whose evidence is only a marker, broad reference helper, row-presence log,
or wrong-polarity fixture must not count as exercised. When real HUV evidence
exists under a more precise symbol, the generated catalog should select that
symbol. Otherwise the row belongs in the blocked ledger until a real fixture,
reference branch, or artifact assertion is added.

Impact:

The headline coverage count now reflects known gaps instead of silently counting
them as exercised. Remaining counted rows still need continued semantic review,
especially artifact/log-backed clusters and accepted reference functions that
cover large obligation families.

### UV-AUDIT-0298: Missing static binding metadata is treated as non-responsible deinit

Severity: High

Status: Subagent-reported and locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:28379`
- `Docs\SPECIFICATION.md:28391`
- `Docs\SPECIFICATION.md:28430`
- `Docs\SPECIFICATION.md:28431`
- `Docs\SPECIFICATION.md:28435`
- `Docs\SPECIFICATION.md:28436`
- `Docs\Internal\UltravioletSpecification.obligations.md:94146`
- `Docs\Internal\UltravioletSpecification.obligations.md:94156`
- `Docs\Internal\UltravioletSpecification.obligations.md:94162`
- `Docs\Internal\UltravioletSpecification.obligations.md:94172`

Implementation/test anchors:

- `Bootstrap\Ultraviolet\src\05_codegen\globals\init.cpp:371`
- `Bootstrap\Ultraviolet\src\05_codegen\globals\init.cpp:385`
- `Bootstrap\Ultraviolet\src\05_codegen\globals\init.cpp:387`
- `Bootstrap\Ultraviolet\src\05_codegen\globals\init.cpp:388`
- `Bootstrap\Ultraviolet\src\05_codegen\globals\init.cpp:392`
- `Bootstrap\Ultraviolet\src\05_codegen\globals\globals.cpp:425`
- `Bootstrap\Ultraviolet\src\05_codegen\globals\globals.cpp:430`
- `Bootstrap\Ultraviolet\src\05_codegen\globals\globals.cpp:440`
- `HelloUltraviolet\Source\Audit\ArtifactProjectExecution.uv:3143`
- `HelloUltraviolet\Source\Audit\ArtifactProjectExecution.uv:3150`
- `HelloUltraviolet\Source\Audit\ArtifactProjectExecution.uv:3154`

Observed behavior:

`LowerStaticDeinitNames` asks for `StaticBindInfo(path, name)` and enters the
`Lower-StaticDeinitNames-Cons-NoResp` branch when metadata is missing or when
metadata exists and says the binding has no responsibility. `StaticBindInfo`
returns no value when `StaticItemOf` or `StaticType` cannot resolve the binding.
That undefined-metadata case is therefore recorded and lowered as if
`StaticBindInfo(path, x).resp != resp` had been proven.

Expected behavior:

`Lower-StaticDeinitNames-Cons-NoResp` is only justified when
`StaticBindInfo(path, x)` is defined and its responsibility is not `resp`.
If static binding metadata cannot be resolved for a name emitted by
`StaticBindList(binding)`, lowering must fail or make the missing metadata
impossible by construction. It must not silently skip the static drop.

Impact:

Responsible module statics can avoid deinitialization whenever static binding
metadata is missing. This is especially dangerous around destructuring statics,
generated static names, and any static form already implicated by
`UV-AUDIT-0256`, because the deinit path can turn a metadata defect into a
silent resource leak.

HelloUltraviolet fixture gap:

`artifactProjectEmitLlExercise` checks that both static-deinit rule labels are
present in the conformance log, but it does not build a fixture where a
responsible static binding loses `StaticBindInfo` and must fail lowering instead
of taking the no-responsibility branch.

### UV-AUDIT-0299: Binding type-annotation representation is underspecified

Severity: Medium

Status: Spec ambiguity with implementation impact locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:18048`
- `Docs\SPECIFICATION.md:18083`
- `Docs\SPECIFICATION.md:18088`
- `Docs\SPECIFICATION.md:19321`
- `Docs\SPECIFICATION.md:19322`
- `Docs\SPECIFICATION.md:19324`
- `Docs\SPECIFICATION.md:7876`
- `Docs\SPECIFICATION.md:7882`
- `Docs\SPECIFICATION.md:28334`
- `Docs\SPECIFICATION.md:28340`

Implementation/test anchors:

- `Bootstrap\Ultraviolet\src\02_source\parser\item\static_decl.cpp:55`
- `Bootstrap\Ultraviolet\src\02_source\parser\item\static_decl.cpp:56`
- `Bootstrap\Ultraviolet\src\02_source\parser\item\static_decl.cpp:65`
- `Bootstrap\Ultraviolet\src\02_source\parser\item\static_decl.cpp:73`
- `Bootstrap\Ultraviolet\src\02_source\parser\item\static_decl.cpp:75`
- `Bootstrap\Ultraviolet\src\02_source\parser\item\static_decl.cpp:90`
- `Bootstrap\Ultraviolet\src\02_source\parser\item\static_decl.cpp:91`
- `Bootstrap\Ultraviolet\src\02_source\parser\item\item_common.cpp:127`
- `Bootstrap\Ultraviolet\src\02_source\parser\item\item_common.cpp:132`
- `Bootstrap\Ultraviolet\include\02_source\ast\nodes\ast_stmts.h:37`
- `Bootstrap\Ultraviolet\include\02_source\ast\nodes\ast_stmts.h:49`
- `Bootstrap\Ultraviolet\include\02_source\ast\nodes\ast_stmts.h:55`
- `Bootstrap\Ultraviolet\src\05_codegen\globals\globals.cpp:113`
- `Bootstrap\Ultraviolet\src\05_codegen\globals\globals.cpp:126`

Observed behavior:

`Parse-BindingAfterLetVar` parses a pattern, then parses an optional type
annotation. Because `ParsePattern` can already consume `x: T` as
`TypedPattern(x, T)`, the later type-annotation parse can see no annotation.
The parser file documents a normalization step, and a `NormalizeBindingPattern`
helper exists, but this binding parser stores `pat.elem` and `ty.elem` directly.

The rest of the implementation compensates inconsistently. Binding type lookup
accepts either `binding.type_opt` or a `TypedPattern` type. Static codegen also
treats `TypedPattern(name, _)` as a `StaticName`, even though the static
lowering rule only defines the single-name path for `IdentifierPattern`.

Expected behavior:

The specification needs a normative representation choice for binding type
annotations before this can be judged mechanically:

- either `let x: T = ...` is represented as `IdentifierPattern(x)` plus
  `ty_opt = T`;
- or it is represented as `TypedPattern(x, T)` plus `ty_opt = none`, and the
  static, item-path, and binding rules must explicitly use typed-pattern names
  where intended.

Impact:

Current behavior can make typed local and static bindings depend on
implementation-specific fallbacks rather than one spec-defined representation.
For statics, that ambiguity affects `StaticName`, `StaticBindList`, `ItemPath`,
`StaticItemOf`, static symbol selection, and deinit metadata resolution.

HelloUltraviolet fixture gap:

HUV has many typed bindings, but there is no focused parser or artifact test
that distinguishes the two representations by inspecting typed static symbol
identity, item paths, and static binding metadata for `let x: T = ...`.

### UV-AUDIT-0300: Rejected-source fixtures still credit parent obligations through child diagnostics

Severity: Medium

Status: Subagent-reported and locally verified.

Scope:

This is a HelloUltraviolet conformance-accounting finding.

Spec anchors:

- `Docs\SPECIFICATION.md:7500`
- `Docs\SPECIFICATION.md:7514`
- `Docs\SPECIFICATION.md:7593`
- `Docs\SPECIFICATION.md:21132`
- `Docs\SPECIFICATION.md:21155`
- `Docs\SPECIFICATION.md:21160`
- `Docs\SPECIFICATION.md:21165`
- `Docs\SPECIFICATION.md:21170`
- `Docs\SPECIFICATION.md:21175`
- `Docs\SPECIFICATION.md:21180`
- `Docs\SPECIFICATION.md:21735`
- `Docs\SPECIFICATION.md:21939`
- `Docs\SPECIFICATION.md:22008`
- `Docs\SPECIFICATION.md:22047`

Implementation/test anchors:

- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:1373`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:3674`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:3761`
- `HelloUltraviolet\Source\Audit\Catalog\PermissionsAndBindingState\BindingActivityStates.uv:65`
- `HelloUltraviolet\Source\Audit\Catalog\KeySystem\SpeculativeExecution.uv:56`
- `HelloUltraviolet\Source\Audit\Catalog\StructuredParallelism\ExecutionDomains.uv:254`
- `HelloUltraviolet\Fixtures\RejectedSource\Permissions\UniqueInactiveUse\Source\Main.uv:14`
- `HelloUltraviolet\Fixtures\RejectedSource\Permissions\UniqueInactiveUse\Expected.uv:3`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\RejectedSource\Permissions.uv:28`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\RejectedSource\Permissions.uv:32`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\RejectedSource\Keys.uv:11`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\RejectedSource\Keys.uv:96`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\RejectedSource\Keys.uv:113`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\RejectedSource\Keys.uv:130`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\RejectedSource\Keys.uv:164`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\RejectedSource\Keys.uv:181`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\RejectedSource\Keys.uv:198`
- `HelloUltraviolet\Fixtures\RejectedSource\Keys\SpeculativeReleaseMode\Expected.uv:3`
- `HelloUltraviolet\Fixtures\RejectedSource\Keys\SpeculativeNestedKeyRule\Expected.uv:3`
- `HelloUltraviolet\Fixtures\RejectedSource\Keys\SpeculativeDefer\Expected.uv:3`
- `HelloUltraviolet\Fixtures\RejectedSource\Keys\SpeculativeWait\Expected.uv:3`
- `HelloUltraviolet\Fixtures\RejectedSource\Keys\SpeculativeFence\Expected.uv:3`
- `HelloUltraviolet\Fixtures\RejectedSource\Keys\SpeculativeOutsideWrite\Expected.uv:3`
- `HelloUltraviolet\Fixtures\RejectedSource\Keys\SpeculativeImpureCall\Expected.uv:3`
- `HelloUltraviolet\Fixtures\RejectedSource\Parallelism\KeyBlockGpuContext\Source\Main.uv:3`
- `HelloUltraviolet\Fixtures\RejectedSource\Parallelism\KeyBlockGpuContext\Expected.uv:3`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\RejectedSource\Parallelism.uv:205`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\RejectedSource\Parallelism.uv:210`

Observed behavior:

The manifest still counts several parent requirements through aggregate
`rejectedSourceFixturesCompileAndEmitExpectedDiagnostics` execution even though
the fixture expectations name narrower child obligations or only a diagnostic
family:

- `req.InactiveUniqueBindingNoDirectUse` is credited, but
  `UniqueInactiveUse/Expected.uv` names `diagnostics.PermissionAdmissibility`.
- `requirement.19.SpeculativeProhibitedOperations` is credited, but the
  speculative-key fixtures name child rules such as `rule.19.K-Spec-No-Defer`,
  `rule.19.K-Spec-No-Wait`, and `rule.19.K-Spec-No-Impure-Call`.
- `requirement.20.KeySystemUnavailableInGpuContext` is credited, but the GPU key
  block fixture names `rule.20.KeyBlock-GPU-Err`.

Expected behavior:

A rejected-source row should credit the exact obligation named by the fixture
expectation, plus any explicitly asserted parent obligation. A fixture that
only names a diagnostic family or child rule must not automatically count as
direct exercise of the parent requirement unless the catalog records and checks
that parent obligation intentionally.

Impact:

The corrected fixture-marker filtering still leaves parent-requirement false
credit in some rejected-source rows. The row has a real compile-and-diagnostic
execution, but the executed expectation does not establish the broader
obligation currently counted in the manifest.

### UV-AUDIT-0301: Contract and invariant lowering rows are credited by conformance text

Severity: Medium

Status: Subagent-reported and locally verified.

Scope:

This is a HelloUltraviolet conformance-accounting finding.

Spec anchors:

- `Docs\SPECIFICATION.md:14784`
- `Docs\SPECIFICATION.md:14788`
- `Docs\SPECIFICATION.md:15050`
- `Docs\SPECIFICATION.md:15054`
- `Docs\SPECIFICATION.md:15299`
- `Docs\SPECIFICATION.md:15304`
- `Docs\Internal\UltravioletSpecification.obligations.md:55662`
- `Docs\Internal\UltravioletSpecification.obligations.md:55671`
- `Docs\Internal\UltravioletSpecification.obligations.md:56496`
- `Docs\Internal\UltravioletSpecification.obligations.md:56511`
- `Docs\Internal\UltravioletSpecification.obligations.md:57238`
- `Docs\Internal\UltravioletSpecification.obligations.md:57254`

Implementation/test anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\contract_clause.cpp:84`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\contract_clause.cpp:94`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:2747`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:2748`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:2788`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:2789`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:2790`
- `HelloUltraviolet\Source\Audit\Catalog\ProceduresAndContracts\ContractClauses.uv:326`
- `HelloUltraviolet\Source\Audit\Catalog\ProceduresAndContracts\ContractClauses.uv:335`
- `HelloUltraviolet\Source\Audit\Catalog\ProceduresAndContracts\Invariants.uv:119`
- `HelloUltraviolet\Source\Audit\Catalog\ProceduresAndContracts\Invariants.uv:128`
- `HelloUltraviolet\Source\Audit\Catalog\ProceduresAndContracts\Invariants.uv:137`
- `HelloUltraviolet\Source\Audit\Catalog\ProceduresAndContracts\CompilerConformanceExecution.uv:89`
- `HelloUltraviolet\Source\Audit\Catalog\ProceduresAndContracts\CompilerConformanceExecution.uv:94`
- `HelloUltraviolet\Source\Audit\ArtifactProjectExecution.uv:4529`
- `HelloUltraviolet\Source\Audit\ArtifactProjectExecution.uv:4538`
- `HelloUltraviolet\Source\Audit\ArtifactProjectExecution.uv:4542`
- `HelloUltraviolet\Source\Audit\ArtifactProjectExecution.uv:4546`
- `HelloUltraviolet\Source\Audit\ArtifactProjectExecution.uv:4554`

Observed behavior:

`req.15.ContractClausesNoIndependentRuntimeEffect` and
`req.15.ContractClauseLoweringViaVerificationResults` are counted through a
catalog helper that checks for the compiler's own conformance rows. Likewise,
`req.15.InvariantRuntimeChecks` and
`req.15.InvariantLoweringViaVerificationLogic` are counted through
`artifactProjectEmitLlExercise`, but the relevant matcher checks
`req.15.InvariantVerificationModeRules`, generic `LoopInv` payload fragments,
and an emitted probe label.

Expected behavior:

Coverage for these rows should prove the operational property, not only that
the compiler recorded a conformance row. Contract clauses need evidence that
they have no independent runtime or lowering path except verification-result
insertion. Invariants need exact-row evidence for entry, back-edge, and
`continue` insertion through the section 15.8.6 rules.

Impact:

HUV can count contract and invariant lowering obligations without independently
exercising the lowering boundary. This weakens coverage for exactly the
contract/runtime split that prior audit findings already show is fragile.

### UV-AUDIT-0302: Behavioral-subtyping predicates appear sensitive to formal parameter names

Severity: Medium

Status: Inspection-backed.

Spec anchors:

- `Docs\SPECIFICATION.md:12812`
- `Docs\SPECIFICATION.md:15331`
- `Docs\SPECIFICATION.md:15335`
- `Docs\SPECIFICATION.md:15336`
- `Docs\SPECIFICATION.md:15340`
- `Docs\SPECIFICATION.md:15341`
- `Docs\SPECIFICATION.md:15345`
- `Docs\SPECIFICATION.md:15346`
- `Docs\Internal\UltravioletSpecification.obligations.md:57338`
- `Docs\Internal\UltravioletSpecification.obligations.md:57351`
- `Docs\Internal\UltravioletSpecification.obligations.md:57367`
- `Docs\Internal\UltravioletSpecification.obligations.md:57383`

Implementation/test anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\signature.cpp:503`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\signature.cpp:528`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\signature.cpp:533`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\type_equiv.cpp:256`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\type_equiv.cpp:265`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\type_equiv.cpp:271`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\record_decl.cpp:917`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\record_decl.cpp:925`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\record_decl.cpp:932`
- `Bootstrap\Ultraviolet\src\04_analysis\contracts\contract_check.cpp:225`
- `Bootstrap\Ultraviolet\src\04_analysis\contracts\contract_check.cpp:236`
- `Bootstrap\Ultraviolet\src\04_analysis\contracts\contract_check.cpp:2191`
- `Bootstrap\Ultraviolet\src\04_analysis\contracts\contract_check.cpp:2209`
- `Bootstrap\Ultraviolet\src\04_analysis\contracts\contract_check.cpp:2220`
- `HelloUltraviolet\Source\Reference\Procedures\BehavioralSubtyping.uv:4`
- `HelloUltraviolet\Source\Reference\Procedures\BehavioralSubtyping.uv:9`
- `HelloUltraviolet\Fixtures\RejectedSource\Procedures\BehavioralSubtypingLiskov\Source\Main.uv:4`
- `HelloUltraviolet\Fixtures\RejectedSource\Procedures\BehavioralSubtypingLiskov\Source\Main.uv:11`
- `HelloUltraviolet\Fixtures\RejectedSource\Procedures\BehavioralSubtypingPrecondition\Source\Main.uv:4`
- `HelloUltraviolet\Fixtures\RejectedSource\Procedures\BehavioralSubtypingPrecondition\Source\Main.uv:11`
- `HelloUltraviolet\Fixtures\RejectedSource\Procedures\BehavioralSubtypingPostcondition\Source\Main.uv:4`
- `HelloUltraviolet\Fixtures\RejectedSource\Procedures\BehavioralSubtypingPostcondition\Source\Main.uv:11`

Observed behavior:

Method signature construction stores formal names in `bindings`, but the
function type used for equivalence carries only parameter mode and type.
Record implementation checking verifies signature equivalence, then passes the
raw class contract AST and raw implementation contract AST to
`CheckBehavioralSubtyping`. That routine calls `PredicateImplies` directly on
the two predicate ASTs. The implication proof context is populated from the
antecedent predicate as written; no class-formal to implementation-formal
renaming is visible before the comparison.

Expected behavior:

Behavioral-subtyping verification should compare contracts under the matched
method-signature correspondence, not under source spelling of implementation
formal names. If a class method and implementation method have equivalent
parameter modes/types but different parameter names, logically equivalent
preconditions and postconditions should still compare under alpha-renaming.

Impact:

An implementation method can be rejected or misclassified solely because it uses
a different parameter name from the class method. Conversely, HUV currently
misses this because accepted and rejected behavioral-subtyping fixtures reuse
the same formal names on both sides.

### UV-AUDIT-0303: Compile-time evaluation lacks tuple and index access

Severity: High

Status: Subagent-reported and locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:9072`
- `Docs\SPECIFICATION.md:9130`
- `Docs\SPECIFICATION.md:9135`
- `Docs\SPECIFICATION.md:9205`
- `Docs\SPECIFICATION.md:9284`
- `Docs\SPECIFICATION.md:9404`
- `Docs\SPECIFICATION.md:24921`
- `Docs\Internal\UltravioletSpecification.obligations.md:84119`
- `Docs\Internal\UltravioletSpecification.obligations.md:84128`

Implementation/test anchors:

- `Bootstrap\Ultraviolet\src\03_comptime\eval.cpp:686`
- `Bootstrap\Ultraviolet\src\03_comptime\eval.cpp:964`
- `Bootstrap\Ultraviolet\src\03_comptime\eval.cpp:978`
- `Bootstrap\Ultraviolet\src\03_comptime\eval.cpp:1094`
- `Bootstrap\Ultraviolet\src\03_comptime\eval.cpp:1128`
- `Bootstrap\Ultraviolet\src\03_comptime\eval.cpp:1130`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:4096`
- `HelloUltraviolet\Source\Audit\Catalog\CompileTimeExecutionAndMetaprogramming\CompileTimeForms.uv:245`
- `HelloUltraviolet\Source\Audit\Catalog\CompileTimeExecutionAndMetaprogramming\CompileTimeForms.uv:248`
- `HelloUltraviolet\Source\Audit\Catalog\CompileTimeExecutionAndMetaprogramming\CompilerConformanceExecution.uv:188`
- `HelloUltraviolet\Source\Audit\Catalog\CompileTimeExecutionAndMetaprogramming\CompilerConformanceExecution.uv:214`

Observed behavior:

`EvalExpr` records `requirement.22.CtEvalOrdinarySemantics` for every visited
expression and can construct compile-time tuple and array values. The same
visitor has explicit branches for field access, calls, method calls, type
literals, and quote handling, but there is no branch for tuple access or index
access in the compile-time evaluator.

HUV counts `requirement.22.CtEvalOrdinarySemantics` through
`comptimeAcceptedProjectFormsConformanceMatches`, which checks only that the
compiler emitted the broad conformance token.

Expected behavior:

Chapter 22 requires compile-time evaluation and execution of non-Chapter-22
forms to use the ordinary expression semantics over `CtValue`. Tuple projection
and array/slice indexing are ordinary expression forms, so `CtEval` must
evaluate them or reject the exact unevaluable form with a spec-defined failure
path. A broad conformance token is not enough evidence for this row.

Impact:

Compile-time constants, `comptime` expressions, array lengths, emitted AST
payloads, and other Phase 2 computations can fail or silently miss ordinary
tuple/index semantics even though HUV reports the broad compile-time evaluation
obligation as covered.

### UV-AUDIT-0304: Async combinator lowering does not store durable wrapper state

Severity: High

Status: Subagent-reported and locally verified.

Spec anchors:

- `Docs\SPECIFICATION.md:24187`
- `Docs\SPECIFICATION.md:24190`
- `Docs\SPECIFICATION.md:24191`
- `Docs\SPECIFICATION.md:24193`
- `Docs\SPECIFICATION.md:24194`
- `Docs\SPECIFICATION.md:24385`
- `Docs\SPECIFICATION.md:24390`
- `Docs\SPECIFICATION.md:24400`
- `Docs\SPECIFICATION.md:24405`
- `Docs\SPECIFICATION.md:24408`
- `Docs\Internal\UltravioletSpecification.obligations.md:82105`
- `Docs\Internal\UltravioletSpecification.obligations.md:82110`
- `Docs\Internal\UltravioletSpecification.obligations.md:82111`
- `Docs\Internal\UltravioletSpecification.obligations.md:82113`
- `Docs\Internal\UltravioletSpecification.obligations.md:82114`
- `Docs\Internal\UltravioletSpecification.obligations.md:82646`
- `Docs\Internal\UltravioletSpecification.obligations.md:82662`
- `Docs\Internal\UltravioletSpecification.obligations.md:82694`
- `Docs\Internal\UltravioletSpecification.obligations.md:82710`
- `Docs\Internal\UltravioletSpecification.obligations.md:82724`

Implementation/test anchors:

- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\method_call.cpp:1892`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\method_call.cpp:1893`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\method_call.cpp:1895`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\method_call.cpp:1897`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\method_call.cpp:1901`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\method_call.cpp:1903`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\call\direct.cpp:616`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\call\direct.cpp:621`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\call\direct.cpp:622`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\call\direct.cpp:1017`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\call\direct.cpp:1030`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\call\direct.cpp:1051`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\call\direct.cpp:1060`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\call\direct.cpp:1066`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\call\direct.cpp:1070`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\call\direct.cpp:1096`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\call\direct.cpp:1118`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\call\direct.cpp:1122`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\call\direct.cpp:1266`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\call\direct.cpp:1287`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\call\direct.cpp:1300`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\call\direct.cpp:1402`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\call\direct.cpp:1408`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:4027`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:4028`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:4029`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:4031`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:4032`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:4033`
- `HelloUltraviolet\Source\Reference\Async\CompositionForms.uv:408`
- `HelloUltraviolet\Source\Reference\Async\CompositionForms.uv:410`
- `HelloUltraviolet\Source\Reference\Async\CompositionForms.uv:415`
- `HelloUltraviolet\Source\Reference\Async\CompositionForms.uv:417`
- `HelloUltraviolet\Source\Reference\Async\CompositionForms.uv:429`
- `HelloUltraviolet\Source\Reference\Async\CompositionForms.uv:431`
- `HelloUltraviolet\Source\Reference\Async\CompositionForms.uv:437`
- `HelloUltraviolet\Source\Reference\Async\CompositionForms.uv:439`
- `HelloUltraviolet\Source\Audit\AsyncArtifactCompositionExecution.uv:153`
- `HelloUltraviolet\Source\Audit\AsyncArtifactCompositionExecution.uv:155`
- `HelloUltraviolet\Source\Audit\AsyncArtifactCompositionExecution.uv:156`
- `HelloUltraviolet\Source\Audit\AsyncArtifactCompositionExecution.uv:158`
- `HelloUltraviolet\Source\Audit\AsyncArtifactCompositionExecution.uv:159`
- `HelloUltraviolet\Source\Audit\AsyncArtifactCompositionExecution.uv:161`

Observed behavior:

Async combinator lowering records the wrapper-lowering rule ids, then the LLVM
call emitter stores the source async value in a local `async_slot`. Map,
filter, fold, and chain operate by repeatedly loading and updating that
source-shaped value. Fold uses a local accumulator slot during the call, and
chain invokes the continuation and stores the resulting async directly as the
result. No durable `MappedAsync`, `FilteredAsync`, `FoldAsync`, or `ChainAsync`
wrapper frame carrying the mapper, predicate, accumulator, continuation, or
inner chained state is materialized.

Expected behavior:

The spec requires async combinators to create wrapper async values and lower
them to wrapper async state machines. Each wrapper lowering must delegate to the
source async through `resume`, store local wrapper state in the generated async
frame, and preserve the section 21.3.5 dynamic semantics across resumptions.

Impact:

The current lowering can satisfy one-output and already-completing examples
without implementing persistent wrapper behavior. Multi-resume map/filter/fold
and chained continuations can lose wrapper-local state or return a source-shaped
async value instead of a wrapper state machine.

HelloUltraviolet fixture gap:

The manifest credits these rows through `Reference::Async` functions that cover
single-output or already-completing cases. `AsyncArtifactCompositionExecution`
also checks for the lowering labels in conformance text. Neither surface proves
that wrapper-local state is stored in the generated async frame and survives
multiple resumes.

### UV-AUDIT-0305: Derive requires/emits ordering is ambiguous and HUV codifies one direction

Severity: Medium

Status: Spec ambiguity with HelloUltraviolet accounting impact.

Spec anchors:

- `Docs\SPECIFICATION.md:25618`
- `Docs\SPECIFICATION.md:25619`
- `Docs\SPECIFICATION.md:25624`
- `Docs\SPECIFICATION.md:25625`
- `Docs\SPECIFICATION.md:25626`
- `Docs\SPECIFICATION.md:25627`
- `Docs\SPECIFICATION.md:25652`
- `Docs\SPECIFICATION.md:25654`
- `Docs\SPECIFICATION.md:25656`
- `Docs\SPECIFICATION.md:25684`
- `Docs\SPECIFICATION.md:25688`
- `Docs\Internal\UltravioletSpecification.obligations.md:86223`
- `Docs\Internal\UltravioletSpecification.obligations.md:86224`
- `Docs\Internal\UltravioletSpecification.obligations.md:86225`
- `Docs\Internal\UltravioletSpecification.obligations.md:86226`
- `Docs\Internal\UltravioletSpecification.obligations.md:86291`
- `Docs\Internal\UltravioletSpecification.obligations.md:86297`
- `Docs\Internal\UltravioletSpecification.obligations.md:86459`

Implementation/test anchors:

- `Bootstrap\Ultraviolet\src\03_comptime\derive.cpp:659`
- `Bootstrap\Ultraviolet\src\03_comptime\derive.cpp:660`
- `Bootstrap\Ultraviolet\src\03_comptime\derive.cpp:661`
- `Bootstrap\Ultraviolet\src\03_comptime\derive.cpp:806`
- `Bootstrap\Ultraviolet\src\03_comptime\derive.cpp:810`
- `Bootstrap\Ultraviolet\src\03_comptime\derive.cpp:811`
- `Bootstrap\Ultraviolet\src\03_comptime\derive.cpp:832`
- `Bootstrap\Ultraviolet\src\03_comptime\derive.cpp:836`
- `Bootstrap\Ultraviolet\src\03_comptime\derive.cpp:856`
- `HelloUltraviolet\Fixtures\AcceptedProjects\ComptimeConformance\Source\Library.uv:62`
- `HelloUltraviolet\Fixtures\AcceptedProjects\ComptimeConformance\Source\Library.uv:106`
- `HelloUltraviolet\Fixtures\AcceptedProjects\ComptimeConformance\Source\Library.uv:119`
- `HelloUltraviolet\Fixtures\AcceptedProjects\ComptimeConformance\Source\Library.uv:121`
- `HelloUltraviolet\Fixtures\AcceptedProjects\ComptimeConformance\Source\Library.uv:156`
- `HelloUltraviolet\Fixtures\AcceptedProjects\ComptimeConformance\Source\Library.uv:157`
- `HelloUltraviolet\Source\Audit\Catalog\CompileTimeExecutionAndMetaprogramming\CompilerConformanceExecution.uv:391`
- `HelloUltraviolet\Source\Audit\Catalog\CompileTimeExecutionAndMetaprogramming\CompilerConformanceExecution.uv:396`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:4207`

Observed behavior:

The specification defines an edge from a requesting target to an emitting target
when the first target's `requires` set intersects the second target's `emits`
set. It then requires a stable topological order of that graph. Under the
usual topological-order reading, this puts the requiring target before the
emitting target.

The compiler implements that literal direction. `DeriveEdge(lhs, rhs)` checks
`lhs.requires` against `rhs.emits`, stores `outgoing[i].push_back(j)`, and emits
zero-indegree requests first. HUV's accepted comptime-conformance fixture lists
the emitting derive target before the requiring derive target in source order,
but the audit check expects the recorded order
`RequireComptimeConformanceDerive,EmitComptimeConformanceDerive`.

Expected behavior:

The spec needs an explicit normative statement for the intended direction. If
`requires C` means "this target depends on a target that emits C", then the
provider should run before the dependent, and the graph edge should point from
emitter to requirer or the topological-order convention should be inverted. If
the current dependent-before-provider order is intentional, the spec should
state that and explain how `requires` is meant to affect execution.

Impact:

This is not a confirmed compiler deviation because the implementation follows
the current literal rule. It is a spec ambiguity and HUV accounting issue:
`requirement.22.DeriveExecutionOrder` is counted by a fixture that codifies the
dependent-before-provider interpretation rather than proving a dependency-style
derive order.

### UV-AUDIT-0306: Poison-check behavior is credited by conformance text

Severity: Medium

Status: Locally verified HelloUltraviolet coverage gap.

Spec anchors:

- `Docs\SPECIFICATION.md:30395`
- `Docs\SPECIFICATION.md:30411`
- `Docs\SPECIFICATION.md:30412`
- `Docs\SPECIFICATION.md:30419`
- `Docs\SPECIFICATION.md:30427`
- `Docs\SPECIFICATION.md:30429`
- `Docs\Internal\UltravioletSpecification.obligations.md:100492`
- `Docs\Internal\UltravioletSpecification.obligations.md:100508`
- `Docs\Internal\UltravioletSpecification.obligations.md:100517`
- `Docs\Internal\UltravioletSpecification.obligations.md:100550`
- `Docs\Internal\UltravioletSpecification.obligations.md:100566`

Implementation/test anchors:

- `Bootstrap\Ultraviolet\src\05_codegen\llvm\llvm_ir_panic.cpp:116`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\llvm_ir_panic.cpp:121`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\llvm_ir_panic.cpp:123`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\llvm_ir_panic.cpp:936`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\llvm_ir_panic.cpp:971`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\llvm_ir_panic.cpp:977`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\llvm_ir_panic.cpp:988`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\llvm_ir_panic.cpp:990`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\llvm_ir_panic.cpp:997`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\llvm_ir_panic.cpp:1000`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:4895`
- `HelloUltraviolet\Source\Audit\Catalog\CommonLoweringProgramLifecycleAndBackend\BackendRequirements.uv:1199`
- `HelloUltraviolet\Source\Audit\Catalog\CommonLoweringProgramLifecycleAndBackend\BackendRequirements.uv:1202`
- `HelloUltraviolet\Source\Audit\ArtifactProjectExecution.uv:3277`
- `HelloUltraviolet\Source\Audit\ArtifactProjectExecution.uv:3281`
- `HelloUltraviolet\Source\Audit\ArtifactProjectExecution.uv:3282`
- `HelloUltraviolet\Source\Audit\ArtifactProjectExecution.uv:3285`
- `HelloUltraviolet\Source\Audit\AcceptedProjectExecution.uv:837`
- `HelloUltraviolet\Source\Audit\AcceptedProjectExecution.uv:843`
- `HelloUltraviolet\Source\Audit\AcceptedProjectExecution.uv:849`

Observed behavior:

`EmitPoisonCheck` loads the poison flag, emits a conditional branch, records
`sem.24.CheckPoisonBehavior`, and writes an init-panic record on the poison
path. HUV credits the row by looking for the conformance label and payload text
such as `reads_poison_flag=true`, `predicate=flag_nonzero`, and
`panic_on_poison=true`.

No HUV fixture establishes two concrete runtime stores with poison flag zero
and nonzero, executes the lowered check in both states, and compares the
nonzero result to `LowerPanic(InitPanic(m))` and the zero result to `Val(())`.

Expected behavior:

The `sem.24.CheckPoisonBehavior` row is semantic, not merely structural. The
testing surface needs an executable reference or generated artifact check that
observes both flag states and validates the required control outcome and panic
record effect.

Impact:

The current compiler path appears structurally plausible, so this entry is a
coverage finding rather than a confirmed compiler defect. HUV can still report
the poison-check behavior row as covered when the checked evidence is only the
compiler's self-reported conformance payload.

### UV-AUDIT-0307: StoreGlobal lowering silently skips or zeroes missing stores

Severity: High

Status: Locally verified compiler defect with HelloUltraviolet coverage gap.

Spec anchors:

- `Docs\SPECIFICATION.md:29767`
- `Docs\SPECIFICATION.md:29768`
- `Docs\SPECIFICATION.md:29777`
- `Docs\SPECIFICATION.md:29897`
- `Docs\SPECIFICATION.md:29898`
- `Docs\SPECIFICATION.md:29900`
- `Docs\SPECIFICATION.md:30414`
- `Docs\SPECIFICATION.md:30419`
- `Docs\Internal\UltravioletSpecification.obligations.md:98472`
- `Docs\Internal\UltravioletSpecification.obligations.md:98482`
- `Docs\Internal\UltravioletSpecification.obligations.md:98516`
- `Docs\Internal\UltravioletSpecification.obligations.md:98528`
- `Docs\Internal\UltravioletSpecification.obligations.md:98889`
- `Docs\Internal\UltravioletSpecification.obligations.md:98898`
- `Docs\Internal\UltravioletSpecification.obligations.md:98901`
- `Docs\Internal\UltravioletSpecification.obligations.md:100530`
- `Docs\Internal\UltravioletSpecification.obligations.md:100546`

Implementation/test anchors:

- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\memory\store_global.cpp:66`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\memory\store_global.cpp:76`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\memory\store_global.cpp:87`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\memory\store_global.cpp:89`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\memory\store_global.cpp:92`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\memory\store_global.cpp:107`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\memory\store_global.cpp:109`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\memory\store_global.cpp:111`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\memory\store_global.cpp:132`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\memory\store_global.cpp:136`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\memory\store_global.cpp:151`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\memory\store_global.cpp:153`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\memory\store_global.cpp:160`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\memory\store_global.cpp:161`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\memory\store_global.cpp:162`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:4816`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:4817`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:4819`
- `HelloUltraviolet\Source\Audit\Catalog\CommonLoweringProgramLifecycleAndBackend\BackendRequirements.uv:488`
- `HelloUltraviolet\Source\Audit\Catalog\CommonLoweringProgramLifecycleAndBackend\BackendRequirements.uv:497`
- `HelloUltraviolet\Source\Audit\Catalog\CommonLoweringProgramLifecycleAndBackend\BackendRequirements.uv:515`
- `HelloUltraviolet\Source\Audit\ArtifactProjectExecution.uv:4768`
- `HelloUltraviolet\Source\Audit\ArtifactProjectExecution.uv:4772`
- `HelloUltraviolet\Source\Audit\ArtifactProjectExecution.uv:4774`
- `HelloUltraviolet\Source\Audit\ArtifactProjectExecution.uv:4775`
- `HelloUltraviolet\Source\Audit\ArtifactProjectExecution.uv:4778`

Observed behavior:

`IRStoreGlobal` resolves the target symbol and type opportunistically. It
returns without a failure if the target is a constant global with no hosted
state slot, if a hosted state slot is expected but no target pointer and no
global variable are available, or if neither target pointer nor global variable
can be resolved. If the stored IR value cannot be evaluated, the emitter writes
an LLVM null value of the target type. The `Lower-StoreGlobal` conformance row
is recorded only after `CreateStore`.

HUV credits the static type and state-reference rows through conformance labels
and payload text, but it does not contain a negative artifact that proves
missing static type, missing state slot, or missing store value is a hard
lowering failure rather than an omitted store or zero write.

Expected behavior:

`Lower-StoreGlobal` has explicit premises for `StaticType(sym)`,
`LLVMTy(T)`, and `StateRef(sym)`, and its conclusion emits
`Store(slot, v, T)`. A failed premise or missing lowered value must not become
a successful no-op or an invented zero value. It must fail lowering through the
diagnostic/error path required for an undefined or ill-formed backend state.

Impact:

Static initialization, poison setting, hosted-session state writes, and shared
library image state can be silently absent or reset to zero while the audit
surface still counts the surrounding backend rows as covered.

### UV-AUDIT-0308: Drop-glue coverage checks declaration shape instead of DropGlueSpec

Severity: Medium

Status: Locally verified HelloUltraviolet coverage gap with spec ambiguity note.

Spec anchors:

- `Docs\SPECIFICATION.md:28749`
- `Docs\SPECIFICATION.md:30327`
- `Docs\SPECIFICATION.md:30328`
- `Docs\Internal\UltravioletSpecification.obligations.md:100183`
- `Docs\Internal\UltravioletSpecification.obligations.md:100186`
- `Docs\Internal\UltravioletSpecification.obligations.md:100197`
- `Docs\Internal\UltravioletSpecification.obligations.md:100209`
- `Docs\Internal\UltravioletSpecification.obligations.md:100210`
- `Docs\Internal\UltravioletSpecification.obligations.md:100214`
- `Docs\Internal\UltravioletSpecification.obligations.md:100223`
- `Docs\Internal\UltravioletSpecification.obligations.md:100224`
- `Docs\Internal\UltravioletSpecification.obligations.md:100226`

Implementation/test anchors:

- `Bootstrap\Ultraviolet\src\05_codegen\cleanup\cleanup.cpp:1796`
- `Bootstrap\Ultraviolet\src\05_codegen\cleanup\cleanup.cpp:1804`
- `Bootstrap\Ultraviolet\src\05_codegen\cleanup\cleanup.cpp:1805`
- `Bootstrap\Ultraviolet\src\05_codegen\cleanup\cleanup.cpp:1811`
- `Bootstrap\Ultraviolet\src\05_codegen\cleanup\cleanup.cpp:1823`
- `Bootstrap\Ultraviolet\src\05_codegen\cleanup\cleanup.cpp:1824`
- `Bootstrap\Ultraviolet\src\05_codegen\cleanup\cleanup.cpp:1831`
- `Bootstrap\Ultraviolet\src\05_codegen\cleanup\cleanup.cpp:1841`
- `Bootstrap\Ultraviolet\src\05_codegen\cleanup\cleanup.cpp:1844`
- `Bootstrap\Ultraviolet\src\05_codegen\cleanup\cleanup.cpp:1846`
- `Bootstrap\Ultraviolet\src\05_codegen\cleanup\cleanup.cpp:1849`
- `Bootstrap\Ultraviolet\src\05_codegen\cleanup\cleanup.cpp:1853`
- `Bootstrap\Ultraviolet\src\05_codegen\cleanup\cleanup.cpp:1861`
- `Bootstrap\Ultraviolet\src\05_codegen\cleanup\cleanup.cpp:1863`
- `Bootstrap\Ultraviolet\src\05_codegen\cleanup\cleanup.cpp:1866`
- `Bootstrap\Ultraviolet\src\05_codegen\cleanup\cleanup.cpp:1877`
- `Bootstrap\Ultraviolet\src\05_codegen\cleanup\cleanup.cpp:1880`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:4878`
- `HelloUltraviolet\Source\Audit\ArtifactProjectExecution.uv:6009`
- `HelloUltraviolet\Source\Audit\ArtifactProjectExecution.uv:6013`
- `HelloUltraviolet\Source\Audit\ArtifactProjectExecution.uv:6025`
- `HelloUltraviolet\Source\Audit\ArtifactProjectExecution.uv:6029`
- `HelloUltraviolet\Source\Audit\ArtifactProjectExecution.uv:6033`
- `HelloUltraviolet\Source\Audit\ArtifactProjectExecution.uv:6037`
- `HelloUltraviolet\Source\Audit\ArtifactProjectExecution.uv:6041`

Observed behavior:

The compiler's drop-glue builder constructs a symbol, reads the value through
the `data` parameter, calls `EmitDropImpl`, and emits a procedure with the
expected data and panic-out parameters. HUV credits `rule.24.EmitDropGlue-Decl`
by checking the declaration label, symbol/helper payloads, parameter text, and
`body=DropGlueIR`.

That evidence does not execute the generated drop glue or compare its effects
against `DropValue(T, v, ∅)`. It also does not prove child-drop order, direct
drop invocation, release behavior, or panic propagation for a value whose type
actually requires drop.

Spec ambiguity note:

`DropGlueSpec` equates the final store produced by `ExecIRSigma(IR, σ)` with
`DropValue(T, v, ∅)`, but leaves the `out` control result unconstrained in the
displayed formula. If control-result equivalence is intended, the specification
should bind it explicitly. Even under the weaker store-equivalence reading, HUV
does not currently prove the required equivalence.

Expected behavior:

Coverage for `EmitDropGlue-Decl` should include an executable or reference
case that calls generated drop glue for a drop-relevant type and validates the
result against the `DropGlueSpec` relation, not only the declaration shape.

Impact:

Vtable drop glue can be counted as covered even if the generated body diverges
from the cleanup semantics that ordinary `DropValue` would apply to the same
value.

### UV-AUDIT-0309: Partial hosted-init cleanup prefix is credited without a failing init path

Severity: Medium

Status: Locally verified HelloUltraviolet coverage gap.

Spec anchors:

- `Docs\SPECIFICATION.md:26368`
- `Docs\SPECIFICATION.md:28654`
- `Docs\SPECIFICATION.md:28691`
- `Docs\SPECIFICATION.md:28694`
- `Docs\SPECIFICATION.md:28760`
- `Docs\SPECIFICATION.md:28761`
- `Docs\Internal\UltravioletSpecification.obligations.md:94928`
- `Docs\Internal\UltravioletSpecification.obligations.md:94937`
- `Docs\Internal\UltravioletSpecification.obligations.md:95348`
- `Docs\Internal\UltravioletSpecification.obligations.md:95357`
- `Docs\Internal\UltravioletSpecification.obligations.md:95361`
- `Docs\Internal\UltravioletSpecification.obligations.md:95370`

Implementation/test anchors:

- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\hosted_emit.cpp:568`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\hosted_emit.cpp:1071`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\hosted_emit.cpp:1073`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\hosted_emit.cpp:1076`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\hosted_emit.cpp:1081`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\hosted_emit.cpp:1087`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\hosted_emit.cpp:1089`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\hosted_emit.cpp:1093`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\hosted_emit.cpp:1094`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:4610`
- `HelloUltraviolet\Source\Audit\Catalog\CommonLoweringProgramLifecycleAndBackend\InitializationAndProgramLifecycle.uv:677`
- `HelloUltraviolet\Source\Audit\AcceptedProjectExecution.uv:2781`
- `HelloUltraviolet\Source\Audit\AcceptedProjectExecution.uv:2789`
- `HelloUltraviolet\Source\Audit\AcceptedProjectExecution.uv:2793`
- `HelloUltraviolet\Fixtures\AcceptedProjects\HostedExportLibrary\Source\Library.uv:31`
- `HelloUltraviolet\Fixtures\AcceptedProjects\HostedExportLibrary\Source\Library.uv:35`
- `HelloUltraviolet\Fixtures\AcceptedProjects\HostedExportLibrary\Source\Library.uv:109`
- `HelloUltraviolet\Fixtures\AcceptedProjects\HostedExportLibrary\Source\Library.uv:120`

Observed behavior:

The hosted emitter records `rule.24.HostSessionInitSigma` unconditionally for
the hosted lifecycle payload and records `req.24.PartialInitPanicCleanupPrefix`
inside the generated failure branch with payload fields for prefix length and
reverse cleanup order. HUV credits the row by checking that
`hosted.conformance.log` contains `req.24.PartialInitPanicCleanupPrefix` and
`cleanup_prefix_reverse_order=true`.

The accepted hosted fixture contains ordinary exported procedures and one
private hosted-state variable, but the searched fixture surface does not create
an initializer that panics after a strict prefix of responsible static bindings
has completed. It therefore does not observe that the failed session handle is
not exposed, that only the completed prefix is dropped, that remaining static
deinit actions in the failing module are skipped, or that later modules are not
deinitialized.

Expected behavior:

Coverage for `req.24.PartialInitPanicCleanupPrefix` needs a hosted-library
case that forces session creation to fail during static initialization after a
known responsible-static prefix, then verifies the cleanup trace/state against
the exact prefix requirements in sections 24.4.4 and 24.5.2.

Impact:

Hosted-library lifecycle rows can be reported covered while the most important
failure-path behavior is only asserted by compiler-emitted conformance text.
This leaves partially initialized session state, cleanup ordering, and failed
handle isolation unexercised.

### UV-AUDIT-0310: Hosted session lifecycle rows are credited without calling lifecycle exports

Severity: High

Status: Locally verified HelloUltraviolet coverage gap.

Spec anchors:

- `Docs\SPECIFICATION.md:26368`
- `Docs\SPECIFICATION.md:26369`
- `Docs\SPECIFICATION.md:26373`
- `Docs\SPECIFICATION.md:28478`
- `Docs\SPECIFICATION.md:28675`
- `Docs\SPECIFICATION.md:28685`
- `Docs\SPECIFICATION.md:28687`
- `Docs\SPECIFICATION.md:28688`
- `Docs\SPECIFICATION.md:28691`
- `Docs\SPECIFICATION.md:28694`
- `Docs\SPECIFICATION.md:28696`
- `Docs\SPECIFICATION.md:28699`
- `Docs\SPECIFICATION.md:28701`
- `Docs\SPECIFICATION.md:28704`
- `Docs\Internal\UltravioletSpecification.obligations.md:88451`
- `Docs\Internal\UltravioletSpecification.obligations.md:88452`
- `Docs\Internal\UltravioletSpecification.obligations.md:88478`
- `Docs\Internal\UltravioletSpecification.obligations.md:94336`
- `Docs\Internal\UltravioletSpecification.obligations.md:95037`
- `Docs\Internal\UltravioletSpecification.obligations.md:95059`
- `Docs\Internal\UltravioletSpecification.obligations.md:95072`
- `Docs\Internal\UltravioletSpecification.obligations.md:95085`
- `Docs\Internal\UltravioletSpecification.obligations.md:95098`
- `Docs\Internal\UltravioletSpecification.obligations.md:95111`
- `Docs\Internal\UltravioletSpecification.obligations.md:95114`
- `Docs\Internal\UltravioletSpecification.obligations.md:95127`
- `Docs\Internal\UltravioletSpecification.obligations.md:95130`
- `Docs\Internal\UltravioletSpecification.obligations.md:95143`
- `Docs\Internal\UltravioletSpecification.obligations.md:95146`

Implementation/test anchors:

- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\hosted_emit.cpp:521`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\hosted_emit.cpp:527`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\hosted_emit.cpp:530`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\hosted_emit.cpp:545`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\hosted_emit.cpp:558`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\hosted_emit.cpp:562`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\hosted_emit.cpp:564`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\hosted_emit.cpp:566`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\hosted_emit.cpp:568`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\hosted_emit.cpp:570`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\hosted_emit.cpp:813`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\hosted_emit.cpp:817`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\hosted_emit.cpp:819`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:4578`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:4579`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:4619`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:4620`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:4621`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:4622`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:4623`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:4624`
- `HelloUltraviolet\Source\Audit\Catalog\CommonLoweringProgramLifecycleAndBackend\InitializationAndProgramLifecycle.uv:389`
- `HelloUltraviolet\Source\Audit\Catalog\CommonLoweringProgramLifecycleAndBackend\InitializationAndProgramLifecycle.uv:398`
- `HelloUltraviolet\Source\Audit\Catalog\CommonLoweringProgramLifecycleAndBackend\InitializationAndProgramLifecycle.uv:758`
- `HelloUltraviolet\Source\Audit\Catalog\CommonLoweringProgramLifecycleAndBackend\InitializationAndProgramLifecycle.uv:767`
- `HelloUltraviolet\Source\Audit\Catalog\CommonLoweringProgramLifecycleAndBackend\InitializationAndProgramLifecycle.uv:776`
- `HelloUltraviolet\Source\Audit\Catalog\CommonLoweringProgramLifecycleAndBackend\InitializationAndProgramLifecycle.uv:785`
- `HelloUltraviolet\Source\Audit\Catalog\CommonLoweringProgramLifecycleAndBackend\InitializationAndProgramLifecycle.uv:794`
- `HelloUltraviolet\Source\Audit\Catalog\CommonLoweringProgramLifecycleAndBackend\InitializationAndProgramLifecycle.uv:803`
- `HelloUltraviolet\Source\Audit\AcceptedProjectExecution.uv:2343`
- `HelloUltraviolet\Source\Audit\AcceptedProjectExecution.uv:2636`
- `HelloUltraviolet\Source\Audit\AcceptedProjectExecution.uv:2731`
- `HelloUltraviolet\Source\Audit\AcceptedProjectExecution.uv:2749`
- `HelloUltraviolet\Source\Audit\AcceptedProjectExecution.uv:2753`
- `HelloUltraviolet\Source\Audit\AcceptedProjectExecution.uv:2772`
- `HelloUltraviolet\Source\Audit\AcceptedProjectExecution.uv:2775`
- `HelloUltraviolet\Source\Audit\AcceptedProjectExecution.uv:2779`
- `HelloUltraviolet\Source\Audit\AcceptedProjectExecution.uv:2781`
- `HelloUltraviolet\Source\Audit\AcceptedProjectExecution.uv:2782`
- `HelloUltraviolet\Source\Audit\AcceptedProjectExecution.uv:2785`
- `HelloUltraviolet\Source\Audit\AcceptedProjectExecution.uv:2825`
- `HelloUltraviolet\Source\Audit\AcceptedProjectExecution.uv:2826`
- `HelloUltraviolet\Source\Audit\AcceptedProjectExecution.uv:2828`
- `HelloUltraviolet\Source\Audit\AcceptedProjectExecution.uv:2846`
- `HelloUltraviolet\Source\Audit\AcceptedProjectExecution.uv:2852`
- `HelloUltraviolet\Source\Audit\AcceptedProjectExecution.uv:2934`
- `HelloUltraviolet\Source\Audit\AcceptedProjectExecution.uv:2937`
- `HelloUltraviolet\Source\Audit\AcceptedProjectExecution.uv:2944`
- `HelloUltraviolet\Source\Audit\AcceptedProjectExecution.uv:2986`
- `HelloUltraviolet\Source\Audit\AcceptedProjectExecution.uv:2987`
- `HelloUltraviolet\Fixtures\AcceptedProjects\HostedExportLibrary\Source\Library.uv:34`
- `HelloUltraviolet\Fixtures\AcceptedProjects\HostedExportLibrary\Source\Library.uv:108`
- `HelloUltraviolet\Fixtures\AcceptedProjects\HostedExportLibrary\Source\Library.uv:119`

Observed behavior:

`acceptedProjectHostedExercise` builds `Fixtures/AcceptedProjects/HostedExportLibrary`
and then checks the produced library, import library, export map, telemetry, and
`hosted.conformance.log`. It verifies that lifecycle and hosted thunk symbols
appear in the map and that the compiler recorded Chapter 24 hosted-session rows.
The fixture source defines exported procedures and one private hosted-state
variable, but the accepted-project harness does not load the built library,
resolve `__ultraviolet_host_session_create` or `__ultraviolet_host_session_destroy`,
call a hosted thunk, test invalid handles, create two live sessions, attempt
reentry, or destroy a session.

Expected behavior:

Rows such as `req.24.HostedSessionLifecycleState`,
`req.24.HostedSessionNoConcurrentReentry`, `rule.24.HostSessionInitSigma`,
`rule.24.HostedCallSigma-Ok`, `rule.24.HostSessionDestroySigma`,
`req.24.SessionStateInitDefinesHostedCells`, `req.24.SessionStateDestroyRemovesHostedCells`,
and `req.24.DistinctHostedState` need an executable hosted-library harness. The
harness should prove successful create/call/destroy transitions, invalid and
busy handle rejection, non-reissue after destroy, distinct state across two
live sessions, and hosted-state cell removal after destroy.

Impact:

HUV reports runtime lifecycle obligations as executed even though this surface
only proves that the compiler produced symbols and self-reported conformance
rows. Session liveness, busy-state rejection, distinct hosted state, handle
validity, and teardown semantics remain unexercised.

### UV-AUDIT-0311: Foundational class obligations are credited without executing the probes

Severity: Medium

Status: Locally verified HelloUltraviolet coverage gap with specification ambiguity.

Spec anchors:

- `Docs\SPECIFICATION.md:13898`
- `Docs\SPECIFICATION.md:13900`
- `Docs\SPECIFICATION.md:13904`
- `Docs\SPECIFICATION.md:13922`
- `Docs\Internal\UltravioletSpecification.obligations.md:52758`
- `Docs\Internal\UltravioletSpecification.obligations.md:52767`
- `Docs\Internal\UltravioletSpecification.obligations.md:52857`
- `Docs\Internal\UltravioletSpecification.obligations.md:52866`

Implementation/test anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\record_decl.cpp:345`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\record_decl.cpp:347`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\record_decl.cpp:359`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\record_decl.cpp:364`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\record_decl.cpp:374`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\modal_decl.cpp:355`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\modal_decl.cpp:384`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\enum_decl.cpp:112`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\enum_decl.cpp:141`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\method_call.cpp:297`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\method_call.cpp:301`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\method_call.cpp:322`
- `Bootstrap\Ultraviolet\src\05_codegen\lower\expr\method_call.cpp:2090`
- `Bootstrap\Ultraviolet\src\05_codegen\llvm\emit\ir\call\direct.cpp:1595`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:2606`
- `HelloUltraviolet\Source\Audit\ArtifactProjectExecution.uv:6077`
- `HelloUltraviolet\Source\Audit\ArtifactProjectExecution.uv:6082`
- `HelloUltraviolet\Source\Audit\ArtifactProjectExecution.uv:6090`
- `HelloUltraviolet\Source\Audit\ArtifactProjectExecution.uv:6094`
- `HelloUltraviolet\Source\Audit\ArtifactProjectExecution.uv:6106`
- `HelloUltraviolet\Source\Audit\ArtifactProjectExecution.uv:6118`
- `HelloUltraviolet\Source\Audit\ArtifactProjectExecution.uv:6122`
- `HelloUltraviolet\Source\Audit\ArtifactProjectExecution.uv:6126`
- `HelloUltraviolet\Source\Audit\ArtifactProjectExecution.uv:6273`
- `HelloUltraviolet\Source\Audit\ArtifactProjectExecution.uv:6393`
- `HelloUltraviolet\Fixtures\ArtifactProjects\EmitLlLibrary\Source\Library.uv:654`
- `HelloUltraviolet\Fixtures\ArtifactProjects\EmitLlLibrary\Source\Library.uv:667`
- `HelloUltraviolet\Fixtures\ArtifactProjects\EmitLlLibrary\Source\Library.uv:671`

Observed behavior:

The compiler records `req.14.HashRequiresEqAndEqualValuesHashEqual` when a
record, modal, or enum implementation includes `Hash` and records whether `Eq`
is also present. The payload asserts `equal_values_hash_equal=semantic_law`.
The method-call lowering path separately records
`req.14.FoundationalIntrinsicCallLowering` for built-in `Eq.eq`,
`Discrete.successor`, and `Discrete.predecessor` dispatch.

HUV's `artifactProjectEmitLlFoundationalClassesMatch` verifies those
conformance-log payloads and checks that the LLVM module defines
`emitLlHashRequiresEqProbe`, `emitLlFoundationalEqIntrinsicProbe`, and
`emitLlFoundationalDiscreteIntrinsicProbe`. The enclosing
`artifactProjectEmitLlExercise` builds the library and inspects artifacts; it
does not load the built library or call those exported probes. The
`req.14.FoundationalIntrinsicCallLowering` row is therefore credited from
labels plus IR shape, not from executing equality or stepping behavior. The
hash-law check is weaker still: local search found no generated HUV catalog or
manifest row for `req.14.HashRequiresEqAndEqualValuesHashEqual`, even though
the matcher expects the compiler to write that label.

Spec ambiguity note:

Section 14.10.4 normatively says that equal values must hash equally from
identical initial hasher states, but it does not state whether an implementation
must prove that law statically, enforce it dynamically, test it only for
built-in/reference classes, or treat it as a programmer obligation. That
ambiguity should be resolved before classifying the absence of general law
enforcement as a compiler defect. The HUV coverage issue is independent:
coverage should not be credited from a conformance payload that merely restates
the law.

Expected behavior:

Coverage for foundational intrinsic lowering should execute the probe library
and verify at least representative `Eq.eq`, `Discrete.successor`, and
`Discrete.predecessor` results, including boundary cases where stepping returns
`()`. Coverage for the hash law should either be blocked until the spec defines
the compiler's enforcement obligation or should execute a concrete law-checking
case for the fixture's `Hash` implementation instead of relying on the
`equal_values_hash_equal=semantic_law` payload.

Impact:

The HUV report can mark foundational class behavior as exercised while only
proving that the compiler produced symbols and self-reported the lowering
labels. Incorrect built-in equality lowering, incorrect discrete boundary
handling, or a fixture hash implementation that violates equal-hash behavior
would not be caught by this artifact-only path.

### UV-AUDIT-0312: Modal invariants skip the public mutable state-field ban

Severity: Medium

Status: Locally verified compiler gap with specification ambiguity.

Spec anchors:

- `Docs\SPECIFICATION.md:10633`
- `Docs\SPECIFICATION.md:10639`
- `Docs\SPECIFICATION.md:10654`
- `Docs\SPECIFICATION.md:11031`
- `Docs\SPECIFICATION.md:11035`
- `Docs\SPECIFICATION.md:15016`
- `Docs\SPECIFICATION.md:15034`
- `Docs\SPECIFICATION.md:15058`
- `Docs\SPECIFICATION.md:15391`

Implementation/test anchors:

- `Bootstrap\Ultraviolet\include\02_source\ast\nodes\ast_items.h:686`
- `Bootstrap\Ultraviolet\include\02_source\ast\nodes\ast_items.h:689`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\record_decl.cpp:695`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\record_decl.cpp:701`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\record_decl.cpp:702`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\record_decl.cpp:715`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\modal_decl.cpp:610`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\modal_decl.cpp:617`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\modal_decl.cpp:897`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\modal_decl.cpp:903`
- `HelloUltraviolet\Fixtures\RejectedSource\Procedures\InvariantPublicMutableField\Source\Main.uv:3`
- `HelloUltraviolet\Fixtures\RejectedSource\Procedures\InvariantPublicMutableField\Source\Main.uv:4`
- `HelloUltraviolet\Fixtures\RejectedSource\Procedures\InvariantPublicMutableField\Source\Main.uv:5`
- `HelloUltraviolet\Fixtures\RejectedSource\Procedures\InvariantPublicMutableField\Expected.uv:3`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\RejectedSource\Procedures.uv:691`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\RejectedSource\Procedures.uv:700`

Observed behavior:

Record declarations with a type invariant scan their record fields and reject
any public field with `E-SEM-2824`. Modal declarations collect state fields and
lower their types, then process the modal invariant through `CheckTypeInvariant`;
they do not perform an equivalent public-field scan over `StateFieldDecl`.

HelloUltraviolet's `InvariantPublicMutableField` fixture covers only a record
with a public field and an invariant. There is no rejected fixture for a modal
type with an invariant and a public state payload field.

Spec ambiguity note:

Section 15.7.4 says "Types with type invariants MUST NOT declare public mutable
fields." Section 13.1 models modal payload fields as `StateFieldDecl` values
with their own visibility, and Section 15.7.3 attaches `invariant_opt` to
`ModalDecl`. The text does not explicitly say whether modal state payload
fields are included in "public mutable fields." If state payload fields are
excluded, the spec should say so; otherwise the modal declaration path is
missing the same check that records already perform.

Expected behavior:

Either modal state payload fields must be checked by the invariant public-field
rule, or the specification must explicitly exempt them. HUV should include a
modal invariant fixture for the chosen semantics instead of relying on the
record-only fixture.

Impact:

If modal state payload fields are public mutable fields under Section 15.7.4, a
modal type can expose mutable public state while claiming an invariant. That
weakens invariant preservation and lets the compiler and HUV report the public
mutable field rule as covered while only exercising records.

### UV-AUDIT-0313: Inline refinement predicates are compared without subject normalization

Severity: High

Status: Locally verified compiler defect.

Spec anchors:

- `Docs\SPECIFICATION.md:6095`
- `Docs\SPECIFICATION.md:6100`
- `Docs\SPECIFICATION.md:6101`
- `Docs\SPECIFICATION.md:6102`
- `Docs\SPECIFICATION.md:6104`
- `Docs\SPECIFICATION.md:6422`
- `Docs\SPECIFICATION.md:6423`
- `Docs\SPECIFICATION.md:6427`
- `Docs\SPECIFICATION.md:6428`
- `Docs\SPECIFICATION.md:13445`
- `Docs\SPECIFICATION.md:13447`
- `Docs\SPECIFICATION.md:13472`

Implementation/test anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\typing\type_lower.cpp:261`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\type_lower.cpp:267`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\type_equiv.cpp:108`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\type_equiv.cpp:116`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\type_equiv.cpp:117`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\type_equiv.cpp:126`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\type_equiv.cpp:489`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\type_equiv.cpp:504`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\subtyping.cpp:845`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\subtyping.cpp:859`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\subtyping.cpp:860`
- `HelloUltraviolet\Source\Reference\Types\Refinements.uv:4`
- `HelloUltraviolet\Source\Reference\Types\Refinements.uv:7`
- `HelloUltraviolet\Source\Reference\Types\Refinements.uv:32`
- `HelloUltraviolet\Source\Reference\Types\Refinements.uv:71`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:2538`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:2539`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:2540`
- `HelloUltraviolet\Source\Audit\Catalog\AbstractionAndPolymorphism\RefinementTypes.uv:74`
- `HelloUltraviolet\Source\Audit\Catalog\AbstractionAndPolymorphism\RefinementTypes.uv:83`
- `HelloUltraviolet\Source\Audit\Catalog\AbstractionAndPolymorphism\RefinementTypes.uv:92`

Observed behavior:

`type_lower.cpp` stores refinement predicates exactly as parsed. The type
equivalence implementation then compares predicate ASTs directly and states in
code comments that all refinement predicates uniformly spell the subject as
`self`. That assumption is false for inline parameter constraints: Section
14.8.7 says an inline parameter predicate references the parameter by name and
must not use `self`.

Subtyping has the same raw-predicate shape. It adds the left refinement
predicate as a proof fact and proves the right refinement predicate without
normalizing the introduced subject.

HelloUltraviolet credits `def.14.PredicateEquiv`,
`rule.14.T-Equiv-Refine`, and `rule.14.T-Equiv-Refine-Norm` from
`runTypesRefinementsReference`, but that reference compares standalone aliases
that both use `self`. The inline parameter specimen at
`Refinements.uv:32` is only used as a successful constrained-parameter call; it
does not compare the inline predicate against an equivalent standalone
predicate.

Expected behavior:

Before type equivalence, unification, or refinement subtyping compares
predicates, the implementation must normalize each predicate by replacing the
introduced refinement subject with the reserved subject symbol required by
`PredNorm`. HUV should include a case where `i32 |: { value > 0 }` and
`i32 |: { self > 0 }` are compared through the specified normalization path.

Impact:

Structurally equivalent refinements can be rejected solely because one source
form used an inline parameter name while another used standalone `self`.
Conversely, HUV can report refinement normalization as exercised without
testing the only syntax form whose subject is not spelled `self`.

### UV-AUDIT-0314: Inline refinement self-ban is only applied to procedure parameters

Severity: Medium

Status: Locally verified compiler and HUV coverage gap.

Spec anchors:

- `Docs\SPECIFICATION.md:13445`
- `Docs\SPECIFICATION.md:13447`
- `Docs\SPECIFICATION.md:13939`
- `Docs\SPECIFICATION.md:14392`
- `Docs\SPECIFICATION.md:14419`
- `Docs\SPECIFICATION.md:14474`
- `Docs\SPECIFICATION.md:14475`
- `Docs\SPECIFICATION.md:27029`
- `Docs\SPECIFICATION.md:27030`

Implementation/test anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\signature.cpp:49`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\signature.cpp:162`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\signature.cpp:416`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\signature.cpp:429`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\signature.cpp:432`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\signature.cpp:479`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\signature.cpp:520`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\signature.cpp:559`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\item\signature.cpp:577`
- `HelloUltraviolet\Fixtures\RejectedSource\Polymorphism\RefinementInlineSelfConstraint\Source\Main.uv:3`
- `HelloUltraviolet\Fixtures\RejectedSource\Polymorphism\RefinementInlineSelfConstraint\Expected.uv:3`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\RejectedSource\Polymorphism.uv:623`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\RejectedSource\Polymorphism.uv:632`

Observed behavior:

`BuildProcedureSignature` calls
`TypeContainsInlineParameterSelfConstraint` for each procedure parameter and
returns `E-TYP-1956` before lowering a parameter type that uses `self` inside an
inline refinement. `BuildMethodSignature` and `BuildTransitionSignature` lower
their non-receiver parameters directly with `LowerTypeWithWF`, with no
equivalent `self` check.

The HUV negative fixture `RefinementInlineSelfConstraint` is a free procedure
only. It does not cover record methods, modal state methods, class methods, or
transitions even though the spec's method and transition parameter rules route
through ordinary parameter lists.

Expected behavior:

Every inline parameter constraint in a procedure, method, class method, state
method, or transition parameter list should reject `self` with `E-TYP-1956`.
HUV should include at least one method or transition fixture so coverage is not
credited from the procedure-only path.

Impact:

Method and transition parameters can bypass the inline-constraint subject rule.
That creates a second source of non-normalized refinement predicates and leaves
HUV unable to catch regressions outside free procedures.

### UV-AUDIT-0315: Phase3 MainCheck failures are recorded as DeclTyping failures

Severity: Medium

Status: Locally verified compiler conformance-evidence defect.

Spec anchors:

- `Docs\SPECIFICATION.md:107`
- `Docs\SPECIFICATION.md:110`
- `Docs\SPECIFICATION.md:118`
- `Docs\SPECIFICATION.md:124`
- `Docs\SPECIFICATION.md:1020`
- `Docs\SPECIFICATION.md:1021`
- `Docs\SPECIFICATION.md:1022`
- `Docs\SPECIFICATION.md:14085`
- `Docs\SPECIFICATION.md:14101`

Implementation/test anchors:

- `Bootstrap\Ultraviolet\src\04_analysis\typing\typecheck.cpp:1056`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\typecheck.cpp:1077`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\typecheck.cpp:1085`
- `Bootstrap\Ultraviolet\src\04_analysis\typing\typecheck.cpp:1281`
- `Bootstrap\Ultraviolet\src\04_analysis\conformance\conformance.cpp:51`
- `Bootstrap\Ultraviolet\src\04_analysis\conformance\conformance.cpp:238`
- `Bootstrap\Ultraviolet\src\04_analysis\conformance\conformance.cpp:244`
- `Bootstrap\Ultraviolet\src\04_analysis\conformance\conformance.cpp:247`
- `Bootstrap\Ultraviolet\src\06_driver\compiler_main.cpp:4963`
- `Bootstrap\Ultraviolet\src\06_driver\compiler_main.cpp:4964`
- `Bootstrap\Ultraviolet\src\06_driver\compiler_main.cpp:4965`
- `HelloUltraviolet\Fixtures\RejectedSource\Procedures\MainMissing\Expected.uv:3`

Observed behavior:

The spec models Phase 3 as three ordered checks:
`ResolveModules`, `DeclTyping`, and `MainCheck`. The conformance implementation
has separate booleans for those checks and `FirstFail` maps a failed
`decl_typing_ok` to index 1 and a failed `main_check_ok` to index 2.

The driver fills both `decl_typing_ok` and `main_check_ok` from the same
aggregate `typecheck_ok` value. `MainCheckProject` is run inside the aggregate
typecheck path only after declaration typing and initialization planning have
not emitted errors. Therefore a missing, duplicate, generic, or bad-signature
`main` failure is indistinguishable from a declaration-typing failure in the
conformance evidence.

Expected behavior:

The driver should retain separate evidence for declaration typing and the
executable main check. A program whose declarations type-check but whose main
check fails should record the `MainCheck` slot as the first failing Phase 3
check, not the `DeclTyping` slot.

Impact:

Diagnostic output can reject the right program for the right source diagnostic
while reporting the wrong conformance failure point. Tooling or tests that
consume `def.FirstFail` can accept an implementation that conflates distinct
Phase 3 obligations.

### UV-AUDIT-0316: Phase3 ordering rows are credited by a reference-only ordering check

Severity: Medium

Status: Locally verified HelloUltraviolet coverage gap.

Spec anchors:

- `Docs\SPECIFICATION.md:107`
- `Docs\SPECIFICATION.md:110`
- `Docs\SPECIFICATION.md:118`
- `Docs\SPECIFICATION.md:124`
- `Docs\SPECIFICATION.md:127`
- `Docs\SPECIFICATION.md:1020`
- `Docs\SPECIFICATION.md:1021`
- `Docs\SPECIFICATION.md:1022`

Implementation/test anchors:

- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:7`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:8`
- `HelloUltraviolet\Source\Audit\Catalog\ConformanceAndNotation\Conformance.uv:29`
- `HelloUltraviolet\Source\Audit\Catalog\ConformanceAndNotation\Conformance.uv:38`
- `HelloUltraviolet\Source\Reference\Conformance\TranslationOrdering.uv:1`
- `HelloUltraviolet\Source\Reference\Conformance\TranslationOrdering.uv:23`
- `HelloUltraviolet\Source\Reference\Conformance\TranslationOrdering.uv:37`

Observed behavior:

The manifest credits `def.Phase3Checks` and `def.Phase3Order` to
`runConformanceTranslationOrderingReference`. That reference creates an
in-memory `TranslationCheckpoint` with fixed numeric positions and returns true
when the numbers are ordered. It does not invoke `uvc`, trigger a
`ResolveModules`, `DeclTyping`, or `MainCheck` failure, or inspect a
conformance log for the ordered `FirstFail(Phase3Checks(...))` result.

Expected behavior:

Phase 3 ordering rows should be covered by executable compiler runs that
separately exercise successful Phase 3 and first failures in resolve, decl
typing, and main check order. A pure reference value-ordering helper can remain
as explanatory material, but it should not be the coverage surface for the
compiler's Phase 3 conformance rows.

Impact:

HUV can report Phase 3 ordering as exercised while the compiler evidence path
still conflates declaration typing and main checking, and while no reference
fixture verifies the actual `FirstFail(Phase3Checks(...))` ordering.

### UV-AUDIT-0317: Attribute lowering rows are credited without executing the artifact probes

Severity: Medium

Status: Locally verified HelloUltraviolet coverage gap.

Spec anchors:

- `Docs\SPECIFICATION.md:6986`
- `Docs\SPECIFICATION.md:7003`
- `Docs\SPECIFICATION.md:7012`
- `Docs\SPECIFICATION.md:7050`
- `Docs\SPECIFICATION.md:7075`
- `Docs\SPECIFICATION.md:7079`
- `Docs\SPECIFICATION.md:7083`
- `Docs\SPECIFICATION.md:7089`
- `Docs\SPECIFICATION.md:7091`
- `Docs\SPECIFICATION.md:7095`
- `Docs\SPECIFICATION.md:7099`

Implementation/test anchors:

- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:1296`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:1301`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:1302`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:1303`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:1304`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:1305`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:1312`
- `HelloUltraviolet\Source\Audit\Catalog\AttributesAndMetadata\LayoutAttributes.uv:56`
- `HelloUltraviolet\Source\Audit\Catalog\AttributesAndMetadata\LayoutAttributes.uv:65`
- `HelloUltraviolet\Source\Audit\Catalog\AttributesAndMetadata\LayoutAttributes.uv:74`
- `HelloUltraviolet\Source\Audit\Catalog\AttributesAndMetadata\LayoutAttributes.uv:83`
- `HelloUltraviolet\Source\Audit\Catalog\AttributesAndMetadata\LayoutAttributes.uv:92`
- `HelloUltraviolet\Source\Audit\Catalog\AttributesAndMetadata\OptimizationAttributes.uv:65`
- `HelloUltraviolet\Source\Audit\AttributesMetadataArtifactExecution.uv:29`
- `HelloUltraviolet\Source\Audit\AttributesMetadataArtifactExecution.uv:84`
- `HelloUltraviolet\Source\Audit\AttributesMetadataArtifactExecution.uv:128`
- `HelloUltraviolet\Source\Audit\AttributesMetadataArtifactExecution.uv:154`
- `HelloUltraviolet\Source\Audit\AttributesMetadataArtifactExecution.uv:177`
- `HelloUltraviolet\Fixtures\ArtifactProjects\AttributeSemantics\Source\Main.uv:20`
- `HelloUltraviolet\Fixtures\ArtifactProjects\AttributeSemantics\Source\Main.uv:24`
- `HelloUltraviolet\Fixtures\ArtifactProjects\AttributeSemantics\Source\Main.uv:28`
- `HelloUltraviolet\Fixtures\ArtifactProjects\AttributeSemantics\Source\Main.uv:32`
- `HelloUltraviolet\Fixtures\ArtifactProjects\AttributeSemantics\Source\Main.uv:36`
- `HelloUltraviolet\Fixtures\ArtifactProjects\AttributeSemantics\Source\Main.uv:40`
- `HelloUltraviolet\Fixtures\ArtifactProjects\AttributeSemantics\Source\Main.uv:44`
- `HelloUltraviolet\Fixtures\ArtifactProjects\AttributeSemantics\Source\Main.uv:49`
- `HelloUltraviolet\Fixtures\ArtifactProjects\AttributeSemantics\Source\Main.uv:54`
- `HelloUltraviolet\Fixtures\ArtifactProjects\AttributeSemantics\Source\Main.uv:59`

Observed behavior:

The manifest maps the layout artifact rows for explicit enum discriminants,
packed layout, alignment, valid combinations, and dynamic semantics to
`attributesMetadataArtifactObligationsExecute`. It also maps
`conformance.OptimizationAttributeLowering` to the same artifact runner. That
runner builds `Fixtures/ArtifactProjects/AttributeSemantics`, checks that IR,
object, and library files exist, then matches conformance-log substrings and a
small number of LLVM symbol fragments.

The fixture source defines exported probes for packed size/alignment, aligned
size/alignment, small-enum size/alignment, and combined optimization-attribute
use. The artifact runner does not load the built library or call those probes.
For optimization attributes, it checks conformance payloads and that the
`never`/`cold` functions exist in IR; it does not inspect the callable result of
`attributeOptimizationUse` or verify the actual backend attributes on the
emitted definitions.

Expected behavior:

Artifact coverage for layout and optimization attributes should execute or
structurally verify the concrete semantics being credited. Layout rows should
check the built artifact's exported `sizeof`/`alignof` probes, or an equivalent
post-lowering oracle. Optimization lowering should inspect the emitted
attributes on the relevant LLVM definitions and/or execute the exported
optimization-use probe for semantic preservation.

Impact:

HUV can mark layout and optimization lowering obligations as exercised while
only proving that the compiler emitted conformance text and a couple of named
LLVM definitions. Incorrect packed alignment, enum discriminant layout, minimum
alignment, omitted `noinline`, or an optimization lowering regression that
changes call behavior would not be caught by this artifact runner.

### UV-AUDIT-0318: FFI-safe umbrella rows are credited by accepted conformance text

Severity: Medium

Status: Locally verified HelloUltraviolet coverage gap.

Spec anchors:

- `Docs\SPECIFICATION.md:25790`
- `Docs\SPECIFICATION.md:25794`
- `Docs\SPECIFICATION.md:25796`
- `Docs\SPECIFICATION.md:25948`
- `Docs\SPECIFICATION.md:25953`
- `Docs\SPECIFICATION.md:25958`
- `Docs\SPECIFICATION.md:25963`
- `Docs\SPECIFICATION.md:25973`
- `Docs\SPECIFICATION.md:25983`
- `Docs\SPECIFICATION.md:25988`
- `Docs\SPECIFICATION.md:25993`
- `Docs\SPECIFICATION.md:26010`
- `Docs\SPECIFICATION.md:26024`
- `Docs\SPECIFICATION.md:26026`
- `Docs\SPECIFICATION.md:26034`
- `Docs\SPECIFICATION.md:26040`
- `Docs\SPECIFICATION.md:26046`

Implementation/test anchors:

- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:4246`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:4247`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:4248`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:4249`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:4250`
- `HelloUltraviolet\Source\Audit\Catalog\ForeignFunctionInterface\FfiSafe.uv:236`
- `HelloUltraviolet\Source\Audit\Catalog\ForeignFunctionInterface\FfiSafe.uv:245`
- `HelloUltraviolet\Source\Audit\Catalog\ForeignFunctionInterface\FfiSafe.uv:254`
- `HelloUltraviolet\Source\Audit\Catalog\ForeignFunctionInterface\FfiSafe.uv:263`
- `HelloUltraviolet\Source\Audit\Catalog\ForeignFunctionInterface\FfiSafe.uv:272`
- `HelloUltraviolet\Source\Reference\FFI\FfiSafe.uv:12`
- `HelloUltraviolet\Source\Reference\FFI\FfiSafe.uv:26`
- `HelloUltraviolet\Source\Reference\FFI\FfiSafe.uv:34`
- `HelloUltraviolet\Source\Reference\FFI\FfiSafe.uv:55`
- `HelloUltraviolet\Source\Audit\ArtifactProjectExecution.uv:3017`
- `HelloUltraviolet\Source\Audit\ArtifactProjectExecution.uv:3038`
- `HelloUltraviolet\Source\Audit\ArtifactProjectExecution.uv:3042`
- `HelloUltraviolet\Source\Audit\ArtifactProjectExecution.uv:3054`
- `HelloUltraviolet\Source\Audit\ArtifactProjectExecution.uv:3058`
- `HelloUltraviolet\Source\Audit\ArtifactProjectExecution.uv:3062`
- `HelloUltraviolet\Source\Audit\ArtifactProjectExecution.uv:3066`
- `HelloUltraviolet\Source\Audit\ArtifactProjectExecution.uv:6400`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\RejectedSource\FFI.uv:422`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\RejectedSource\FFI.uv:456`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\RejectedSource\FFI.uv:473`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\RejectedSource\FFI.uv:490`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\RejectedSource\FFI.uv:507`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\RejectedSource\FFI.uv:558`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\RejectedSource\FFI.uv:575`
- `HelloUltraviolet\Source\Audit\FixtureCatalog\RejectedSource\FFI.uv:592`

Observed behavior:

The manifest maps `requirement.23.FfiSafeProhibitedCategories`,
`requirement.23.FfiSafeRaiiByValueRule`,
`requirement.23.FfiSafeGenericBounds`,
`requirement.23.FfiSafeDynamicSemantics`, and
`requirement.23.FfiSafeLowering` to `artifactProjectEmitLlExercise`. Inside
that artifact runner, `artifactProjectEmitLlFfiSafeRulesMatch` checks only that
the accepted EmitLl build's conformance log contains those labels and selected
payload fragments such as `required_bounds=FfiSafe;result=accepted`.

The concrete rejected-source fixture catalog does cover many FFI-safe
diagnostics: prohibited categories, missing `#layout(C)`, non-FFI-safe record
fields and enum payloads, incomplete layout, and unbounded generic parameters.
Those fixtures are not the target credited for the five umbrella requirement
rows above. The accepted `runFFIFfiSafeReference` covers several positive value
forms, but it also is not the credited target for those rows.

Expected behavior:

Umbrella FFI-safe rows should be credited from the concrete positive and
negative surfaces that exercise the rule. Prohibited categories and generic
bounds should point at the rejected fixtures that actually fail; the RAII
by-value row should include both the accepted `#ffi_pass_by_value` case and a
rejected missing-attribute case; dynamic semantics and lowering should be tied
to ABI-boundary behavior or explicitly blocked when the spec states no
feature-local runtime or lowering mechanism exists.

Impact:

HUV can report broad FFI-safe requirements as exercised by an accepted artifact
that merely self-reports conformance labels. Regressions in rejected
diagnostics, missing negative coverage for by-value RAII, or ABI-boundary
lowering mistakes can be hidden behind rows that never cite the test surface
where those obligations are actually checked.

### UV-AUDIT-0319: RegionsAndFrames credits closure and loop provenance without exercising those forms

Severity: High

Status: Locally verified HelloUltraviolet coverage gap.

Spec anchors:

- `Docs\SPECIFICATION.md:4343`
- `Docs\SPECIFICATION.md:4348`
- `Docs\SPECIFICATION.md:4360`
- `Docs\SPECIFICATION.md:4364`
- `Docs\SPECIFICATION.md:4369`
- `Docs\SPECIFICATION.md:4387`
- `Docs\SPECIFICATION.md:4389`
- `Docs\SPECIFICATION.md:4399`
- `Docs\SPECIFICATION.md:4404`
- `Docs\SPECIFICATION.md:4409`
- `Docs\SPECIFICATION.md:4416`
- `Docs\SPECIFICATION.md:4423`
- `Docs\SPECIFICATION.md:4424`

Implementation/test anchors:

- `HelloUltraviolet\Source\Reference\Authority\RegionsAndFrames.uv:3`
- `HelloUltraviolet\Source\Reference\Authority\RegionsAndFrames.uv:14`
- `HelloUltraviolet\Source\Reference\Authority\RegionsAndFrames.uv:36`
- `HelloUltraviolet\Source\Reference\Authority\RegionsAndFrames.uv:63`
- `HelloUltraviolet\Source\Reference\Authority\RegionsAndFrames.uv:78`
- `HelloUltraviolet\Source\Audit\Catalog\AbstractMachineObjectsResponsibilityAndAuthority\RegionsFramesAndProvenance.uv:344`
- `HelloUltraviolet\Source\Audit\Catalog\AbstractMachineObjectsResponsibilityAndAuthority\RegionsFramesAndProvenance.uv:353`
- `HelloUltraviolet\Source\Audit\Catalog\AbstractMachineObjectsResponsibilityAndAuthority\RegionsFramesAndProvenance.uv:389`
- `HelloUltraviolet\Source\Audit\Catalog\AbstractMachineObjectsResponsibilityAndAuthority\RegionsFramesAndProvenance.uv:398`
- `HelloUltraviolet\Source\Audit\Catalog\AbstractMachineObjectsResponsibilityAndAuthority\RegionsFramesAndProvenance.uv:407`
- `HelloUltraviolet\Source\Audit\Catalog\AbstractMachineObjectsResponsibilityAndAuthority\RegionsFramesAndProvenance.uv:425`
- `HelloUltraviolet\Source\Audit\Catalog\AbstractMachineObjectsResponsibilityAndAuthority\RegionsFramesAndProvenance.uv:434`
- `HelloUltraviolet\Source\Audit\Catalog\AbstractMachineObjectsResponsibilityAndAuthority\RegionsFramesAndProvenance.uv:470`
- `HelloUltraviolet\Source\Audit\Catalog\AbstractMachineObjectsResponsibilityAndAuthority\RegionsFramesAndProvenance.uv:479`
- `HelloUltraviolet\Source\Audit\Catalog\AbstractMachineObjectsResponsibilityAndAuthority\RegionsFramesAndProvenance.uv:488`
- `HelloUltraviolet\Source\Audit\Catalog\AbstractMachineObjectsResponsibilityAndAuthority\RegionsFramesAndProvenance.uv:506`
- `HelloUltraviolet\Source\Audit\Catalog\AbstractMachineObjectsResponsibilityAndAuthority\RegionsFramesAndProvenance.uv:533`
- `HelloUltraviolet\Source\Audit\Catalog\AbstractMachineObjectsResponsibilityAndAuthority\RegionsFramesAndProvenance.uv:542`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:717`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:718`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:723`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:724`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:726`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:727`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:731`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:732`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:733`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:735`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:738`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:739`

Observed behavior:

`runAuthorityRegionsAndFramesReference` and its helper procedures exercise
region options, nested `region` and `frame` blocks, allocation expressions,
`Region::new_scoped`, `alloc`, and `free_unchecked`. The file contains ordinary
`if` statements but no closure expression, `if is` case expression, case
expression, loop form, break value, iterator binding, or heap-escape scenario.

The generated catalog nevertheless credits that same accepted reference target
for provenance obligations including `P-If-Is`, `P-If-Cases`,
`ClosureEscapeCheck`, both closure provenance rules, break and iterator
provenance, all three loop provenance rules, the no-general-heap-escape
requirement, assignment escape checking, and the provenance escape judgment set.

Expected behavior:

Each provenance rule family should be credited by a specimen that contains the
syntax and control-flow shape consumed by the rule. Closure provenance needs
capturing and non-capturing closure values; loop provenance needs infinite,
conditional, and iterator loops with relevant break behavior; case provenance
needs `if is` and case expressions; escape-check rows need direct assignment,
closure, and heap-escape boundaries or explicit blocked rows when not yet
implemented.

Spec ambiguity:

None identified. The cited rules are syntax- and judgment-specific; an accepted
region/frame arithmetic reference cannot itself exercise closure, case, loop,
iterator, or escape judgments that are absent from the source.

Impact:

HUV can report a large portion of the region/provenance model as covered by a
single happy-path region reference. Regressions in closure escape enforcement,
loop break provenance joins, iterator-element provenance, case-branch
provenance, or heap-escape rejection would not be detected by the credited
target.

### UV-AUDIT-0320: QualifiedResolution credits optional-clause yes branches without clause syntax

Severity: High

Status: Locally verified HelloUltraviolet coverage gap with noted ambiguity boundary.

Spec anchors:

- `Docs\SPECIFICATION.md:5289`
- `Docs\SPECIFICATION.md:5290`
- `Docs\SPECIFICATION.md:5292`
- `Docs\SPECIFICATION.md:5295`
- `Docs\SPECIFICATION.md:5299`
- `Docs\SPECIFICATION.md:5301`
- `Docs\SPECIFICATION.md:5302`
- `Docs\SPECIFICATION.md:5304`
- `Docs\SPECIFICATION.md:5306`
- `Docs\SPECIFICATION.md:5307`
- `Docs\SPECIFICATION.md:5309`
- `Docs\SPECIFICATION.md:5311`
- `Docs\SPECIFICATION.md:5313`
- `Docs\SPECIFICATION.md:5316`
- `Docs\SPECIFICATION.md:5318`
- `Docs\SPECIFICATION.md:5319`
- `Docs\SPECIFICATION.md:5321`
- `Docs\SPECIFICATION.md:5323`
- `Docs\SPECIFICATION.md:5324`
- `Docs\SPECIFICATION.md:5326`
- `Docs\SPECIFICATION.md:5328`
- `Docs\SPECIFICATION.md:5331`

Implementation/test anchors:

- `HelloUltraviolet\Source\Reference\Names\QualifiedResolution.uv:6`
- `HelloUltraviolet\Source\Reference\Names\QualifiedResolution.uv:11`
- `HelloUltraviolet\Source\Reference\Names\QualifiedResolution.uv:23`
- `HelloUltraviolet\Source\Reference\Names\QualifiedResolution.uv:26`
- `HelloUltraviolet\Source\Reference\Names\QualifiedResolution.uv:128`
- `HelloUltraviolet\Source\Reference\Names\QualifiedResolution.uv:129`
- `HelloUltraviolet\Source\Reference\Names\QualifiedResolution.uv:199`
- `HelloUltraviolet\Source\Audit\Catalog\NameResolutionAndVisibility\SharedResolutionHelpersAndResolutionPass.uv:146`
- `HelloUltraviolet\Source\Audit\Catalog\NameResolutionAndVisibility\SharedResolutionHelpersAndResolutionPass.uv:155`
- `HelloUltraviolet\Source\Audit\Catalog\NameResolutionAndVisibility\SharedResolutionHelpersAndResolutionPass.uv:164`
- `HelloUltraviolet\Source\Audit\Catalog\NameResolutionAndVisibility\SharedResolutionHelpersAndResolutionPass.uv:173`
- `HelloUltraviolet\Source\Audit\Catalog\NameResolutionAndVisibility\SharedResolutionHelpersAndResolutionPass.uv:182`
- `HelloUltraviolet\Source\Audit\Catalog\NameResolutionAndVisibility\SharedResolutionHelpersAndResolutionPass.uv:191`
- `HelloUltraviolet\Source\Audit\Catalog\NameResolutionAndVisibility\SharedResolutionHelpersAndResolutionPass.uv:200`
- `HelloUltraviolet\Source\Audit\Catalog\NameResolutionAndVisibility\SharedResolutionHelpersAndResolutionPass.uv:209`
- `HelloUltraviolet\Source\Audit\Catalog\NameResolutionAndVisibility\SharedResolutionHelpersAndResolutionPass.uv:218`
- `HelloUltraviolet\Source\Audit\Catalog\NameResolutionAndVisibility\SharedResolutionHelpersAndResolutionPass.uv:227`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:993`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:994`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:995`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:996`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:997`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:998`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:999`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:1000`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:1001`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:1002`

Observed behavior:

`QualifiedResolution.uv` contains records, a class, type aliases, ordinary
procedures, qualified type paths, one generic type application
`GenericEnumReference<i32>`, enum cases, record patterns, and a local `using`
alias. It does not declare generic type parameters, predicate clauses, contract
clauses, or invariants; a direct search for `where`, `requires`, `ensures`,
`invariant`, `@entry`, `@result`, and `@old` in the file returns no matches.

The generated shared-resolution catalog nevertheless credits
`runNamesQualifiedResolutionReference` for `ResolveGenericParamsOpt-Yes`,
`ResolveTypeParam`, both type-parameter-list rules,
`ResolvePredicateClauseOpt-Yes`, the predicate-requirement list and predicate
rules, `ResolveContractClauseOpt-Yes`, and `ResolveInvariantOpt-Yes`.

Expected behavior:

Yes-branch optional-clause rows should be credited by sources that actually
contain the corresponding optional syntax. Generic-parameter resolution needs a
generic declaration with one or more type parameters and bounds/defaults where
applicable. Predicate, contract, and invariant rows need declarations carrying
those clauses so their contained type and expression resolution paths are
executed.

Spec ambiguity:

There is a limited coverage-accounting ambiguity for the `None` optional-clause
rows: a declaration that omits an optional clause may be acceptable evidence for
the absence branch. That ambiguity does not extend to the `Yes` rows listed
above, which are specified in terms of present parameters, predicates, contract
clauses, or invariant clauses.

Impact:

HUV can mark the shared name-resolution helper paths for generic declarations,
predicate clauses, contracts, and invariants as exercised by a qualified-name
reference that never contains those clauses. Regressions in clause-local name
resolution, type-parameter binding, predicate type resolution, contract
expression resolution, or invariant type/expression resolution would not be
detected by the credited target.

### UV-AUDIT-0321: Literal-value references credit lexer comment and token-stream obligations

Severity: High

Status: Agent-reported and locally verified HelloUltraviolet coverage gap.

Spec anchors:

- `Docs\SPECIFICATION.md:2052`
- `Docs\SPECIFICATION.md:2062`
- `Docs\SPECIFICATION.md:2066`
- `Docs\SPECIFICATION.md:2174`
- `Docs\SPECIFICATION.md:2179`
- `Docs\SPECIFICATION.md:2183`
- `Docs\SPECIFICATION.md:2191`
- `Docs\SPECIFICATION.md:2470`
- `Docs\SPECIFICATION.md:2605`
- `Docs\SPECIFICATION.md:2610`
- `Docs\SPECIFICATION.md:2615`

Implementation/test anchors:

- `HelloUltraviolet\Source\Reference\SourceText\Literals.uv:7`
- `HelloUltraviolet\Source\Reference\SourceText\Literals.uv:16`
- `HelloUltraviolet\Source\Reference\SourceText\Literals.uv:84`
- `HelloUltraviolet\Source\Reference\SourceText\Literals.uv:138`
- `HelloUltraviolet\Source\Reference\SourceText\Literals.uv:144`
- `HelloUltraviolet\Source\Reference\SourceText\Literals.uv:146`
- `HelloUltraviolet\Source\Reference\SourceText\Literals.uv:152`
- `HelloUltraviolet\Source\Audit\Catalog\SourceTextAndLexicalStructure\LexicalAnalysis.uv:11`
- `HelloUltraviolet\Source\Audit\Catalog\SourceTextAndLexicalStructure\LexicalAnalysis.uv:38`
- `HelloUltraviolet\Source\Audit\Catalog\SourceTextAndLexicalStructure\LexicalAnalysis.uv:236`
- `HelloUltraviolet\Source\Audit\Catalog\SourceTextAndLexicalStructure\LexicalAnalysis.uv:245`
- `HelloUltraviolet\Source\Audit\Catalog\SourceTextAndLexicalStructure\LexicalAnalysis.uv:281`
- `HelloUltraviolet\Source\Audit\Catalog\SourceTextAndLexicalStructure\LexicalAnalysis.uv:794`
- `HelloUltraviolet\Source\Audit\Catalog\SourceTextAndLexicalStructure\LexicalAnalysis.uv:893`
- `HelloUltraviolet\Source\Audit\Catalog\SourceTextAndLexicalStructure\LexicalAnalysis.uv:902`
- `HelloUltraviolet\Source\Audit\Catalog\SourceTextAndLexicalStructure\LexicalAnalysis.uv:911`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:335`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:338`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:360`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:361`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:365`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:422`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:433`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:434`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:435`

Observed behavior:

`runSourceTextLiteralsReference` validates accepted literal values, escape byte
contents, integer and float suffixes, null/unit literals, and tuple projection.
The source file contains only module/procedure documentation comments as
incidental source text, and the reference function does not inspect the token
stream, token spans, EOF token, lexeme binding, documentation-comment payloads,
ordinary line-comment skipping, nested block-comment scanning, or token
candidate selection.

The generated lexical-analysis catalog nevertheless credits the literal-value
reference for lexer input, lexeme binding, comment scan input, line-comment
scan, block-comment state, token candidates, and the line/doc/block comment
lexing transitions.

Expected behavior:

Lexer and comment obligations should be credited by tests that inspect lexical
outputs directly: token kinds, lexemes, spans, EOF placement, documentation
comment records, ordinary line comments, nested block comments, and candidate
selection. Literal-value execution can remain evidence for accepted literal
semantics, but it should not count as evidence for token-stream and comment
machinery it does not assert.

Spec ambiguity:

None identified for the credited rows above. The specification defines these as
lexer/token/comment judgments, so an accepted value-level literal reference
without token or comment assertions is not direct evidence for them.

Impact:

HUV can report lexer and comment coverage while only checking runtime literal
values. Regressions in comment skipping, documentation-comment capture, EOF span
placement, lexeme slicing, or token-candidate selection can remain undetected by
the credited target.

### UV-AUDIT-0322: Module aggregation credits file parsing obligations from an in-memory value reference

Severity: High

Status: Agent-reported and locally verified HelloUltraviolet coverage gap.

Spec anchors:

- `Docs\SPECIFICATION.md:8132`
- `Docs\SPECIFICATION.md:8135`
- `Docs\SPECIFICATION.md:8139`
- `Docs\SPECIFICATION.md:8141`
- `Docs\SPECIFICATION.md:8158`
- `Docs\SPECIFICATION.md:8163`
- `Docs\SPECIFICATION.md:8168`
- `Docs\SPECIFICATION.md:8173`
- `Docs\SPECIFICATION.md:8181`
- `Docs\SPECIFICATION.md:8187`
- `Docs\SPECIFICATION.md:8188`
- `Docs\SPECIFICATION.md:8228`
- `Docs\SPECIFICATION.md:8232`
- `Docs\SPECIFICATION.md:8237`

Implementation/test anchors:

- `HelloUltraviolet\Source\Reference\Modules\Aggregation.uv:9`
- `HelloUltraviolet\Source\Reference\Modules\Aggregation.uv:28`
- `HelloUltraviolet\Source\Reference\Modules\Aggregation.uv:34`
- `HelloUltraviolet\Source\Reference\Modules\Aggregation.uv:41`
- `HelloUltraviolet\Source\Reference\Modules\Aggregation.uv:43`
- `HelloUltraviolet\Source\Reference\Modules\Aggregation.uv:46`
- `HelloUltraviolet\Source\Reference\Modules\Aggregation.uv:47`
- `HelloUltraviolet\Source\Audit\Catalog\ModuleLevelForms\ModuleAndFileAggregation.uv:173`
- `HelloUltraviolet\Source\Audit\Catalog\ModuleLevelForms\ModuleAndFileAggregation.uv:209`
- `HelloUltraviolet\Source\Audit\Catalog\ModuleLevelForms\ModuleAndFileAggregation.uv:218`
- `HelloUltraviolet\Source\Audit\Catalog\ModuleLevelForms\ModuleAndFileAggregation.uv:245`
- `HelloUltraviolet\Source\Audit\Catalog\ModuleLevelForms\ModuleAndFileAggregation.uv:272`
- `HelloUltraviolet\Source\Audit\Catalog\ModuleLevelForms\ModuleAndFileAggregation.uv:317`
- `HelloUltraviolet\Source\Audit\Catalog\ModuleLevelForms\ModuleAndFileAggregation.uv:326`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:1506`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:1510`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:1511`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:1514`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:1517`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:1522`
- `HelloUltraviolet\Audit\ExerciseQualityManifest.csv:1523`

Observed behavior:

`runModulesAggregationReference` constructs a `ModuleAggregationReference`
record with constant counts, calls a same-module helper, and calls an imported
submodule helper. The reference validates ordinary resolved values after the
project has already compiled; it does not assert `ModuleMap`, file byte reads,
`LoadSource`, `ParseFile`, `ParseFileDiag`, `ParseModule`, or `ParseModules`
outputs.

The generated module-aggregation catalog nevertheless credits that target for
module-map construction, parse-module rule references and inputs,
`ParseModule-Ok`, parse-file diagnostics, parse-modules inputs, and
`ParseModules-Ok`.

Expected behavior:

Module/file aggregation rows should be credited by a project or artifact fixture
that observes the project file set and validates parsed module outputs, name
collection after parsing, and diagnostics across multiple source files. A
runtime reference that checks constructed values and imported functions should
only credit the narrow same-module and imported-submodule behavior it actually
asserts.

Spec ambiguity:

None identified. The cited rules explicitly sequence source roots, file reads,
source loading, parsing, diagnostics, and module-list aggregation; those effects
are not observable in the accepted reference function's assertions.

Impact:

HUV can mark project/file aggregation as covered without any oracle for source
file discovery, file reading, parse diagnostics, or module-list construction.
Regressions in module-map construction, parse-module short-circuit behavior, or
multi-file aggregation can remain hidden behind an in-memory value reference.

## Continuing Audit Queue

The next scan pass should prioritize:

- backend lowering, runtime checks, and poison/initialization behavior;
- contracts, invariants, permissions, and capability authority;
- async, comptime, derive, and quote/splice expansion;
- `HelloUltraviolet` executable-reference failures and selector coverage gaps;
- diagnostic first-failure ordering and fixture completeness.
