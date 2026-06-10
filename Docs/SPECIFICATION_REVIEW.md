# SPECIFICATION.md Review — Correctness, Completeness, and Formalism Audit

Reviewed: `Docs/SPECIFICATION.md` (31,100 lines), 2026-06-09.
Method: full structural/mechanical analysis of the entire document (cross-references, diagnostic codes, rule labels, grammar definition/use/reachability), plus close reading of Chapters 0–2, 6, 8, 16–17, 19–21, 24 and targeted reads elsewhere. All line numbers below refer to the current file.

Overall: the document is unusually rigorous for a draft — every `§N.N` cross-reference resolves, the phase/conformance spine (§1.1, §1.5) is fully wired to defined judgments, the template discipline of §0.3 is followed nearly everywhere, and the diagnostics infrastructure, layout chapter, and comptime expansion machinery are coherent. The defects below are concentrated in (a) one genuine type-soundness hole, (b) infrastructure that is declared but never instantiated, (c) the appendix grammar, which does not compose, and (d) drift between duplicated statements of the same rules.

---

## 1. Critical

### 1.1 Exhaustiveness checking is unsound; Preservation is violated

The chain, each link verified:

1. `Pat-Literal-R` (line 18318) admits literal patterns in any pattern position, including enum payload positions (`Pat-Enum-Tuple-R`, line 18662, recurses with `◁` on payload subpatterns).
2. `CaseVariants` (line 19046) counts a variant as covered by *any* `EnumPattern` naming it, regardless of payload-pattern refutability. A case `Some(1)` counts as full coverage of `Some`.
3. `T-IfCase-Enum` (line 19059) accepts `IfCaseExpr` with no `else` when `CaseVariants(cases) = VariantNames(E)`, typing the whole expression as the common body type `T_r`.
4. `EvalIfCases-None` (line 18958) evaluates a fully fallen-through case list to `(Val(()), σ)`.

Consequence: an expression statically typed `T_r` (say `i32`) evaluates to `()` at runtime when the scrutinee is `Some(2)`. This falsifies the Preservation property claimed in §8.4 (line 6488) and, in a systems language with layout-bearing types, is a memory-safety-class defect, not a style issue.

The same hole exists for unions: `UnionTypesExhaustive` (line 19053) uses `PatternMayMatchType` (line 19052), which holds whenever the pattern *type-checks* against the member (`∃ B. Γ ⊢ p ◁ T ⊣ B`). A literal pattern `5` "may match" `i32` and thus counts as covering the entire `i32` member.

Fix options (pick one and state it normatively): require payload subpatterns in coverage-counted cases to be irrefutable (extend `Irrefutable`, line 19035, with enum/modal payload clauses and use it in `CaseVariants`/`UnionTypesExhaustive`); or implement a proper usefulness algorithm (Maranget-style) for the nested-pattern space; and independently change `EvalIfCases-None` to `Ctrl(Panic)` with a `MatchFail` panic reason so any residual static gap fails loudly instead of materializing a unit value of the wrong type.

### 1.2 The diagnostic-code binding mechanism is declared but never instantiated

§2.4 (lines 478–487) defines `SpecCode : DiagId ⇀ DiagCode` as "the owning construct section of this specification assigns diagnostic code `c` to identifier `id`," and the `(Code)` rule plus 249 distinct `c = Code(id)` premises throughout the document depend on it. But no section anywhere assigns a code to a rule identifier: the diagnostic tables are keyed by code and prose condition only (e.g., §12.10, line 10750), and the association from `Index-NonIndexable` (line 9486) to `E-SEM-2527` (line 18212) exists only by reading condition prose. Formally, `SpecCode(id) = ⊥` for every id, every `Code(id)` premise is unsatisfiable, and per §1.2's Undefinedness Policy the entire error-reporting layer collapses into `Static-Undefined`.

Compounding this, at least 20 rules pass diagnostic *codes* directly where an *id* is expected — e.g., lines 3907, 4331, 6177 (`c = Code(E-TYP-1520)`) — contradicting the declared signature.

Fix: add a `Rule` (DiagId) column to every normative diagnostic table, defining `SpecCode` explicitly; normalize all `Code(...)` arguments to ids. This is mechanical but large; until done, the spec's own conformance machinery cannot be implemented as written.

### 1.3 Appendix B ("Complete Grammar Reference") does not compose into a grammar

