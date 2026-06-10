# SPECIFICATION.md Change Proposal

Status: **APPLIED 2026-06-09** (all parts; A20 fully applied: §2.4 binding convention is normative; all 249 distinct Code(id) DiagIds are bound to their owning table rows (23 direct-code premises converted; 236 parenthetical row bindings)). Open-point rulings: A4 = no parenthesized types (default, unanswered); A5 = delete; B2.1 = marked indices allowed (canonical §19.1.1 grammar adopted in B.9); B2.3 = shared schema; A15 = codes moved to §14.11.
Basis: `Docs/SPECIFICATION_REVIEW.md` findings plus rulings given 2026-06-09: B1 = irrefutable-payload coverage; B2 = author missing rules; B3 = arithmetic signed `>>`; B4 = syntactic predicate equivalence + canonical unions; B5 = happens-before axiomatization; B6 = small-step relation; B7 = user-extendable capability classes; B8 = comptime `ConstLen`, cross-module comptime rejected with error.

Conventions: line numbers refer to the current `SPECIFICATION.md`. Each change states location, action, and exact replacement/insertion text. Rule text follows house style (`**(Rule-Name)**`, premises, bar, conclusion). Items marked ⚙ are deterministic bulk transformations described precisely rather than spelled out per-site.

---

## Part 1 — Group A (mechanical/consistency changes)

- **A1.** Lines 30870–30871: in B.7, replace `"where"` with `"|:"` in both `type_invariant` and `loop_invariant` (canonical introducer per ruling; `where` removed from the language).
- **A2.** Replace nonterminal `invariant_clause` with `type_invariant` at lines 9825, 10090, 10769, 30817, 30826, 30833.
- **A3.** Insert `key_boundary ::= "#"` into the §12.6.1 EBNF block (after `record_field`) and into Appendix B.6 (before `abstract_field`).
- **A4.** Appendix B.2: insert after `non_permission_type`:
  `non_union_type ::= primitive_type | tuple_type | array_type | slice_type | function_type | closure_type | safe_pointer_type | raw_pointer_type | string_type | bytes_type | dynamic_type | opaque_type | nominal_type | modal_state_type | "(" type ")"`
  and in §12.8.1 (line 10432) rename `non_perm_type` → `non_union_type`. (If parenthesized types are not intended, drop the last alternative — confirm.)
- **A5.** Appendix B.9: rename `key_block` → `key_block_stmt`; delete `speculative_block` (subsumed by `key_block_head`); resolve `coarsened_path` (orphaned and inconsistent with the prefix-marker form — propose deletion; confirm).
- **A6.** Appendix B.3 `primary_expr`: append alternatives `| enum_literal | move_expr | copy_expr | widen_expr | address_of_expr | null_ptr_expr | transmute_expr | alloc_expr | sync_expr | race_expr | all_expr | wait_expr | yield_expr | yield_from_expr | spawn_expr | parallel_block | dispatch_expr | fence_expr`. Delete dead duplicates `call_expr`, `method_call`, `static_call`, `try_expr` (all expressed by `postfix_suffix`).
- **A7.** Copy `binding_decl` (line 7704) and `predicate_clause` (line 12551) definitions into Appendix B.6/B.7; add `fence_expr ::= "fence" "(" fence_order ")"` and `fence_order ::= "acquire" | "release" | "seqcst"` to Appendix B.9.
- **A8.** Appendix naming: `expr` → `expression` in B.2 `array_type`; `int_type` → `integer_type` in B.8 `layout_kind`; `decimal_literal` → `decimal_integer` in B.3 `postfix_suffix`.
- **A9.** Lines 6836, 6933, 6936, 7097, 21506, 25589: replace `attr_open` with `"#"` and delete `attr_close`.
- **A10.** §7.5 (5034–5046): restate `Bind-Record`/`Bind-Enum`/`Bind-TypeAlias` (and the sibling `Bind-*` rules using tagged-tuple shorthand) over the canonical constructors, e.g. conclusion `Γ ⊢ ItemBindings(RecordDecl(_, _, name, _, _, _, _, _, _, _), p) ⇓ [(name, ⟨Type, p, ⊥, Decl⟩)]`; then delete the duplicate statements at 9911 (§12.6.4), 10185 (§12.7.4), 10649 (§12.9.4). §7.5 is the single home (shared framework, §0.1 rule 4).
- **A11.** §10.1.6 (7295–7302): delete the three premise-less rules; replace the body with: "Permission qualifiers do not alter value layout. `layout`, `sizeof`, and `alignof` for `TypePerm(p, T)` are defined by `Layout-Perm`, `Size-Perm`, and `Align-Perm` in §24.2.2."
- **A12.** ⚙ De-duplicate the 106 verbatim rule copies: for each duplicated label, keep the statement in the owning chapter (evaluation rules with the syntactic owner; equivalence rules in §8; binding rules in §7.5) and replace others with a cross-reference sentence. Includes `EvalSigma-Call-Proc` (keep 16352; drop 12331, 14548), `T-Equiv-Refine`/`PredicateEquiv` (keep §8.1; drop 13728/13732), second `RaceStepStream-Yield` (drop 24102). Full label list available from the review tooling; applied label-by-label.
- **A13.** §8.3: insert before `Unify-Ctor-Mismatch`:
  `HeadCtor(T)` = the outermost constructor name of `T` (`TypePrim(n) ↦ n`, `TypeTuple(_) ↦ tuple`, `TypeArray(_,_) ↦ array`, `TypeSlice(_) ↦ slice`, `TypeUnion(_) ↦ union`, `TypeFunc(_,_) ↦ function`, `TypeClosure(_,_,_) ↦ closure`, `TypePtr(_,_) ↦ ptr`, `TypeRawPtr(_,_) ↦ rawptr`, `TypePerm(_,_) ↦ perm`, `TypeApply(p,_) ↦ ⟨apply, p⟩`, `TypePath(p) ↦ ⟨path, p⟩`, each remaining constructor to a distinct symbol).
  Rewrite the `Unify-Ctor-Mismatch` premise to `HeadCtor(T) ≠ HeadCtor(U)    T ∉ TVar    U ∉ TVar`. Add: "A decomposition rule and a failure rule never apply to the same pair; failure rules apply only when no decomposition or variable rule applies."
