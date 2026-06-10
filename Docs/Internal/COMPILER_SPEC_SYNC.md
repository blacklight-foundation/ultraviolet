# Compiler ↔ Specification Sync Audit

Date: 2026-06-09. Basis: `Docs/SPECIFICATION.md` as updated this session vs `Bootstrap/Ultraviolet` at current HEAD. Every claim below was verified against the named source file.

## Status: EXECUTED 2026-06-09

All required updates below have been applied, with one explicitly scoped residual:

1. **Exhaustiveness** — DONE. `EnumPatternCoversVariant`/`ModalPatternCoversState` added (`pattern_common.cpp`, declared in `type_pattern.h`); `ArmVariants`/`ArmStates`/`UnionTypesExhaustive` rewritten over them (`if_case_check.cpp`, both synthesis and checking sites); `ifcase.unmatched` now stores a `MatchFail` panic record and returns instead of `CreateUnreachable()` (`if_case.cpp`).
2. **Panic codes** — DONE. Spec §24.5.2 adopts the implementation's `0x000B`–`0x0011` (ContractPre…LoopInv, AsyncFailed) and `MatchFail = 0x0012`; compiler adds `PanicReason::MatchFail` (`checks.h`, `checks.cpp`, `llvm_ir_panic.cpp`).
3. **PredicateEquiv** — DONE. Prover-implication fallback removed; structural equality only (`type_equiv.cpp`); `PredicatesImply` deleted.
4. **Diagnostic registry** — DONE. Generator parses §2.4 parenthetical bindings; both `.inc` files regenerated (756 map entries; all 248 spec ids present, zero conflicts); `W-SAFE-0100` → `W-SAF-0100` across source; `#test` wrong-target now emits `E-TST-0109` at both registry sites; `E-TST-0109` added to message table and obligation categorization. `--check` and `validate_diagnostic_spec_sync.py` pass.
5. **ConstLen-Comptime** — DONE. Pure, capability-free, emission-free `EvalExpr` bridge in the default `ConstLen` case (`const_len.cpp`), using post-Phase-2 `comptime_procedures` and a scratch diagnostic stream.
6. **E-CTE-0090** — DONE. `CtEnv.phase2_expanded_modules` added; Phase-2 cross-module visibility narrowed to Phase-1 module views (`pass.cpp`); `FindNamedDeclsInModule` detects resolution that would only succeed against another module's Phase-2 expansion and emits `E-CTE-0090` (`reflect.cpp`).
7. **Capability classes (B7, first layer)** — DONE for classification and satisfaction: open `IsCapabilityClass` (superclass-closure, cycle-guarded) in `context_caps.cpp`/`cap_system.h`; `DynamicCapabilityTypeImplementsClass` accepts user capability classes and `$Derived`-satisfies-ancestor via superclass reachability (`classes.cpp`). Reserved-name checks intentionally remain built-in-only.
8. **Variance placement** — VERIFIED conformant: the subtype query returns an advisory id with `subtype=false`; emission is caller-side. `SPEC_RULE` labels updated to `Chk-Generic-Invariant-Err`/`Chk-Generic-Variance-Err`.
9. **Union ordering** — VERIFIED conformant: `SortUnionMembers` applied before member-wise comparison in both `type_equiv.cpp` and `type_infer.cpp`.

**Residual (explicitly out of scope, by design):** generalizing the callgraph `CapabilityKind`/`CapabilitySet` representation so user-defined capability classes participate as first-class authority kinds (full `CapDerive` inference for user classes). Under NAA-4 this is an enforcement-*precision* enhancement, not a soundness gap: user capabilities confer authority only through encapsulated built-in capability values, which the existing structural containment analysis (`TypeContainsCapability`) and call gating already track. Verification of all edits: per-file `g++ -fsyntax-only` clean (LLVM 21 headers); full build/tests not run in this environment (Windows-configured build tree; Linux sandbox).

## HelloUltraviolet conformance suite — EXECUTED 2026-06-09