A reachability analysis from `source_file`/`top_level_item` over all 339 appendix productions leaves **121 productions unreachable**. This is not noise; the canonical grammar cannot derive large parts of the language:

- **The type grammar is severed at its root.** `non_permission_type ::= union_type | non_union_type` and `union_type ::= non_union_type (...)` — but `non_union_type` is defined nowhere in the document. Every concrete type form (`primitive_type`, `tuple_type`, `array_type`, `slice_type`, `function_type`, pointer types, `string_type`, `nominal_type`, `refinement_type`, …) is therefore unreachable. The chapter grammar has the same hole under a different name: §12.8.1 (line 10432) uses `non_perm_type`, also never defined.
- **Expression forms are not wired into `expression`.** `primary_expr` (B.3) lists only literal/identifier/path/tuple/array/record/closure/if/loop/block/comptime/quote forms. Defined-but-orphaned: `move_expr`, `copy_expr`, `widen_expr`, `try_expr`, `address_of_expr`, `null_ptr_expr`, `transmute_expr`, `alloc_expr`, `enum_literal`, `method_call`, `static_call`, `call_expr` (calls are reachable only via `postfix_suffix`; the three standalone call productions are dead duplicates).
- **All concurrency/async forms are unreachable.** `sync_expr`, `race_expr`, `all_expr`, `wait_expr`, `yield_expr`, `yield_from_expr`, `spawn_expr`, `parallel_block`, `dispatch_expr`, `key_block`, `speculative_block`, `fence_expr` (the last absent from Appendix B entirely) are referenced by no expression or statement production. B.5 `statement` references `key_block_stmt`, which the appendix never defines (B.9 defines `key_block` instead; the chapter defines `key_block_stmt` at line 20607).
- Other dangling references inside the appendix: `binding_decl` (used by `static_decl`; defined only at line 7704), `predicate_clause` (defined only at line 12551), `invariant_clause` and `key_boundary` (defined **nowhere** in the document; see §2.1 below), `state_method_signature` (chapter-only, twice — see §2.2), plus naming drift `expr`/`expression` (B.2 `array_type`), `int_type`/`integer_type` (B.8 `layout_kind`), `decimal_literal`/`decimal_integer` (B.3 `postfix_suffix`).

Fix: define `non_union_type` (and reconcile with `non_perm_type`); add the missing alternatives to `primary_expr`/`unary_expr`/`statement`; define or import `key_block_stmt`, `binding_decl`, `predicate_clause`, `invariant_clause`, `key_boundary`, `state_method_signature`; delete or fold the dead duplicate call/try productions; add a CI check that every appendix production is reachable and every referenced nonterminal is defined.

### 1.4 Roughly 60 parser rules exist only as names ("phantom rules")

§19.1.2, §19.2.2 (line 20622), §20.1.2 (line 21615), §20.4.2 (line 22247), and §20.5.2 (line 22355) state that parsing "is defined by the following source rules" and then enumerate rule names — `Parse-KeyBlockHead-*`, `Parse-KeyPath*`, `Parse-KeyOption*`, `Parse-Parallel*`, `Parse-Spawn*`, `Parse-Dispatch*`, `Parse-ReduceOp-*`, etc. None of these rules is stated anywhere in the document. The Key System and Structured Parallelism surface syntax consequently has no normative parsing semantics. The phrase "source rules" suggests these bodies lived in a pre-reorganization draft and were dropped during migration.

Also referenced but never defined: `EvalSigma-Index-Ctrl-Base`, `EvalSigma-Index-Ctrl-Idx`, `EvalSigma-FieldAccess-Ctrl`, `EvalSigma-TupleAccess-Ctrl` (control propagation for access expressions, cited at line 16124), `EvalSigma-Ident-Poison(-RecordCtor)`, `EvalSigma-Path-Poison(-RecordCtor)`, `Lower-BinOp-Ok`, `Lower-BinOp-Panic`, `Lower-Cast`, `Lower-Cast-Panic`, `Lower-Transmute`, `Lower-Transmute-Err`, and `Chk-Block-Return`/`-Tail`/`-Unit`.

### 1.5 `Unify-Ctor-Mismatch` uses the wrong `TypeCtor` and makes the solver nondeterministic