- **A14.** §19.3.4: recast `K-Dynamic-Index-Conflict` (21008) as `Γ ⊢ KeyBlockStmt(...) ⇑ c` with premise `c = Code(K-Dynamic-Index-Conflict)`; recast `K-Read-Write-Reject` (21025) as `Γ ⊢ S ⇑ c` with `c = Code(K-Read-Write-Reject)`; recast `K-RMW-Permitted` as judgment `Γ ⊢ RMWOk(P, S)`; register the new judgment names in §1.2 `StaticJudgSet`; replace ASCII bars at 21034/21039 with box-drawing bars. Side condition added to index-equivalence clause 4 (20972): "…and the binding is immutable (`let`) or has no intervening mutation between the compared references."
- **A15.** §8.2 (6176–6189): delete `Sub-Generic-Invariant-Err`, `Sub-Generic-Covariant-Err`, `Sub-Generic-Contravariant-Err`; add to §14.2.4 a use-site rule emitting `E-TYP-1520`/`E-TYP-1521` where a required subtype/instantiation check fails. (Codes stay owned by §8.5 table or move to §14.11 — propose moving rows to §14.11; confirm.)
- **A16.** §9.6.7 (7214): change `E-MOD-2452` row to `E-TST-0109` (next free); update the one body mention and Appendix A.
- **A17.** ⚙ Regenerate Appendix A from body tables: add `E-MOD-1304/1308/1309/1310`, `E-SEM-2525/2526/3030/3031/3032`, `E-SYS-3360`, `E-TYP-2001/2002/2007/2008/2009` under their owning sections; remove stale `E-CNF-0403/0404/0405`, `E-MOD-1303`, `E-MOD-1306`, `E-TYP-1821`.
- **A18.** Editorial: "reultraviolet" → "recursive" (6951, 10748); delete HTML comments (3179, 4749, 11636 — 11636's storage-duration content is promoted into §13.6.5 prose if not already stated); line 16596 → "Ordered float comparisons with a NaN operand yield `false`; `==` yields `false`; `!=` yields `true`." (delete the old-draft citation); reword 379 to drop "in this reorganization"; align §0.2 outline appendix names with actual headers.
- **A19.** §1.1/§1.2: restate `Phase3Order` (124) as `Γ ⊢ Phase3Order(P) ⇓ ok ⇔ …`; define `FirstFail` to treat any `⇓ _` outcome as pass (1015–1017 already do; add the value-producing case explicitly). Extend `CheckKind` with `ShiftRange`, `CastRange`; `RuntimeCheck` with the same; add `RuntimeBehavior(ShiftRange) = Panic`, `RuntimeBehavior(CastRange) = Panic`; add a sentence deferring the full panic taxonomy to §24.5.2.
- **A20.** ⚙ Add a `Rule` (DiagId) column to every normative diagnostics table, naming the owning rule(s) per code — this instantiates `SpecCode` (§2.4). Convert direct-code usages (3907, 3912, 3917, 4331, 6177, 6182, 6187, 10886, and remaining ~12 sites) to rule ids.

---

## Part 2 — B1: Exhaustiveness soundness fix (Option 1) + match-failure panic

### B1.1 New coverage predicates — insert in §17.6.4 after `Irrefutable` (line 19035)

```text
CoversVariant(EnumPattern(_, v, ⊥), E, v) ⇔ VariantPayload(E, v) = ⊥
CoversVariant(EnumPattern(_, v, TuplePayloadPattern([p_1, …, p_n])), E, v) ⇔
  VariantPayload(E, v) = TuplePayload([T_1, …, T_n]) ∧ ∀ i. Irrefutable(p_i, T_i)
CoversVariant(EnumPattern(_, v, RecordPayloadPattern(io)), E, v) ⇔
  VariantPayload(E, v) = RecordPayload(_) ∧ ∀ fp ∈ io. Irrefutable(PatOf(fp), EnumFieldType(E, v, FieldName(fp)))
CoversVariant(p, E, v) does not hold otherwise.

CoveredVariants(cases, E) = { v | ∃ p, b. ⟨p, b⟩ ∈ cases ∧ CoversVariant(p, E, v) }

CoversState(ModalPattern(S, io), modal_ref, S) ⇔
  ∀ fp ∈ io. Irrefutable(PatOf(fp), ModalPayloadMap(modal_ref, S)(FieldName(fp)))
CoveredStates(cases, modal_ref) = { S | ∃ p, b. ⟨p, b⟩ ∈ cases ∧ CoversState(p, modal_ref, S) }

CoversMember(p, T) ⇔ Irrefutable(p, T)
UnionTypesExhaustive(cases, types) ⇔ ∀ T ∈ types. ∃ case ∈ cases. ∃ p, b. case = ⟨p, b⟩ ∧ CoversMember(p, T)
```

A case pattern whose payload subpatterns are refutable matches only part of its variant, state, or member and therefore MUST NOT count toward exhaustiveness.

### B1.2 Replacements

- Line 19046: delete `CaseVariants` (superseded by `CoveredVariants`).
- Line 19048: delete `CaseStates` (superseded by `CoveredStates`).
- Lines 19052–19053: replace with the `CoversMember`/`UnionTypesExhaustive` definitions above (`PatternMayMatchType` is retained only if referenced elsewhere; it is not — delete it).
- In `T-IfCase-Enum` (19060), `Chk-IfCase-Enum` (19101), `IfCase-Enum-NonExhaustive` (19106): replace `CaseVariants(cases) = VariantNames(E)` with `CoveredVariants(cases, E) = VariantNames(E)` (set equality over the name set).
- In `T-IfCase-Modal` (19067), `Chk-IfCase-Modal` (19111), `IfCase-Modal-NonExhaustive` (19072): replace `CaseStates(cases) = States(M)` with `CoveredStates(cases, modal_ref) = States(M)`.

### B1.3 Runtime fall-through becomes a panic

Replace `EvalIfCases-None` (18958–18961) with:

```text
**(EvalIfCases-None)**
else_opt = ⊥
──────────────────────────────────────────────────────────────────────────────
Γ ⊢ EvalIfCaseListSigma([], else_opt, v, σ) ⇓ (Ctrl(Panic), σ)
```

§24.5.2 additions: extend `PanicReason` (28793) with `MatchFail`; insert `PanicCode(MatchFail) = 0x000B` after line 28804; extend `PanicSite` (28807) with `MatchFailSite` and add `PanicReasonOf(MatchFailSite) = MatchFail`.

§17.5.6 addition (one sentence): "When `else_opt = ⊥`, `LowerIfCases` MUST emit a trailing arm that lowers to `LowerPanic(MatchFail)`."

§17.6.7 prose update: "…and for case analyses counted as exhaustive: a case contributes coverage only when its payload subpatterns are irrefutable."

---

## Part 3 — B2: Authored rule bodies

All rules below are NEW normative text. Shared helper, to be added to §5.4 (Shared Grammar Policy and Parser Helpers):

```text
IsCtxIdent(t, s) ⇔ t.kind = Identifier ∧ t.lexeme = s
```

Span construction in conclusions uses the existing §5.4 span-derivation convention for the consumed token range; the helper name is aligned with §5.4 at application time (noted as `SpanTok(P, P')` below).

### B2.1 §19.1.2 — Key path parsing (replaces the name-only list)

```text
**(Parse-KeyMarkerOpt-Yes)**
IsOp(Tok(P), "#")
────────────────────────────────────────
Γ ⊢ ParseKeyMarkerOpt(P) ⇓ (Advance(P), true)

**(Parse-KeyMarkerOpt-No)**
¬ IsOp(Tok(P), "#")
────────────────────────────────────────
Γ ⊢ ParseKeyMarkerOpt(P) ⇓ (P, false)

**(Parse-KeyField)**
Γ ⊢ ParseKeyMarkerOpt(P) ⇓ (P_1, m)    Γ ⊢ ParseIdent(P_1) ⇓ (P_2, name)
────────────────────────────────────────────────────────────────────────────
Γ ⊢ ParseKeyField(P) ⇓ (P_2, Field(m, name))

**(Parse-KeyIndex)**
IsPunc(Tok(P), "[")    Γ ⊢ ParseExpr(Advance(P)) ⇓ (P_1, e)    IsPunc(Tok(P_1), "]")
────────────────────────────────────────────────────────────────────────────────────────
Γ ⊢ ParseKeyIndex(P) ⇓ (Advance(P_1), Index(false, e))

**(Parse-KeySegs-Field)**
IsPunc(Tok(P), ".")    Γ ⊢ ParseKeyField(Advance(P)) ⇓ (P_1, seg)    Γ ⊢ ParseKeySegs(P_1) ⇓ (P_2, segs)
────────────────────────────────────────────────────────────────────────────────────────────────────────────
Γ ⊢ ParseKeySegs(P) ⇓ (P_2, [seg] ++ segs)

**(Parse-KeySegs-Index)**
IsPunc(Tok(P), "[")    Γ ⊢ ParseKeyIndex(P) ⇓ (P_1, seg)    Γ ⊢ ParseKeySegs(P_1) ⇓ (P_2, segs)
────────────────────────────────────────────────────────────────────────────────────────────────
Γ ⊢ ParseKeySegs(P) ⇓ (P_2, [seg] ++ segs)

**(Parse-KeySegs-End)**
¬ IsPunc(Tok(P), ".")    ¬ IsPunc(Tok(P), "[")
────────────────────────────────────────────────
Γ ⊢ ParseKeySegs(P) ⇓ (P, [])

**(Parse-KeyPathExpr)**
Γ ⊢ ParseKeyField(P) ⇓ (P_1, root)    Γ ⊢ ParseKeySegs(P_1) ⇓ (P_2, segs)
────────────────────────────────────────────────────────────────────────────
Γ ⊢ ParseKeyPathExpr(P) ⇓ (P_2, ⟨root, segs⟩)
```

Open point for approval: index segments currently parse with `marked = false`; the grammar provides no marker position before `[`. If marked index segments are intended, the grammar and `Parse-KeyIndex` need a `key_marker?` before `index_suffix`.

### B2.2 §19.2.2 — Key block parsing (replaces the name-only list)

```text
**(Parse-KeyMode-Read)**
IsCtxIdent(Tok(P), "read")
────────────────────────────────────────
Γ ⊢ ParseKeyMode(P) ⇓ (Advance(P), Read)

**(Parse-KeyMode-Write)**
IsCtxIdent(Tok(P), "write")
────────────────────────────────────────
Γ ⊢ ParseKeyMode(P) ⇓ (Advance(P), Write)

**(Parse-KeyMode-Err)**
¬ IsCtxIdent(Tok(P), "read")    ¬ IsCtxIdent(Tok(P), "write")    c = Code(Parse-KeyMode-Err)
──────────────────────────────────────────────────────────────────────────────────────────────
Γ ⊢ ParseKeyMode(P) ⇑ c

**(Parse-KeyBlockHead-Read)**
IsOp(Tok(P), "%")    IsCtxIdent(Tok(Advance(P)), "read")
────────────────────────────────────────────────────────────
Γ ⊢ ParseKeyBlockHead(P) ⇓ (Advance(Advance(P)), ⟨Read, ⊥⟩)

**(Parse-KeyBlockHead-Write)**
IsOp(Tok(P), "%")    IsCtxIdent(Tok(Advance(P)), "write")
────────────────────────────────────────────────────────────
Γ ⊢ ParseKeyBlockHead(P) ⇓ (Advance(Advance(P)), ⟨Write, ⊥⟩)

**(Parse-KeyBlockHead-Release)**
IsOp(Tok(P), "%")    IsCtxIdent(Tok(Advance(P)), "release")    Γ ⊢ ParseKeyMode(Advance(Advance(P))) ⇓ (P_1, mode)
────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
Γ ⊢ ParseKeyBlockHead(P) ⇓ (P_1, ⟨Release, mode⟩)

**(Parse-KeyBlockHead-SpeculativeWrite)**
IsOp(Tok(P), "%")    IsCtxIdent(Tok(Advance(P)), "speculative")    IsCtxIdent(Tok(Advance(Advance(P))), "write")
──────────────────────────────────────────────────────────────────────────────────────────────────────────────────
Γ ⊢ ParseKeyBlockHead(P) ⇓ (Advance(Advance(Advance(P))), ⟨SpeculativeWrite, ⊥⟩)

**(Parse-KeyPathList-Cons)**
Γ ⊢ ParseKeyPathExpr(P) ⇓ (P_1, kp)    Γ ⊢ ParseKeyPathListTail(P_1) ⇓ (P_2, kps)
────────────────────────────────────────────────────────────────────────────────────
Γ ⊢ ParseKeyPathList(P) ⇓ (P_2, [kp] ++ kps)

**(Parse-KeyPathListTail-Comma)**
IsPunc(Tok(P), ",")    Γ ⊢ ParseKeyPathExpr(Advance(P)) ⇓ (P_1, kp)    Γ ⊢ ParseKeyPathListTail(P_1) ⇓ (P_2, kps)
──────────────────────────────────────────────────────────────────────────────────────────────────────────────────
Γ ⊢ ParseKeyPathListTail(P) ⇓ (P_2, [kp] ++ kps)

**(Parse-KeyPathListTail-End)**
¬ IsPunc(Tok(P), ",")
────────────────────────────────────────
Γ ⊢ ParseKeyPathListTail(P) ⇓ (P, [])

**(Parse-KeyOption-Ordered)**
IsCtxIdent(Tok(P), "ordered")
────────────────────────────────────────
Γ ⊢ ParseKeyOption(P) ⇓ (Advance(P), Ordered)

**(Parse-KeyOptionsOpt-None)**
¬ IsPunc(Tok(P), "[")
──────────────────────────────────────────────────────
Γ ⊢ ParseKeyOptionsOpt(P) ⇓ (P, ⟨ordered: false⟩)

**(Parse-KeyOptionsOpt-Some)**
IsPunc(Tok(P), "[")    Γ ⊢ ParseKeyOption(Advance(P)) ⇓ (P_1, o)    Γ ⊢ ParseKeyOptionTail(P_1) ⇓ (P_2, os)    IsPunc(Tok(P_2), "]")
──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
Γ ⊢ ParseKeyOptionsOpt(P) ⇓ (Advance(P_2), ⟨ordered: Ordered ∈ [o] ++ os⟩)

**(Parse-KeyOptionTail-Comma)**
IsPunc(Tok(P), ",")    ¬ IsPunc(Tok(Advance(P)), "]")    Γ ⊢ ParseKeyOption(Advance(P)) ⇓ (P_1, o)    Γ ⊢ ParseKeyOptionTail(P_1) ⇓ (P_2, os)
──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
Γ ⊢ ParseKeyOptionTail(P) ⇓ (P_2, [o] ++ os)

**(Parse-KeyOptionTail-TrailingComma)**
IsPunc(Tok(P), ",")    IsPunc(Tok(Advance(P)), "]")
──────────────────────────────────────────────────────
Γ ⊢ ParseKeyOptionTail(P) ⇓ (Advance(P), [])

**(Parse-KeyOptionTail-End)**
¬ IsPunc(Tok(P), ",")
────────────────────────────────────────
Γ ⊢ ParseKeyOptionTail(P) ⇓ (P, [])

**(Parse-KeyBlock-Stmt)**
Γ ⊢ ParseKeyBlockHead(P) ⇓ (P_1, ⟨kind, mode⟩)    Γ ⊢ ParseKeyPathList(P_1) ⇓ (P_2, paths)
Γ ⊢ ParseKeyOptionsOpt(P_2) ⇓ (P_3, options)    Γ ⊢ ParseBlockExpr(P_3) ⇓ (P_4, body)    sp = SpanTok(P, P_4)
──────────────────────────────────────────────────────────────────────────────────────────────────────────────
Γ ⊢ ParseStmt(P) ⇓ (P_4, KeyBlockStmt(⊥, kind, paths, mode, options, body, sp))
```

`attrs_opt` is populated by the shared attributed-statement mechanism of Chapter 9; `Parse-KeyBlock-Stmt` itself produces `⊥`. `ParseStmt` dispatches to `Parse-KeyBlock-Stmt` when `IsOp(Tok(P), "%")`.

### B2.3 §20.1.2 — Parallel block parsing (replaces the name-only list)

The list-tail rules follow the exact `Parse-KeyOptionTail-*` shape; only the element parser differs. To avoid four near-identical tail families, the proposal introduces one shared schema in §5.5:

```text
**(Parse-BracketOptListOpt-None)**
¬ IsPunc(Tok(P), "[")
──────────────────────────────────────────
Γ ⊢ ParseOptListOpt(El, P) ⇓ (P, [])

**(Parse-BracketOptListOpt-Yes)**
IsPunc(Tok(P), "[")    Γ ⊢ ParseOptList(El, Advance(P)) ⇓ (P_1, os)    IsPunc(Tok(P_1), "]")
────────────────────────────────────────────────────────────────────────────────────────────────
Γ ⊢ ParseOptListOpt(El, P) ⇓ (Advance(P_1), os)

**(Parse-OptList-Empty)**
IsPunc(Tok(P), "]")
──────────────────────────────────────
Γ ⊢ ParseOptList(El, P) ⇓ (P, [])

**(Parse-OptList-Cons)**
¬ IsPunc(Tok(P), "]")    Γ ⊢ El(P) ⇓ (P_1, o)    Γ ⊢ ParseOptListTail(El, P_1) ⇓ (P_2, os)
──────────────────────────────────────────────────────────────────────────────────────────────
Γ ⊢ ParseOptList(El, P) ⇓ (P_2, [o] ++ os)

**(Parse-OptListTail-Comma)**
IsPunc(Tok(P), ",")    ¬ IsPunc(Tok(Advance(P)), "]")    Γ ⊢ El(Advance(P)) ⇓ (P_1, o)    Γ ⊢ ParseOptListTail(El, P_1) ⇓ (P_2, os)
──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
Γ ⊢ ParseOptListTail(El, P) ⇓ (P_2, [o] ++ os)

**(Parse-OptListTail-TrailingComma)**
IsPunc(Tok(P), ",")    IsPunc(Tok(Advance(P)), "]")
──────────────────────────────────────────────────────
Γ ⊢ ParseOptListTail(El, P) ⇓ (Advance(P), [])

**(Parse-OptListTail-End)**
¬ IsPunc(Tok(P), ",")
────────────────────────────────────────
Γ ⊢ ParseOptListTail(El, P) ⇓ (P, [])
```

The per-feature names in §19.2.2/§20.1.2/§20.4.2/§20.5.2 (`Parse-ParallelOptList-*`, `Parse-SpawnOptList-*`, `Parse-DispatchOptList-*`, and the `*-OptsOpt-*` pairs) are then defined as instantiations: e.g. `Parse-ParallelOptList-Cons = Parse-OptList-Cons[El := ParseParallelOpt]`. (Alternative: spell each family out verbatim; ~48 additional rules. The schema is recommended — confirm.)

Feature-specific element rules:

```text
**(Parse-ParallelOpt-Cancel)**
IsCtxIdent(Tok(P), "cancel")    IsPunc(Tok(Advance(P)), ":")    Γ ⊢ ParseExpr(Advance(Advance(P))) ⇓ (P_1, e)
──────────────────────────────────────────────────────────────────────────────────────────────────────────────
Γ ⊢ ParseParallelOpt(P) ⇓ (P_1, Cancel(e))

**(Parse-ParallelOpt-Name)**
IsCtxIdent(Tok(P), "name")    IsPunc(Tok(Advance(P)), ":")    Tok(Advance(Advance(P))).kind = StringLiteral    s = StringValue(Tok(Advance(Advance(P))))
─────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
Γ ⊢ ParseParallelOpt(P) ⇓ (Advance(Advance(Advance(P))), Name(s))

**(Parse-ParallelOpt-Workgroup)**
IsCtxIdent(Tok(P), "workgroup")    IsPunc(Tok(Advance(P)), ":")    Γ ⊢ ParseExpr(Advance(Advance(P))) ⇓ (P_1, e)
──────────────────────────────────────────────────────────────────────────────────────────────────────────────────
Γ ⊢ ParseParallelOpt(P) ⇓ (P_1, Workgroup(e))

**(Parse-ParallelOpt-Workgroups)**
IsCtxIdent(Tok(P), "workgroups")    IsPunc(Tok(Advance(P)), ":")    Γ ⊢ ParseExpr(Advance(Advance(P))) ⇓ (P_1, e)
────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
Γ ⊢ ParseParallelOpt(P) ⇓ (P_1, Workgroups(e))

**(Parse-Parallel-Expr)**
IsKw(Tok(P), "parallel")    Γ ⊢ ParseExpr(Advance(P)) ⇓ (P_1, domain)
Γ ⊢ ParseOptListOpt(ParseParallelOpt, P_1) ⇓ (P_2, opts)    Γ ⊢ ParseBlockExpr(P_2) ⇓ (P_3, body)
────────────────────────────────────────────────────────────────────────────────────────────────────
Γ ⊢ ParsePrimaryExpr(P) ⇓ (P_3, ParallelExpr(domain, opts, body))
```

### B2.4 §20.4.2 — Spawn parsing

```text
**(Parse-SpawnOpt-Name)**
IsCtxIdent(Tok(P), "name")    IsPunc(Tok(Advance(P)), ":")    Tok(Advance(Advance(P))).kind = StringLiteral    s = StringValue(Tok(Advance(Advance(P))))
─────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
Γ ⊢ ParseSpawnOpt(P) ⇓ (Advance(Advance(Advance(P))), Name(s))

**(Parse-SpawnOpt-Affinity)**
IsCtxIdent(Tok(P), "affinity")    IsPunc(Tok(Advance(P)), ":")    Γ ⊢ ParseExpr(Advance(Advance(P))) ⇓ (P_1, e)
──────────────────────────────────────────────────────────────────────────────────────────────────────────────────
Γ ⊢ ParseSpawnOpt(P) ⇓ (P_1, Affinity(e))

**(Parse-SpawnOpt-Priority)**
IsCtxIdent(Tok(P), "priority")    IsPunc(Tok(Advance(P)), ":")    Γ ⊢ ParseExpr(Advance(Advance(P))) ⇓ (P_1, e)
──────────────────────────────────────────────────────────────────────────────────────────────────────────────────
Γ ⊢ ParseSpawnOpt(P) ⇓ (P_1, Priority(e))

**(Parse-Spawn-Expr)**
IsKw(Tok(P), "spawn")    Γ ⊢ ParseOptListOpt(ParseSpawnOpt, Advance(P)) ⇓ (P_1, opts)    Γ ⊢ ParseBlockExpr(P_1) ⇓ (P_2, body)
──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
Γ ⊢ ParsePrimaryExpr(P) ⇓ (P_2, SpawnExpr(opts, body))
```

### B2.5 §20.5.2 — Dispatch parsing

```text
**(Parse-ReduceOp-Op)**
Tok(P).kind = Operator    Tok(P).lexeme ∈ {"+", "*"}
────────────────────────────────────────────────────────
Γ ⊢ ParseReduceOp(P) ⇓ (Advance(P), Tok(P).lexeme)

**(Parse-ReduceOp-Ident)**
Tok(P).kind = Identifier
────────────────────────────────────────────────────────
Γ ⊢ ParseReduceOp(P) ⇓ (Advance(P), Tok(P).lexeme)

**(Parse-DispatchOpt-Reduce)**
IsCtxIdent(Tok(P), "reduce")    IsPunc(Tok(Advance(P)), ":")    Γ ⊢ ParseReduceOp(Advance(Advance(P))) ⇓ (P_1, op)
────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
Γ ⊢ ParseDispatchOpt(P) ⇓ (P_1, Reduce(op))

**(Parse-DispatchOpt-Ordered)**
IsCtxIdent(Tok(P), "ordered")
──────────────────────────────────────────────
Γ ⊢ ParseDispatchOpt(P) ⇓ (Advance(P), Ordered)

**(Parse-DispatchOpt-Chunk)**
IsCtxIdent(Tok(P), "chunk")    IsPunc(Tok(Advance(P)), ":")    Γ ⊢ ParseExpr(Advance(Advance(P))) ⇓ (P_1, e)
──────────────────────────────────────────────────────────────────────────────────────────────────────────────
Γ ⊢ ParseDispatchOpt(P) ⇓ (P_1, Chunk(e))

**(Parse-DispatchOpt-Workgroup)**
IsCtxIdent(Tok(P), "workgroup")    IsPunc(Tok(Advance(P)), ":")    Γ ⊢ ParseExpr(Advance(Advance(P))) ⇓ (P_1, e)
──────────────────────────────────────────────────────────────────────────────────────────────────────────────────
Γ ⊢ ParseDispatchOpt(P) ⇓ (P_1, Workgroup(e))

**(Parse-KeyClauseOpt-Yes)**
IsCtxIdent(Tok(P), "key")    Γ ⊢ ParseKeyPathExpr(Advance(P)) ⇓ (P_1, path)    Γ ⊢ ParseKeyMode(P_1) ⇓ (P_2, mode)
─────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
Γ ⊢ ParseKeyClauseOpt(P) ⇓ (P_2, ⟨path, mode⟩)

**(Parse-KeyClauseOpt-None)**
¬ IsCtxIdent(Tok(P), "key")
────────────────────────────────────────
Γ ⊢ ParseKeyClauseOpt(P) ⇓ (P, ⊥)

**(Parse-Dispatch-Expr)**
IsKw(Tok(P), "dispatch")    Γ ⊢ ParsePattern(Advance(P)) ⇓ (P_1, pat)    IsCtxIdent(Tok(P_1), "in")
Γ ⊢ ParseExpr(Advance(P_1)) ⇓ (P_2, range)    Γ ⊢ ParseKeyClauseOpt(P_2) ⇓ (P_3, kc)
Γ ⊢ ParseOptListOpt(ParseDispatchOpt, P_3) ⇓ (P_4, opts)    Γ ⊢ ParseBlockExpr(P_4) ⇓ (P_5, body)
────────────────────────────────────────────────────────────────────────────────────────────────────
Γ ⊢ ParsePrimaryExpr(P) ⇓ (P_5, DispatchExpr(pat, range, kc, opts, body))
```

The `range` operand parses as a general expression; the range-type requirement is enforced by §20.5.4. Appendix B.10 is updated to match (A-item B.10 reconciliation): `block_option ::= "cancel" ":" expression | "name" ":" string_literal | "workgroup" ":" expression | "workgroups" ":" expression`; `spawn_option ::= "name" ":" string_literal | "affinity" ":" expression | "priority" ":" expression`; `dispatch_option ::= "reduce" ":" reduce_op | "ordered" | "chunk" ":" expression | "workgroup" ":" expression`.

### B2.6 §16.2.5 — Access-expression control propagation

```text
**(EvalSigma-FieldAccess-Ctrl)**
Γ ⊢ EvalSigma(base, σ) ⇓ (Ctrl(κ), σ_1)
──────────────────────────────────────────────────────────────
Γ ⊢ EvalSigma(FieldAccess(base, f), σ) ⇓ (Ctrl(κ), σ_1)

**(EvalSigma-Index-Ctrl-Base)**
Γ ⊢ EvalSigma(base, σ) ⇓ (Ctrl(κ), σ_1)
──────────────────────────────────────────────────────────────
Γ ⊢ EvalSigma(IndexAccess(base, idx), σ) ⇓ (Ctrl(κ), σ_1)

**(EvalSigma-Index-Ctrl-Idx)**
Γ ⊢ EvalSigma(base, σ) ⇓ (Val(v_b), σ_1)    Γ ⊢ EvalSigma(idx, σ_1) ⇓ (Ctrl(κ), σ_2)
────────────────────────────────────────────────────────────────────────────────────────
Γ ⊢ EvalSigma(IndexAccess(base, idx), σ) ⇓ (Ctrl(κ), σ_2)
```

(`EvalSigma-TupleAccess-Ctrl` already exists at 9128 and is the model.)

### B2.7 §16.1.5 — Poisoned-module evaluation

```text
**(EvalSigma-Ident-Poison)**
LookupBind(σ, x) undefined    Γ ⊢ ResolveValueName(x) ⇓ ent    ent.origin_opt = mp    PoisonedModule(σ, PathOfModule(mp))
──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
Γ ⊢ EvalSigma(Identifier(x), σ) ⇓ (Ctrl(Panic), σ)

**(EvalSigma-Path-Poison)**
Γ ⊢ ResolveQualified(path, name, ValueKind) ⇓ ent    ent.origin_opt = mp    PoisonedModule(σ, PathOfModule(mp))
──────────────────────────────────────────────────────────────────────────────────────────────────────────────────
Γ ⊢ EvalSigma(Path(path, name), σ) ⇓ (Ctrl(Panic), σ)

**(EvalSigma-Ident-Poison-RecordCtor)**
LookupBind(σ, x) undefined    Γ ⊢ ResolveValueName(x) ⇑    Γ ⊢ ResolveRecordPath([], x) ⇓ p    SplitLast(p) = (mp, _)    PoisonedModule(σ, mp)
──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
Γ ⊢ EvalSigma(Identifier(x), σ) ⇓ (Ctrl(Panic), σ)

**(EvalSigma-Path-Poison-RecordCtor)**
Γ ⊢ ResolveQualified(path, name, ValueKind) ⇑    Γ ⊢ ResolveRecordPath(path, name) ⇓ p    SplitLast(p) = (mp, _)    PoisonedModule(σ, mp)
────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
Γ ⊢ EvalSigma(Path(path, name), σ) ⇓ (Ctrl(Panic), σ)
```

The panic record carries `InitPanic(mp)` per §24.5.2; lowering is already covered by `CheckPoison`/`PoisonJudg` (30405).

### B2.8 §16.4.6 — Binary-operator lowering

```text
BinOpPanicReasons(op, T) =
  [Overflow]            if op ∈ {"+", "-", "*", "**"} ∧ StripPerm(T) = TypePrim(t) ∧ t ∈ IntTypes
  [DivZero, Overflow]   if op ∈ {"/", "%"} ∧ StripPerm(T) = TypePrim(t) ∧ t ∈ IntTypes
  [Shift]               if op ∈ {"<<", ">>"}
  []                    otherwise

NeedsBinOpPanicCheck(op, T) ⇔ BinOpPanicReasons(op, T) ≠ []

**(Lower-BinOp-Ok)**
Γ ⊢ LowerExpr(e_1) ⇓ ⟨IR_1, v_1⟩    Γ ⊢ LowerExpr(e_2) ⇓ ⟨IR_2, v_2⟩    T = ExprType(e_1)    ¬ NeedsBinOpPanicCheck(op, T)    v_r = FreshTemp("binop")
──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
Γ ⊢ LowerBinOp(op, e_1, e_2) ⇓ ⟨SeqIR(IR_1, IR_2, BinOpIR(op, v_1, v_2, v_r)), v_r⟩

**(Lower-BinOp-Panic)**
Γ ⊢ LowerExpr(e_1) ⇓ ⟨IR_1, v_1⟩    Γ ⊢ LowerExpr(e_2) ⇓ ⟨IR_2, v_2⟩    T = ExprType(e_1)    rs = BinOpPanicReasons(op, T)    rs ≠ []    v_r = FreshTemp("binop")
────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
Γ ⊢ LowerBinOp(op, e_1, e_2) ⇓ ⟨SeqIR(IR_1, IR_2, CheckBinOpIR(op, v_1, v_2, rs), PanicCheckIR, BinOpIR(op, v_1, v_2, v_r)), v_r⟩
```

`CheckBinOpIR` evaluates each reason's check exactly as the corresponding `PanicSite` of §24.5.2 (`DivZeroCheck`, `OverflowCheck`, `ShiftCheck`); the `i128`/`u128` division note of §24.7 applies unchanged.

### B2.9 §16.5.6 — Cast and transmute lowering

```text
CastNeedsCheck(S, T) ⇔ (S = TypePrim(s) ∧ s ∈ FloatTypes ∧ T = TypePrim(t) ∧ t ∈ IntTypes) ∨ (S = TypePrim("u32") ∧ T = TypePrim("char"))

**(Lower-Cast)**
Γ ⊢ LowerExpr(e) ⇓ ⟨IR_e, v⟩    S = ExprType(e)    ¬ CastNeedsCheck(S, T)    v_r = FreshTemp("cast")
──────────────────────────────────────────────────────────────────────────────────────────────────────
Γ ⊢ LowerCast(e, T) ⇓ ⟨SeqIR(IR_e, CastIR(S, T, v, v_r)), v_r⟩

**(Lower-Cast-Panic)**
Γ ⊢ LowerExpr(e) ⇓ ⟨IR_e, v⟩    S = ExprType(e)    CastNeedsCheck(S, T)    v_r = FreshTemp("cast")
────────────────────────────────────────────────────────────────────────────────────────────────────
Γ ⊢ LowerCast(e, T) ⇓ ⟨SeqIR(IR_e, CheckCastIR(S, T, v), PanicCheckIR, CastIR(S, T, v, v_r)), v_r⟩

InvalidPatterns(T) ⇔ ∃ bits. |bits| = sizeof(T) ∧ ¬ ValidValue(T, bits)

**(Lower-Transmute)**
Γ ⊢ LowerExpr(e) ⇓ ⟨IR_e, v⟩    ¬ InvalidPatterns(T_2)    v_r = FreshTemp("transmute")
────────────────────────────────────────────────────────────────────────────────────────
Γ ⊢ LowerTransmute(T_1, T_2, e) ⇓ ⟨SeqIR(IR_e, TransmuteIR(T_1, T_2, v, v_r)), v_r⟩

**(Lower-Transmute-Err)**
Γ ⊢ LowerExpr(e) ⇓ ⟨IR_e, v⟩    InvalidPatterns(T_2)    v_r = FreshTemp("transmute")
──────────────────────────────────────────────────────────────────────────────────────────────────────────────────
Γ ⊢ LowerTransmute(T_1, T_2, e) ⇓ ⟨SeqIR(IR_e, CheckTransmuteIR(T_2, v), PanicCheckIR, TransmuteIR(T_1, T_2, v, v_r)), v_r⟩
```

`CheckCastIR` panics with reason `Cast` exactly when `CastVal(S, T, v)` is undefined; `CheckTransmuteIR` panics with reason `Cast` exactly when the source bits are not `ValidValue` for `T_2`. This matches `EvalSigma-Cast-Panic`/`EvalSigma-Transmute-Ctrl` and gives `W-SAFE-0100` its runtime counterpart.

### B2.10 §18.1.4 — Checking-mode block rules

```text
**(Chk-Block-Tail)**
Γ_0 = PushScope(Γ)    Γ_0; R; L ⊢ stmts ⇒ Γ_1 ▷ ⟨Res, Brk, BrkVoid⟩    Γ ⊢ WarnResultUnreachable(stmts) ⇓ ok
ResType(Res) = ⊥    tail_opt = e    ¬ IsReturnTail(e)    Γ_1; R; L ⊢ e ⇐ T ⊣ C
──────────────────────────────────────────────────────────────────────────────────────────────────────────────
Γ; R; L ⊢ BlockExpr(stmts, tail_opt) ⇐ T ⊣ C

**(Chk-Block-Return)**
Γ_0 = PushScope(Γ)    Γ_0; R; L ⊢ stmts ⇒ Γ_1 ▷ ⟨Res, Brk, BrkVoid⟩    Γ ⊢ WarnResultUnreachable(stmts) ⇓ ok
(ResType(Res) ≠ ⊥ ∨ (tail_opt = e ∧ IsReturnTail(e) ∧ Γ_1; R; L ⊢ e : TypePrim("!")))
──────────────────────────────────────────────────────────────────────────────────────────────────────────────
Γ; R; L ⊢ BlockExpr(stmts, tail_opt) ⇐ T ⊣ ∅

**(Chk-Block-Unit)**
Γ_0 = PushScope(Γ)    Γ_0; R; L ⊢ stmts ⇒ Γ_1 ▷ ⟨Res, Brk, BrkVoid⟩    Γ ⊢ WarnResultUnreachable(stmts) ⇓ ok
ResType(Res) = ⊥    tail_opt = ⊥    Γ ⊢ TypePrim("()") <: T
──────────────────────────────────────────────────────────────────────────────────────────────────────────────
Γ; R; L ⊢ BlockExpr(stmts, tail_opt) ⇐ T ⊣ ∅
```

`IsReturnTail(e)` holds for the return-shaped tails recognized by `BlockInfo-ReturnTail`; the three rules partition exactly as `BlockInfo-Tail` / `BlockInfo-ReturnTail` + `BlockInfo-Res` / `BlockInfo-Unit`.

---

## Part 4 — B3: Arithmetic shift-right for signed operands

Replace `ShiftOp` (line 16538) with:

```text
ShiftOp(op, t, v_1, v_2) = v ⇔ v_1 = IntVal(t, x_1) ∧ v_2 = IntVal("u32", n) ∧ w = IntWidth(t) ∧ 0 ≤ n < w ∧ (
  (op = "<<" ∧ u_1 = ToUnsigned(w, x_1) ∧ u = (u_1 · 2^n) mod 2^w ∧
    ((t ∈ SignedIntTypes ∧ v = IntVal(t, ToSigned(w, u))) ∨ (t ∈ UnsignedIntTypes ∧ v = IntVal(t, u))))
  ∨ (op = ">>" ∧ t ∈ UnsignedIntTypes ∧ v = IntVal(t, ⌊x_1 / 2^n⌋))
  ∨ (op = ">>" ∧ t ∈ SignedIntTypes ∧ v = IntVal(t, ⌊x_1 / 2^n⌋)))
```

where `⌊·⌋` rounds toward negative infinity. Add the normative note: "`>>` is an arithmetic shift on signed operands (sign-extending) and a logical shift on unsigned operands. The right operand of `<<` and `>>` MUST have type `u32`; a shift amount `n ≥ IntWidth(t)` leaves `ShiftOp` undefined and panics with reason `Shift`." Add to §24.7.8's shift lowering mapping (or §24.7.3 attribute table if owned there): signed `>>` lowers to `ashr`, unsigned `>>` to `lshr`, `<<` to `shl`, each guarded by `ShiftCheck` per `CheckedShifts` (29506).

---

## Part 5 — B4: Decidable refinement equivalence and canonical unions

### B5.1 Replace `PredicateEquiv` (line 6037; the §14.8 duplicate at 13728 is deleted by A12)

```text
PredSubject(P) = the binder identifier the refinement form introduces for the refined value in P
PredNorm(P) = P with every free occurrence of PredSubject(P) replaced by the reserved subject symbol ⌀, and no other rewriting
PredicateEquiv(P_1, P_2) ⇔ PredNorm(P_1) = PredNorm(P_2)    (structural AST equality)
```

Note (normative): predicates that are semantically equivalent but structurally distinct after subject renaming are NOT equivalent for type equivalence. Implementations MUST NOT accept broader equivalences. This makes `Γ ⊢ T ≡ U` decidable and aligns it with `Unify-Refine`/`Unify-Refine-Pred-Fail` (which gain the same `PredNorm` comparison in place of raw `pred_T ≠ pred_U`).

### B5.2 Canonical union form — insert in §8.1 after `MembersEq` (5915)

```text
TypeKeyString(T) = TypeRender(AliasExpand(T))
UnionSort([T_1, …, T_n]) = the stable sort of [T_1, …, T_n] by byte-wise lexicographic order of TypeKeyString
```

§8.3: delete the `TypeUnion` clause from `Unify-Rigid-Fail` (6384) and add:

```text
**(Unify-Union-Eq)**
T = TypeUnion(Ts)    U = TypeUnion(Us)    TVars(T) = ∅    TVars(U) = ∅    UnionSort(Ts) = UnionSort(Us)
──────────────────────────────────────────────────────────────────────────────────────────────────────────
⟨UnifyStep({(T, U)} ∪ C, θ)⟩ → ⟨UnifyStep(C, θ)⟩

**(Unify-Union-Fail)**
T = TypeUnion(Ts)    U = TypeUnion(Us)    (TVars(T) ≠ ∅ ∨ TVars(U) ≠ ∅ ∨ UnionSort(Ts) ≠ UnionSort(Us))
──────────────────────────────────────────────────────────────────────────────────────────────────────────
⟨UnifyStep({(T, U)} ∪ C, θ)⟩ → ⟨UnifyFail⟩
```

Unification over unions is order-insensitive but member-syntactic; unions containing unsolved type variables do not unify (set unification is intentionally out of scope). `T-Equiv-Union`'s permutation form (5952) is unchanged and strictly contains the solver's relation; the solver is the decidable approximation.

---

## Part 6 — B5: Happens-before memory model (replaces the informal constraint list in §19.7.4/§19.7.5)

The following becomes the body of §19.7.5 (the §19.7.4 ordering table is retained as an informative summary, marked as such).

### Memory actions

```text
TaskId = task identities introduced by Chapters 20–21; the entry task is t_main.
Loc = storage locations of the runtime state σ (§6.5).

MemAction =
  Access(t, l, kind, ord)      kind ∈ {Read, Write}, ord ∈ {plain, relaxed, acquire, release, acqrel, seqcst}
| KeyAcquire(t, ks)            ks = key set acquired by a key block (§19.2.5)
| KeyRelease(t, ks)
| FenceAct(t, O)               O ∈ {acquire, release, seqcst}
| TaskStart(t) | TaskEnd(t)
| SpawnAct(t_parent, t_child) | JoinAct(t_child, t_parent)
```

Keyed and `shared` accesses carry the `EffectiveOrdering` of §19.7.3; all other accesses are `plain`. `KeyAcquire`/`KeyRelease` and ordered-commit events are the `KeyEffect` observables of §6.1.4.

### Sequenced-before

`sb` is the per-task total order over that task's actions induced by the sequence points of §6.1.5 and, between adjacent sequence points, by `Children_LTR` (§24.7.7).

### Synchronizes-with

`sw` is the least relation containing:

1. **Key transfer.** `KeyRelease(t_1, ks_1) sw KeyAcquire(t_2, ks_2)` whenever some key in `ks_1` overlaps some key in `ks_2` (`KeysOverlap`, §19.3.5) and the release immediately precedes the acquire in that key's serialization order.
2. **Task creation.** `SpawnAct(t_p, t_c) sw TaskStart(t_c)`; the action of a `parallel` or `dispatch` block that logically creates a work item sw the first action of that item.
3. **Task completion.** `TaskEnd(t_c) sw JoinAct(t_c, t_p)`; every work item's `TaskEnd` sw the enclosing block's exit (§20.1.5 fork-join), and `TaskEnd` of a `Spawned` task sw the completion of the `wait`/`sync` that consumes it (§21.2.5).
4. **Release/acquire accesses.** `Access(t_1, l, Write, o_w)` with `o_w ∈ {release, acqrel, seqcst}` sw `Access(t_2, l, Read, o_r)` with `o_r ∈ {acquire, acqrel, seqcst}` when the read reads-from that write.
5. **Fences.** `FenceAct(t_1, release)` sw `FenceAct(t_2, acquire)` when an `Access(t_1, l, Write, o)` sequenced after the release fence is read-from by an `Access(t_2, l, Read, o')` sequenced before the acquire fence, for `o, o' ∈ {relaxed, …}`. `seqcst` fences additionally participate in clauses 4–5 as both acquire and release.
6. **Ordered commit.** The commit action of an `ordered` key block sw the acquisition of the next block in canonical order (§19.2.5).

### Happens-before, coherence, visibility

```text
hb = (sb ∪ sw)⁺        hb MUST be irreflexive.
```

For each location `l` there is a total modification order `mo_l` over `Write` actions on `l`, consistent with `hb`. A `plain` or key-covered read of `l` MUST read-from the `hb`-latest write to `l` (unique by race-freedom below). A `relaxed`/`acquire`/`seqcst` read of `l` MUST read-from a write `W` such that the read does not happen-before `W` and there is no write `W'` with `W →mo_l W'` and `W' hb` the read (coherence). All `seqcst` actions and `seqcst` fences belong to one total order `S` consistent with `hb` and with every `mo_l`; `seqcst` reads read-from the latest `S`-prior write permitted by coherence.

Values MUST NOT be produced out of thin air: in every execution, the reads-from justification of relaxed reads MUST be acyclic (`(rf ∪ sb)⁺` irreflexive).

### Data races

Two actions conflict iff they access the same location, at least one is a `Write`, and they are from different tasks. An execution has a data race iff some pair of conflicting actions, at least one of which is `plain`, is unordered by `hb`. Programs whose safe fragment is accepted by Chapters 10 and 19 have no data races (`Data-Race-Freedom`, §8.4): every `shared` access is key-covered and key transfer establishes `hb`. A data race introduced through `unsafe`, raw pointers, or FFI implies `OutsideConformance`.

### Key-transfer visibility (normative)

If `A` is any access sequenced before `KeyRelease(t_1, ks)` in `t_1`, and `KeyAcquire(t_2, ks')` with overlapping keys synchronizes with it, then `A hb B` for every `B` sequenced after the acquire in `t_2` — **regardless of memory-order attributes on `A` or `B`**. Memory-order attributes weaken only same-location atomic visibility outside key transfer and the implementation's reordering latitude inside a held-key body; they MUST NOT weaken clause 1 `sw` edges (this restates 21555 in model terms).

The three informal constraints currently at 21574–21578 are deleted; `Fence(O)` ordering events map to clauses 4–5.

---

## Part 7 — B6: Small-step expression relation (new content for §8.4)

§8.4 currently quantifies over an undefined `e → e'`. The proposal defines a focusing machine over the existing big-step machinery and restates the properties over it. The statement-level machine (`ExecState`, 19389) and the key/region/frame `Step-Exec-*` rules are reused unchanged.

### New §8.4 prelude — "The Step Relation"

```text
Frame = ⟨ctor, vs, es⟩
  where `ctor` is an expression constructor of arity |vs| + 1 + |es|,
        `vs` are already-evaluated operand values (left of the hole),
        `es` are pending operand expressions (right of the hole),
        and operand order is Children_LTR (§24.7.7).
ScopeFrame = the block, key, region, and frame configurations of ExecState (§18.1.5, §18.7.5, §18.8.5, §19.2.5)

K = [Frame | ScopeFrame]    (continuation stack, innermost first)
Config = ⟨Focus, K, σ⟩    Focus ∈ Expr ∪ {Done(out)}    out ∈ {Val(v), Ctrl(κ)}

**(Step-Focus-Down)**
e = ctor(e_1, …, e_n)    n ≥ 1    ¬ Redex(e)    e_1 not a value
──────────────────────────────────────────────────────────────────────────
⟨e, K, σ⟩ → ⟨e_1, ⟨ctor, [], [e_2, …, e_n]⟩ :: K, σ⟩

**(Step-Focus-Next)**
──────────────────────────────────────────────────────────────────────────────────────────
⟨Done(Val(v)), ⟨ctor, vs, [e] ++ es⟩ :: K, σ⟩ → ⟨e, ⟨ctor, vs ++ [v], es⟩ :: K, σ⟩

**(Step-Redex)**
Γ ⊢ EvalSigmaBase(ctor(v_1, …, v_n), σ) ⇓ (out, σ')
──────────────────────────────────────────────────────────────────────────
⟨Done(Val(v_n)), ⟨ctor, [v_1, …, v_{n-1}], []⟩ :: K, σ⟩ → ⟨Done(out), K, σ'⟩

**(Step-Ctrl-Unwind)**
F is a Frame (not a ScopeFrame)
──────────────────────────────────────────────────────────────
⟨Done(Ctrl(κ)), F :: K, σ⟩ → ⟨Done(Ctrl(κ)), K, σ⟩
```

`Redex(e)` holds when every operand position of `e` is a value or `e` is a leaf (literals, names). `EvalSigmaBase` denotes exactly the base-case `EvalSigma`/`ExecSigma` rules — those whose premises contain no recursive `EvalSigma` on subexpressions (e.g. `EvalSigma-Index`, `EvalSigma-Index-OOB`, `UnOp`/`BinOp` application, `MatchPattern` selection). Composite forms with scoped or short-circuit evaluation (blocks, `if`, loops, calls, key/region/frame statements, `defer`, pattern dispatch) do not use `Step-Focus-Down`; they push their existing scope configurations: calls push the callee body block via `BlockEnter`, and the `Step-Exec-*` rules of §§18–19 are imported as steps over `ScopeFrame`s. `Ctrl(κ)` propagates by `Step-Ctrl-Unwind` through value frames and is intercepted by the `ScopeFrame` whose existing rules handle it (loop frames absorb `Break`/`Continue`; cleanup runs per `Step-Exec-*-Exit-Ctrl`).

```text
⟨e, σ⟩ →* (out, σ') ⇔ ⟨e, [], σ⟩ →* ⟨Done(out), [], σ'⟩
```

**Coherence (required metatheorem).** `Γ ⊢ EvalSigma(e, σ) ⇓ (out, σ')` ⇔ `⟨e, σ⟩ →* (out, σ')`.

### Restated properties

Config typing: `Γ; R; L ⊢ ⟨e, K, σ⟩ : T` holds when `e : T_e` and `K` is a well-typed continuation from `T_e` to `T` (each frame's hole type matches; ScopeFrames type per their owning chapters).

- **(Progress)** If `Γ; R; L ⊢ C : T` and `C` is not `⟨Done(out), [], σ⟩`, then `C → C'` for some `C'`, or `C` is blocked on a host primitive (§6.2) or a key acquisition (§19.2.5).
- **(Preservation)** If `Γ; R; L ⊢ C : T` and `C → C'`, then `Γ; R; L ⊢ C' : T`.

The remaining §8.4 properties are restated over `→` where they mention steps and are otherwise unchanged. The §8.4 sentence "Formal proofs are deferred to supplementary materials" is retained.

---

## Part 8 — B7: User-extendable capability classes

Ruling: capability classes are ordinary classes that users may extend and instantiate; user-defined capabilities are admitted.

### §6.1.1 replacements

Replace the closed `CapabilityClass` set (3148) and the nine enumerated `CapInType(TypeDynamic([...]))` lines (3165–3173) with:

```text
BuiltinCapabilityClass = {IO, Network, HeapAllocator, Reactor, ExecutionDomain, System, Time, MonotonicTime, WallTime}

CapClass(p) ⇔ p ∈ BuiltinCapabilityClass
            ∨ (ClassDecl(p) = C ∧ ∃ B ∈ SuperclassPaths(C). CapClass(B))

CapabilityClass = { p | CapClass(p) }

CapInType(TypeDynamic([p])) = {p}    if CapClass(p)
CapInType(TypeDynamic([p])) = ∅      otherwise
```

`CapInType(TypePath([Context]))` (3164) and the structural-distribution clauses (3174–3180) are unchanged; structural distribution covers records that implement a capability class and hold capability fields.

Replace `CapClosure` (3151, 3154–3162) and `EffectiveCaps` (3184) with:

```text
CapUp(c) = {c} ∪ ⋃{ CapUp(B) | ClassDecl(c) = C ∧ B ∈ SuperclassPaths(C) ∧ CapClass(B) }

CapDerive(c) = {c} ∪ DeriveSet(c)
DeriveSet(Time) = {MonotonicTime, WallTime}
DeriveSet(c) = ∅    for the remaining built-in capability classes
DeriveSet(c) = ⋃{ CapDerive(c') | c' ∈ CapResultClasses(c) }    for user capability classes
CapResultClasses(c) = { p | CapClass(p) ∧ TypeDynamic([p]) occurs in a method result type of ClassDecl(c) }

EffectiveCaps(T) = ⋃{ CapUp(c) ∪ CapDerive(c) | c ∈ CapInType(T) }
```

`CapUp` makes a derived capability satisfy requirements stated against its capability ancestors (call gating, §6.1.2 NAA-3 condition `CapReq(d_tgt) ⊆ EffectiveCapReq(d_src)` is unchanged). `CapDerive` generalizes the old `CapClosure(Time)` special case: possession grants what the class's own interface can mint. Both close under the least-fixed-point/memoization language already at 3182.

### New requirement — append to §6.1.2

**(NAA-4) User capabilities confer no new root authority.** Constructing a value of a user-defined capability class requires no ambient grant. A user-defined capability class confers authority only through the built-in capability values it encapsulates; every externally observable effect remains gated by NAA-3. Declaring a class a capability class (by extending one) subjects its values to attenuation requirements (§6.1.3) with respect to the capability values they encapsulate.

### Consequential edits

- §1.1 `CapConstructs` (229): replace the fixed name set with `{ c | CapClass(c) ∧ … }` keeping the `Context` clause.
- §14.9.3 (13799): replace the closed `CapClass = {…}` set with a reference to §6.1.1's open `CapabilityClass`; add prose: "User classes that declare a capability superclass via `<:` are capability classes. The built-in interfaces below are the root capability classes."
- §14.9.4: add the well-formedness sentence: "A class is a capability class iff `CapClass` holds; capability classhood is determined by the superclass relation alone and MUST NOT depend on attributes or naming."

---

## Part 9 — B8: Comptime `ConstLen` + cross-module comptime restriction

### B9.1 §8.1 — extend `ConstLen`

Insert after `ConstLen-Path` (5906–5908):

```text
CtPureEnv(Γ) = the compile-time environment of §22.1.5 with no capability bindings and no emission rights
Φ_pure = ⟨files: ∅, root: ⊥, [], [], 0⟩

**(ConstLen-Comptime)**
e ∉ Literal forms of ConstLen-Lit    e ∉ Path forms of ConstLen-Path
Γ ⊢ CtEval(CtPureEnv(Γ), Φ_pure, e) ⇓ (CtPrim(n), _, Φ_1)    CtPendingEmits(Φ_1) = []    n ∈ ℕ    InRange(n, "usize")
──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
Γ ⊢ ConstLen(e) ⇓ n
```

Array-length expressions thereby admit any pure, capability-free, emission-free compile-time-evaluable expression (arithmetic, `comptime` procedure calls, generic const parameters). Evaluation occurs during Phase 3 over the Phase-2-expanded module set; any `CtBuiltinCall` that requires capabilities or emission leaves `CtEval` undefined, so `ConstLen-Err` (5910) applies unchanged. `E-TYP-1810`'s condition text now matches the rules.

### B9.2 §22.1.4 — cross-module Phase 2 dependency rejection

Add to §22.1.4 Static Semantics:

```text
EmittedInPhase2(d) ⇔ d was appended by a CtPendingEmits transfer during CtExecModuleSeq (§22.1.5)

**(CtExpand-CrossModule-Emit-Err)**
comptime form f occurs in module M    Γ ⊢ CtExpand resolution of a name in f targets declaration d in module M'
M' ≠ M    EmittedInPhase2(d)    c = Code(CtExpand-CrossModule-Emit-Err)
──────────────────────────────────────────────────────────────────────────────────────────────────────────────────
Γ ⊢ CtExpandItem(Ξ, Φ, f) ⇑ c
```

Normative statement: "Phase 2 execution MUST NOT depend on declarations emitted by Phase 2 execution of a different module. Phase-1 (source-present) declarations of other modules MAY be referenced. Cross-module comptime emission dependencies are unsupported and rejected."

§22.6 diagnostics table — add row: `E-CTE-0090` | Error | Compile-time | Compile-time form depends on a declaration emitted by Phase 2 execution of another module (`CtExpand-CrossModule-Emit-Err`).

§1.5 line 348 — replace "in deterministic dependency order" with "in the deterministic module order defined by §22.1.5".

---

## Application order and verification plan

1. A18/A1–A9 (text and grammar mechanical fixes) — verify by re-running the review tooling: nonterminal def/use, reachability from `source_file`, zero `where`-invariants.
2. A10–A15, A19 (rule statement repairs) — verify: zero duplicate labels with differing bodies; no premise-less rules with free conclusion metavariables.
3. B1 (soundness) — verify: no occurrence of `CaseVariants`/`CaseStates`/`PatternMayMatchType`; `EvalIfCases-None` concludes `Ctrl(Panic)`; `PanicCode` total over the extended `PanicReason`.
4. B2 (authored rules) + A5/A6/B2.5 grammar reconciliation — verify: every rule name referenced in §§19–20 prose resolves to a stated rule; option grammars match `ParallelOpt`/`SpawnOpt`/`DispatchOpt` constructors.
5. B3, B4 — verify: solver has exactly one applicable rule per constraint shape (spot-check tuple/union/refine pairs).
6. B5, B6 — internal consistency read-through; confirm §8.4 references only defined relations.
7. B7, B8 — verify: `CapClosure` no longer referenced (replaced by `CapUp`/`CapDerive`); `E-CTE-0090` indexed in Appendix A.
8. A12, A16, A17, A20 (bulk mechanical) last, then regenerate Appendix A and re-run all checks.

Open points needing a one-word ruling before application: A4 (parenthesized types in `non_union_type`?), A5 (`coarsened_path` delete?), A15 (move variance error codes to §14.11?), B2.1 (marked index segments?), B2.3 (shared option-list schema vs. spelling out ~48 rules?).