- **Stale oracles fixed:** `TestAttributeWrongTarget` now expects `E-TST-0109` (was `E-MOD-2452`) and `ValidTransmuteTarget` expects `W-SAF-0100` (was `W-SAFE-0100`) — updated in the fixture `Expected.uv` files and the hand-written audit catalogs (`FixtureCatalog/RejectedSource/Attributes.uv`, `ExpectedFiles.uv`, `FixtureCatalog/DiagnosticSource/Expressions.uv`, `ExpectedFiles.uv`, `AttributesMetadataArtifactExecution.uv`). `LayoutWrongTarget` correctly retains `E-MOD-2452`.
- **New negative fixture:** `Fixtures/RejectedSource/Patterns/IfCaseEnumRefutablePayloadCoverage` — refutable payload subpattern (`Value(1)`) plus unit arm without `else`, expecting `E-SEM-2741` under the tightened `CoversVariant` rule. Registered through the full chain: `Patterns.uv` (count 19→20 + specimen block), `ExpectedFiles.uv` (content-check procedure), `Api.uv` (using + `recordReferenceResult`). `GenerateHelloCatalog.py` regenerated; `--check` passes.
- **Breakage scan:** no checked-in fixture or reference source uses refutable-payload arms as exhaustiveness coverage (all literal-payload matches found are constructor calls or have `else`), so the tightened rule should not break existing specimens. Confirm by running the suite after rebuild.
- **Remaining fixture work (scoped, needs a rebuilt compiler to validate):** accepted-project fixtures for `ConstLen-Comptime` array lengths and user-defined capability classes (both require hand-registration in `AcceptedProjectExecution.uv`); a rejected fixture for `E-CTE-0090` cross-module Phase-2 emission dependency (needs a two-module comptime emission setup); a runtime `MatchFail` (0x0012) panic fixture in an executable-output category. These exercise new behavior that the pre-built binaries cannot validate (sandbox glibc too old to run the existing Linux `uvc`).

## Required updates

### 1. Exhaustiveness soundness fix (spec B1) — highest priority

The compiler has exactly the soundness hole the spec had, in both halves:

- `src/04_analysis/typing/if_case_check.cpp` — `ArmVariants` (~line 1114) counts a variant as covered by any `EnumPattern` naming it, ignoring payload-subpattern refutability; `ArmStates` likewise; `UnionTypesExhaustive` (~line 1445) treats a member as covered when `TypePatternAgainstType` succeeds (may-match). All three must adopt the spec's `CoversVariant` / `CoversState` / `CoversMember` (irrefutable payload subpatterns required for coverage; §17.6.4).
- `src/05_codegen/llvm/emit/ir/control/if_case.cpp` (~line 1627) — a no-else if-case with no fallthrough arm lowers the merge block to `CreateUnreachable()`. Combined with the unsound static check this is reachable UB today (e.g. `Some(1)` / `None` arms, scrutinee `Some(2)`). Per updated §17.5.6 and `EvalIfCases-None`, lowering MUST emit a trailing arm that panics with `MatchFail`.

### 2. MatchFail panic code — requires a small spec follow-up first

`include/05_codegen/checks/checks.h` already assigns `0x000B`–`0x0011` to `ContractPre`, `ContractPost`, `AsyncFailed`, `ForeignPre`, `ForeignPost`, `TypeInv`, `LoopInv` — reasons the spec's §24.5.2 `PanicReason`/`PanicCode` tables do not define (pre-existing spec gap). The spec change assigned `MatchFail = 0x000B`, colliding with `ContractPre`.

Proposed reconciliation (needs approval as a spec edit): adopt the implementation's `0x000B`–`0x0011` assignments into §24.5.2 and move `MatchFail` to `0x0012`; the compiler then adds `MatchFail = 0x0012` to `PanicReason` plus the corresponding `PanicCode`/`PanicReasonString` rows and uses it from item 1's trailing arm.

### 3. Refinement type equivalence (spec B4)

`src/04_analysis/typing/type_equiv.cpp` — `PredicateEquiv` (~line 134) tries structural equality, then falls back to prover-based mutual implication (`PredicatesImply` / `StaticProof`). The updated §8.1 mandates `PredNorm` structural equality only and states implementations MUST NOT accept broader equivalences. Remove the implication fallback from type equivalence (the prover remains correct for refinement *subsumption* checks elsewhere); confirm binder handling matches `PredNorm` subject substitution.

### 4. Diagnostic registry and code changes