`Unify-Ctor-Mismatch` (line 6395) fails unification when `TypeCtor(T) ≠ TypeCtor(U)`. The only `TypeCtor` defined in the document (§1.1, lines 145–172) returns the *deep set* of constructor names — `TypeCtor(TypeTuple([i32])) = {tuple, i32}` but `TypeCtor(TypeTuple([TVar α])) = {tuple}`. Since neither side is a `TVar`, the rule fires and the solver can step to `UnifyFail` even though `Unify-Tuple` (line 6257) simultaneously permits a correct decomposition step. The transition system thus has races to contradictory outcomes for ordinary unification problems. Define a head-constructor function for Chapter 8 (or restate the premise as "head constructors differ") and state a rule-priority/confluence discipline for the solver.

---

## 2. Major

### 2.1 Direct contradiction on invariant syntax; two grammar nonterminals undefined everywhere

§15.7.1 (lines 15272–15274) defines `type_invariant`/`loop_invariant` introduced by `"|:" "{"`. Appendix B.7 (line 30871) defines `type_invariant` with `"where" "{"`. One of these is stale. Meanwhile the declaration grammars (lines 9825, 10090, 10769 and appendix B.6) hang an optional `invariant_clause` off records/enums/modals — a nonterminal never defined and never formally connected to `type_invariant`. `key_boundary` (used at lines 9827, 11203, 12912, 30846) is likewise never defined; from §19.2.5 prose (line 20771) it is evidently the `#` field marker, but the production must exist.

### 2.2 Duplicated rule statements with real drift (the spec violates its own §0.1 rule 2)

116 rule labels are defined more than once (106 verbatim copies, e.g., `EvalSigma-Call-Proc` stated identically at lines 12331, 14548, and 16352; `T-Equiv-Refine`/`PredicateEquiv` at 6032/6037 and again at 13728/13732). §0.1 rule 2 mandates exactly one normative home. Verbatim duplication is already a drift hazard, and drift has in fact occurred:

- **§7.5 `Bind-Record`/`Bind-Enum`/`Bind-TypeAlias`** (lines 5034–5046) pattern-match 7-field tuples `⟨RecordDecl, _, name, _, _, _, _⟩`, while the canonical constructors carry 10 fields (§1.1 `ItemKind`, lines 135–139; §12.6.4, line 9911). The §7.5 statements cannot match any well-formed AST node.
- **§10.1.6 `Layout-Perm`/`SizeOf-Perm`/`AlignOf-Perm`** (lines 7295–7302) are stated as bare conclusions with unbound right-hand metavariables (`L`, `n`, `a`) and no premises — vacuous as written. The correct premised versions exist in §24.2.2 (line 27646), under partially different names (`Size-Perm`/`Align-Perm`), which also breaks label-based cross-referencing.
- **`state_method_signature`** has two conflicting EBNF definitions: line 11267 (`receiver method_param_list? ... return_opt`) vs line 14573 (`receiver ("," param_list)? ... ("->" type)?`).
- **`RaceStepStream-Yield`** is stated twice within the same subsection (§21.3.5, lines 24048 and 24102) — an apparent paste duplication.

Recommendation: delete restatements in favor of cross-references, and add tooling that rejects duplicate rule labels.

### 2.3 `E-MOD-2452` is owned by two sections with different meanings

Defined in §9.1.7 (line 6769) as "Attribute not valid on target declaration kind" and in §9.6.7 (line 7214) as "`#test` applied outside an ordinary procedure". §2.3 defines `SeverityColumn`/`ConditionColumn` as functions of the code; dual ownership with different condition text makes them ill-defined. Give the `#test` case its own code (the `E-TST-01xx` block is the natural home).

### 2.4 §19.3.4 conflict rules step outside the document's own judgment discipline

`K-Dynamic-Index-Conflict` (line 21008), `K-Read-Write-Reject` (line 21025), and `K-RMW-Permitted` (line 21030) conclude bare `Reject`/`Permitted` — words that belong to no declared judgment set in §1.2's `StaticJudgSet`, carry no `Γ ⊢`, and name no diagnostic id (the mapping to `E-CON-0010`/`E-CON-0060` is implicit). The `IllFormed`/`Static-Undefined` machinery therefore does not apply to exactly the rules where it matters most. Also cosmetic but tooling-hostile: `K-RMW-Explicit-Warn`/`K-RMW-Contention-Warn` (lines 21034, 21039) use ASCII dashes for the inference bar while the rest of the document uses box-drawing characters.