- `tools/generate_diagnostic_registry.py` builds the id→code map by carrying forward existing entries (`read_existing_diag_map`) plus code→code identities; it does not read the spec's now-normative parenthetical bindings (§2.4). Extend it to parse backtick-quoted rule labels from the Condition column of spec tables. Diff results against `src/00_core/generated/diag_registry.inc`: **0 conflicts** with the 28 existing rule-id entries, **226 spec bindings missing**, and `generate_diagnostic_registry.py --check` already reports both generated files (`diag_registry.inc`, `typecheck_diag_map.inc`) stale.
- Code renames/additions to flow through source: `W-SAFE-0100` → `W-SAF-0100` (`src/00_core/diagnostics.cpp` lines ~366, ~523, plus any emit sites); the `#test`-outside-procedure case must emit `E-TST-0109` instead of `E-MOD-2452` (generic wrong-target keeps `E-MOD-2452`; see ternary at `src/02_source/attributes/attribute_registry.cpp:~1162` and the `attribute_targets.cpp` sites); new `E-CTE-0090` (item 6).
- Five impl-only DiagIds have no spec rule label: `If-Branch-Mismatch`, `IfCase-Branch-Mismatch`, `IfCase-NonExhaustive`, `Record-Method-Dup`, `Record-Method-RecvSelf-Err`. Either rename to the spec's rule labels (e.g. `IfCase-Enum-NonExhaustive`) or keep them registered in `UVDiagCodeMap` as implementation-internal aliases — pick one policy.

### 5. Comptime array lengths (spec B8, `ConstLen-Comptime`)

`src/04_analysis/typing/const_len.cpp` implements only `ConstLen-Lit`, `ConstLen-Path`, `ConstLen-Err`. The new `ConstLen-Comptime` rule admits any pure, capability-free, emission-free compile-time-evaluable expression. Wire the Phase-3 const-eval path into the comptime evaluator with those restrictions.

### 6. Cross-module Phase-2 dependency rejection (spec B8)

No occurrence of `E-CTE-0090` or a cross-module emission-dependency check in `src/03_comptime`. Implement `CtExpand-CrossModule-Emit-Err`: during Phase 2, resolving a name to a declaration *emitted* by Phase 2 of a different module is an error; Phase-1 declarations of other modules remain referenceable.

### 7. User-extendable capability classes (spec B7)

`src/04_analysis/caps/` is structured around the closed built-in set (`cap_io.cpp`, `cap_network.cpp`, …); there is no superclass-derived capability classification (`CapClass`), no `CapUp`/`CapDerive` (which replace `CapClosure`), and no NAA-4 admission of user capability construction. This is the one genuinely new feature: classes extending a capability class via `<:` become capability classes; `EffectiveCaps(T) = ⋃ CapUp(c) ∪ CapDerive(c)`; user capability values construct freely and confer authority only through encapsulated built-in capabilities.

### 8. Variance diagnostic placement (spec A15) — verify

`E-TYP-1520`/`E-TYP-1521` are categorized in `src/00_core/diagnostics.cpp` (~line 261). The spec moved emission from the subtyping relation (`Sub-Generic-*-Err`, deleted) to checking-mode use sites (`Chk-Generic-Invariant-Err`/`Chk-Generic-Variance-Err`, §14.2.4). Verify the emit sites fire only where the subtype relationship is *required*, not on every failed variance probe (e.g. inside overload or subsumption candidate testing).

### 9. Union unification ordering (spec B4) — verify

`src/04_analysis/typing/type_refs.cpp` documents `SortUnionMembers` (canonical union ordering), so the design is present. Verify the unifier (a) compares unions after canonical sort (permutation-insensitive) and (b) refuses to unify unions containing unsolved type variables, per `Unify-Union-Eq`/`Unify-Union-Fail`.

## Confirmed no-change areas

- **Signed `>>`**: `src/05_codegen/llvm/llvm_ub_safe.cpp` (~509) already emits `ashr` for signed / `lshr` for unsigned — the spec was corrected *to match the implementation* (B3). Shift checks (`ShiftCheck`) present.
- **Concurrency surface syntax**: the parser already accepts every option the authored rules define — `cancel`/`name`/`workgroup`/`workgroups` (`parallel_expr.cpp`), `affinity`/`priority` (`spawn_expr.cpp`), `ordered`/`chunk`/`workgroup`/`reduce` (`dispatch_expr.cpp`), and `#` key markers (`key_block_stmt.cpp:51`). The B2 rules were authored to the chapter grammar the parser follows; Appendix B was the stale artifact.
- **Panic-check framework**: DivZero/Overflow/Shift/Bounds/Cast/NullDeref/ExpiredDeref check lowering present (`checks.cpp`, `llvm_ub_safe.cpp`), matching §24.5.2 and the extended §1.2 taxonomy.
- **Spec-structural changes** (rule de-duplication, Appendix A/B repairs, §8.4 step relation, B5 memory model axiomatization): no direct code impact; B5 constrains future optimization passes rather than current emission.

## Suggested order

1 → 2 (spec follow-up + code) → 4 (tooling + regenerate) → 3 → 5 → 6 → 9 → 8 → 7 (largest, feature-level).