Additionally, index-equivalence clause 4 (line 20972, "both are references to the same variable binding in scope") needs a no-intervening-mutation (or `let`-only) side condition to be sound for `var` bindings.

### 2.5 No formal memory model behind §19.7

The ordering table (§19.7.4) gives one-line glosses ("subsequent reads see prior writes") rather than a happens-before/synchronizes-with axiomatization; there is no coherence statement, no definition of a data race for sub-`seqcst` accesses, and no out-of-thin-air exclusion. More fundamentally, the design tension is unresolved: §8.4 claims data-race freedom via key serialization, yet §19.7 permits `relaxed` orderings on keyed/shared accesses — if keys already serialize all shared access, weaker orderings should be semantically inert (and then why do they exist?); if attributes weaken key-implied ordering, the resulting executions need a model. For a systems language this chapter needs either a real axiomatic model (C++11-style, as Rust adopted) or an explicit normative statement that orderings below `seqcst` are advisory-only with key-serialization semantics preserved.

`Lower-Ordered-Access` (line 21588) also overlaps every other `LowerExpr` rule for any expression containing a shared access, with no priority discipline stated.

### 2.6 Signed `>>` is specified as a logical shift

`ShiftOp` (line 16538) converts to unsigned, floor-divides, and converts back: `-1i32 >> 1` yields `i32::MAX`, not `-1`. Every mainstream systems language defines arithmetic shift-right for signed operands. If intentional, it needs a loud normative note plus rationale (it will astonish every reader and miscompile every ported algorithm); more likely the `ToUnsigned` round-trip is a drafting error. Also note the RHS is required to be exactly `u32` (`v_2 = IntVal("u32", n)`) — a deliberate-looking but undocumented asymmetry worth one sentence of rationale.

### 2.7 The §1.2 check taxonomy is incomplete relative to the panic framework

`CheckKind`/`RuntimeCheck` (lines 278–281) enumerate `{IntegerOverflow, SliceBounds, IntDivisionByZero}` as runtime checks, but §24.5.2 defines `PanicReason`/`PanicSite` (lines 28793, 28807) including `Shift`, `Cast`, `NullDeref`, `ExpiredDeref` checks with lowering obligations (`CheckedShifts`, `CheckedDivRem`, line 29506). Either extend §1.2's sets or state that §1.2 lists only the user-facing taxonomy and defer to §24.5.2 as canonical.

### 2.8 Undecidable type equivalence for refinements; ≡ vs. unification asymmetry

`PredicateEquiv` (lines 6037, 13728) defines refinement-type equivalence as semantic equivalence over all stores — undecidable, hence `Γ ⊢ T ≡ U` is undecidable as specified. Meanwhile `Unify-Refine-Pred-Fail` (line 6373) demands *syntactic* predicate equality, and `T-Equiv-Union` (line 5952) is permutation-closed while `Unify-Rigid-Fail` (line 6383) fails any syntactically unequal union pair. Decide a decidable canonical story (syntactic-after-normalization, or an SMT-decidable fragment with a named decision procedure) and make ≡ and the solver agree on it.

### 2.9 §8.4 metatheory is stated over an undefined relation

Progress and Preservation (lines 6482–6489) quantify over a small-step relation `e → e'` that the specification never defines — all dynamic semantics are big-step (`EvalSigma ⇓`). Either restate the properties big-step (type soundness via `⇓`-indexed preservation plus a coinductive/fuel-based progress analogue) or mark §8.4 informative. (Per finding 1.1, Preservation is currently also false.)

### 2.10 Capability classification inconsistency

§6.1.1 `CapInType` (lines 3164–3177) assigns capabilities only to `TypeDynamic([...])` forms plus `TypePath([Context])`, while §1.1 `CapConstructs` (line 229) also classifies bare `TypePath([IO])` etc. as capability constructs. If capability classes are only ever used via `$Class` dynamic types and `Context`, fix §1.1; if nominal references are possible, fix §6.1.1.

---

## 3. Minor and editorial

1. **Find/replace corruption:** "reultraviolet" at lines 6951 and 10748 — both should read "recursive" (casualty of a `Cursive` → `Ultraviolet` rename).
2. **Migration leftovers in normative text:** HTML comments embedding source-draft text at lines 3179, 4749, 11636; "The old draft defines NaN-sensitive comparison behavior…" at line 16596 (the behavior itself should be stated normatively, the citation removed); "in this reorganization" at line 379.
3. **Appendix A drift (index is informative but should still be accurate):** used in body, absent from index: `E-MOD-1304`, `E-MOD-1308`–`1310`, `E-SEM-2525`, `E-SEM-2526`, `E-SEM-3030`–`3032`, `E-SYS-3360`, `E-TYP-2001`, `E-TYP-2002`, `E-TYP-2007`–`2009`. Indexed but nonexistent in body: `E-CNF-0403`–`0405`, `E-MOD-1303`, `E-MOD-1306`, `E-TYP-1821`.
4. **§0.2 outline vs. actual headers:** outline says "Appendix A. Diagnostic Index Reference" / "Appendix C. AST Form Index Reference"; actual headers omit "Reference".
5. **`attr_open`/`attr_close`** used in six chapter productions (lines 6836, 6933, 6936, 7097, 21506, 25589) but never defined; B.8 spells the same constructs with a literal `"#"`. Define them once or normalize.
6. **B.10 option grammars are stale vs. chapters:** `dispatch_option` lacks `ordered`/`chunk`/`workgroup` (cf. line 22373), `spawn_option` lacks `affinity`/`priority` (cf. lines 22260–22261), `block_option` lacks `name`/`workgroup(s)` (cf. lines 21629–21630). Whichever side is canonical, reconcile.
7. **`ConstLen`** (lines 5900–5913) admits only integer literals and references to literal-initialized statics. No arithmetic (`[T; 2*N]`), no comptime-evaluated lengths — surprising given Chapter 22's machinery, and `E-TYP-1810`'s wording ("not a compile-time constant") implies more generality than the rules deliver. State the restriction explicitly or extend `ConstLen` to comptime evaluation.
8. **§1.1 `Phase3Order`** (line 124) is defined as a bare proposition, unlike phases 1/2/4 which are `Γ ⊢ … ⇓ ok` judgments; and `FirstFail` consumes `ResolveModules(P_ct) ⇓ Ms_res` — an output-producing judgment — inside a pass/fail check list (line 118). Harmless in intent, type-mismatched in form.
9. **Sub-Generic-*-Err** (lines 6176–6189) attach diagnostics to the subtyping *relation*: any failed variance query "emits," including benign failures inside checks that legitimately probe subtyping (e.g., subsumption attempts, union membership). Error emission should live at use sites that *require* the subtype relationship.
10. **Cross-module comptime dependencies:** §1.5 promises "deterministic dependency order"; §22.1.5 (`ComptimePass`, line 24967) executes modules in fixed `Phase2ModuleOrder` sequence. If a comptime form in an earlier module needs declarations emitted by a later module, behavior is unspecified. State that cross-module comptime dependencies are unsupported (and diagnose them), or define the dependency ordering.
11. **`EvalIfCases-None` silently yields `()`** even in statement contexts where the static rules are sound; for a language whose design contract is "explicit over implicit," silent no-match fall-through deserves reconsideration independent of finding 1.1.
12. **Rendering nit:** `Underline` (line 539) assumes single-line spans; no rule handles `start_line ≠ end_line` in `RenderRich`.

---

## 4. Systemic recommendations

The two failure patterns behind most findings are (1) duplicated normative statements drifting apart, and (2) infrastructure declared in Chapters 1–2 that downstream chapters never actually instantiate. Both are mechanically preventable:

- Build a spec linter over the existing rule syntax: unique rule labels; every backticked rule reference resolves to a defined rule; every `Code(id)` argument resolves to a labeled rule and a table row; every EBNF nonterminal defined exactly once and reachable from `source_file`; every diagnostic code owned by exactly one table; Appendix A regenerated, not hand-maintained.
- Replace all verbatim rule restatements with cross-references to the single normative home (§0.1 rule 2 already mandates this; it needs enforcement).
- Restore the dropped parser-rule bodies for Chapters 19–20 from the pre-reorganization draft, or rewrite them; until then those chapters' Syntax/Parsing sections are names without semantics.
- Add a `Rule` column to every diagnostics table to instantiate `SpecCode`.

Severity-ordered priority: 1.1 (soundness) → 1.3/1.4 (grammar and parsing holes) → 1.2 (diagnostic binding) → 1.5/2.2 (solver and drifted rules) → the §2 consistency items → §3 cleanup.
