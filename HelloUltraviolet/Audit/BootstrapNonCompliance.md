# Bootstrap Non-Compliance Ledger

This ledger records places where `HelloUltraviolet` uses spec-valid source and
the current bootstrap compiler rejects, misparses, mischecks, mislowers, or
miscompiles that source.

## UVBOOT-0001: Single `if ... is` Identifier Pattern

Status: repaired in the workspace bootstrap and verified by
`Cursive.exe build HelloUltraviolet --check --target-profile x86_64-win64`.

Reference source:

- `Source/Reference/Patterns/BasicPatterns.uv`

Spec obligations exercised:

- `rule.16.Parse-If-Is-Single`
- `rule.17.Parse-Pattern-Identifier`
- `Pat-Ident-R`
- `Match-Ident`

Spec basis:

- `SPECIFICATION.md:16780` defines single-case `if ... is pattern block_expr`.
- `SPECIFICATION.md:17916` defines identifier patterns.
- `SPECIFICATION.md:17963` binds an identifier pattern to the scrutinee type.
- `SPECIFICATION.md:17993` binds an identifier pattern to the matched value.

Spec-valid specimen:

```ultraviolet
let identifier_hit: bool = if 7 is bound_value {
    bound_value == 7
} else {
    false
}
```

Observed bootstrap result:

```text
./LLVMBootstrap/cursive/build/windows/Release/Cursive.exe build HelloUltraviolet --check --target-profile x86_64-win64

error[E-SRC-0520]: Generic syntax error (unexpected token)
  --> C:/Dev/Ultraviolet/HelloUltraviolet/Source/Reference/Patterns/BasicPatterns.uv:16:21
16 |         bound_value == 7
16 |                     ^^
```

Bootstrap owner:

- `LLVMBootstrap/cursive/src/02_source/parser/expr/if_expr.cpp`
- `LLVMBootstrap/cursive/src/02_source/parser/pattern/pattern_common.cpp`

Failure analysis:

`ParseIfHeadNoElse` routes `Parse-If-Is-Single` through
`ParseIfCaseClause`. `ParseIfCaseClause` first calls `ParsePattern` on the
tokens after `is`. For `bound_value { ... }`, `ParsePatternAtom` sees an
identifier followed by `{` and takes the record-pattern branch before it reaches
the identifier-pattern fallback. The parser therefore treats the then-block as a
record-pattern payload and reports syntax errors inside the block body.

Required bootstrap behavior:

For single `if ... is`, after `is` and when the next token is not `{`, the
parser must honor the spec production as `ParsePattern` followed by `ParseBlock`.
An identifier pattern immediately followed by the then-block brace must remain
an `IdentifierPattern`, not a `RecordPattern`.

Repair:

- `LLVMBootstrap/cursive/src/02_source/parser/expr/if_expr.cpp` now keeps the
  full pattern parse first and then falls back to the spec's identifier-pattern
  form when the full parse does not leave the parser positioned at a block
  brace.

## UVBOOT-0002: Zero-Sized Transition Receiver Argument Shift

Status: repaired in the workspace bootstrap and verified by:

```text
./LLVMBootstrap/cursive/build/windows/Release/cursive_codegen_abi_sret_test.exe
./LLVMBootstrap/cursive/build/windows/Release/Cursive.exe build HelloUltraviolet --check --target-profile x86_64-win64 --incremental off
./LLVMBootstrap/cursive/build/windows/Release/Cursive.exe build HelloUltraviolet --target-profile x86_64-win64 --incremental off
./HelloUltraviolet/build/bin/HelloUltraviolet.exe
```

Reference source:

- `Source/Reference/ModalTypes/Transitions.uv`

Spec obligations exercised:

- `T-Modal-Transition`
- `ApplyTransitionSigma`
- `ValidateModalState`
- `Lowering` for modal transitions

Spec basis:

- `SPECIFICATION.md:11242` types a transition call on a unique source state as
  the target state.
- `SPECIFICATION.md:11274` states that modal transitions consume the source
  state value and produce a target state value.
- `SPECIFICATION.md:11284` extracts the transition body's returned value.
- `SPECIFICATION.md:11288` validates that the returned value is the target
  modal state.
- `SPECIFICATION.md:11292` states that transition lowering returns a fresh
  target-state value constructed by the transition body.

Spec-valid specimen:

```ultraviolet
let closed: unique ModalReference@Closed = ModalReference@Closed {}
let opened: ModalReference@Open = closed~>open(23)
return opened.value == 23
```

Observed bootstrap result:

```text
./HelloUltraviolet/build/bin/HelloUltraviolet.exe

reference failed: runModalTypesTransitionsReference
exit=1
```

Bootstrap owner:

- `LLVMBootstrap/cursive/src/05_codegen/llvm/llvm_call.cpp`

Failure analysis:

The transition receiver `ModalReference@Closed` has zero runtime size, so the
lowered callee ABI elides that parameter. `EmitABICall` skipped ABI parameters
with no LLVM parameter index without advancing the source-argument cursor. The
next source argument, `value`, was therefore looked up at the receiver's source
argument index and the transition body received the wrong value.

Required bootstrap behavior:

When a source parameter is zero-sized and elided from the LLVM ABI, the caller
must still advance the source-argument cursor for that source parameter. Hidden
panic-out parameters remain excluded from the source-argument cursor.

Repair:

- `LLVMBootstrap/cursive/src/05_codegen/llvm/llvm_call.cpp` now advances the
  source-argument cursor when a non-hidden parameter is elided from the lowered
  LLVM call signature.
- `LLVMBootstrap/cursive/src/tests/codegen_abi_sret_test.cpp` includes a
  regression fixture with a zero-sized modal transition receiver and a real
  transition argument.

## UVBOOT-0003: Typed Union Case Payload Binding

Status: repaired in the workspace bootstrap and verified by:

```text
./LLVMBootstrap/cursive/build/windows/Release/cursive_codegen_abi_sret_test.exe
./LLVMBootstrap/cursive/build/windows/Release/Cursive.exe build HelloUltraviolet --check --target-profile x86_64-win64 --incremental off --build-progress off
./LLVMBootstrap/cursive/build/windows/Release/Cursive.exe build HelloUltraviolet --target-profile x86_64-win64 --incremental off --build-progress off
./HelloUltraviolet/build/bin/HelloUltraviolet.exe
```

Reference source:

- `Source/Reference/DataTypes/Unions.uv`

Spec obligations exercised:

- `PatternNarrow-Union`
- `CaseScope-Narrow`
- `EvalIfCase-Hit`
- `Lower-Pat-General`
- `T-IfCase-Union`

Spec basis:

- `SPECIFICATION.md:18490` narrows union patterns by matching the case pattern
  against each union member type.
- `SPECIFICATION.md:18495` refines the scrutinee binding and introduces the
  pattern bindings for the selected case scope.
- `SPECIFICATION.md:18515` evaluates a matching case by binding the pattern's
  matched values before executing the body.
- `SPECIFICATION.md:18580` lowers pattern binding from `MatchPattern`,
  `BindOrder`, and `LowerBindList`.
- `SPECIFICATION.md:18654` types exhaustive union case analysis over the union
  member set.

Spec-valid specimen:

```ultraviolet
public type DataUnionReference = i32 | bool

let numeric: DataUnionReference = numericUnionReference()
let boolean: DataUnionReference = booleanUnionReference()
let numeric_ok: bool = if numeric is {
    value: i32 { value == 9 }
    value: bool { value == false }
}
let boolean_ok: bool = if boolean is {
    value: i32 { value == 0 }
    value: bool { value == true }
}
return numeric_ok && boolean_ok
```

Observed bootstrap result:

```text
./HelloUltraviolet/build/bin/HelloUltraviolet.exe

reference failed: runDataTypesUnionsReference
exit=1
```

The lowered IR for each typed case bound the whole union value:

```text
bind %value = %numeric
bind %value = %boolean
```

Bootstrap owner:

- `LLVMBootstrap/cursive/src/05_codegen/lower/expr/if_case_expr.cpp`
- `LLVMBootstrap/cursive/src/05_codegen/lower/pattern/pattern_common.cpp`
- `LLVMBootstrap/cursive/src/05_codegen/lower/pattern/typed_pattern.cpp`

Failure analysis:

Typed union cases were accepted by static analysis, but lowering did not bind
the selected member payload. Two lowering defects combined:

- `LowerIfCaseClauseImpl` refined the scrutinee binding before
  `LowerBindPattern`, so the binder saw the scrutinee as the narrowed member
  type instead of the original union representation.
- `ResolvePatternAliasType` and typed-pattern binding logic did not resolve
  single-segment type aliases through the current module scope, so aliases such
  as `DataUnionReference` did not expose their `i32 | bool` union body to
  pattern binding.

Required bootstrap behavior:

Typed union case labels must discriminate on the union member and bind the
selected member payload to the typed pattern variable. Scrutinee narrowing is
available to the arm body, while payload extraction is lowered from the original
union value.

Repair:

- `LLVMBootstrap/cursive/src/05_codegen/lower/expr/if_case_expr.cpp` now runs
  pattern binding before branch-local scrutinee refinement.
- `LLVMBootstrap/cursive/src/05_codegen/lower/pattern/pattern_common.cpp` now
  resolves single-segment type aliases through the current module scope and
  resolves aliases on both the typed pattern annotation and the scrutinee type
  before selecting a union member payload.
- `LLVMBootstrap/cursive/src/05_codegen/lower/pattern/typed_pattern.cpp` now
  uses the same current-module alias lookup for typed-pattern lowering.
- `LLVMBootstrap/cursive/src/tests/codegen_abi_sret_test.cpp` includes a
  regression fixture with `PrimitiveUnion = i32 | bool`; it exits `3` if typed
  union case payload binding fails.

## UVBOOT-0004: Conditional Loop Value Result Type

Status: repaired in the workspace bootstrap and verified by:

```text
./LLVMBootstrap/cursive/build/windows/Release/cursive_codegen_abi_sret_test.exe
```

Reference source:

- `Source/Reference/Expressions/Control.uv`

Spec obligations exercised:

- `LoopTypeFin`
- `T-Loop-Conditional`
- `EvalSigma-Loop-Cond-Break`

Spec basis:

- `SPECIFICATION.md:16764` defines `loop` expressions with an optional
  condition.
- `SPECIFICATION.md:16815` defines `LoopTypeFin`: when all breaks carry a
  value and `ResType(Brk) = T`, the conditional loop expression has type `T`.
- `SPECIFICATION.md:16870` applies `LoopTypeFin` to conditional-loop typing.
- `SPECIFICATION.md:16997` evaluates a conditional loop value break as the
  loop expression value.

Spec-valid specimen:

```ultraviolet
let loop_value: i32 = loop true {
    break case_value + 4
}
```

Observed bootstrap result:

```text
./LLVMBootstrap/cursive/build/windows/Release/Cursive.exe build HelloUltraviolet --check --target-profile x86_64-win64 --incremental off --build-progress off

error[E-MOD-2402]: Type annotation incompatible with inferred type
  --> C:/Dev/Ultraviolet/HelloUltraviolet/Source/Reference/Expressions/Control.uv:18:5
18 |     let loop_value: i32 = loop true {
18 |     ^
```

Bootstrap owner:

- `LLVMBootstrap/cursive/src/04_analysis/typing/expr/loop_conditional.cpp`
- `LLVMBootstrap/cursive/src/04_analysis/typing/expr/loop_infinite.cpp`

Failure analysis:

`TypeLoopConditionalExpr` computed `LoopTypeFin` by unioning value-break types
with `()`, so a loop that only breaks with `i32` synthesized `i32 | ()` instead
of `i32`. The expression-specific infinite-loop helper had the corresponding
void/value union behavior for `LoopTypeInf`, while the statement-block helper
already followed the spec rule shape.

Required bootstrap behavior:

For conditional loops, no value breaks produce `()`. Value-only breaks produce
the common break value type. Mixed value and void breaks, or incompatible value
break types, fail `LoopTypeFin`.

Repair:

- `LLVMBootstrap/cursive/src/04_analysis/typing/expr/loop_conditional.cpp` now
  computes `LoopTypeFin` with type-equivalence checks and returns the common
  break value type for value-only breaks.
- `LLVMBootstrap/cursive/src/04_analysis/typing/expr/loop_infinite.cpp` now
  computes `LoopTypeInf` with the same void/value discipline.
- `LLVMBootstrap/cursive/src/tests/codegen_abi_sret_test.cpp` includes a
  regression fixture that assigns `loop true { break 5 }` to `i32`; it exits
  `4` if the loop result value is wrong.

## UVBOOT-0005: Non-Capturing Closure Lowering Uses Moved Return Type

Status: repaired in the workspace bootstrap and verified by:

```text
./LLVMBootstrap/cursive/build/windows/Release/cursive_codegen_abi_sret_test.exe
```

Reference source:

- `Source/Reference/Expressions/ClosuresAndPipelines.uv`

Spec obligations exercised:

- `T-Closure-NonCapturing`
- `Lower-Expr-Closure-NonCapturing`
- `EvalSigma-Closure-NonCapturing`
- `EvalSigma-ClosureCall`
- `T-Pipeline`

Spec basis:

- `SPECIFICATION.md:17538` types a non-capturing closure as a function type.
- `SPECIFICATION.md:17792` lowers a non-capturing closure to a callable symbol
  with a null closure environment.
- `SPECIFICATION.md:17797` defines the non-capturing closure code symbol.
- `SPECIFICATION.md:17813` applies a closure call using the closure code and
  argument values.
- `SPECIFICATION.md:17652` types a pipeline expression over a function or
  closure callee.

Spec-valid specimen:

```ultraviolet
let non_capturing = |value: i32| -> i32 value + 1
let empty = || 5
let call_ok: bool = non_capturing(4) == 5
let empty_ok: bool = empty() == 5
let pipeline_value: i32 = 8 => non_capturing
return call_ok && empty_ok && pipeline_value == 9
```

Observed bootstrap result:

```text
./LLVMBootstrap/cursive/build/windows/Release/Cursive.exe build HelloUltraviolet --target-profile x86_64-win64 --incremental off --build-progress off

fatal: Unhandled structured exception. (ACCESS_VIOLATION, 0xC0000005)
```

Debug stack excerpt:

```text
cursive::analysis::AppendTypeString
cursive::analysis::TypeToString
cursive::codegen::ScopedLLVMTypeQuery::ScopedLLVMTypeQuery
cursive::codegen::LLVMEmitter::GetLLVMType
cursive::codegen::LLVMEmitter::EmitBindVar
```

Bootstrap owner:

- `LLVMBootstrap/cursive/src/05_codegen/lower/expr/closure_expr.cpp`

Failure analysis:

The non-capturing closure lowering path generated a `ProcIR`, computed the
closure code procedure return type, queued the procedure by moving the `ProcIR`,
then registered the closure symbol value type using `proc.ret` after the move.
That produced a `TypeFunc` with a null return type. LLVM emission crashed while
printing the function type for a local binding.

Required bootstrap behavior:

The lowered non-capturing closure value must retain the generated procedure's
return type when registering the symbol's function type. Moving the queued
procedure must not invalidate the type recorded for the expression result.

Repair:

- `LLVMBootstrap/cursive/src/05_codegen/lower/expr/closure_expr.cpp` now stores
  the non-capturing closure result return type before queueing the generated
  procedure and uses that retained type when registering the result symbol.
- `LLVMBootstrap/cursive/src/tests/codegen_abi_sret_test.cpp` includes a
  regression fixture with non-capturing and empty closures; it exits `5` if
  construction or calls fail.

## UVBOOT-0006: Captured Closure Environment and Call Lowering

Status: repaired in the workspace bootstrap and verified by:

```text
./LLVMBootstrap/cursive/build/windows/Release/cursive_codegen_abi_sret_test.exe
```

Reference source:

- `Source/Reference/Expressions/ClosuresAndPipelines.uv`

Spec obligations exercised:

- `T-Closure-Capturing`
- `EvalSigma-Closure-Capturing`
- `EvalSigma-ClosureCall`
- `Lower-Expr-Closure-Capturing`
- `Lower-Closure-Call`

Spec basis:

- `SPECIFICATION.md:17545` types a local captured closure as `TypeClosure`.
- `SPECIFICATION.md:17715` evaluates a captured closure by building and storing
  a capture environment.
- `SPECIFICATION.md:17787` defines closure environment layout from the captured
  fields.
- `SPECIFICATION.md:17802` lowers captured closures by lowering the capture
  environment and returning `ClosureVal(env_ptr, sym)`.
- `SPECIFICATION.md:17813` applies closure calls by binding the environment and
  then the call arguments.

Spec-valid specimen:

```ultraviolet
let base_value: i32 = 3
let capturing = |value: i32| -> i32 value + base_value
return capturing(4) == 7
```

Observed bootstrap result:

```text
./HelloUltraviolet/build/bin/HelloUltraviolet.exe

fatal: Unhandled structured exception. (ACCESS_VIOLATION, 0xC0000005)
stacktrace:
  [0] HelloUltraviolet!HelloUltraviolet::Reference::Expressions:x3arunExpressionsClosuresAndPipelinesReference
```

Bootstrap owner:

- `LLVMBootstrap/cursive/src/05_codegen/lower/expr/call.cpp`
- `LLVMBootstrap/cursive/src/05_codegen/lower/expr/closure_expr.cpp`
- `LLVMBootstrap/cursive/src/05_codegen/llvm/emit/ir/value/evaluate.cpp`

Failure analysis:

Captured closure calls were lowered through the ordinary direct-call path on
the closure-pair local instead of the closure-call path that extracts
`env_ptr` and `code_ptr`. After routing through closure-call lowering, the
captured closure still stored a null environment pointer when no active region
alias existed: closure lowering emitted `IRAlloc`, and LLVM allocation emission
defaults `IRAlloc` to null without an active region. The generated LLVM stored
the captured `base_value` address into `ptr null`.

Required bootstrap behavior:

Captured closure calls must lower through `LowerClosureCall`, passing the
environment pointer followed by source arguments and the hidden panic-out
argument. Captured closure environments must materialize valid storage for the
closure lifetime whether or not the source is currently inside an explicit
region allocation context.

Repair:

- `LLVMBootstrap/cursive/src/05_codegen/lower/expr/call.cpp` now routes
  `TypeClosure` callees through `LowerClosureCall`.
- `LLVMBootstrap/cursive/src/05_codegen/llvm/emit/ir/value/evaluate.cpp` now
  declares queued procedure symbols from registered proc signatures when a
  closure value materializes its code pointer before the extra procedure body
  is emitted.
- `LLVMBootstrap/cursive/src/05_codegen/lower/expr/closure_expr.cpp` now uses a
  stack-backed closure environment when no active region alias is available and
  keeps region allocation for active-region contexts.
- `LLVMBootstrap/cursive/src/tests/codegen_abi_sret_test.cpp` includes a
  captured-closure regression; it exits `6` if the captured closure call fails.

## UVBOOT-0007: Rule Diagnostic IDs Escaping Typecheck Output

Status: repaired in the workspace bootstrap and verified by the rejected-source
fixture matrix.

Rejected reference sources:

- `Fixtures/RejectedSource/Statements/BreakOutsideLoop/Source/Main.uv`
- `Fixtures/RejectedSource/Statements/ContinueOutsideLoop/Source/Main.uv`
- `Fixtures/RejectedSource/Expressions/CallNonFunction/Source/Main.uv`

Spec obligations exercised:

- `rule.18.Break-Outside-Loop`
- `rule.18.Continue-Outside-Loop`
- `rule.16.Call-Callee-NotFunc`
- `diag.18.StatementDiagnosticsSupplement`
- `diag.16.CallExpressions`

Spec basis:

- `SPECIFICATION.md:19997` assigns `E-SEM-3162` to `break` outside `loop`.
- `SPECIFICATION.md:19998` assigns `E-SEM-3163` to `continue` outside `loop`.
- `SPECIFICATION.md:17878` assigns `E-SEM-2531` to non-function callees.

Spec-invalid specimens:

```ultraviolet
public procedure breakOutsideLoopReference() -> i32 {
    break
    return 0
}
```

```ultraviolet
public procedure continueOutsideLoopReference() -> i32 {
    continue
    return 0
}
```

```ultraviolet
public procedure callNonFunctionReference() -> i32 {
    let value: i32 = 3
    return value()
}
```

Observed bootstrap result:

```text
error: Internal error: unknown diagnostic id 'Break-Outside-Loop'
error: Internal error: unknown diagnostic id 'Continue-Outside-Loop'
error: Internal error: unknown diagnostic id 'Call-Callee-NotFunc'
```

Bootstrap owner:

- `LLVMBootstrap/cursive/src/04_analysis/typing/stmt/break_stmt.cpp`
- `LLVMBootstrap/cursive/src/04_analysis/typing/stmt/continue_stmt.cpp`
- `LLVMBootstrap/cursive/src/04_analysis/memory/calls.cpp`

Failure analysis:

The affected typecheck paths correctly rejected the invalid source, but they
returned rule identifiers as diagnostic ids. `BuildResolvedTypecheckDiagnostic`
could not map those rule names to spec diagnostic codes, so user-facing output
became an internal compiler error instead of the required SPEC code.

Required bootstrap behavior:

Rejected source must report the diagnostic code assigned by the owning SPEC
diagnostic table. Rule labels may still be recorded for SPEC tracing, but the
diagnostic id passed to the emitter must resolve to `E-SEM-3162`,
`E-SEM-3163`, or `E-SEM-2531` for these cases.

Repair:

- `break_stmt.cpp` now returns `E-SEM-3162` for `Break-Outside-Loop`.
- `continue_stmt.cpp` now returns `E-SEM-3163` for `Continue-Outside-Loop`.
- `calls.cpp` now returns `E-SEM-2531` for `Call-Callee-NotFunc`.

## UVBOOT-0008: Call Argument Type Mismatch Reported Generic Check Code

Status: repaired in the workspace bootstrap and verified by the
`CallArgType` rejected-source fixture.

Rejected reference source:

- `Fixtures/RejectedSource/Expressions/CallArgType/Source/Main.uv`

Spec obligations exercised:

- `rule.16.Call-ArgType-Err`
- `diag.16.CallExpressions`

Spec basis:

- `SPECIFICATION.md:15998` defines `Call-ArgType-Err` for arguments whose type
  is not compatible with the parameter type.
- `SPECIFICATION.md:17880` assigns `E-SEM-2533` to argument type incompatibility
  with a parameter type.

Spec-invalid specimen:

```ultraviolet
public procedure oneArg(value: i32) -> i32 {
    return value
}

public procedure callArgTypeReference() -> i32 {
    return oneArg(true)
}
```

Observed bootstrap result:

```text
error[E-SEM-2526]: Expression type incompatible with expected type
```

Bootstrap owner:

- `LLVMBootstrap/cursive/src/04_analysis/memory/calls.cpp`

Failure analysis:

The call checker pre-checked non-provenance arguments against the expected
parameter type. When that contextual check failed with the generic expected-type
mismatch diagnostic, the call checker returned `E-SEM-2526` before reaching the
call-specific argument compatibility rule.

Required bootstrap behavior:

When a call argument is well-formed as an expression but incompatible with the
formal parameter type, the call expression checker must report `E-SEM-2533`.
Nested expression failures still propagate their own diagnostic ids.

Repair:

- `calls.cpp` now maps contextual expected-type mismatch results in argument
  checking to `E-SEM-2533` while preserving non-mismatch diagnostics.

## UVBOOT-0009: Statement Contextual Checks Reported Generic Type Mismatch

Status: repaired in the workspace bootstrap and verified by rejected-source
statement fixtures.

Rejected reference sources:

- `Fixtures/RejectedSource/Statements/AssignTypeMismatch/Source/Main.uv`
- `Fixtures/RejectedSource/Statements/ReturnTypeMismatch/Source/Main.uv`
- `Fixtures/RejectedSource/Statements/DeferNonUnit/Source/Main.uv`
- `Fixtures/RejectedSource/Statements/DeferNonLocal/Source/Main.uv`

Spec obligations exercised:

- `rule.18.Assign-Type-Err`
- `Return-Type-Err`
- `rule.18.Defer-NonUnit-Err`
- `Defer-NonLocal-Err`
- `diag.18.StatementDiagnosticsSupplement`

Spec basis:

- `SPECIFICATION.md:19346` defines `Assign-Type-Err` for assignment value
  types incompatible with the target place type.
- `SPECIFICATION.md:19815` checks returned values against the body return type,
  and `SPECIFICATION.md:20005` assigns `E-SEM-3161` to return type mismatch.
- `SPECIFICATION.md:19491` defines `Defer-NonUnit-Err`, and
  `SPECIFICATION.md:20003` assigns `E-SEM-3151`.
- `SPECIFICATION.md:19496` defines `Defer-NonLocal-Err`, and
  `SPECIFICATION.md:20004` assigns `E-SEM-3152`.

Spec-invalid specimens:

```ultraviolet
public procedure assignTypeMismatchReference() -> i32 {
    var value: i32 = 1
    value = true
    return value
}
```

```ultraviolet
public procedure returnTypeMismatchReference() -> i32 {
    return true
}
```

```ultraviolet
public procedure deferNonUnitReference() -> i32 {
    defer {
        1
    }
    return 0
}
```

Observed bootstrap result:

```text
error[E-SEM-2526]: Expression type incompatible with expected type
```

Bootstrap owner:

- `LLVMBootstrap/cursive/src/04_analysis/typing/stmt/assign_stmt.cpp`
- `LLVMBootstrap/cursive/src/04_analysis/typing/stmt/return_stmt.cpp`
- `LLVMBootstrap/cursive/src/04_analysis/typing/stmt/defer_stmt.cpp`

Failure analysis:

The affected statement checkers used contextual expression checking and
propagated the generic expected-type mismatch diagnostic before translating the
failure through the statement-specific rule that owns the diagnostic.
`Defer-NonLocal-Err` also returned its rule label instead of the assigned
statement diagnostic code.

Required bootstrap behavior:

Statement-level contextual type failures must report the diagnostic code
assigned to the owning statement rule: `E-SEM-3133`, `E-SEM-3161`, or
`E-SEM-3151`. Non-local control flow in a defer block must report
`E-SEM-3152`.

Repair:

- `assign_stmt.cpp` now maps generic expected-type mismatch to `E-SEM-3133`.
- `return_stmt.cpp` now maps generic expected-type mismatch to `E-SEM-3161`.
- `defer_stmt.cpp` now maps generic expected-type mismatch to `E-SEM-3151` and
  non-local defer control to `E-SEM-3152`.

## UVBOOT-0010: Missing Procedure Return Annotation Has No Emitted Code

Status: repaired in the workspace bootstrap and verified by the
`MissingReturnType` rejected-source fixture.

Rejected reference source:

- `Fixtures/RejectedSource/Procedures/MissingReturnType/Source/Main.uv`

Spec obligations involved:

- `rule.15.WF-ProcedureDecl-MissingReturnType`
- `diag.15.ProcedureContractAndEntryDiagnosticsSupplement`

Spec basis:

- `SPECIFICATION.md:14135` requires a diagnostic when a procedure declaration
  omits an explicit return annotation.
- `SPECIFICATION.md:14305` says procedure diagnostics include missing explicit
  return annotations.

Spec-invalid specimen:

```ultraviolet
public procedure missingReturnTypeReference() {
    return 1
}
```

Previous bootstrap result:

```text
error: Internal error: unknown diagnostic id 'WF-ProcedureDecl-MissingReturnType'
```

Repaired bootstrap result:

```text
error: Static rule failed without assigned diagnostic code: WF-ProcedureDecl-MissingReturnType
```

Bootstrap owner:

- `LLVMBootstrap/cursive/src/04_analysis/typing/item/procedure_decl.cpp`
- diagnostic code selection and mapping for
  `WF-ProcedureDecl-MissingReturnType`

Failure analysis:

The procedure declaration checker correctly recognizes the missing return
annotation rule, but it returns the rule label as the diagnostic id. The
diagnostic emitter has no mapping for that label, so the user-facing result is
an internal compiler error.

Required bootstrap behavior:

Missing procedure return annotations must emit a SPEC diagnostic code or a
documented uncoded static diagnostic path. The current rule label must not reach
the external diagnostic emitter as an unmapped diagnostic id.

Repair:

- `typecheck_diag_lookup.h` now recognizes known static rules with no assigned
  diagnostic code and emits an uncoded static-rule diagnostic instead of an
  internal compiler error.

## UVBOOT-0011: Resolver Main-Multiple Diagnostic Mapping

Status: repaired in the workspace bootstrap and verified by the `MainMultiple`
rejected-source fixture.

Rejected reference source:

- `Fixtures/RejectedSource/Procedures/MainMultiple/Source/Main.uv`

Spec obligations involved:

- `rule.15.Main-Multiple`
- `diag-table.15.ProcedureContractEntryDiagnostics`

Spec basis:

- `SPECIFICATION.md:14167` requires a diagnostic when an executable project has
  more than one `main` declaration.
- `SPECIFICATION.md:15372` assigns `E-MOD-2430` to multiple `main` procedures.

Spec-invalid specimen:

```ultraviolet
public procedure main(context: Context) -> i32 {
    return 0
}

public procedure main(context: Context) -> i32 {
    return 1
}
```

Observed bootstrap result:

```text
error: Internal error: resolver failed with unmapped diagnostic id `E-MOD-2430`.
```

Repaired bootstrap result:

```text
error[E-MOD-2430]: Multiple `main` procedures defined
```

Bootstrap owner:

- `LLVMBootstrap/cursive/src/04_analysis/resolve/collect_toplevel.cpp`
- `LLVMBootstrap/cursive/src/04_analysis/resolve/resolve_module.cpp`

Failure analysis:

Top-level name collection correctly identifies duplicate executable `main`
procedures and returns the assigned SPEC diagnostic code `E-MOD-2430`. The
resolver diagnostic emission path did not pass that assigned code through its
mapping table, so the diagnostic was converted into an internal compiler error.

Required bootstrap behavior:

Duplicate executable `main` declarations must surface `E-MOD-2430` as the
external diagnostic.

Repair:

- `resolve_module.cpp` now maps resolver diagnostic id `E-MOD-2430` to the
  registered diagnostic code `E-MOD-2430`.

## UVBOOT-0012: Missing Method Lookup Diagnostic Mapping

Status: repaired in the workspace bootstrap and verified by the
`MethodNotFound` rejected-source fixture.

Rejected reference source:

- `Fixtures/RejectedSource/Expressions/MethodNotFound/Source/Main.uv`

Spec obligations involved:

- `rule.15.LookupMethod-NotFound`
- `diag.16.ExpressionDiagnosticsSupplement`

Spec basis:

- `SPECIFICATION.md:14575` requires a diagnostic when method lookup finds no
  direct method and no inherited class default.
- `SPECIFICATION.md:17883` assigns `E-SEM-2536` to method-not-found
  diagnostics.

Spec-invalid specimen:

```ultraviolet
public record MethodLookupTarget {
    public value: i32
}

public procedure methodNotFoundReference() -> i32 {
    let target: MethodLookupTarget = MethodLookupTarget { value: 1 }
    return target~>missing()
}
```

Observed bootstrap result:

```text
error: Static rule failed without assigned diagnostic code: LookupMethod-NotFound
```

Repaired bootstrap result:

```text
error[E-SEM-2536]: Method not found for receiver type
```

Bootstrap owner:

- `LLVMBootstrap/cursive/src/04_analysis/typing/expr/method_call.cpp`
- `LLVMBootstrap/cursive/src/04_analysis/typing/typecheck_diag_lookup.h`

Failure analysis:

Method-call typing correctly identifies `LookupMethod-NotFound`, but the shared
typecheck diagnostic lookup did not map that rule label to the diagnostic code
assigned by the expression diagnostics supplement.

Required bootstrap behavior:

Missing method lookup must surface `E-SEM-2536` as the external diagnostic.

Repair:

- `typecheck_diag_lookup.h` now maps `LookupMethod-NotFound` to `E-SEM-2536`.

## UVBOOT-0013: Closure Parameter Inference Diagnostic Mapping

Status: repaired in the workspace bootstrap and verified by the
`ClosureParamInference` rejected-source fixture.

Rejected reference source:

- `Fixtures/RejectedSource/Expressions/ClosureParamInference/Source/Main.uv`

Spec obligations involved:

- `rule.16.Infer-Closure-Params-Err`
- `diag.16.ClosureAndPipelineExpressions`
- `diag.16.ExpressionDiagnosticsSupplement`

Spec basis:

- `SPECIFICATION.md:17592` requires `E-SEM-2591` when a closure parameter lacks
  an annotation and no expected type is available.
- `SPECIFICATION.md:17886` assigns `E-SEM-2591` to closure parameter inference
  failure.

Spec-invalid specimen:

```ultraviolet
public procedure closureParamInferenceReference() -> i32 {
    let identity = |value| value
    return identity(1)
}
```

Observed bootstrap result:

```text
error: Internal error: unknown diagnostic id 'Infer-Closure-Params-Err'
```

Repaired bootstrap result:

```text
error[E-SEM-2591]: Closure parameter type cannot be inferred
```

Bootstrap owner:

- `LLVMBootstrap/cursive/src/04_analysis/typing/expr/closure_expr.cpp`
- `LLVMBootstrap/cursive/src/04_analysis/typing/type_infer.cpp`
- `LLVMBootstrap/cursive/src/04_analysis/typing/typecheck_diag_lookup.h`

Failure analysis:

Closure typing correctly identifies `Infer-Closure-Params-Err`, but the shared
typecheck diagnostic lookup did not map that rule label to the diagnostic code
assigned by the expression diagnostics supplement.

Required bootstrap behavior:

Closure parameter inference failure must surface `E-SEM-2591` as the external
diagnostic.

Repair:

- `typecheck_diag_lookup.h` now maps `Infer-Closure-Params-Err` to
  `E-SEM-2591`.

## UVBOOT-0014: Transmute Size Diagnostic Mapping

Status: repaired in the workspace bootstrap and verified by the
`TransmuteSizeMismatch` rejected-source fixture.

Rejected reference source:

- `Fixtures/RejectedSource/Expressions/TransmuteSizeMismatch/Source/Main.uv`

Spec obligations involved:

- `rule.16.T-Transmute-SizeEq`
- `diag.16.CastAndTransmuteExpressions`
- `diag.16.ExpressionDiagnosticsSupplement`

Spec basis:

- `SPECIFICATION.md:16405` defines transmute size compatibility.
- `SPECIFICATION.md:17887` assigns `E-MEM-3031` to source and target size
  mismatch.

Spec-invalid specimen:

```ultraviolet
public procedure transmuteSizeMismatchReference() -> i64 {
    var result: i64 = 0
    unsafe {
        result = transmute<i32, i64>(1)
    }
    return result
}
```

Observed bootstrap result:

```text
error: Static rule failed without assigned diagnostic code: T-Transmute-SizeEq
```

Repaired bootstrap result:

```text
error[E-MEM-3031]: `transmute` source and target sizes differ
```

Bootstrap owner:

- `LLVMBootstrap/cursive/src/04_analysis/typing/expr/transmute_expr.cpp`
- `LLVMBootstrap/cursive/src/04_analysis/typing/typecheck_diag_lookup.h`

Failure analysis:

Transmute typing correctly identifies the violated size-compatibility rule, but
the shared typecheck diagnostic lookup did not map that rule label to the
diagnostic code assigned by the expression diagnostics supplement.

Required bootstrap behavior:

Transmute source and target size mismatch must surface `E-MEM-3031`.

Repair:

- `typecheck_diag_lookup.h` now maps `T-Transmute-SizeEq` to `E-MEM-3031`.
- The same lookup now maps `T-Transmute-AlignEq` to `E-UNS-0104` for the
  paired transmute alignment rule.

## UVBOOT-0015: Non-Bitcopy Identifier Value Use

Status: repaired in the workspace bootstrap and verified by the
`ValueUseNonBitcopyPlace` rejected-source fixture.

Rejected reference source:

- `Fixtures/RejectedSource/Expressions/ValueUseNonBitcopyPlace/Source/Main.uv`

Spec obligations involved:

- `rule.16.ValueUse-NonBitcopyPlace`
- `diag.16.AccessAndPlaceExpressions`
- `diag.16.ExpressionDiagnosticsSupplement`

Spec basis:

- `SPECIFICATION.md:15813` requires a diagnostic when a place is used as a
  value and its expression type is not `Bitcopy`.
- `SPECIFICATION.md:17890` assigns `E-UNS-0107` to non-`Bitcopy` place
  expression value use.

Spec-invalid specimen:

```ultraviolet
public procedure valueUseNonBitcopyPlaceReference() -> unique i32 {
    let payload: unique i32 = 1
    return payload
}
```

Observed bootstrap result:

```text
accepted without diagnostics
```

Repaired bootstrap result:

```text
error[E-UNS-0107]: Non-`Bitcopy` place expression used as value
```

Bootstrap owner:

- `LLVMBootstrap/cursive/src/04_analysis/typing/type_infer.cpp`

Failure analysis:

Checked expression typing inferred a place expression's type and accepted
subsumption without applying the `ValueUse-NonBitcopyPlace` rule. That allowed
plain identifier returns such as `return payload` to pass when `payload` had a
non-`Bitcopy` type. Direct callable and pattern contexts keep their synthesis
path, while checked value contexts now apply the value-use rule.

Required bootstrap behavior:

Plain identifier value use of a non-`Bitcopy` place must surface `E-UNS-0107`.

Repair:

- `CheckExprAgainst` now reports `ValueUse-NonBitcopyPlace` when the checked
  expression is a place, is not an explicit `move`, and its inferred type is
  not `Bitcopy`.

## UVBOOT-0016: Record Default Construction Diagnostic Mapping

Status: repaired in the workspace bootstrap and verified by the
`RecordDefaultInit` rejected-source fixture.

Rejected reference source:

- `Fixtures/RejectedSource/Expressions/RecordDefaultInit/Source/Main.uv`

Spec obligations involved:

- `rule.16.Record-Default-Init-Err`
- `diag.16.ConstructionExpressions`
- `diag-table.12.DataTypeDiagnostics`

Spec basis:

- `SPECIFICATION.md:16708` defines default record construction rejection for
  records that are not default-constructible.
- `SPECIFICATION.md:9938` assigns `E-TYP-1911` to record default construction
  without default initializers for every field.

Spec-invalid specimen:

```ultraviolet
public record NonDefaultPayload {
    public value: i32
}

public procedure recordDefaultInitReference() -> NonDefaultPayload {
    return NonDefaultPayload()
}
```

Observed bootstrap result:

```text
error: Static rule failed without assigned diagnostic code: Record-Default-Init-Err
```

Repaired bootstrap result:

```text
error[E-TYP-1911]: Default record construction requires default initializer for every field
```

Bootstrap owner:

- `LLVMBootstrap/cursive/src/04_analysis/composite/records.cpp`
- `LLVMBootstrap/cursive/src/04_analysis/typing/typecheck_diag_lookup.h`

Failure analysis:

Record default-call typing correctly identified `Record-Default-Init-Err`, but
the shared typecheck diagnostic lookup did not map that rule label to the
diagnostic code assigned by the data type diagnostics table.

Required bootstrap behavior:

Default construction of a non-default-constructible record must surface
`E-TYP-1911`.

Repair:

- `typecheck_diag_lookup.h` now maps `Record-Default-Init-Err` to
  `E-TYP-1911`.

## UVBOOT-0017: Ptr::null() Expected-Type Handling

Status: repaired in the workspace bootstrap and verified by the
`PtrNullSynthesis`, `PtrNullCheck`, and `PtrNullReturn` fixtures.

Rejected reference source:

- `Fixtures/RejectedSource/Expressions/PtrNullSynthesis/Source/Main.uv`
- `Fixtures/RejectedSource/Expressions/PtrNullCheck/Source/Main.uv`

Accepted reference source:

- `Fixtures/AcceptedProjects/PtrNullReturn/Source/Library.uv`

Spec obligations involved:

- `rule.16.Syn-PtrNull-Err`
- `rule.16.Chk-PtrNull-Err`
- `rule.16.Chk-Null-Ptr`
- `rule.16.EvalSigma-PtrNull`
- `rule.16.Lower-Expr-PtrNull`

Spec basis:

- `SPECIFICATION.md:15546` allows `Ptr::null()` when checked against
  `Ptr<U>@Null` or `Ptr<U>`.
- `SPECIFICATION.md:15551` and `SPECIFICATION.md:15556` route synthesis and
  incompatible checked pointer-null failures through `PtrNull-Infer-Err`.
- `SPECIFICATION.md:6396` assigns `E-TYP-1530` to type inference failure.

Spec-valid specimen:

```ultraviolet
public procedure ptrNullReturnReference() -> Ptr<i32> {
    return Ptr::null()
}
```

Observed bootstrap result:

```text
error[E-TYP-1530]: Type inference failed; unable to determine type
```

Repaired bootstrap result:

```text
accepted without diagnostics
```

Bootstrap owner:

- `LLVMBootstrap/cursive/src/04_analysis/typing/typecheck_diag_lookup.h`
- `LLVMBootstrap/cursive/src/04_analysis/typing/stmt/return_stmt.cpp`

Failure analysis:

The typecheck diagnostic lookup did not resolve `PtrNull-Infer-Err` to
`E-TYP-1530`, and the safe-pointer return provenance check synthesized
`Ptr::null()` after the return expression had already checked successfully
against the declared safe pointer return type.

Required bootstrap behavior:

`Ptr::null()` must fail in synthesis position and when checked against a
non-pointer type, both as `E-TYP-1530`; the same expression must succeed when
checked against a nullable safe pointer type.

Repair:

- `typecheck_diag_lookup.h` maps `PtrNull-Infer-Err` to `E-TYP-1530`.
- `return_stmt.cpp` treats a direct `Ptr::null()` return as having no escaping
  local safe-pointer provenance.

## UVBOOT-0018: Range Pattern Identifier Endpoint Before Case Body

Status: identified in the workspace bootstrap during pattern fixture probing.

Rejected-source probe:

- Local probe for `RangePatternNonConst`; the committed corpus fixture uses a
  non-integer literal endpoint to exercise the same SPEC diagnostic path.

Spec obligation involved:

- `rule.17.RangePattern-NonConst`

Spec basis:

- `SPECIFICATION.md:18388` defines `RangePattern-NonConst` for range patterns
  whose low or high endpoint is not a compile-time integer constant.
- `SPECIFICATION.md:18372` parses each range endpoint as a pattern atom, which
  includes identifier patterns from `SPECIFICATION.md:17916`.

Spec-valid rejected-source specimen:

```ultraviolet
public procedure rangePatternNonConstReference(upper: i32) -> i32 {
    let value: i32 = 2
    return if value is 1..upper {
        1
    } else {
        0
    }
}
```

Observed bootstrap result:

```text
error[E-SRC-0520]: Generic syntax error (unexpected token)
  --> local RangePatternNonConst probe:6:9
6 |         1
6 |         ^
```

Expected bootstrap behavior:

The parser should produce a `RangePattern` with `upper` as the high endpoint,
then the typechecker should reject it through `RangePattern-NonConst`.

Bootstrap owner:

- `LLVMBootstrap/cursive/src/02_source/parser/expr/if_expr.cpp`
- `LLVMBootstrap/cursive/src/02_source/parser/pattern/pattern_common.cpp`

Failure analysis:

Inside a single `if ... is` clause, the range parser parses the high endpoint
with ordinary pattern-atom rules. When the high endpoint is an identifier
immediately followed by the case-body brace, `ParsePatternAtom` treats
`upper { ... }` as a record pattern and consumes the case body as pattern
syntax. The existing single-identifier fallback handles a whole identifier
pattern before a body brace, but it does not cover an identifier endpoint inside
a range pattern.

## UVBOOT-0019: Record Method Receiver Diagnostic Classification

Status: repaired in the workspace bootstrap and verified by the
`RecordMethodReceiverNotSelf` fixture.

Rejected reference source:

- `Fixtures/RejectedSource/Procedures/RecordMethodReceiverNotSelf/Source/Main.uv`

Spec obligation involved:

- `rule.15.Record-Method-RecvSelf-Err`

Spec basis:

- `SPECIFICATION.md:14414` rejects explicit record-method receivers whose type
  is not `Self` or permission-qualified `Self`.

Spec-valid rejected-source specimen:

```ultraviolet
public record ReceiverOwner {
    public value: i32

    public procedure readValue(self: i32) -> i32 {
        return self
    }
}
```

Observed bootstrap result:

```text
error: Internal error: unknown diagnostic id 'Record-Method-RecvSelf-Err'
```

Repaired bootstrap result:

```text
./LLVMBootstrap/cursive/build/windows/Release/Cursive.exe build HelloUltraviolet/Fixtures/RejectedSource/Procedures/RecordMethodReceiverNotSelf --check --target-profile x86_64-win64 --build-progress off

exit=1
error[E-TYP-1912]: Explicit receiver type must be `Self` for record methods
```

Bootstrap owner:

- `LLVMBootstrap/cursive/src/04_analysis/composite/record_methods.cpp`
- `LLVMBootstrap/cursive/tools/generate_diagnostic_registry.py`
- `LLVMBootstrap/cursive/tools/static_rule_mapping.json`
- `LLVMBootstrap/cursive/src/00_core/generated/diag_registry.inc`
- `LLVMBootstrap/cursive/src/00_core/generated/static_rule_registry.inc`
- `LLVMBootstrap/cursive/src/04_analysis/typing/item/typecheck_diag_map.inc`

Failure analysis:

The record-method receiver checker returned `Record-Method-RecvSelf-Err` as the
diagnostic id but did not trace the SPEC rule at the emission point. The
generated static-rule registry therefore omitted the rule, and the typecheck
diagnostic resolver classified the id as an internal unknown instead of an
uncoded static-rule diagnostic.

Repair:

- `record_methods.cpp` now records `SPEC_RULE("Record-Method-RecvSelf-Err")`
  before returning the diagnostic id.
- The diagnostic and static-rule registries map `Record-Method-RecvSelf-Err` to
  the SPEC-defined `E-TYP-1912`, and the Release `cursive` target was rebuilt
  successfully.

## UVBOOT-0020: Contract Entry Bitcopy Enforcement

Status: repaired in the workspace bootstrap and verified by the
`ContractEntryNonBitcopy` fixture.

Rejected reference source:

- `Fixtures/RejectedSource/Procedures/ContractEntryNonBitcopy/Source/Main.uv`

Spec obligations involved:

- `rule.15.Entry-Type`
- `req.15.ContractEntryConstraints`

Spec basis:

- `SPECIFICATION.md:14950-14960` requires `@entry(expr)` to appear only in
  postconditions, to capture a pure parameter or receiver expression, and to
  require the captured expression type to satisfy `BitcopyType`.

Spec-valid rejected-source specimen:

```ultraviolet
public procedure contractEntryNonBitcopyReference(value: unique string@Managed) -> bool
|: => @entry(value) == @entry(value)
{
    return true
}
```

Observed bootstrap result before repair:

```text
Compilation succeeded.
```

Repaired bootstrap result:

```text
error[E-SEM-2805]: `@entry()` result type not `BitcopyType`
```

Bootstrap owner:

- `LLVMBootstrap/cursive/src/04_analysis/typing/expr/contract_entry.cpp`
- `LLVMBootstrap/cursive/src/04_analysis/typing/type_expr.cpp`
- `LLVMBootstrap/cursive/src/04_analysis/typing/item/contract_clause.cpp`
- `LLVMBootstrap/cursive/src/04_analysis/contracts/contract_intrinsics.cpp`
- `LLVMBootstrap/cursive/src/04_analysis/typing/item/procedure_decl.cpp`

Failure analysis:

The bootstrap had two related gaps. The expression-level `@entry` paths accepted
`CloneType` as an alternative to `BitcopyType`, while the procedure contract
declaration path validated placement and purity without typing the contract
predicate under `ContractPhase::Postcondition`. A spec-valid postcondition that
captured `unique string@Managed` therefore compiled even though the captured
entry value is not bitcopy.

Repair:

- The expression-level `@entry` checks now require `BitcopyType` only.
- Procedure contract predicates are typed before body checking with
  `ContractPhase::Precondition` or `ContractPhase::Postcondition`; the existing
  `TypeExpr` `@entry` rule now owns `E-SEM-2805`.
- Contract-entry comments and helper wording were aligned to the bitcopy-only
  SPEC rule.

## UVBOOT-0021: Non-Boolean Contract Predicate Diagnostic Classification

Status: repaired in the workspace bootstrap and verified by the
`ContractPredicateNonBool` fixture.

Rejected reference source:

- `Fixtures/RejectedSource/Procedures/ContractPredicateNonBool/Source/Main.uv`

Spec obligation involved:

- `rule.15.WF-Contract`

Spec basis:

- `SPECIFICATION.md:14658-14663` defines `WF-Contract` as requiring
  preconditions and postconditions to type as `bool`.
- `SPECIFICATION.md:15368-15395` lists the procedure, contract, and entry
  diagnostics supplement; `E-CON-0004` is not part of that contract diagnostic
  surface and is reserved by Chapter 19 for key escape diagnostics.

Spec-invalid rejected-source specimen:

```ultraviolet
public procedure contractPredicateNonBoolReference(value: i32) -> i32
|: value
{
    return value
}
```

Observed bootstrap result before repair:

```text
error[E-CON-0004]: Key escapes its defining scope
```

Repaired bootstrap result:

```text
error: Static rule failed without assigned diagnostic code: WF-Contract
```

Bootstrap owner:

- `LLVMBootstrap/cursive/src/04_analysis/typing/item/procedure_decl.cpp`
- `LLVMBootstrap/cursive/src/04_analysis/contracts/contract_check.cpp`

Failure analysis:

The newly added procedure contract predicate typing pass correctly rejected a
non-boolean predicate, but it reused `E-CON-0004` for the no-specific-code case.
That diagnostic belongs to key-scope escape checking, so the emitted diagnostic
message was unrelated to the violated contract rule.

Repair:

- `procedure_decl.cpp` now reports the uncoded `WF-Contract` static rule when a
  contract predicate has no type diagnostic or does not type as `bool`.
- `contract_check.cpp` documentation now reflects that `WF-Contract` owns the
  non-boolean contract predicate case.

## UVBOOT-0022: Contract Entry Diagnostic Precedence and Side-Effect Mapping

Status: repaired in the workspace bootstrap and verified by the
`ContractEntryMovedParameter`, `ContractEntrySideEffect`, and
`ContractEntryCapability` fixtures.

Rejected reference sources:

- `Fixtures/RejectedSource/Procedures/ContractEntryMovedParameter/Source/Main.uv`
- `Fixtures/RejectedSource/Procedures/ContractEntrySideEffect/Source/Main.uv`
- `Fixtures/RejectedSource/Procedures/ContractEntryCapability/Source/Main.uv`

Spec obligations involved:

- `diag.15.ProcedureContractEntryDiagnosticsOwnership`
- `diag-table.15.ProcedureContractEntryDiagnostics`

Spec basis:

- `SPECIFICATION.md:14950-14960` defines `@entry(expr)` constraints for
  postconditions.
- `SPECIFICATION.md:14995` assigns diagnostics to `@entry` expressions with
  side effects or capability requirements and to moved-parameter references.
- `SPECIFICATION.md:15376-15377` assigns `E-CON-0415` and `E-CON-0416`.
- `SPECIFICATION.md:15385` assigns `E-SEM-2807`.

Observed bootstrap result before repair:

```text
error[E-SEM-2802]: Impure expression in contract predicate
```

Repaired bootstrap results:

```text
error[E-SEM-2807]: `@entry()` references parameter whose value is unavailable after binding
error[E-CON-0416]: Side-effecting operation in `@entry` expression
error[E-CON-0415]: Capability-requiring operation in `@entry` expression
```

Bootstrap owner:

- `LLVMBootstrap/cursive/src/04_analysis/typing/item/procedure_decl.cpp`
- `LLVMBootstrap/cursive/src/04_analysis/contracts/contract_intrinsics.cpp`
- `LLVMBootstrap/cursive/src/04_analysis/typing/type_expr.cpp`
- `LLVMBootstrap/cursive/src/04_analysis/typing/expr/contract_entry.cpp`

Failure analysis:

The procedure contract path allowed the generic purity checker to classify some
`@entry` failures before the `@entry` intrinsic validator. The side-effect
scanner also did not classify allocation, move, transmute, propagation, loop,
block, unsafe, and fence forms as side-effecting `@entry` operands even though
those forms are already treated as impure by the general contract checker.

Repair:

- `procedure_decl.cpp` now validates contract intrinsics before running the
  generic contract well-formedness pass, so `@entry`-specific diagnostics take
  precedence for `@entry` operands.
- `contract_intrinsics.cpp`, `type_expr.cpp`, and
  `typing/expr/contract_entry.cpp` now map capability-requiring `@entry`
  operands to `E-CON-0415` and side-effecting `@entry` operands to
  `E-CON-0416`.

## UVBOOT-0023: Let Refutable Pattern Static Rule Diagnostic Registration

Status: repaired in the workspace bootstrap and verified by the
`LetRefutablePattern` fixture.

Rejected reference source:

- `Fixtures/RejectedSource/Statements/LetRefutablePattern/Source/Main.uv`

Spec obligation involved:

- `rule.18.Let-Refutable-Pattern-Err`

Spec basis:

- `SPECIFICATION.md:19152-19153` requires a diagnostic when a literal, enum,
  modal, or range pattern appears in an irrefutable `let` binding context.

Rejected-source specimen:

```ultraviolet
public procedure letRefutablePatternReference(value: i32) -> i32 {
    let 1 = value
    return value
}
```

Observed bootstrap result before repair:

```text
error: Internal error: unknown diagnostic id 'Let-Refutable-Pattern-Err'
```

Repaired bootstrap result:

```text
./LLVMBootstrap/cursive/build/windows/Release/Cursive.exe build HelloUltraviolet/Fixtures/RejectedSource/Statements/LetRefutablePattern --check --target-profile x86_64-win64 --build-progress off

exit=1
error[E-SEM-2711]: Refutable pattern in irrefutable context (`let`)
```

Bootstrap owner:

- `LLVMBootstrap/cursive/src/04_analysis/typing/stmt/stmt_common.cpp`
- `LLVMBootstrap/cursive/tools/generate_diagnostic_registry.py`
- `LLVMBootstrap/cursive/tools/static_rule_mapping.json`
- `LLVMBootstrap/cursive/src/00_core/generated/diag_registry.inc`
- `LLVMBootstrap/cursive/src/00_core/generated/static_rule_registry.inc`
- `LLVMBootstrap/cursive/src/04_analysis/typing/item/typecheck_diag_map.inc`

Failure analysis:

The statement pattern checker returned `Let-Refutable-Pattern-Err` for a
refutable `let` pattern but traced `Pat-Refutable-Err`. The generated static-rule
registry therefore knew about the traced helper name rather than the emitted
SPEC rule diagnostic id.

Repair:

- `stmt_common.cpp` now records `SPEC_RULE("Let-Refutable-Pattern-Err")` before
  returning that diagnostic id.
- The diagnostic and static-rule registries map `Let-Refutable-Pattern-Err` to
  the SPEC-defined `E-SEM-2711`, and the Release `cursive` target was rebuilt
  successfully.

## UVBOOT-0024: Async Return Type Diagnostic Ownership

Status: repaired in the workspace bootstrap and verified by:

```text
powershell.exe ... run_vsdev_cmake_build.ps1 -SourceDir C:\Dev\Ultraviolet\LLVMBootstrap\cursive -BuildDir C:\Dev\Ultraviolet\LLVMBootstrap\cursive\build\windows -Config Release -Target cursive
./LLVMBootstrap/cursive/build/windows/Release/Cursive.exe build HelloUltraviolet/Fixtures/RejectedSource/Statements/AsyncReturnTypeMismatch --check --target-profile x86_64-win64 --incremental off --build-progress off
./LLVMBootstrap/cursive/build/windows/Release/Cursive.exe build HelloUltraviolet/Fixtures/RejectedSource/Statements/AsyncReturnUnitMismatch --check --target-profile x86_64-win64 --incremental off --build-progress off
```

Rejected-source specimens:

- `Fixtures/RejectedSource/Statements/AsyncReturnTypeMismatch/Source/Main.uv`
- `Fixtures/RejectedSource/Statements/AsyncReturnUnitMismatch/Source/Main.uv`

Spec obligations exercised:

- `rule.18.Return-Async-Type-Err`
- `rule.18.Return-Async-Unit-Err`

Spec basis:

- `SPECIFICATION.md:19825` assigns `E-CON-0203` to async return values whose
  expression type is incompatible with the async `Result` parameter.
- `SPECIFICATION.md:19830` assigns `E-CON-0203` to empty async returns when
  the async `Result` parameter is not unit.

Spec-valid rejected specimen:

```ultraviolet
public procedure asyncReturnTypeMismatchReference() -> Async<(), (), i32, !> {
    return true
}
```

Observed bootstrap result before repair:

```text
error[E-SEM-2526]: Expression type incompatible with expected type
  --> .../AsyncReturnTypeMismatch/Source/Main.uv:4:5
4 |     return true
4 |     ^^^^^^^^^^^
```

Bootstrap owner:

- `LLVMBootstrap/cursive/src/04_analysis/typing/stmt/return_stmt.cpp`

Failure analysis:

`TypeReturnStmt` correctly detected that the enclosing return type had an
async signature, but when `CheckExprAgainst` produced the generic expected-type
diagnostic `E-SEM-2526`, the async-return path propagated that generic code
instead of mapping the failure to the statement-owned `E-CON-0203` required by
`Return-Async-Type-Err`.

Required bootstrap behavior:

Async return expression incompatibility must be reported as `E-CON-0203` for
the async return rule. Expression diagnostics that identify an independently
ill-formed return expression may still propagate from the expression checker.

Repair:

- `LLVMBootstrap/cursive/src/04_analysis/typing/stmt/return_stmt.cpp` now maps
  the generic expected-type mismatch `E-SEM-2526` to `E-CON-0203` inside the
  async return branch while preserving other expression-owned diagnostics.

## UVBOOT-0025: Raw Dereference Assignment Mutability

Status: repaired in the workspace bootstrap and verified by:

```text
powershell.exe ... run_vsdev_cmake_build.ps1 -SourceDir C:\Dev\Ultraviolet\LLVMBootstrap\cursive -BuildDir C:\Dev\Ultraviolet\LLVMBootstrap\cursive\build\windows -Config Release -Target cursive
./LLVMBootstrap/cursive/build/windows/Release/Cursive.exe build HelloUltraviolet/Fixtures/AcceptedProjects/ExpressionSemantics --check --target-profile x86_64-win64 --incremental off --build-progress off
```

Accepted-source specimen:

- `Fixtures/AcceptedProjects/ExpressionSemantics/Source/Library.uv`

Spec obligations exercised:

- `rule.16.T-Deref-Raw`
- `rule.16.DerefPlaceTypingFamily`
- `rule.16.T-AddrOf`

Spec basis:

- `SPECIFICATION.md:17163-17166` types raw pointer dereference inside an
  `unsafe` span as a valid value expression.
- `SPECIFICATION.md:17168` defines raw dereference place typing.
- `SPECIFICATION.md:12017-12021` defines `P-Deref-Raw-Mut` as a mutable raw
  dereference place.

Spec-valid accepted specimen:

```ultraviolet
public procedure derefPlaceReference(pointer: *mut i32) -> i32 {
    unsafe {
        *pointer = 17
    }
    return unsafe { *pointer }
}
```

Observed bootstrap result before repair:

```text
error[E-MOD-2401]: Reassignment of immutable `let` binding
  --> .../ExpressionSemantics/Source/Library.uv:32:8
32 | public procedure derefPlaceReference(pointer: *mut i32) -> i32 {
32 |
```

Bootstrap owners:

- `LLVMBootstrap/cursive/src/04_analysis/typing/stmt/assign_stmt.cpp`
- `LLVMBootstrap/cursive/src/04_analysis/typing/stmt/compound_assign_stmt.cpp`
- `LLVMBootstrap/cursive/src/04_analysis/memory/borrow_bind.cpp`

Failure analysis:

Assignment typing and borrow/binding analysis followed a dereference place back
to the pointer parameter binding and treated the write as reassignment of that
immutable parameter. The spec places mutability for raw pointer writes on the
dereferenced place type: `*imm T` yields a const place and `*mut T` yields a
unique place.

Repair:

- Assignment typing now skips root-binding mutability checks when the assigned
  place writes through a dereference, leaving const and shared checks on the
  typed place intact.
- Borrow/binding assignment validation now applies the same dereference-write
  distinction before issuing `E-MOD-2401`.

## UVBOOT-0026: Extern Call Unsafe Diagnostic Code

Status: repaired.

Rejected-source specimens:

- `Fixtures/RejectedSource/Expressions/ExternCallUnsafeRequirement/Source/Main.uv`
- `Fixtures/RejectedSource/FFI/ExternCallSafety/Source/Main.uv`

Spec obligation exercised:

- `req.16.ExternProcedureCallsRequireUnsafe`
- `requirement.23.ExternCallSafety`

Spec basis:

- `SPECIFICATION.md:16032` requires calls to `extern` procedures outside
  `unsafe` to be rejected by the FFI boundary rule.
- `SPECIFICATION.md:25688` requires calls to extern procedures to appear within
  an `unsafe` block.
- `SPECIFICATION.md:25703` assigns diagnostic code `E-TYP-2106` to calls to
  extern procedures outside `unsafe`.

Spec-valid rejected specimen:

```ultraviolet
extern "C" {
    public procedure importedValue() -> i32
}

public procedure externCallUnsafeRequirementReference() -> i32 {
    return importedValue()
}
```

Observed bootstrap result before repair:

```text
error: Static rule failed without assigned diagnostic code: Call-Extern-Unsafe-Err
  --> .../ExternCallUnsafeRequirement/Source/Main.uv:8:5
8 |     return importedValue()
8 |     ^^^^^^^^^^^^^^^^^^^^^^
```

Observed bootstrap result after repair:

```text
error[E-TYP-2106]: Call to `extern` procedure outside `unsafe`
  --> .../ExternCallSafety/Source/Main.uv:8:5
8 |     return importedValue()
8 |     ^^^^^^^^^^^^^^^^^^^^^^
```

Bootstrap owner:

- `LLVMBootstrap/cursive/src/04_analysis/typing/expr/call.cpp`

Repair analysis:

The bootstrap enforces the required rejection, but the diagnostic is emitted as
the static rule `Call-Extern-Unsafe-Err` unless the typechecker maps the branch
to the SPEC-assigned diagnostic code. The owner branch now preserves the static
rule marker and emits `E-TYP-2106`.

Repaired bootstrap behavior:

Extern procedure calls outside `unsafe` must report the SPEC diagnostic code
`E-TYP-2106` for `Call-Extern-Unsafe-Err`.

## UVBOOT-0027: FfiSafe Generic and Incomplete Layout Diagnostics

Status: repaired.

Rejected-source specimens:

- `Fixtures/RejectedSource/FFI/FfiSafeIncompleteLayout/Source/Main.uv`
- `Fixtures/RejectedSource/FFI/FfiSafeRecordGenericUnbounded/Source/Main.uv`
- `Fixtures/RejectedSource/FFI/FfiSafeEnumGenericUnbounded/Source/Main.uv`

Spec obligations exercised:

- `rule.23.FfiSafe-Incomplete-Err`
- `rule.23.FfiSafe-Record-Generic-Unbounded-Err`
- `rule.23.FfiSafe-Enum-Generic-Unbounded-Err`

Spec basis:

- `SPECIFICATION.md:25557-25560` assigns `E-TYP-2628` when layout cannot be
  computed for a record or enum type.
- `SPECIFICATION.md:25562-25570` assigns `E-TYP-2629` for generic record and
  enum paths whose field or payload type parameters are not bounded by
  `FfiSafe`.

Observed bootstrap result before repair:

```text
FfiSafeIncompleteLayout: E-TYP-2626
FfiSafeRecordGenericUnbounded: E-TYP-2628
FfiSafeEnumGenericUnbounded: E-TYP-2628
```

Bootstrap owner:

- `LLVMBootstrap/cursive/src/04_analysis/typing/type_predicates.cpp`

Failure analysis:

The FFI-safe diagnostic walker collapsed recursive or otherwise incomplete
field/payload layout failures into the broader record/enum field diagnostics.
It also resolved generic arguments before checking the unbounded generic
predicate rule, so bare generic record and enum paths without `FfiSafe(T)`
bounds became incomplete-layout diagnostics.

Repair:

- Nested `E-TYP-2628` diagnostics are now preserved through record-field and
  enum-payload FFI-safe checks.
- Bare generic record and enum paths now check missing `FfiSafe(T)` predicate
  requirements before generic-argument resolution.

Verified result after repair:

```text
FfiSafeIncompleteLayout: E-TYP-2628
FfiSafeRecordGenericUnbounded: E-TYP-2629
FfiSafeEnumGenericUnbounded: E-TYP-2629
```

## UVBOOT-0028: Foreign Contract Predicate Diagnostics

Status: repaired.

Rejected-source specimens:

- `Fixtures/RejectedSource/FFI/ForeignPredicateContext/Source/Main.uv`
- `Fixtures/RejectedSource/FFI/ForeignPostconditionPredicateBindings/Source/Main.uv`
- `Fixtures/RejectedSource/FFI/NullResultWellFormedness/Source/Main.uv`
- `Fixtures/RejectedSource/FFI/ForeignEnsuresNullResult/Source/Main.uv`
- `Fixtures/RejectedSource/FFI/ErrorPredicateWellFormedness/Source/Main.uv`
- `Fixtures/RejectedSource/FFI/ForeignContractDiagnostics/Source/Main.uv`

Spec obligations exercised:

- `requirement.23.ForeignPredicateContext`
- `requirement.23.ForeignPostconditionPredicateBindings`
- `requirement.23.NullResultWellFormedness`
- `rule.23.ForeignEnsures-NullResult-Err`
- `requirement.23.ErrorPredicateWellFormedness`
- `diagnostics.23.ForeignContractDiagnostics`

Spec basis:

- `SPECIFICATION.md:26310-26319` restricts foreign contract predicates to
  in-scope parameter values, pure forms, and allowed result/error/null
  postcondition bindings.
- `SPECIFICATION.md:26358-26376` requires `@null_result` predicates to appear
  only when the return type is a nullable pointer type.
- `SPECIFICATION.md:26416-26422` assigns `E-SEM-2851`, `E-SEM-2852`,
  `E-SEM-2853`, `E-SEM-2855`, and `E-SEM-2856` to the corresponding foreign
  contract predicate failures.

Observed bootstrap result before repair:

```text
ForeignPredicateContext: exit 0
ForeignPostconditionPredicateBindings: exit 0
NullResultWellFormedness: E-SEM-2853
ForeignEnsuresNullResult: E-SEM-2853
```

Bootstrap owners:

- `LLVMBootstrap/cursive/src/04_analysis/contracts/contract_intrinsics.cpp`
- `LLVMBootstrap/cursive/src/04_analysis/typing/item/extern_block.cpp`

Failure analysis:

The extern-procedure declaration path recorded foreign contract clauses and
deferred to contract intrinsic resolution, but it did not validate predicate
purity or binding scope while building the extern procedure info. The
`@null_result` check rejected non-nullable returns through the broader
postcondition predicate diagnostic instead of the SPEC-assigned null-result
diagnostic.

Repair:

- Extern procedure typing now validates each foreign contract predicate against
  the allowed parameter names and postcondition-only bindings before resolving
  the foreign contract clause.
- Foreign precondition impurity maps to `E-SEM-2851`; out-of-scope predicate
  names map to `E-SEM-2852`; foreign postcondition impurity maps to
  `E-SEM-2853`; `@result` in a non-return context maps to `E-SEM-2854`.
- `@null_result` on a non-nullable foreign return now maps to `E-SEM-2856`.

Verified result after repair:

```text
ForeignPredicateContext: E-SEM-2851
ForeignPostconditionPredicateBindings: E-SEM-2852
NullResultWellFormedness: E-SEM-2856
ForeignEnsuresNullResult: E-SEM-2856
ErrorPredicateWellFormedness: E-SEM-2855
ForeignContractDiagnostics: E-SEM-2853
```

## UVBOOT-0029: Null Literal Expected-Type Diagnostic

Status: repaired.

Rejected-source specimen:

- `Fixtures/RejectedSource/Expressions/NullLiteralExpected/Source/Main.uv`

Spec obligation exercised:

- `def.16.NullLiteralExpected`

Spec basis:

- `SPECIFICATION.md:15521-15542` defines checked `null` literals as valid only
  when the expected type is a raw pointer.
- `SPECIFICATION.md:6396` assigns `E-TYP-1530` to failed type inference where
  the checker cannot determine a valid type.

Observed bootstrap result before repair:

```text
NullLiteralExpected: error: Internal error: unknown diagnostic id 'NullLiteral-Infer-Err'
```

Bootstrap owner:

- `LLVMBootstrap/cursive/src/04_analysis/typing/literals.cpp`
- `LLVMBootstrap/cursive/src/04_analysis/typing/type_infer.cpp`
- `LLVMBootstrap/cursive/src/04_analysis/typing/typecheck_diag_lookup.h`

Failure analysis:

The literal checker and expression inference path correctly rejected a `null`
literal checked against `i32`, but they surfaced the internal sentinel
`NullLiteral-Infer-Err` without resolving it to a SPEC diagnostic code. The
diagnostic renderer therefore produced an internal compiler error instead of a
language diagnostic.

Repair:

- `NullLiteral-Infer-Err` now resolves through the typecheck diagnostic lookup
  to `E-TYP-1530`, matching the existing `PtrNull-Infer-Err` diagnostic
  ownership for expected-type failures.

Verified result after repair:

```text
NullLiteralExpected: E-TYP-1530
```

## UVBOOT-0030: Field Access Visibility Diagnostic

Status: repaired.

Rejected-source specimen:

- `Fixtures/RejectedSource/Expressions/FieldVisibility/Source/Main.uv`

Spec obligation exercised:

- `def.16.FieldVisibility`

Spec basis:

- `SPECIFICATION.md:9935` assigns `E-TYP-1905` to field access that is not
  visible in the current scope.
- `SPECIFICATION.md:15884` requires diagnostics for unknown or inaccessible
  record fields in access expressions.

Observed bootstrap result before repair:

```text
FieldVisibility: error: Static rule failed without assigned diagnostic code: FieldAccess-NotVisible
```

Bootstrap owner:

- `LLVMBootstrap/cursive/src/04_analysis/typing/expr/field_access.cpp`
- `LLVMBootstrap/cursive/src/04_analysis/typing/typecheck_diag_lookup.h`

Failure analysis:

The field-access type checker correctly rejected a read of a private field from
outside the declaring module, but it surfaced the internal static rule
`FieldAccess-NotVisible` without resolving it to the SPEC diagnostic code used
for inaccessible record fields.

Repair:

- `FieldAccess-NotVisible` now resolves through the typecheck diagnostic lookup
  to `E-TYP-1905`.

Verified result after repair:

```text
FieldVisibility: E-TYP-1905
```

## UVBOOT-0031: Widen Non-Modal Diagnostic

Status: repaired.

Rejected-source specimen:

- `Fixtures/RejectedSource/Expressions/WidenTypingDiagnosticsOwnership/Source/Main.uv`

Spec obligation exercised:

- `req.16.WidenTypingDiagnosticsOwnershipForCastTransmute`

Spec basis:

- `SPECIFICATION.md:11336-11339` defines `Widen-NonModal` for applying
  `widen` to a non-modal operand.
- `SPECIFICATION.md:12369` assigns `E-TYP-2071` to `widen` applied to a
  non-modal type.
- `SPECIFICATION.md:12370` assigns `E-TYP-2072` to `widen` applied to an
  already-general modal type.

Observed bootstrap result before repair:

```text
WidenTypingDiagnosticsOwnership: error: Static rule failed without assigned diagnostic code: Widen-NonModal
```

Bootstrap owner:

- `LLVMBootstrap/cursive/src/04_analysis/typing/expr/unary.cpp`
- `LLVMBootstrap/cursive/src/04_analysis/typing/typecheck_diag_lookup.h`

Failure analysis:

The unary-expression checker correctly rejected `widen 1`, but it surfaced the
internal `Widen-NonModal` sentinel without resolving it to the SPEC diagnostic
code. The adjacent already-general modal widening sentinel had the same
diagnostic lookup gap.

Repair:

- `Widen-NonModal` now resolves through the typecheck diagnostic lookup to
  `E-TYP-2071`.
- `Widen-AlreadyGeneral` now resolves through the same lookup to `E-TYP-2072`.

Verified result after repair:

```text
WidenTypingDiagnosticsOwnership: E-TYP-2071
```

## UVBOOT-0032: Free Procedure Overload Sets

Status: repaired for semantic checking, lowering, standalone build, and
runtime execution.

Spec-valid specimen:

- `Fixtures/BootstrapNonCompliance/Procedures/FreeProcedureOverloadResolution/Source/Main.uv`

Spec obligations exercised:

- `req.15.FreeProcedureOverloadResolutionBeforeCallTyping`
- `req.15.FreeCallOverloadResolutionAlgorithm`
- `req.15.NoRuntimeOverloadSearch`
- `req.15.OverloadResolutionCompleteBeforeLowering`

Spec basis:

- `SPECIFICATION.md:14593` states that overloading introduces no additional
  surface syntax beyond ordinary procedure and method declarations.
- `SPECIFICATION.md:14613-14624` defines free-procedure overload resolution by
  candidate selection, type filtering, preference, and selected unique target.
- `SPECIFICATION.md:14629` states that execution performs no runtime overload
  search.
- `SPECIFICATION.md:14633` states that lowering consumes the selected symbol
  after overload resolution is complete.

Spec-valid source:

```ultraviolet
public procedure selectOverload(value: i32) -> i32 {
    return value + 1
}

public procedure selectOverload(value: bool) -> i32 {
    if value {
        return 2
    }

    return 0
}

public procedure freeProcedureOverloadResolutionReference() -> i32 {
    return selectOverload(4) + selectOverload(true)
}
```

Previous bootstrap result:

```text
error[E-MOD-1302]: Duplicate declaration in module scope
  --> .../FreeProcedureOverloadResolution/Source/Main.uv:7:8
7 | public procedure selectOverload(value: bool) -> i32 {
```

Bootstrap owner:

- `LLVMBootstrap/cursive/src/04_analysis/resolve/collect_toplevel.cpp`
- `LLVMBootstrap/cursive/src/04_analysis/typing/expr/call.cpp`
- `LLVMBootstrap/cursive/src/05_codegen/symbols/mangle.cpp`
- `LLVMBootstrap/cursive/src/05_codegen/lower/lower_module.cpp`
- `LLVMBootstrap/cursive/src/05_codegen/lower/lower_proc.cpp`
- `LLVMBootstrap/cursive/src/05_codegen/lower/expr/call.cpp`

Failure analysis:

The bootstrap name collector stores a single module-scope value binding per
identifier and rejects the second ordinary procedure before call typing can
build an overload set. The call type checker also indexes procedures by a single
name-to-procedure entry, so it has no representation for candidate sets or the
§15.3.4 selection algorithm.

Required bootstrap behavior:

Module-scope collection must admit multiple visible free procedures with the
same name when their erased parameter-mode/type signatures differ. Call typing
must resolve that overload set before ordinary call typing and hand lowering the
selected procedure symbol.

Lowering must retain the selected declaration from semantic analysis, and
internal symbols for same-name non-ABI overload declarations must remain
distinct so the selected overload body is the body that executes.

Repair:

- `collect_toplevel.cpp` now permits same-name free procedure declarations to
  merge into one module-scope value binding while preserving duplicate `main`
  handling and non-procedure name conflicts.
- `expr/call.cpp` now indexes all same-name procedures in a module and resolves
  direct free calls against the candidate set before ordinary call typing. It
  filters by arity and argument compatibility, applies exact-match preference,
  and reports `E-SEM-3031` or `E-SEM-3030` for failed selection.
- Typechecking now records the selected overload target for each resolved call,
  and call lowering consumes that selected target instead of re-resolving by
  name.
- `mangle.cpp`, `lower_module.cpp`, and `lower_proc.cpp` now assign distinct
  internal symbols to non-ABI same-name procedure overload declarations within
  a module. This preserves selected overload identity during signature
  registration, body emission, and direct call lowering.
- `Fixtures/RejectedSource/Procedures/NoMatchingOverload` now exercises the
  no-matching-candidate diagnostic path for
  `req.15.FreeCallOverloadResolutionAlgorithm`.
- `HelloUltraviolet/Source/Reference/Procedures/Overloading.uv` now exercises
  arity-based free overload selection, parameter-type free overload selection,
  and same-name receiver method lookup as executable reference source.

Verified results after repair:

```text
FreeProcedureOverloadResolution --check: EXIT 0
FreeProcedureOverloadResolution build: EXIT 0
NoMatchingOverload --check: E-SEM-3031, EXIT 1
OverloadProbe runtime: EXIT 8
HelloUltraviolet --check: EXIT 0
HelloUltraviolet build/run/audit: EXIT 0/0/0
```

## UVBOOT-0033: Shared Dynamic Class Receiver Well-Formedness

Status: repaired.

SPEC-invalid specimen:

- `Fixtures/RejectedSource/Keys/SharedDynamicMutatingReceiver/Source/Main.uv`

Spec obligation exercised:

- `requirement.19.SharedDynamicClassRejectsMutatingReceivers`

Spec basis:

- `SPECIFICATION.md:20097` permits `shared $Cl` only when every
  vtable-eligible procedure in the class has a `const` receiver.
- `SPECIFICATION.md:20105-20106` states that `shared $Cl` is ill-formed when
  any method requires `shared` (`~%`) or `unique` (`~!`) receiver permission.
- `SPECIFICATION.md:20146` assigns `E-CON-0083` to `shared $Class` where the
  class has `~%` or `~!` methods.

Spec-invalid source:

```ultraviolet
public class SharedDynamicReceiverClass {
    public procedure mutate(~!) -> i32 {
        return 1
    }
}

public procedure sharedDynamicMutatingReceiverReference(
    value: shared $SharedDynamicReceiverClass
) -> i32 {
    return 0
}
```

Verified bootstrap result:

```text
./LLVMBootstrap/cursive/build/windows/Release/Cursive.exe build HelloUltraviolet/Fixtures/RejectedSource/Keys/SharedDynamicMutatingReceiver --check --target-profile x86_64-win64 --build-progress off

exit=1
error[E-CON-0083]: `shared $Class` where class has `~%`/`~!` methods
```

Bootstrap repair owner:

- `LLVMBootstrap/cursive/src/04_analysis/typing/type_wf.cpp`
- `LLVMBootstrap/cursive/src/00_core/generated/static_rule_registry.inc`

Repair summary:

The class-analysis path already computes vtable eligibility in
`VTableEligible`, and dynamic class object type construction is routed through
`MakeTypeDynamic`. The dynamic type well-formedness path now checks the
vtable-eligible method set for `shared $Class` and rejects any dispatchable
method whose receiver permission is `~%` or `~!`.

Implemented bootstrap behavior:

Dynamic class object type well-formedness must resolve the target class, inspect
the vtable-eligible effective method set, and reject `shared $Class` with
`E-CON-0083` when any dispatchable method has a `~%` or `~!` receiver.

## UVBOOT-0034: Tuple Pattern Arity Diagnostic Code Assignment

Status: open.

SPEC-invalid specimen:

- `Fixtures/RejectedSource/Patterns/TuplePatternArity/Source/Main.uv`

Spec obligation exercised:

- `rule.17.Pat-Tuple-R-Arity-Err`

Spec basis:

- `SPECIFICATION.md:18118-18121` rejects tuple patterns whose element count
  differs from the matched tuple type arity and names
  `Code(Pat-Tuple-Arity-Err)`.
- `SPECIFICATION.md:18173` says diagnostics are defined for tuple-pattern
  arity mismatch and unknown record fields.
- `SPECIFICATION.md:18729-18737` lists Chapter 17 pattern diagnostic codes but
  assigns no concrete code for tuple-pattern arity mismatch.

Spec-invalid source:

```ultraviolet
public procedure tuplePatternArityReference() -> i32 {
    let pair: (i32, bool) = (1, true)
    let (count, is_ready, extra) = pair
    return count
}
```

Observed bootstrap result:

```text
./LLVMBootstrap/cursive/build/windows/Release/Cursive.exe build HelloUltraviolet/Fixtures/RejectedSource/Patterns/TuplePatternArity --check --target-profile x86_64-win64 --build-progress off

exit=1
error: Static rule failed without assigned diagnostic code: Pat-Tuple-Arity-Err
```

Bootstrap owner:

- `LLVMBootstrap/cursive/src/04_analysis/typing/pattern/pattern_common.cpp`
- `LLVMBootstrap/cursive/src/04_analysis/typing/typecheck_diag_lookup.h`
- `LLVMBootstrap/cursive/tools/generate_diagnostic_registry.py`
- `LLVMBootstrap/cursive/src/00_core/generated/diag_registry.inc`

Failure analysis:

The pattern checker correctly rejects the source through the tuple-pattern
arity rule, but the rule cannot be surfaced as a concrete expected diagnostic
because the SPEC names `Code(Pat-Tuple-Arity-Err)` without a corresponding
Chapter 17 diagnostic code. The diagnostic registry can map rule names only
when the SPEC defines the target code.

Required bootstrap behavior:

After the SPEC assigns a concrete diagnostic code for `Pat-Tuple-Arity-Err`,
the bootstrap diagnostic registry must map that rule name to the assigned code
and the `TuplePatternArity` fixture must use the concrete expected diagnostic.

## UVBOOT-0035: Record Method Duplicate Diagnostic Code Mapping

Status: repaired.

SPEC-invalid specimen:

- `Fixtures/RejectedSource/Procedures/RecordMethodDuplicate/Source/Main.uv`

Spec obligation exercised:

- `rule.15.Record-Method-Dup`

Spec basis:

- `SPECIFICATION.md:14446-14448` rejects duplicate record method names.
- `SPECIFICATION.md:19999` assigns `E-SEM-3012` to duplicate method names in a
  type.

Verified bootstrap result:

```text
./LLVMBootstrap/cursive/build/windows/Release/Cursive.exe build HelloUltraviolet/Fixtures/RejectedSource/Procedures/RecordMethodDuplicate --check --target-profile x86_64-win64 --build-progress off

exit=1
error[E-SEM-3012]: Duplicate method name in type
```

Bootstrap repair owner:

- `LLVMBootstrap/cursive/tools/generate_diagnostic_registry.py`
- `LLVMBootstrap/cursive/tools/static_rule_mapping.json`
- `LLVMBootstrap/cursive/src/00_core/generated/diag_registry.inc`
- `LLVMBootstrap/cursive/src/00_core/generated/static_rule_registry.inc`
- `LLVMBootstrap/cursive/src/04_analysis/typing/item/typecheck_diag_map.inc`

Repair summary:

The record method checker already rejected duplicate method names with
`Record-Method-Dup`. The diagnostic and static-rule registries now map that rule
name to the SPEC-defined `E-SEM-3012`.

## UVBOOT-0036: Class Default Method Access to Required Class Field

Status: open.

Spec-valid specimen:

- `Fixtures/BootstrapNonCompliance/Polymorphism/ClassDefaultMethodFieldAccess/Source/Main.uv`

Spec obligations exercised:

- `rule.14.Parse-ClassItem-Field`
- `def.EffectiveClassMembers`
- `rule.14.Impl-Field`
- `rule.14.T-Class-Method-Body`

Spec basis:

- `SPECIFICATION.md:12802-12805` defines class fields as class items.
- `SPECIFICATION.md:12952-12978` defines effective class fields from class
  linearization.
- `SPECIFICATION.md:12993-12996` type-checks concrete class method bodies with
  `self` bound to `Self`.
- `SPECIFICATION.md:13138-13143` requires implementers to satisfy class fields.

Observed bootstrap result:

```text
./LLVMBootstrap/cursive/build/windows/Release/Cursive.exe build HelloUltraviolet --check --target-profile x86_64-win64 --build-progress off

error[E-TYP-1904]: Access to nonexistent field
  --> C:/Dev/Ultraviolet/HelloUltraviolet/Source/Reference/Polymorphism/Classes.uv:15:10
```

Bootstrap owner:

- `LLVMBootstrap/cursive/src/04_analysis/typing/item/class_decl.cpp`
- `LLVMBootstrap/cursive/src/04_analysis/typing/expr/field_access.cpp`
- `LLVMBootstrap/cursive/src/04_analysis/composite/classes.cpp`

Failure analysis:

The class field is parsed and the implementer is required to provide it, but
class default method body checking does not make the effective class field table
available to `self` field lookup. The checker therefore treats `self.inspected_value`
inside the class method body as an unknown field.

Required bootstrap behavior:

When checking a concrete class method body, field lookup on `Self` must include
the effective class fields of the current class.

## UVBOOT-0037: Generic Class-Bound Method Lookup

Status: open.

Spec-valid specimen:

- `Fixtures/BootstrapNonCompliance/Polymorphism/GenericClassBoundMethodLookup/Source/Main.uv`

Spec obligations exercised:

- `rule.14.Parse-TypeBoundsOpt-Yes`
- `rule.14.T-Constraint-Sat`
- `rule.14.GenericCallInference`
- `rule.14.T-Generic-Call`

Spec basis:

- `SPECIFICATION.md:12380-12587` defines inline class bounds on generic
  parameters.
- `SPECIFICATION.md:12564-12573` requires instantiations to satisfy bounds.
- `SPECIFICATION.md:12639-12702` includes bounds in generic call inference and
  generic-call typing.

Observed bootstrap result:

```text
./LLVMBootstrap/cursive/build/windows/Release/Cursive.exe build HelloUltraviolet --check --target-profile x86_64-win64 --build-progress off

error[E-SEM-2536]: Method not found for receiver type
  --> C:/Dev/Ultraviolet/HelloUltraviolet/Source/Reference/Polymorphism/GenericParameters.uv:34:5
34 |     return value~>classValue()
```

Bootstrap owner:

- `LLVMBootstrap/cursive/src/04_analysis/generics/where_bounds.cpp`
- `LLVMBootstrap/cursive/src/04_analysis/typing/expr/method_call.cpp`
- `LLVMBootstrap/cursive/src/04_analysis/composite/record_methods.cpp`

Failure analysis:

The generic parameter bound is accepted syntactically, but method lookup for a
generic receiver does not consult the receiver type parameter's class-bound set.
The checker therefore cannot resolve a class method that is guaranteed by the
bound.

Required bootstrap behavior:

Method lookup for a type parameter with class bounds must expose the effective
methods of those bounds when typing the generic declaration body.

## UVBOOT-0038: Generic Record Literal Expected-Type Construction

Status: open.

Spec-valid specimen:

- `Fixtures/BootstrapNonCompliance/Polymorphism/GenericRecordLiteralExpectedType/Source/Main.uv`

Spec obligations exercised:

- `rule.14.DefaultArgs`
- `rule.14.T-Generic-Type`
- `rule.16.Parse-Record-Literal`
- `rule.16.T-Record-Literal`

Spec basis:

- `SPECIFICATION.md:12556-12563` defines default generic arguments.
- `SPECIFICATION.md:12623-12631` defines generic type use as `TypeApply`.
- `SPECIFICATION.md:16617-16620` parses record literals as record expressions.
- `SPECIFICATION.md:16677-16679` types record literals by field set and field
  type.

Observed bootstrap result:

```text
./LLVMBootstrap/cursive/build/windows/Release/Cursive.exe build HelloUltraviolet --check --target-profile x86_64-win64 --build-progress off

error[E-MOD-2402]: Type annotation incompatible with inferred type
  --> C:/Dev/Ultraviolet/HelloUltraviolet/Source/Reference/Polymorphism/GenericParameters.uv:59:5
59 |     let carrier: GenericCarrier<i32> = GenericCarrier { value: 19, tag: 29 }
```

Bootstrap owner:

- `LLVMBootstrap/cursive/src/04_analysis/typing/expr/record_literal.cpp`
- `LLVMBootstrap/cursive/src/04_analysis/typing/stmt/let_stmt.cpp`
- `LLVMBootstrap/cursive/src/04_analysis/generics/monomorphize.cpp`

Failure analysis:

The parser follows the spec record-literal surface, but the expression checker
infers the unapplied record type from the literal name and the annotated
expected type is checked only after expression inference. The expected
`GenericRecord<i32>` application is therefore not used to instantiate the record
field types during literal checking.

Required bootstrap behavior:

When a record literal is checked against an expected `TypeApply`, the record
literal checker must instantiate the record declaration with those type
arguments before checking field initializers and returning the literal type.

## UVBOOT-0039: Generic Type Application Arity Well-Formedness

Status: repaired.

SPEC-invalid specimen:

- `Fixtures/RejectedSource/Polymorphism/GenericTypeApplyArgCount/Source/Main.uv`

Spec obligation exercised:

- `rule.14.WF-Apply-ArgCount-Err`

Spec basis:

- `SPECIFICATION.md:12697-12708` requires a generic type use to reject when
  `DefaultArgs(params_gen, args) = bottom`.
- `SPECIFICATION.md:13975` assigns `E-TYP-2303` to wrong type-argument count.

Observed bootstrap result before repair:

```text
./LLVMBootstrap/cursive/build/windows/Release/Cursive.exe build HelloUltraviolet/Fixtures/RejectedSource/Polymorphism/GenericTypeApplyArgCount --check --target-profile x86_64-win64 --build-progress off

exit=0
```

Verified bootstrap result after repair:

```text
error[E-TYP-2303]: Wrong number of type arguments
  --> .../GenericTypeApplyArgCount/Source/Main.uv:8:8
8 | public procedure genericTypeApplyArgCountReference(
```

Bootstrap repair owner:

- `LLVMBootstrap/cursive/src/04_analysis/typing/type_wf.cpp`

Repair summary:

`LowerType` already preserved generic type arguments as `TypeApply`, but
`TypeWFImpl` only applied generic arity checks to async and modal-state forms.
The user-defined `TypeApply` branch now consults `TypeParamsOf`, compares the
provided count against the required/defaulted range, and reports `E-TYP-2303`.
The `TypePath` branch also rejects references to generic declarations when the
source omits required type arguments.

## UVBOOT-0040: Explicit Generic Call Arguments Bypassed in Check Mode

Status: repaired.

SPEC-invalid specimen:

- `Fixtures/RejectedSource/Polymorphism/GenericCallArgCount/Source/Main.uv`

Spec obligation exercised:

- `rule.14.Generic-Call-ArgCount-Err`

Spec basis:

- `SPECIFICATION.md:12692-12695` requires `CallTypeArgs` to reject when
  `DefaultArgs(params_gen, [A_1, ..., A_k]) = bottom`.
- `SPECIFICATION.md:13975` assigns `E-TYP-2303` to wrong type-argument count.

Observed bootstrap result before repair:

```text
./LLVMBootstrap/cursive/build/windows/Release/Cursive.exe build HelloUltraviolet/Fixtures/RejectedSource/Polymorphism/GenericCallArgCount --check --target-profile x86_64-win64 --build-progress off

exit=0
```

Verified bootstrap result after repair:

```text
error[E-TYP-2303]: Wrong number of type arguments
  --> .../GenericCallArgCount/Source/Main.uv:8:5
8 |     return chooseLeft<
```

Bootstrap repair owner:

- `LLVMBootstrap/cursive/src/04_analysis/typing/type_infer.cpp`
- `LLVMBootstrap/cursive/src/04_analysis/typing/expr/call.cpp`
- `LLVMBootstrap/cursive/src/04_analysis/typing/expr/call_type_args.cpp`

Repair summary:

The resolver lowers `CallTypeArgsExpr` to `CallExpr` with populated
`generic_args`. Check-mode typing was running generic-call inference for every
`CallExpr`, including those with explicit `generic_args`, so it inferred
`<i32, bool>` from the value arguments and ignored the extra explicit `i32`.
Check mode now uses inference only when the source omitted explicit generic
arguments, and explicit generic-call arity failures report `E-TYP-2303`.

## UVBOOT-0041: Override Misuse Diagnostic Code Mapping

Status: repaired.

SPEC-invalid specimens:

- `Fixtures/RejectedSource/Polymorphism/OverrideAbstractMethod/Source/Main.uv`
- `Fixtures/RejectedSource/Polymorphism/OverrideMissingOnDefault/Source/Main.uv`

Spec obligations exercised:

- `rule.14.Override-Abstract-Err`
- `rule.14.Override-Missing-Err`

Spec basis:

- `SPECIFICATION.md:13107-13112` rejects `override` on implementations of
  abstract class methods.
- `SPECIFICATION.md:13122-13126` rejects replacements of concrete default
  methods that omit `override`.
- `SPECIFICATION.md:13990-13991` assigns `E-TYP-2501` and `E-TYP-2502`.

Observed bootstrap result before repair:

```text
error: Static rule failed without assigned diagnostic code: Override-Abstract-Err
error: Static rule failed without assigned diagnostic code: Override-Missing-Err
```

Verified bootstrap result after repair:

```text
error[E-TYP-2501]: `override` used on abstract procedure implementation
error[E-TYP-2502]: Missing `override` on concrete procedure replacement
```

Bootstrap repair owner:

- `LLVMBootstrap/cursive/src/04_analysis/typing/item/record_decl.cpp`

Repair summary:

The implementation checker already detected both override misuse rules, but
returned the static rule ids as diagnostics. The checker now reports the
concrete spec diagnostic codes `E-TYP-2501` and `E-TYP-2502`.

## UVBOOT-0042: Override Without Concrete Default Diagnostic Code Mapping

Status: repaired.

SPEC-invalid specimen:

- `Fixtures/RejectedSource/Polymorphism/OverrideNoConcrete/Source/Main.uv`

Spec obligation exercised:

- `rule.14.Override-NoConcrete`

Spec basis:

- `SPECIFICATION.md:13129-13133` rejects `override` on an implementing method
  when no implemented class contributes a concrete default method with that
  name.
- `SPECIFICATION.md:14007` assigns `E-UNS-0105` to override with no concrete
  procedure to override.

Observed bootstrap result before repair:

```text
error: Static rule failed without assigned diagnostic code: Override-NoConcrete
  --> .../OverrideNoConcrete/Source/Main.uv:7:10
```

Verified bootstrap result after repair:

```text
error[E-UNS-0105]: `override` used with no concrete procedure to override
  --> .../OverrideNoConcrete/Source/Main.uv:7:10
```

Bootstrap repair owner:

- `LLVMBootstrap/cursive/src/04_analysis/typing/item/record_decl.cpp`

Repair summary:

The implementation checker already enforced `Override-NoConcrete`, but returned
the static rule id as the diagnostic. The checker now reports the concrete spec
diagnostic code `E-UNS-0105`.

## UVBOOT-0043: Foundational Bitcopy Requires Explicit Clone

Status: repaired.

SPEC-invalid specimen:

- `Fixtures/RejectedSource/Polymorphism/BitcopyFieldNonBitcopy/Source/Main.uv`

Spec obligation exercised:

- `diag.14.FoundationalClasses`

Spec basis:

- `SPECIFICATION.md:13876` states that `Bitcopy`, `Clone`, `Drop`, and
  `FfiSafe` foundational class bounds are interpreted through intrinsic
  satisfaction judgments.
- `SPECIFICATION.md:13890-13900` defines `BitcopyTypeCore` for records in
  terms of all fields satisfying `BitcopyType`.
- `SPECIFICATION.md:14008` assigns `E-TYP-2622` to a `BitcopyType` with a
  non-`BitcopyType` field.

Observed bootstrap result before repair:

```text
error[E-TYP-2503]: Type does not implement required procedure from class or has incompatible signature
  --> .../BitcopyFieldNonBitcopy/Source/Main.uv:3:10
```

Verified bootstrap result after repair:

```text
error[E-TYP-2622]: `BitcopyType` has non-`BitcopyType` field
  --> .../BitcopyFieldNonBitcopy/Source/Main.uv:3:10
```

Bootstrap repair owner:

- `LLVMBootstrap/cursive/src/04_analysis/typing/item/record_decl.cpp`
- `LLVMBootstrap/cursive/src/04_analysis/typing/item/enum_decl.cpp`

Repair summary:

The record and enum implementation-conflict checks required every declaration
listing `Bitcopy` to also list `Clone`. The SPEC does not require that explicit
class-list pairing because `CloneType(T)` is discharged intrinsically for
`BitcopyType(T)`. The checker now allows `:< Bitcopy` without `:< Clone`, and
the record non-bitcopy-field path reports the concrete `E-TYP-2622` diagnostic.

## UVBOOT-0044: Reserved Capability and Foundational Name Protection

Status: repaired.

SPEC-invalid specimens:

- `Fixtures/RejectedSource/Polymorphism/CapabilityClassNameReserved/Source/Main.uv`
- `Fixtures/RejectedSource/Polymorphism/FoundationalClassNameReserved/Source/Main.uv`

Spec obligations exercised:

- `req.14.CapabilityClassNamesReserved`
- `req.14.FoundationalClassesSyntaxAndReservedNames`

Spec basis:

- `SPECIFICATION.md:13765` reserves the built-in capability class names
  `FileSystem`, `Network`, `HeapAllocator`, `ExecutionDomain`, and `Reactor`.
- `SPECIFICATION.md:13846` reserves the foundational names `Bitcopy`, `Clone`,
  `Drop`, `FfiSafe`, `Eq`, `Hasher`, `Hash`, `Iterator`, and `Step`.
- `SPECIFICATION.md:5750` assigns `E-MOD-1304` to identifier reuse from an
  enclosing scope, including universe names.

Observed bootstrap results before repair:

```text
FoundationalClassNameReserved:
error: Internal error: resolver diagnostic id `Validate-Module-Special-Shadow-Err` mapped to unregistered diagnostic code `E-CNF-0404`.

CapabilityClassNameReserved:
exit=0
```

Verified bootstrap results after repair:

```text
error[E-MOD-1304]: Unresolved module: path prefix did not resolve to a module
  --> .../FoundationalClassNameReserved/Source/Main.uv:3:8

error[E-MOD-1304]: Unresolved module: path prefix did not resolve to a module
  --> .../CapabilityClassNameReserved/Source/Main.uv:3:8
```

Bootstrap repair owner:

- `LLVMBootstrap/cursive/src/04_analysis/resolve/scopes.cpp`
- `LLVMBootstrap/cursive/src/04_analysis/resolve/resolve_module.cpp`

Repair summary:

`HeapAllocator` and `FileSystem` were missing from the bootstrap's protected
special-name set even though §14.9 reserves them. The resolver also mapped
special-name validation to an unregistered `E-CNF-0404` diagnostic. The
protected special-name set now includes those capability names, and reserved
primitive, special, and async universe-name validation routes through the
registered `E-MOD-1304` diagnostic.

Residual diagnostic text note:

The generated `E-MOD-1304` diagnostic text currently comes from the §11.5.7
module-path row instead of the §7.8 name-reuse row. The fixture validates the
registered code path; the duplicated diagnostic-code text ownership remains a
separate diagnostic registry cleanup.

## UVBOOT-0045: Inline Parameter Refinement `self` Diagnostic

Status: repaired.

SPEC-invalid specimen:

- `Fixtures/RejectedSource/Polymorphism/RefinementInlineSelfConstraint/Source/Main.uv`

Spec obligations exercised:

- `diag.14.RefinementTypes`
- `diag-table.14.RefinementPolymorphismDiagnostics`

Spec basis:

- `SPECIFICATION.md:13535` binds `self` only within a standalone refinement
  type.
- `SPECIFICATION.md:13971` requires `E-TYP-1956` when `self` is used in an
  inline parameter constraint.

Observed bootstrap result before repair:

```text
RefinementInlineSelfConstraint:
exit=0
```

Verified bootstrap result after repair:

```text
error[E-TYP-1956]: `self` used in inline parameter constraint
  --> .../RefinementInlineSelfConstraint/Source/Main.uv:3:8
```

Bootstrap repair owner:

- `LLVMBootstrap/cursive/src/04_analysis/typing/item/signature.cpp`
- `LLVMBootstrap/cursive/src/00_core/generated/static_rule_registry.inc`

Repair summary:

Procedure signature analysis now detects `self` in inline parameter refinement
predicates before lowering the parameter type through ordinary refinement
well-formedness. Standalone refinement types still bind `self`; inline
parameter constraints now report the required `E-TYP-1956` diagnostic.

## UVBOOT-0046: Duplicate Erased Free-Procedure Overload Signatures Accepted

Status: repaired.

SPEC-invalid specimen:

- `Fixtures/RejectedSource/Procedures/DuplicateErasedOverloadSignature/Source/Main.uv`

Spec obligation exercised:

- `req.15.DuplicateErasedOverloadSignaturesForbidden`

Spec basis:

- `SPECIFICATION.md:14598` states that same-name overloads with identical
  parameter-mode/type signatures after generic-parameter erasure are ill-formed.
- `SPECIFICATION.md:14614` assigns `E-SEM-3032` to duplicate overload
  signatures after generic erasure.

Observed bootstrap result before repair:

```text
Same-name free procedures were collected as an overload set, but declaration
typing had no duplicate-erased-signature check for the set.
```

Verified bootstrap result after repair:

```text
error[E-SEM-3032]: Duplicate signature in overload set
  --> .../DuplicateErasedOverloadSignature/Source/Main.uv:7:8
```

Bootstrap repair owner:

- `LLVMBootstrap/cursive/src/04_analysis/typing/typecheck.cpp`
- `LLVMBootstrap/cursive/src/00_core/generated/static_rule_registry.inc`

Repair summary:

Declaration typing now lowers each same-name free-procedure signature in the
procedure's generic scope, erases generic parameters to a canonical type
variable, compares parameter modes and `TypeKeyOf` parameter types, and reports
`E-SEM-3032` on duplicate erased overload signatures.

## UVBOOT-0047: Generic Monomorphization Diagnostics

Status: repaired in the workspace bootstrap and verified by:

```text
LLVMBootstrap/cursive/build/windows/Release/Cursive.exe build HelloUltraviolet/Fixtures/RejectedSource/Polymorphism/GenericInfiniteMonomorphization --target-profile x86_64-win64 --build-progress off
LLVMBootstrap/cursive/build/windows/Release/Cursive.exe build HelloUltraviolet/Fixtures/RejectedSource/Polymorphism/GenericInstantiationDepthLimit --target-profile x86_64-win64 --build-progress off
LLVMBootstrap/cursive/build/windows/Release/Cursive.exe build HelloUltraviolet --check --target-profile x86_64-win64 --build-progress off
LLVMBootstrap/cursive/build/windows/Release/Cursive.exe build HelloUltraviolet --target-profile x86_64-win64 --build-progress off
HelloUltraviolet/build/bin/HelloUltraviolet.exe
HelloUltraviolet/build/bin/HelloUltraviolet.exe --audit
```

Reference source:

- `Fixtures/RejectedSource/Polymorphism/GenericInfiniteMonomorphization/Source/Main.uv`
- `Fixtures/RejectedSource/Polymorphism/GenericInstantiationDepthLimit/Source/Main.uv`

Spec obligations exercised:

- `req.GenericInfiniteMonomorphizationRejected`
- `req.GenericInstantiationDepthLimit`

Spec basis:

- `SPECIFICATION.md:12818` requires infinite monomorphization recursion to be
  rejected.
- `SPECIFICATION.md:12819` sets the maximum instantiation depth to 128.
- `SPECIFICATION.md:14132-14133` defines `E-TYP-2307` and `E-TYP-2308`.

Observed bootstrap result before repair:

Spec-valid generic-instantiation fixtures reached lowering and caused an
unhandled stack overflow instead of reporting the required SPEC diagnostics.

Verified bootstrap result after repair:

```text
GenericInfiniteMonomorphization: exit=1, E-TYP-2307
GenericInstantiationDepthLimit: exit=1, E-TYP-2308
```

Bootstrap repair owner:

- `LLVMBootstrap/cursive/src/05_codegen/lower/expr/call.cpp`
- `LLVMBootstrap/cursive/include/05_codegen/lower/lower_expr.h`
- `LLVMBootstrap/cursive/src/06_driver/pipeline.cpp`
- `LLVMBootstrap/cursive/src/CMakeLists.txt`

Repair summary:

Generic-call lowering now records active generic declaration frames, detects
same-declaration recursion with changed instantiation arguments, and emits
`E-TYP-2307`. The same path checks active instantiation depth against
`MonomorphizeContext::kMaxDepth` and emits `E-TYP-2308`. The Windows bootstrap
compiler executable reserves enough native stack for the SPEC-defined
128-instantiation boundary.

## UVBOOT-0048: Binary Expression Resolution Stack Overflow

Status: repaired in the workspace bootstrap and verified by:

```text
LLVMBootstrap/cursive/build/Release/Cursive.exe build HelloUltraviolet --check --target-profile x86_64-win64 --build-progress off
LLVMBootstrap/cursive/build/Release/Cursive.exe build HelloUltraviolet --target-profile x86_64-win64 --build-progress off
HelloUltraviolet/build/bin/HelloUltraviolet.exe
HelloUltraviolet/build/bin/HelloUltraviolet.exe --audit
LLVMBootstrap/cursive/build/Release/cursive_resolver_binary_chain_conformance_test.exe
```

Reference source:

- `Source/Reference/Parallelism/ExecutionDomains.uv`
- `Source/Reference/Parallelism/CaptureSemantics.uv`
- generated catalog and reference-runner aggregation source containing long
  binary expression chains

Spec obligations exercised:

- `ResolveExpr-Hom`
- `requirement.20.ExecutionDomainContextMethods`
- `rule.20.T-GpuIntrinsic`
- `rule.20.EvalSigma-GPU-Parallel`
- `rule.20.EvalSigma-GPU-Dispatch`
- `rule.20.EvalSigma-GpuBarrier`

Spec basis:

- `SPECIFICATION.md:5734-5738` requires expression resolution to traverse
  expression substructure homomorphically.
- `SPECIFICATION.md:16272-16290` defines logical operator expressions as
  unbounded left chains in the grammar.
- `SPECIFICATION.md:20546` requires subexpressions to preserve left-to-right
  evaluation order.
- `SPECIFICATION.md:21683-21693` defines `ctx.gpu()` as an execution-domain
  constructor.
- `SPECIFICATION.md:21781-21794` defines GPU intrinsic calls in GPU contexts.
- `SPECIFICATION.md:21840-21866` defines GPU parallel, dispatch, and barrier
  dynamic semantics.

Spec-valid specimen:

```ultraviolet
return parallel context~>gpu() [
    workgroup: (1024usize, 1usize, 1usize),
    workgroups: (1usize, 1usize, 1usize)
] {
    dispatch index in 0usize..1usize [reduce: +, workgroup: (1024usize, 1usize, 1usize)] {
        gpu_barrier()
        gpu_linear_id() + (payload.value as usize) + index
    }
}
```

Observed bootstrap result before repair:

When the existing GPU reference functions were made reachable from the
accepted runtime flow, `Cursive.exe build HelloUltraviolet --check` crashed
with an unhandled stack overflow in `ResolveExpr` while resolving binary
expressions. A minimal GPU probe compiled cleanly, isolating the crash to the
full corpus' long generated expression chains rather than the GPU construct.

Bootstrap repair owner:

- `LLVMBootstrap/cursive/src/04_analysis/resolve/resolve_expr.cpp`

Repair summary:

Expression resolution now resolves same-operator binary chains iteratively for
all binary operators while preserving the original AST shape. The existing
region-allocation `^` special case still runs before the iterative chain path.
The focused compiler regression
`cursive_resolver_binary_chain_conformance_test` now builds a catalog-scale
generated membership expression with `--check`, covering the source shape that
exposed this resolver defect without relying on conformance trace volume.

## UVBOOT-0049: Configured CPU Execution Domain Lowering

Status: repaired in the workspace bootstrap and verified by:

```text
LLVMBootstrap/cursive/build/Release/Cursive.exe build HelloUltraviolet --check --target-profile x86_64-win64 --build-progress off
LLVMBootstrap/cursive/build/Release/Cursive.exe build HelloUltraviolet --target-profile x86_64-win64 --build-progress off
HelloUltraviolet/build/bin/HelloUltraviolet.exe
HelloUltraviolet/build/bin/HelloUltraviolet.exe --audit
python3 Tools/ExtractObligationLedger.py --check
git -c filter.lfs.process= -c filter.lfs.clean=cat -c filter.lfs.smudge=cat \
  -c filter.lfs.required=false diff --check -- \
  HelloUltraviolet/Source/Reference/Parallelism/ParallelBlocks.uv \
  HelloUltraviolet/Source/Reference/Parallelism/Spawn.uv \
  HelloUltraviolet/Source/Reference/Parallelism/Dispatch.uv \
  HelloUltraviolet/Source/Reference/Parallelism/ExecutionDomains.uv \
  LLVMBootstrap/cursive/include/05_codegen/intrinsics/builtins.h \
  LLVMBootstrap/cursive/src/05_codegen/intrinsics/builtins.cpp \
  LLVMBootstrap/cursive/src/05_codegen/intrinsics/intrinsics_interface.cpp \
  LLVMBootstrap/cursive/src/05_codegen/lower/expr/method_call.cpp \
  LLVMBootstrap/cursive/src/05_codegen/llvm/emit/ir/async/spawn.cpp \
  LLVMBootstrap/cursive/runtime/include/cursive_rt.h \
  LLVMBootstrap/cursive/runtime/include/cursive_rt_language_symbols.h \
  LLVMBootstrap/cursive/runtime/src/context/context.c \
  LLVMBootstrap/cursive/runtime/src/concurrency/parallel.c \
  LLVMBootstrap/cursive/runtime/src/internal/rt_internal.h
```

Reference source:

- `Source/Reference/Parallelism/ExecutionDomains.uv`

Spec obligations exercised:

- `requirement.20.ExecutionDomainContextMethods`
- `rule.20.T-Parallel`
- `rule.20.DomainCtorOk`
- `rule.20.ResolveSpawnOpt-Affinity`
- `rule.20.ResolveSpawnOpt-Priority`

Spec basis:

- `SPECIFICATION.md:21489-21490` accepts `ctx.cpu(mask)` and
  `ctx.cpu(mask, priority)` when the arguments typecheck as `CpuSet` and
  `Priority`.
- `SPECIFICATION.md:21683-21690` defines `ctx.cpu(mask)` as a CPU execution
  domain restricted to the mask and `ctx.cpu(mask, priority)` as the same domain
  with a default task priority.
- `SPECIFICATION.md:22120-22121` requires spawn affinity and priority to govern
  worker selection and task priority.

Spec-valid specimen:

```ultraviolet
internal procedure cpuMaskPriorityDomainReference(context: Context) -> i32 {
    let affinity: CpuSet = 1u64
    let priority: Priority = Priority::Normal
    return parallel context~>cpu(affinity, priority) {
        15
    }
}
```

Observed bootstrap result before repair:

`Cursive.exe build HelloUltraviolet --check` accepted the source, but full
codegen failed at `LLVMBootstrap/cursive/src/05_codegen/lower/expr/call.cpp:1310`
because `Context` method-call lowering invoked `LowerArgs` with empty
parameter mode/type lists for `ctx.cpu(mask)` and `ctx.cpu(mask, priority)`.
After the lowering arity defect was repaired, the final link step exposed the
matching runtime gap: the configured CPU domain constructor was not exported
from the Ultraviolet runtime archive.

Bootstrap repair owner:

- `LLVMBootstrap/cursive/src/05_codegen/lower/expr/method_call.cpp`
- `LLVMBootstrap/cursive/src/05_codegen/intrinsics/builtins.cpp`
- `LLVMBootstrap/cursive/src/05_codegen/intrinsics/intrinsics_interface.cpp`
- `LLVMBootstrap/cursive/runtime/include/cursive_rt.h`
- `LLVMBootstrap/cursive/runtime/include/cursive_rt_language_symbols.h`
- `LLVMBootstrap/cursive/runtime/src/context/context.c`
- `LLVMBootstrap/cursive/runtime/src/concurrency/parallel.c`

Repair summary:

Context method-call lowering now uses the semantic `Context` method signature
for configured CPU-domain arguments and routes nonzero-arity `cpu` calls to a
configured runtime constructor. `C0ExecutionDomain` now carries affinity and
default priority, the runtime exports the configured constructor for both
language symbol prefixes, and spawned/dispatch work inherits the enclosing
domain defaults unless an explicit spawn option supplies a different value.

## UVBOOT-0050: Async State Resume and Composition Lowering

Status: repaired in the workspace bootstrap and verified by:

```text
LLVMBootstrap/cursive/build/Release/Cursive.exe build HelloUltraviolet --target-profile x86_64-win64 --build-progress off
HelloUltraviolet/build/bin/HelloUltraviolet.exe
HelloUltraviolet/build/bin/HelloUltraviolet.exe --audit
LLVMBootstrap/cursive/build/Release/Cursive.exe build HelloUltraviolet --check --target-profile x86_64-win64 --build-progress off
python3 Tools/ExtractObligationLedger.py --check
```

Reference source:

- `Source/Reference/Async/StateMachine.uv`
- `Source/Reference/Async/CompositionForms.uv`
- `Source/Reference/Async/AsyncKeyIntegration.uv`

Spec obligations exercised:

- `rule.21.AsyncStateMachine`
- `rule.21.AsyncResume`
- `rule.21.AsyncYield`
- `rule.21.AsyncYieldFrom`
- `rule.21.AsyncSync`
- `rule.21.AsyncAll`
- `rule.21.AsyncRace`
- `rule.21.AsyncComposition`

Spec basis:

- `SPECIFICATION.md:22695-22734` defines async values as modal state-machine
  values with `@Completed`, `@Suspended`, and `@Failed` states.
- `SPECIFICATION.md:23718` defines `Async@Suspended.resume(input)` with a
  `unique` receiver and an input value.
- `SPECIFICATION.md:23891-23934` defines streaming race suspension and
  resumption behavior: the yielded arm remains the active arm when resumed.
- `SPECIFICATION.md:24369-24373` defines `async` frame creation as allocating
  a fresh frame.

Spec-valid specimens:

```ultraviolet
let suspended: unique AsyncReferenceComputation = asyncSuspendsReference(18)
return if suspended is {
    @Suspended { output } {
        let resumed = suspended~>resume(output + 1)
        if resumed is {
            @Completed { value } { value == 19 }
            @Suspended { false }
            @Failed { false }
        }
    }
    @Completed { false }
    @Failed { false }
}
```

```ultraviolet
let delegated: AsyncReferenceComputation = asyncYieldReleaseFromReference(42)
```

Observed bootstrap result before repair:

The compiler accepted portions of the async reference source but either
miscompiled the runtime behavior or rejected the spec-valid source at later
bootstrap boundaries. The executable printed the failing compiled reference
symbol, including:

```text
catalog compiled symbol failed: runAsyncStateMachineManualResumeReference
reference failed: catalogCompiledSymbolsExecute
exit=1
```

Failure analysis:

Several async-specific bootstrap paths used generic or desugared types after
the semantic checker had already established a more precise async signature:

- Fresh async creation did not materialize to an explicitly expected
  permission-qualified async type, preventing ordinary async calls from being
  manually resumed through the specified `unique` receiver.
- `if ... is` modal-state narrowing lost the receiver permission on async modal
  states, so a unique async value narrowed to `Async@Suspended` no longer had
  the permission required by `resume`.
- Streaming-race suspension returned the yielded value but did not retain the
  yielded arm's frame pointer in the async payload, so resumption could not
  continue the active yielded arm.
- The resume runtime call did not consistently materialize receiver and input
  values as addressable pointers for the runtime ABI.
- `yield` and `yield from` lowering looked up async signatures through the raw
  return type and missed aliases such as `AsyncReferenceComputation`, causing
  yielded input values to lower as `unit`.
- `Async@Suspended.resume(input)` lowering kept the builtin generic parameter
  `TIn` instead of specializing it from the receiver's concrete async type,
  so the addressable input temporary could be created with the wrong size.
- `yield` resume continuations loaded the runtime input but did not leave an
  addressable materialized value for the following source binding, so
  `let resumed = yield value` could bind the default value instead of the
  caller-provided resume input.
- Builtin `Async@Suspended.resume` dispatch could queue an instantiated generic
  state-method body before selecting the runtime builtin symbol. The builtin
  declaration body is intentionally empty, so the generated method did not
  execute the SPEC-defined runtime resume operation.

Bootstrap repair owner:

- `LLVMBootstrap/cursive/src/04_analysis/typing/type_infer.cpp`
- `LLVMBootstrap/cursive/src/04_analysis/typing/if_case_check.cpp`
- `LLVMBootstrap/cursive/src/05_codegen/llvm/emit/ir/async/race_yield.cpp`
- `LLVMBootstrap/cursive/src/05_codegen/llvm/emit/ir/async/yield.cpp`
- `LLVMBootstrap/cursive/src/05_codegen/llvm/emit/ir/call/direct.cpp`
- `LLVMBootstrap/cursive/src/05_codegen/lower/expr/yield_expr.cpp`
- `LLVMBootstrap/cursive/src/05_codegen/lower/expr/yield_from_expr.cpp`
- `LLVMBootstrap/cursive/src/05_codegen/lower/expr/method_call.cpp`

Repair summary:

Async creation now honors the expected permission-qualified async type when a
fresh frame is created. Modal-state pattern narrowing preserves receiver
permission for narrowed async states. Streaming race stores the yielded arm
frame pointer in the async payload before returning `@Suspended`. Async resume
calls materialize receiver and input values as runtime ABI pointers. `yield`
and `yield from` lowering resolve async signatures through aliases. Method-call
lowering specializes the builtin `resume` input parameter from the receiver's
concrete `Async<Out, In, Result, Error>` signature before lowering the input
argument. Yield resume lowering now materializes the resume input into
addressable continuation storage and preserves that storage for the following
binding. Builtin modal runtime methods such as `Async@Suspended.resume` are
selected before generic state-method instantiation, so `a~>resume(input)`
routes to `runtime::async::resume`.

Verified bootstrap result after repair:

```text
Visual Studio bootstrap build wrapper: exit=0, rebuilt method_call.cpp, yield.cpp, and Cursive.exe
Cursive.exe clean HelloUltraviolet: exit=0
Cursive.exe build HelloUltraviolet --target-profile x86_64-win64 --build-progress off --max-errors 20: exit=0, 8 warnings, 2 infos
HelloUltraviolet.exe: exit=0, 0-byte stdout/stderr
HelloUltraviolet.exe --audit: exit=0, 0-byte stdout/stderr
Cursive.exe build HelloUltraviolet --check --target-profile x86_64-win64 --build-progress off --max-errors 20: exit=0, 8 warnings, 2 infos
python3 Tools/ExtractObligationLedger.py --check: exit=0, obligations=6045
```

## UVBOOT-0052: Statement-Position Quote Splice Parsing and Expansion

Status: repaired in the workspace bootstrap and verified by
`Cursive.exe build HelloUltraviolet --check --target-profile x86_64-win64`.

Reference source:

- `Source/Reference/Comptime/QuoteSpliceEmission.uv`

Spec obligations exercised:

- `rule.22.Parse-Quote-Pattern`
- `def.22.SpliceCompat`
- `requirement.22.StringSpliceIdentifierHygiene`
- `def.22.RenderSplice`
- `rule.22.CtEval-Quote`
- `requirement.22.QuoteBuildSpliceOrder`
- `requirement.22.QuoteSpliceEmissionLowering`

Spec basis:

- `SPECIFICATION.md:25249-25255` defines `quote pattern`, expression splices,
  and identifier splices.
- `SPECIFICATION.md:25314-25319` defines splice compatibility, including
  `SpliceCompat(Stmt, Ast::Stmt)` and string-valued identifier splices.
- `SPECIFICATION.md:25331-25335` permits `$ident` in identifier-expression,
  identifier-pattern, typed-pattern, and parameter-binding positions and makes
  string-valued identifier splices bind in the emission environment.
- `SPECIFICATION.md:25341-25348` requires quoted statement parsing to use the
  ordinary statement parser extended with splice nodes and requires statement
  splices to render `Ast::Stmt` or `Ast::Expr` payloads.
- `SPECIFICATION.md:25363-25368` defines quote evaluation as parsing the
  quoted body, building splices, and returning the resulting `Ast`.

Spec-valid specimen:

```ultraviolet
let binding_name = "spliced_reference_value"
let binding_pattern: Ast::Pattern = quote pattern { $binding_name: i32 }
let binding_expr: Ast::Expr = quote { $binding_name + 1 }
let return_statement: Ast::Stmt = quote { return $(binding_expr) }
let third_ast: Ast::Item = quote {
    internal procedure emittedIdentifierAndPatternSpliceReferenceValue() -> i32 {
        let $(binding_pattern) = 52
        $(return_statement);
    }
}
emitter~>emit(third_ast)
```

Observed bootstrap result before repair:

The compiler accepted the outer quote expression but expanded the emitted
procedure as though the statement-position splice did not contribute the
spliced `return` statement. The later procedure body check then rejected the
emitted source:

```text
error[E-TYP-1507]: Procedure with non-unit return type requires explicit return statement
  --> .../Source/Reference/Comptime/QuoteSpliceEmission.uv:25:22
```

Failure analysis:

The parser's statement expression-start predicate did not include the `$`
operator. In a quoted block, a statement beginning with `$(...)` therefore
entered the parser's error-statement path instead of producing an expression
statement containing `SpliceExprNode`. The quote builder also rendered
expression splices through the expression renderer from expression-statement
positions, so an `Ast::Stmt` value was not rendered through the statement
splice renderer when it appeared as a statement body contribution.

Bootstrap repair owner:

- `LLVMBootstrap/cursive/src/02_source/parser/stmt/expr_stmt.cpp`
- `LLVMBootstrap/cursive/src/03_comptime/quote.cpp`

Repair summary:

The statement expression-start predicate now treats `$` as an expression start,
allowing `$(` to reach the splice parser in statement position. The quote
builder now detects expression statements whose value is a splice expression
and renders them through `RenderStmtSplice`. It also handles a block tail that
is an `Ast::Stmt` splice by appending the rendered statement and clearing the
tail expression, so both parsed statement-list and block-tail positions follow
the statement-splice rendering rule.

Verified bootstrap result after repair:

```text
Visual Studio bootstrap build wrapper: exit=0, rebuilt expr_stmt.cpp, quote.cpp, and Cursive.exe
Focused quote statement-splice probe: exit=0
Cursive.exe build HelloUltraviolet --check --target-profile x86_64-win64 --build-progress off: exit=0, total diagnostic set is five warnings plus two infos
```

## UVBOOT-0051: Shared Closure Capture Warning Duplication

Status: repaired in the workspace bootstrap and verified by
`Cursive.exe build HelloUltraviolet --check --target-profile x86_64-win64`.

Reference source:

- `Source/Reference/Keys/AcquisitionBlocks.uv`

Spec obligations exercised:

- `rule.19.ClosureInvocationSharedCaptures`
- `diag.19.Key-Warn-SharedCapture`

Spec basis:

- `SPECIFICATION.md:17681-17684` defines closure capture sets from free
  variables in closure bodies.
- `SPECIFICATION.md:20649-20656` defines local closure invocation with
  `shared` captures as acquiring required keys using lexical roots, executing
  the closure body, and releasing keys at invocation end.
- `SPECIFICATION.md:20706` assigns `W-CON-0009` to the compile-time warning
  condition "Closure captures `shared` data".

Spec-valid specimen:

```ultraviolet
internal procedure localClosureSharedCaptureValue() -> i32 {
    var shared_value: shared i32 = 13
    let reader = || -> i32 {
        #shared_value read {
            return shared_value + 0
        }
        return 0
    }
    return reader()
}
```

Observed bootstrap result before repair:

The compiler accepted the source but emitted `W-CON-0009` twice for the same
closure-body span:

```text
warning[W-CON-0009]: Closure captures shared data
  --> .../Source/Reference/Keys/AcquisitionBlocks.uv:38:28
warning[W-CON-0009]: Closure captures shared data
  --> .../Source/Reference/Keys/AcquisitionBlocks.uv:38:28
```

Bootstrap owner:

- `LLVMBootstrap/cursive/src/04_analysis/typing/expr/closure_expr.cpp`

Failure analysis:

The closure typing path evaluates the shared-capture warning from more than
one contextual pass over the same closure body. Both passes reached the same
diagnostic site and appended the same diagnostic id and span. The SPEC defines
the warning condition for a closure that captures `shared` data; duplicate
emission for the identical code/span does not add a distinct source condition
and pollutes the reference corpus diagnostic surface.

Required bootstrap behavior:

For a single closure capture condition at a single source span, the checker
must emit one `W-CON-0009` diagnostic.

Repair:

- `LLVMBootstrap/cursive/src/04_analysis/typing/expr/closure_expr.cpp` now
  checks the active diagnostic stream for an existing `W-CON-0009` at the same
  span before appending the shared-capture warning.

Verified bootstrap result after repair:

```text
Visual Studio bootstrap build wrapper: exit=0, rebuilt closure_expr.cpp and Cursive.exe
Cursive.exe build HelloUltraviolet --check --target-profile x86_64-win64 --build-progress off: exit=0, one W-CON-0009 at AcquisitionBlocks.uv:38, total diagnostic set is five warnings plus two infos
Cursive.exe build HelloUltraviolet --target-profile x86_64-win64 --build-progress off: exit=0, same diagnostic set
HelloUltraviolet.exe: exit=0, 0-byte stdout/stderr
HelloUltraviolet.exe --audit: exit=0, 0-byte stdout/stderr
python3 Tools/ExtractObligationLedger.py --check: exit=0, obligations=6045
```

## UVBOOT-0053: Reactor Builtin Method Calls and Future Alias Inference

Status: repaired in the workspace bootstrap and verified by
`Cursive.exe build HelloUltraviolet --check --target-profile x86_64-win64`.

Reference source:

- `Source/Reference/Async/SuspensionForms.uv`

Spec obligations exercised:

- `rule.21.Wait`
- `rule.21.Wait-Tracked`
- `rule.21.Wait-Spawned`
- `rule.13.ContextCapabilityFields`

Spec basis:

- `SPECIFICATION.md:13872-13875` defines the generic `Reactor::run` and
  `Reactor::register` methods; `register` takes `Future<T, E>` and returns
  `Tracked<T, E>`.
- `SPECIFICATION.md:13984-13996` requires calls on dynamic receivers of builtin
  capability classes, including `Reactor`, to lower to builtin method symbols
  rather than ordinary vtable-call sequences.
- `SPECIFICATION.md:29042-29051` includes `Reactor::run` and
  `Reactor::register` in the builtin method table.

Spec-valid specimen:

```ultraviolet
internal procedure asyncTrackedFutureReference(value: i32) -> Future<i32, bool> {
    return value
}

internal procedure asyncWaitTrackedReference(context: Context) -> bool {
    let tracked: Tracked<i32, bool> =
        context.reactor~>register(asyncTrackedFutureReference(25))
    let result: i32 | bool = wait tracked
    return i32BoolUnionIsValue(result, 25)
}
```

Observed bootstrap result before repair:

The compiler rejected the builtin `Reactor` method call even though
`context.reactor` has the SPEC-defined dynamic builtin capability type:

```text
error[E-TYP-2540]: Cannot call non-vtable-eligible procedure on dynamic receiver
```

After the dynamic builtin receiver path was selected, generic argument
inference still failed for the `Future<T, E>` parameter when the actual return
type was represented through the canonical async alias:

```text
error[E-SEM-2533]: Argument type is incompatible with parameter type
```

After those semantic defects were repaired, full object emission exposed the
same source path as a runtime-interface defect:

```text
[cursive] EmitObjForModule: module=HelloUltraviolet::Reference::Async LLVM module emission failed before object generation
error[E-OUT-0402]: Failed to emit object file (codegen or write)
```

Bootstrap owner:

- `LLVMBootstrap/cursive/src/04_analysis/typing/expr/method_call.cpp`
- `LLVMBootstrap/cursive/src/05_codegen/intrinsics/intrinsics_interface.cpp`
- `LLVMBootstrap/cursive/runtime/include/cursive_rt.h`
- `LLVMBootstrap/cursive/runtime/src/concurrency/parallel.c`

Failure analysis:

The method-call checker applied ordinary dynamic-dispatch vtable eligibility
before recognizing the builtin capability class receiver path. That rejected
SPEC-defined `Reactor` builtin methods, whose calls are required to lower
through builtin method symbols. The generic method binder also matched formal
and actual type paths without first following top-level aliases, so a
`Future<T, E>` formal did not bind against the actual
`Async<(), (), T, E>` representation. Once the source reached codegen, the
runtime declaration table classified `Reactor::register` as a runtime symbol
but did not provide its concrete type-erased C ABI signature, so
`RuntimeDeclsCover` rejected the module before object generation.

Required bootstrap behavior:

Calls to `Reactor` methods through `Context.reactor` must use builtin
capability method typing/lowering, and generic inference for builtin methods
must bind through aliases such as `Future<T, E>`. A lowered runtime reference
to `Reactor::register` must also have a matching runtime ABI declaration and
implementation prototype.

Repair:

- `LLVMBootstrap/cursive/src/04_analysis/typing/expr/method_call.cpp` now keeps
  builtin capability dynamic receiver calls on the builtin method path instead
  of applying the ordinary vtable eligibility rejection.
- Method-call generic binding now follows top-level aliases when comparing
  applied type paths, allowing `Future<T, E>` to bind against the canonical
  async representation.
- `LLVMBootstrap/cursive/src/05_codegen/intrinsics/intrinsics_interface.cpp`
  now declares the type-erased runtime ABI for `Reactor::register`.
- The runtime header and implementation now expose the matching
  `Reactor::register` C prototype.

Verified bootstrap result after repair:

```text
Visual Studio bootstrap build wrapper: exit=0, rebuilt method_call.cpp and Cursive.exe
Cursive.exe build HelloUltraviolet --check --target-profile x86_64-win64 --build-progress on --debug pipeline --max-errors 1: exit=0, total diagnostic set is five warnings plus two infos
Visual Studio bootstrap build wrapper: exit=0, rebuilt intrinsics_interface.cpp, runtime objects, Cursive.exe, and UltravioletRT.lib
Cursive.exe build HelloUltraviolet --target-profile x86_64-win64 --build-progress off --max-errors 1: exit=0, total diagnostic set is five warnings plus two infos
HelloUltraviolet.exe: exit=0, 0-byte stdout/stderr
HelloUltraviolet.exe --audit: exit=0, 0-byte stdout/stderr
```

## UVBOOT-0077: Compile-Time Reflection Type Names Exclude Source Spans

Spec-valid accepted source:

```ultraviolet
derive target EmitComptimeDeriveReference(target: Type) |: emits ComptimeDeriveClass {
    let target_name = introspect~>type_name(target)
    let target_module = introspect~>module_path(target)
    let ast: Ast::Item = if target_name ==
        "HelloUltraviolet::Reference::Comptime::ComptimeDerivedReference" &&
        target_module == diagnostics~>current_module()
    {
        quote {
            internal procedure emittedComptimeDeriveReferenceValue() -> i32 {
                return 37
            }
        }
    } else {
        quote {
            internal procedure emittedComptimeDeriveReferenceValue() -> i32 {
                return 0
            }
        }
    }
    emitter~>emit(ast)
}
```

Observed bootstrap result before repair:

```text
Cursive.exe build HelloUltraviolet --check --target-profile x86_64-win64 --build-progress off --max-errors 40: exit=0
warning[W-CTE-0071]: HelloUltraviolet::Reference::Comptime::ComptimeDerivedReference [54:10-56:2]
warning[W-CTE-0071]: HelloUltraviolet::Reference::Comptime

Cursive.exe build HelloUltraviolet --target-profile x86_64-win64 --build-progress on --max-errors 20: exit=0
HelloUltraviolet.exe: exit=1
catalog compiled symbol failed: runComptimeDeriveTargetsReference
reference failed: catalogCompiledSymbolsExecute
reference failed: runComptimeDeriveTargetsReference
```

Failure analysis:

`SPECIFICATION.md` §22.3.5 defines `CtBuiltin-Reflect-TypeName` as returning
`CtString(TypeRender(T))`. `TypeRender(TypePath(...))` renders the type path
text and does not include diagnostic or source span decoration. The bootstrap
reflection builtin used the AST debug renderer with default dump options, whose
default includes source spans. As a result, `introspect.type_name(target)` for
a derive target subject returned
`HelloUltraviolet::Reference::Comptime::ComptimeDerivedReference [54:10-56:2]`
instead of the SPEC `TypeRender` string.

Required bootstrap behavior:

Compile-time reflection type-name strings must be stable SPEC renderings of
types, not debug dumps. Source spans remain available through the dedicated
reflection metadata fields and `diagnostics.current_span`; they are not part of
`type_name`.

Repair:

- `LLVMBootstrap/cursive/src/03_comptime/reflect.cpp` now renders
  `introspect.type_name` with `ast::DumpOptions.include_spans = false`.

Verified bootstrap result after repair:

```text
Visual Studio bootstrap build wrapper, target=cursive: exit=0, rebuilt reflect.cpp and Cursive.exe
Cursive.exe build HelloUltraviolet --check --target-profile x86_64-win64 --build-progress off --max-errors 20: exit=0, warnings=10, infos=3
Cursive.exe build HelloUltraviolet --target-profile x86_64-win64 --build-progress on --max-errors 20: exit=0, warnings=10, infos=3, duration=366.10s
HelloUltraviolet.exe: exit=0, 0-byte stdout/stderr
HelloUltraviolet.exe --audit: exit=0, 0-byte stdout/stderr
```

## UVBOOT-0076: Pure-Comptime Proof Handles Computed Boolean Predicate Bodies

Spec-valid accepted source:

```ultraviolet
comptime internal procedure contractCompileTimePredicate() -> bool {
    let lower: usize = 2usize
    let upper: usize = 3usize
    return lower < upper && upper == lower + 1usize
}

internal procedure contractPureComptimeReference(value: i32) -> i32
|: contractCompileTimePredicate()
{
    return value
}

internal procedure contractPureComptimeExercise(value: i32) -> bool {
    return contractPureComptimeReference(value) == value
}
```

Observed bootstrap result before repair:

```text
Cursive.exe build HelloUltraviolet --check --target-profile x86_64-win64 --build-progress off --max-errors 20: exit=1
error[E-SEM-2801]: Contract predicate not provable outside `[[dynamic]]` scope
  --> C:/dev/ultraviolet/HelloUltraviolet/Source/Reference/Procedures/Contracts.uv:368:5
368 |     return contractPureComptimeReference(value) == value
```

Failure analysis:

`SPECIFICATION.md` §15 admits `Pure-Comptime` as a contract predicate purity
rule, and Chapter 22 defines compile-time procedures as Phase 2-only callable
bindings in compile-time contexts. The predicate above is a no-argument
compile-time procedure whose result is deterministically `true`, but the
bootstrap prover recognized only compile-time boolean procedures whose bodies
returned the literal token `true`.

That literal-only recognizer made the reference source look unprovable at the
ordinary call site even though the predicate body used only immutable locals,
constant integer literals, pure integer arithmetic, and pure boolean
comparison/conjunction.

Required bootstrap behavior:

Static contract proof for `Pure-Comptime` must evaluate simple no-argument
compile-time boolean predicates whose bodies compute a constant boolean result
through immutable local bindings and pure constant expressions. Literal
`return true` is one valid case, not the whole proof surface.

Repair:

`LLVMBootstrap/cursive/src/04_analysis/typing/expr/call.cpp` now proves
no-argument compile-time boolean predicates with immutable `let` locals,
constant integer and boolean expressions, arithmetic, comparisons, equality,
negation, conjunction, and disjunction before using that result in contract
precondition checking.

Verified bootstrap result after repair:

```text
Visual Studio bootstrap build wrapper, target=cursive: exit=0, rebuilt call.cpp and Cursive.exe
Cursive.exe build HelloUltraviolet --check --target-profile x86_64-win64 --build-progress off --max-errors 20: exit=0, warnings=10, infos=3
Cursive.exe build HelloUltraviolet --target-profile x86_64-win64 --build-progress on --max-errors 20: exit=0, warnings=10, infos=3, duration=384.56s
HelloUltraviolet.exe: exit=0, 0-byte stdout/stderr
HelloUltraviolet.exe --audit: exit=0, 0-byte stdout/stderr
```

## UVBOOT-0066: Permission Diagnostic Routing and Shared Write Gate

Spec-defined rejected source:

```ultraviolet
public procedure constMutationPermissionReference() -> i32 {
    var cell: const PermissionDiagnosticCell = PermissionDiagnosticCell { value: 1 }
    cell.value = 2
    return cell.value + 0
}

public procedure uniqueInactiveUseReference() -> i32 {
    var cell: unique PermissionDiagnosticCell = PermissionDiagnosticCell { value: 1 }
    return consumeAfterBorrow(cell, move cell)
}

public procedure sharedMutationWithoutKeyReference() -> i32 {
    var cell: shared PermissionDiagnosticCell = PermissionDiagnosticCell { value: 1 }
    cell.value = 2
    return cell.value + 0
}
```

Observed bootstrap results before repair:

```text
ConstMutation: emitted E-SEM-3132 for aggregate const-path mutation
UniqueInactiveUse: the initial non-overlapping source shape compiled; the ArgPass source shape correctly exposed E-TYP-1602
SharedMutationWithoutKey: exited 0 instead of emitting E-TYP-1604
ReceiverPermissionMismatch: emitted E-TYP-1605 as specified
```

Bootstrap owners:

- `LLVMBootstrap/cursive/src/04_analysis/typing/stmt/assign_stmt.cpp`
- `LLVMBootstrap/cursive/src/04_analysis/typing/stmt/compound_assign_stmt.cpp`
- `LLVMBootstrap/cursive/src/04_analysis/memory/borrow_bind.cpp`

Failure analysis:

`SPECIFICATION.md` §10.4.7 defines permission-admissibility diagnostics
`E-TYP-1601`, `E-TYP-1602`, `E-TYP-1604`, and `E-TYP-1605`. The shared-write
assignment path checked for read-then-write and covering-key conflicts, but
when no key was held and no more specific key conflict applied, it marked the
write as if a key existed and allowed the assignment to compile.

Const mutation also has overlapping SPEC ownership: §10.4.7 names
`E-TYP-1601` for mutation through a `const` path, while §18.11 names
`E-SEM-3132` for assignment through `const` permission. The implemented
reading, recorded in `Audit/SpecClarificationsNeeded.md`, is that aggregate
const-path mutation exercises `E-TYP-1601`, while direct assignment to a root
`const` binding keeps the Chapter 18 assignment diagnostic.

Required bootstrap behavior:

- Aggregate mutation through a `const` permission-qualified path emits
  `E-TYP-1601`.
- Moving a unique value while it is inactive from an earlier non-consuming
  admissible argument emits `E-TYP-1602`.
- Direct mutation through a `shared` field path without a held write key emits
  `E-TYP-1604` unless a more specific key-system diagnostic applies.
- Incompatible receiver permission calls emit `E-TYP-1605`.

Repair:

- Assignment typing now distinguishes root-identifier const assignment from
  aggregate const-path mutation when routing the diagnostic code.
- Binding-state assignment diagnostics use the same distinction, preventing a
  second Chapter 18 diagnostic on the aggregate const-path fixture.
- Shared assignment typing now emits `E-TYP-1604` when no covering write key is
  held and no more specific key-system diagnostic owns the failure.
- The rejected-source permission fixtures now exercise `E-TYP-1601`,
  `E-TYP-1602`, `E-TYP-1604`, and `E-TYP-1605`.

Verified bootstrap result after repair:

```text
Visual Studio bootstrap build wrapper, target=cursive: exit=0, rebuilt Cursive.exe
Cursive.exe build HelloUltraviolet/Fixtures/RejectedSource/Permissions/ConstMutation --check --target-profile x86_64-win64 --build-progress off --max-errors 8: exit=1, emitted E-TYP-1601
Cursive.exe build HelloUltraviolet/Fixtures/RejectedSource/Permissions/UniqueInactiveUse --check --target-profile x86_64-win64 --build-progress off --max-errors 8: exit=1, emitted E-TYP-1602
Cursive.exe build HelloUltraviolet/Fixtures/RejectedSource/Permissions/SharedMutationWithoutKey --check --target-profile x86_64-win64 --build-progress off --max-errors 8: exit=1, emitted E-TYP-1604
Cursive.exe build HelloUltraviolet/Fixtures/RejectedSource/Permissions/ReceiverPermissionMismatch --check --target-profile x86_64-win64 --build-progress off --max-errors 8: exit=1, emitted E-TYP-1605
Cursive.exe build HelloUltraviolet --check --target-profile x86_64-win64 --build-progress off --max-errors 20: exit=0, total diagnostic set is eight warnings plus two infos
Cursive.exe build HelloUltraviolet --target-profile x86_64-win64 --build-progress off --max-errors 20: exit=0, total diagnostic set is eight warnings plus two infos
HelloUltraviolet.exe: exit=0, 0-byte stdout/stderr
HelloUltraviolet.exe --audit: exit=0, 0-byte stdout/stderr
python3 Tools/ExtractObligationLedger.py --check: exit=0, obligations=6045
git -c filter.lfs.process= -c filter.lfs.required=false diff --check: exit=0
```

## UVBOOT-0064: Enum Diagnostics Must Preserve SPEC Codes

Status: repaired in the workspace bootstrap and verified by direct rejected
fixture builds plus `HelloUltraviolet --check`.

Rejected-source specimens:

- `Fixtures/RejectedSource/Expressions/EnumDuplicateVariant`
- `Fixtures/RejectedSource/Expressions/EnumUnknownVariant`
- `Fixtures/RejectedSource/Expressions/EnumTupleArity`
- `Fixtures/RejectedSource/Expressions/EnumRecordMissingField`

Spec obligations exercised:

- `Enum-Variant-Dup`
- `Enum-Lit-Unknown`
- `Enum-Lit-Tuple-Arity-Err`
- `Enum-Lit-Record-MissingField`

Observed bootstrap result before repair:

```text
EnumDuplicateVariant: error[E-TYP-2505] Name conflict among class members
EnumUnknownVariant: error[E-MOD-1301] Unresolved name
EnumTupleArity: error[E-SEM-3161] Return type mismatch
EnumRecordMissingField: error[E-SEM-3161] Return type mismatch
```

Bootstrap owners:

- `LLVMBootstrap/cursive/src/04_analysis/typing/item/enum_decl.cpp`
- `LLVMBootstrap/cursive/src/04_analysis/resolve/resolve_qual.cpp`
- `LLVMBootstrap/cursive/src/04_analysis/resolve/resolve_module.cpp`
- `LLVMBootstrap/cursive/src/04_analysis/typing/expr/enum_literal.cpp`

Failure analysis:

The bootstrap used a class-member duplicate diagnostic for duplicate enum
variants, resolved an unknown variant as a generic missing name, and allowed
enum payload shape mismatches to fall through until the enclosing return
statement produced a generic type mismatch. Each source form is a spec-defined
enum diagnostic, so the enum owner path must report the enum diagnostic code
directly.

Required bootstrap behavior:

Duplicate enum variant names report `E-TYP-2002`. Qualified enum construction
against an existing enum path with a missing variant reports `E-TYP-2007`.
Tuple-like enum literal arity mismatches report `E-TYP-2008`. Record-like enum
literal missing or unknown fields report `E-TYP-2009`.

Repair:

- Enum declaration duplicate-variant checks now use `Enum-Variant-Dup` and
  `E-TYP-2002`.
- Qualified enum resolution now distinguishes an existing enum path with a
  missing variant from a generic unresolved name and emits `E-TYP-2007`.
- Resolver diagnostic mapping now carries `E-TYP-2007` through to the
  diagnostic registry.
- Enum literal payload checking now emits `E-TYP-2008` for tuple arity
  mismatches and `E-TYP-2009` for missing or unknown record payload fields.

Verified bootstrap result after repair:

```text
Visual Studio bootstrap build wrapper: exit=0, rebuilt Cursive.exe
EnumDuplicateVariant: exit=1, error[E-TYP-2002]
EnumUnknownVariant: exit=1, error[E-TYP-2007]
EnumTupleArity: exit=1, error[E-TYP-2008]
EnumRecordMissingField: exit=1, error[E-TYP-2009]
Cursive.exe build HelloUltraviolet --check --target-profile x86_64-win64 --build-progress off --max-errors 20: exit=0, total diagnostic set is seven warnings plus two infos
```

## UVBOOT-0063: Record Duplicate Field Diagnostic Uses SPEC Code

Spec-valid rejected source:

```ultraviolet
public record DuplicateRecordFieldPayload {
    public value: i32
    public value: bool
}
```

Observed bootstrap result before repair:

```text
error: Static rule failed without assigned diagnostic code: WF-Record-DupField
  --> C:/dev/ultraviolet/HelloUltraviolet/Fixtures/RejectedSource/Expressions/RecordDuplicateField/Source/Main.uv:3:8
3 | public record DuplicateRecordFieldPayload {
3 |
```

Bootstrap owners:

- `LLVMBootstrap/cursive/src/04_analysis/typing/item/record_decl.cpp`
- `LLVMBootstrap/cursive/src/04_analysis/composite/records.cpp`

Failure analysis:

`SPECIFICATION.md` §12.6.7 assigns `E-TYP-1901` to duplicate field names in
record declarations, and the formal rule `WF-Record-DupField` carries
`c = Code(WF-Record-DupField)`. The bootstrap detected the correct static rule
but returned the rule identifier as the diagnostic id, so the typecheck
diagnostic layer rendered the fallback uncoded static-rule message.

Repair:

- The duplicate-field checks in `record_decl.cpp` now return `E-TYP-1901`.
- The shared composite record well-formedness helper in `records.cpp` now also
  returns `E-TYP-1901`.

Verified bootstrap result after repair:

```text
Visual Studio bootstrap build wrapper: exit=0, rebuilt Cursive.exe
Cursive.exe build HelloUltraviolet/Fixtures/RejectedSource/Expressions/RecordDuplicateField --check --target-profile x86_64-win64 --build-progress off --max-errors 4: exit=1, emits E-TYP-1901
Cursive.exe build HelloUltraviolet --check --target-profile x86_64-win64 --build-progress off --max-errors 12: exit=0, total diagnostic set is seven warnings plus two infos
```

## UVBOOT-0054: Compile-Time Procedure Ordinary Control Propagation

Status: repaired in the workspace bootstrap and verified by
`Cursive.exe build HelloUltraviolet --check --target-profile x86_64-win64`.

Reference source:

- `Source/Reference/Comptime/CompileTimeForms.uv`

Spec obligations exercised:

- `rule.22.Parse-CtProc`
- `rule.22.T-CtProc`
- `requirement.22.CtEvalOrdinarySemantics`
- `requirement.22.CompileTimeProcedureContextRestriction`

Spec basis:

- `SPECIFICATION.md:24713-24718` defines `CtProc` as well-formed with optional
  generic parameters and a body checked in the compile-time environment.
- `SPECIFICATION.md:24723-24725` requires compile-time procedures to be
  callable from compile-time contexts and rejected from runtime contexts.
- `SPECIFICATION.md:24750-24751` requires `CtEval` and `CtExec` for ordinary
  forms inside compile-time execution to preserve ordinary child order, scope,
  pattern binding, control propagation, and operator semantics.

Spec-valid specimen:

```ultraviolet
comptime internal procedure chooseComptimeReferenceValue<TValue>(
    first: TValue,
    second: TValue,
    choose_first: bool
) -> TValue
{
    if choose_first {
        return first
    }
    return second
}

let generic_chosen: usize =
    comptime { chooseComptimeReferenceValue(31usize, 37usize, true) }
```

Observed bootstrap result before repair:

The compile-time pass failed to evaluate the ordinary `if` expression inside
the compile-time procedure body. The unexpanded call then reached Phase 3 as
though it were a runtime reference to a compile-time procedure:

```text
error[E-CTE-0034]: Compile-time procedure referenced from runtime context
```

Bootstrap owner:

- `LLVMBootstrap/cursive/src/03_comptime/eval.cpp`

Failure analysis:

The Phase 2 compile-time evaluator handled literal, identifier, binary,
block, call, method-call, type-literal, and quote expressions, but it did not
evaluate ordinary `if` expressions. `EvalBlock` also discarded a `return`
propagated through an expression statement. That violated the SPEC rule that
ordinary forms inside compile-time execution preserve ordinary control
propagation. A compile-time procedure whose selected ordinary `if` branch
returned a value therefore failed to produce a compile-time result.

Required bootstrap behavior:

`CtEval` must evaluate ordinary `if` expressions in compile-time contexts by
evaluating the condition, selecting the matching branch, and propagating
ordinary return flow through expression statements.

Repair:

- `LLVMBootstrap/cursive/src/03_comptime/eval.cpp` now evaluates ordinary
  `IfExpr` in compile-time execution.
- `EvalBlock` now returns an expression-statement result when that result
  carries compile-time `return` propagation.

Verified bootstrap result after repair:

```text
Visual Studio bootstrap build wrapper: exit=0, rebuilt eval.cpp and Cursive.exe
Cursive.exe build HelloUltraviolet --check --target-profile x86_64-win64 --build-progress off --max-errors 1: exit=0, total diagnostic set is five warnings plus two infos
```

## UVBOOT-0055: Region and Frame Target Lowering Uses Stable Region Locals

Status: repaired in the workspace bootstrap and verified by
`Cursive.exe build HelloUltraviolet --target-profile x86_64-win64`.

Reference source:

- `Source/Reference/Authority/RegionsAndFrames.uv`

Spec obligations exercised:

- `rule.18.Region`
- `rule.18.Region-Options`
- `rule.18.Frame-Implicit`
- `rule.18.Frame-Explicit`
- `rule.16.Alloc-Implicit`
- `rule.16.Alloc-Explicit`
- `req.18.RegionArenaCapability`
- `req.18.FrameAllocationAuthority`

Spec basis:

- `SPECIFICATION.md:10769-10783` defines region statements with optional
  region options, optional aliases, and scoped execution.
- `SPECIFICATION.md:10881-10883` defines the `Region::new_scoped` builtin
  constructor returning an active region.
- `SPECIFICATION.md:19933-19949` defines implicit and explicit frame
  statements and requires an explicit frame target to be an active region.
- `SPECIFICATION.md:20008-20035` defines allocation expressions with implicit
  allocation in the current region and explicit allocation through
  `region ^ expr`.

Spec-valid specimen:

```ultraviolet
internal procedure allocationExpressionValue() -> i32 {
    region as scratch {
        let implicit_value: i32 = ^7
        let explicit_value: i32 = scratch ^ 11
        frame {
            let frame_value: i32 = ^13
            frame scratch {
                let target_value: i32 = scratch ^ 17
                return implicit_value + explicit_value + frame_value + target_value
            }
        }
    }
    return 0
}
```

Observed bootstrap result before repair:

The first expanded region/frame specimen reached lowering as spec-valid
source but failed module preparation because an implicit allocation stored the
stable lowered region alias as though it were a source identifier:

```text
[cursive] EnsureCodegenModule: lowering failed for module 'HelloUltraviolet::Reference::Authority' (resolve_failed=true, codegen_failed=false) unresolved=[__bind_42_scratch]
error: project codegen context preparation failed
```

After the implicit allocation path was corrected, object emission still failed
for explicit frame targets and region-provenance bindings whose source aliases
were used after region-scope lowering had already introduced stable locals:

```text
[cursive] codegen failure at .../ir_storage_emit.cpp:434
[cursive] EmitObjForModule: module=HelloUltraviolet::Reference::Authority LLVM module emission failed before object generation
error[E-OUT-0402]: Failed to emit object file (codegen or write)
```

The remaining explicit allocation specimen built but executed with runtime
panic code `0x0005` because `scratch ^ 11` lowered `scratch` as an ordinary
local read instead of using the region-handle local created for the region
alias.

Bootstrap owner:

- `LLVMBootstrap/cursive/src/05_codegen/lower/expr/alloc_expr.cpp`
- `LLVMBootstrap/cursive/src/05_codegen/lower/stmt/frame_stmt.cpp`
- `LLVMBootstrap/cursive/src/05_codegen/lower/stmt/let_stmt.cpp`
- `LLVMBootstrap/cursive/src/05_codegen/lower/stmt/var_stmt.cpp`
- `LLVMBootstrap/cursive/src/05_codegen/lower/expr/loop_iter.cpp`
- `LLVMBootstrap/cursive/src/05_codegen/globals/binding_storage.cpp`

Failure analysis:

Region aliases have source-level names during analysis and stable lowered local
names during IR/codegen. The region/frame lowering paths mixed those two name
spaces. Implicit allocation, explicit allocation, explicit frame targets, and
region-provenance binding storage each needed to carry the stable region local
when the source target was a known region binding.

Required bootstrap behavior:

Region and frame lowering must preserve the analysis meaning of a source alias
while emitting IR that targets the stable region-handle local. Allocations,
frame scopes, and bindings derived from region provenance must all target that
same stable local through object emission and runtime execution.

Repair:

- `alloc_expr.cpp` now uses the active stable region local for implicit
  allocation and translates explicit region bindings such as `scratch ^ value`
  to the stable region local before lowering the allocation.
- `frame_stmt.cpp` now resolves explicit frame targets through the stable
  region binding when the target name is a local region alias.
- `let_stmt.cpp`, `var_stmt.cpp`, and `loop_iter.cpp` now translate
  region-provenance bindings to stable region locals before recording binding
  storage metadata.
- `binding_storage.cpp` now resolves bound region targets through the stable
  binding name.

Verified bootstrap result after repair:

```text
Visual Studio bootstrap build wrapper: exit=0, rebuilt alloc_expr.cpp and Cursive.exe
Cursive.exe build HelloUltraviolet --check --target-profile x86_64-win64 --build-progress off --max-errors 1: exit=0, total diagnostic set is six warnings plus two infos
Cursive.exe build HelloUltraviolet --target-profile x86_64-win64 --build-progress off --max-errors 1: exit=0, total diagnostic set is six warnings plus two infos
HelloUltraviolet.exe: exit=0, 0-byte stdout/stderr
HelloUltraviolet.exe --audit: exit=0, 0-byte stdout/stderr
python3 Tools/ExtractObligationLedger.py --check: exit=0, obligations=6045
```

## UVBOOT-0056: Contract Predicate Source Forms and Compile-Time Procedure Metadata

Reference source:

- `Source/Reference/Procedures/Contracts.uv`

Spec obligations exercised:

- `grammar.15.ContractClauses`
- `rule.15.WF-Contract`
- `rule.15.Pure-Block`
- `rule.15.Pure-Comptime`
- `req.15.ContractEntryConstraints`
- `req.15.ContractClauseLoweringViaVerificationResults`

Spec basis:

- `SPECIFICATION.md:14772-14812` defines contract clause syntax for
  pre-only, pre/post, and post-only forms.
- `SPECIFICATION.md:14821-14934` defines contract predicate well-formedness
  and the pure expression forms allowed in contract predicates, including
  block expressions and compile-time procedure calls.
- `SPECIFICATION.md:14947-14953` defines precondition and postcondition
  evaluation contexts and states that contract clauses affect execution only
  through verification and inserted checks.
- `SPECIFICATION.md:15020-15125` defines `@result` and `@entry(expr)`,
  including the entry-state capture semantics.
- `SPECIFICATION.md:24725-24728` requires runtime procedure bodies to avoid
  naming or calling compile-time procedures, while `SPECIFICATION.md:24749-24794`
  removes `CtProc` declarations from expanded runtime items.

Spec-valid specimens:

```ultraviolet
internal procedure contractEntryReference(value: i32) -> i32
|: value > 0 => @entry(value) == value && @result == value + 2
{
    return value + 2
}

internal procedure contractPureBlockReference(value: i32) -> i32
|: {
    let local_value: i32 = value
    local_value > 0
}
{
    return value
}

internal procedure contractPureComptimeReference(value: i32) -> i32
|: contractCompileTimePredicate()
{
    return value
}
```

Observed bootstrap result before repair:

The first full contract-clause specimen exposed four separate bootstrap
non-conformance points:

```text
|: { let local_value: i32 = value; local_value > 0 }
```

initially failed during parsing because the predicate parser did not delegate
to the full expression parser for block expressions.

```text
error[E-SEM-2801]: Contract predicate not provable outside dynamic context
```

was reported for a return whose postcondition used
`@entry(value) == value && @result == value + 2`, even though the entry value
and returned value were statically stable at the return point.

```text
error[E-SEM-2802]: Impure expression in contract predicate
```

was reported for an `if` expression and then for a block expression inside a
contract predicate, even though §15.4 admits both forms when their children are
pure.

```text
error[E-SEM-2802]: Impure expression in contract predicate
```

was also reported for `contractCompileTimePredicate()` inside a contract
predicate. The expanded module had correctly removed the `CtProc` from runtime
items under §22, but the later contract checker no longer had enough
compile-time procedure metadata to apply `Pure-Comptime`.

Bootstrap owner:

- `LLVMBootstrap/cursive/src/02_source/parser/expr/expr_common.cpp`
- `LLVMBootstrap/cursive/src/04_analysis/typing/stmt/return_stmt.cpp`
- `LLVMBootstrap/cursive/src/04_analysis/typing/expr/block_expr.cpp`
- `LLVMBootstrap/cursive/src/04_analysis/contracts/contract_check.cpp`
- `LLVMBootstrap/cursive/src/04_analysis/typing/expr/call.cpp`
- `LLVMBootstrap/cursive/include/02_source/ast/nodes/ast_module.h`
- `LLVMBootstrap/cursive/src/03_comptime/pass.cpp`
- `LLVMBootstrap/cursive/src/04_analysis/composite/function_types.cpp`
- `LLVMBootstrap/cursive/src/04_analysis/typing/expr/path.cpp`
- `LLVMBootstrap/cursive/src/00_core/generated/static_rule_registry.inc`

Failure analysis:

The parser treated contract predicates as a narrower expression category than
the SPEC defines. Contract typing also applied an early purity-context
restriction to block expressions instead of letting the contract purity
judgment validate the block's statements and tail expression. Postcondition
proof substituted `@entry(expr)` only when the expression had already been
captured by a runtime check path, so statically stable entry expressions were
not available to the proof context. Finally, Phase 2 correctly removed
compile-time procedure declarations from runtime items, but the analysis
module lost the non-runtime metadata needed by `Pure-Comptime`.

Required bootstrap behavior:

Contract predicates must parse through the same expression parser used by
ordinary expressions. The contract checker must decide purity using §15.4,
including pure block expressions and compile-time procedure calls. Static
postcondition proof must substitute stable `@entry(expr)` values at return
points. Compile-time procedures must remain absent from runtime items while
remaining visible as non-runtime metadata to contract predicate analysis.

Repair:

- `ParsePredicateExpr` now delegates to the full expression parser.
- Return postcondition checking now substitutes stable `@entry(expr)` values
  for return-point proof.
- Block expression typing no longer rejects contract-predicate block
  expressions before the contract purity checker can validate them.
- Contract purity and call typing now recognize compile-time procedures in
  contract-predicate contexts.
- Expanded modules now retain a non-runtime `comptime_procedures` metadata
  list, and the call, path, function-type, and contract-check lookup paths use
  that metadata without reintroducing `CtProc` items into runtime output.
- The static rule registry was regenerated after adding the bootstrap
  `Pure-Comptime` rule reference.

Spec clarification recorded:

`HelloUltraviolet/Audit/SpecClarificationsNeeded.md` records the intended
reading that ordinary contract predicate checking is a static context for
`Pure-Comptime`, while runtime procedure bodies still cannot name or call
compile-time procedures.

Verified bootstrap result after repair:

```text
Visual Studio bootstrap build wrapper: exit=0, rebuilt Cursive.exe
Cursive.exe build HelloUltraviolet --check --target-profile x86_64-win64 --build-progress off --max-errors 1: exit=0, total diagnostic set is six warnings plus two infos
Cursive.exe build HelloUltraviolet --target-profile x86_64-win64 --build-progress off: exit=0, total diagnostic set is six warnings plus two infos
HelloUltraviolet.exe: exit=0, 0-byte stdout/stderr
HelloUltraviolet.exe --audit: exit=0, 0-byte stdout/stderr
```

## UVBOOT-0057: Return Postcondition `@result is` Proof Reduction

Spec-valid source:

```ultraviolet
internal type PostconditionUnion = i32 | bool

internal procedure postconditionUnionNumericReference() -> PostconditionUnion
|: => if @result is numeric_value: i32 { numeric_value > 0 } else { false }
{
    return 7
}

internal procedure postconditionUnionBooleanReference() -> PostconditionUnion
|: => if @result is {
    numeric_value: i32 {
        numeric_value > 0
    }
    flag_value: bool {
        flag_value
    }
}
{
    return true
}
```

Observed bootstrap result before repair:

```text
error[E-SEM-2801]: Contract predicate not provable outside `[[dynamic]]` scope
  --> C:/dev/ultraviolet/HelloUltraviolet/Source/Reference/Procedures/Postconditions.uv:42:5
42 |     return 7
```

Bootstrap owner:

- `LLVMBootstrap/cursive/src/04_analysis/typing/stmt/return_stmt.cpp`

Failure analysis:

The return-postcondition verifier substituted `@result` for simple expression
forms only, so predicates using `if @result is ...` and `if @result is { ... }`
were passed to the static prover with no reduced result branch. Even after
result substitution, the verifier had no return-point reduction for a known
concrete returned value selecting a typed-pattern branch, and branch bodies were
represented as block expressions whose tail value was not exposed to the proof.

Required bootstrap behavior:

For postcondition proof at a concrete return site, `@result` substitution must
apply through if, if-is, if-case, call, aggregate, and block expression forms.
When the substituted scrutinee has a concrete non-union type, typed-pattern
if-is and if-case branches can be reduced to the matching branch, with
whole-value pattern bindings substituted into that branch body. Expression
blocks with no statements and a tail expression must expose their tail value to
the proof reducer.

Repair:

- Return postcondition substitution now traverses if, if-is, if-case, calls,
  qualified applies, method calls, casts, records, and expression blocks.
- Return postcondition proof now simplifies field access over returned record
  literals and expression-block tails.
- Return postcondition proof now reduces known typed-pattern if-is and if-case
  branches for concrete non-union scrutinee types and substitutes whole-value
  pattern bindings into the selected branch.

Source correction made during the same slice:

The first draft of the propagation postcondition specimen used
`PostconditionUnion?` inside a procedure returning `PostconditionUnion`. §16.8.4
defines `SuccessMember(R, U)` as the one union member of `U` that is not a
subtype of the enclosing return type `R`, with all other members propagated.
Because both members of `i32 | bool` are subtypes of `i32 | bool`, that source
has no unique success member and was corrected to a `bool`-returning propagation
reference.

Verified bootstrap result after repair:

```text
Visual Studio bootstrap build wrapper: exit=0, rebuilt Cursive.exe
Cursive.exe build HelloUltraviolet --check --target-profile x86_64-win64 --build-progress off --max-errors 1: exit=0, total diagnostic set is six warnings plus two infos
```

## UVBOOT-0058: Pure Contract Predicate Proof Coverage

Spec-valid source:

```ultraviolet
internal procedure contractPureIfIsNoElseReference(value: ContractPureUnion) -> i32
|: {
    let matched_unit: () = if value is _: i32 {
        ()
    };
    true
}
{
    return 1
}

internal procedure contractPureCastExercise(value: i32) -> bool {
    if value as i64 > 0 {
        return contractPureCastReference(value) == value
    }

    return false
}

internal procedure contractPureRecordExercise(value: i32) -> bool {
    if (ContractPureBox {
        value: value,
        values: [value, value + 1, value + 2],
        pair: (value, value + 1)
    }.value > 0) {
        return contractPureRecordReference(value) == value
    }

    return false
}

internal procedure contractPureComptimeExercise(value: i32) -> bool {
    return contractPureComptimeReference(value) == value
}
```

Observed bootstrap results before repair:

```text
error[E-SRC-0520]: expected expression at `}`
  --> C:/dev/ultraviolet/HelloUltraviolet/Source/Reference/Procedures/Contracts.uv:115:5

error[E-SEM-2801]: Contract predicate not provable outside `[[dynamic]]` scope
  --> C:/dev/ultraviolet/HelloUltraviolet/Source/Reference/Procedures/Contracts.uv:236:5
236 |     if value as i64 > 0 {

error[E-SEM-2801]: Contract predicate not provable outside `[[dynamic]]` scope
  --> C:/dev/ultraviolet/HelloUltraviolet/Source/Reference/Procedures/Contracts.uv:330:5
330 |     if (ContractPureBox {

error[E-SEM-2801]: Contract predicate not provable outside `[[dynamic]]` scope
  --> C:/dev/ultraviolet/HelloUltraviolet/Source/Reference/Procedures/Contracts.uv:366:5
366 |     return contractPureComptimeReference(value) == value
```

Bootstrap owners:

- `LLVMBootstrap/cursive/src/02_source/parser/item/contract_clause.cpp`
- `LLVMBootstrap/cursive/src/04_analysis/contracts/verification.cpp`
- `LLVMBootstrap/cursive/src/04_analysis/typing/expr/call.cpp`

Failure analysis:

The contract-clause parser propagated the non-braced contract-arrow stop rule
into braced predicate blocks, so a pure block predicate containing a no-else
`if is` expression with unit result was rejected before semantic analysis.

After parser repair, call-site precondition proof failed for spec-valid pure
predicate forms because proof equality and substitution covered only a narrow
expression subset. `Pure-Cast`, `Pure-Record`, aggregate predicates, block
predicates, control-flow predicates, and call predicates require structural
comparison and actual-parameter substitution through the same expression forms
that the contract purity rules admit. Record predicates also exposed mixed
resolved and source-spelled type paths in otherwise identical predicate ASTs.

The compile-time predicate specimen exposed a final proof gap: `Pure-Comptime`
allows compile-time procedure calls inside contract predicates, and the
bootstrap already types those calls in contract-predicate contexts, but
call-site precondition checking did not fold a compile-time boolean predicate
that reduces to `true`.

Required bootstrap behavior:

Braced contract predicate blocks must parse as full block expressions. Static
proof must compare and substitute the full pure-predicate source surface
admitted by §15.4.4, including casts, tuples, arrays, records, field/index
access, if/if-is/if-case, blocks, builtin calls, ordinary pure calls, method
calls, ranges, `@entry`, and compile-time predicate calls. Equivalent local and
resolved type paths must compare as the same predicate target when they name
the same final declaration. Compile-time boolean predicates with literal
results must contribute to call-site precondition proof.

Repair:

- Braced contract predicates now keep full block-expression parsing instead of
  using the contract-arrow stop rule meant for non-braced preconditions.
- Static predicate equality now structurally compares the pure predicate AST
  surface needed by the Procedures / Contracts reference specimens.
- Call-site precondition substitution now traverses aggregate, control-flow,
  block, range, entry, call, method, and cast expression forms.
- Call-site precondition checking now accepts a proven simple predicate after
  substitution, including no-argument compile-time boolean predicates that
  return a literal `true`.

Verified bootstrap result after repair:

```text
Visual Studio bootstrap build wrapper: exit=0, rebuilt Cursive.exe
Cursive.exe build HelloUltraviolet --check --target-profile x86_64-win64 --build-progress off --max-errors 1: exit=0, total diagnostic set is six warnings plus two infos
Cursive.exe build HelloUltraviolet --target-profile x86_64-win64 --build-progress off: exit=0, total diagnostic set is six warnings plus two infos
HelloUltraviolet.exe: exit=0, 0-byte stdout/stderr
HelloUltraviolet.exe --audit: exit=0, 0-byte stdout/stderr
Tools/ExtractObligationLedger.py --check: exit=0, obligations=6045
```

## UVBOOT-0061: Multiline Compile-Time Expression Brace Boundary

Spec-valid source:

```ultraviolet
let span_line_ok: bool = comptime {
    reflectedSourceSpanHasExtent(diagnostics~>current_span())
}
```

Observed bootstrap result before repair:

```text
Cursive.exe build HelloUltraviolet --check --target-profile x86_64-win64 --build-progress off --max-errors 12: exit=0
Cursive.exe build HelloUltraviolet --target-profile x86_64-win64 --build-progress off --max-errors 12: exit=1
[cursive] codegen failure at C:\Dev\Ultraviolet\LLVMBootstrap\cursive\src\05_codegen\lower\expr\expr_common.cpp:254
[cursive] EnsureCodegenModule: lowering failed for module 'HelloUltraviolet::Reference::Comptime' (resolve_failed=false, codegen_failed=true)
error: project codegen context preparation failed
```

Temporary parser/evaluator tracing identified an `ErrorExpr [61:40-67:1]`
inside the `ComptimeExpr [61:30-67:6]`.

Bootstrap owner:

- `LLVMBootstrap/cursive/src/02_source/parser/expr/comptime_expr.cpp`

Failure analysis:

`SPECIFICATION.md` §22.1 defines `comptime_expr ::= attribute_list? "comptime"
"{" expression "}"`, and §4.1.7 defines newline filtering around delimiter
depth and continuation. A compile-time expression block therefore accepts an
ordinary expression split across lines inside the braces. The bootstrap
`ParseCtBlockExpr` path parsed immediately after `{` and required `}`
immediately after the expression. The retained newline after `{` became the
start of an error expression, and the parse failure survived the `--check` path
until full code generation rejected the unexpanded compile-time expression.

Required bootstrap behavior:

`comptime { expression }` must accept newlines around the expression boundary
consistently with ordinary expression whitespace. Parse failures inside the
compile-time expression block must be reported during parsing or checking
instead of surfacing later as a codegen context preparation failure.

Repair:

- `ParseCtBlockExpr` now skips retained newlines after `{` before parsing the
  expression.
- `ParseCtBlockExpr` now skips retained newlines before requiring the closing
  `}`.

Verified bootstrap result after repair:

```text
Visual Studio bootstrap build wrapper: exit=0, rebuilt Cursive.exe
Cursive.exe build HelloUltraviolet --check --target-profile x86_64-win64 --build-progress off --max-errors 12: exit=0, total diagnostic set is seven warnings plus two infos
Cursive.exe build HelloUltraviolet --target-profile x86_64-win64 --build-progress off --max-errors 12: exit=0, total diagnostic set is seven warnings plus two infos
HelloUltraviolet.exe: exit=0, 0-byte stdout/stderr
HelloUltraviolet.exe --audit: exit=0, 0-byte stdout/stderr
Tools/ExtractObligationLedger.py --check: exit=0, obligations=6045
```

## UVBOOT-0060: Keyword-Named Compile-Time Metadata Field Access

Spec-valid source:

```ultraviolet
comptime loop field in introspect~>fields(Type::<ComptimeReflectedRecord>) {
    reflected_field_primitive_type_count = reflected_field_primitive_type_count +
        comptime if introspect~>category(field.type) == TypeCategory::Primitive {
            1usize
        } else {
            0usize
        }
}
```

Observed bootstrap result before repair:

```text
error[E-SRC-0520]: Generic syntax error (unexpected token)
  --> C:/dev/ultraviolet/HelloUltraviolet/Source/Reference/Comptime/Reflection.uv:85:52
85 |             comptime if introspect~>category(field.type) == TypeCategory::Primitive {
85 |                                                    ^^^^
```

Bootstrap owners:

- `LLVMBootstrap/cursive/src/02_source/parser/expr/postfix.cpp`
- `LLVMBootstrap/cursive/src/02_source/parser/expr/field_access.cpp`

Failure analysis:

`SPECIFICATION.md` §22.2.3 defines `FieldInfoFields` with a field named
`type`, and §22.3.5 requires `introspect.fields(ty)` to return those
`FieldInfo` values. The accepted source must therefore be able to select the
metadata field as `field.type`. The bootstrap lexer tokenizes `type` as a
keyword, and the dot-field parsing path accepted only `Identifier` tokens after
`.`. General identifier parsing already preserved keyword lexemes for selected
identifier slots, but dot-field parsing bypassed that logic and rejected the
SPEC-defined metadata field before semantic analysis.

Required bootstrap behavior:

Dot-field selection must accept keyword tokens as field selector names. The
selector position reads an existing member name and does not introduce a local
binder, so reserved-binder diagnostics do not apply there. Semantic analysis
still resolves the selected lexeme against the receiver type's fields.

Repair:

- Dot-field parsing in the postfix parser now accepts either an identifier token
  or a keyword token for the selector lexeme.
- The standalone field-access parser path was updated with the same selector
  rule.

Verified bootstrap result after repair:

```text
Visual Studio bootstrap build wrapper: exit=0, rebuilt Cursive.exe
Cursive.exe build HelloUltraviolet --check --target-profile x86_64-win64 --build-progress off --max-errors 12: exit=0, total diagnostic set is seven warnings plus two infos
```

## UVBOOT-0059: Escaping Closure Expected-Type Alias Normalization

Spec-valid source:

```ultraviolet
internal type EscapingSharedKeyReader = || -> i32 [shared: { shared_value: shared i32 }]

internal procedure escapingClosureSharedCaptureValue() -> i32 {
    var shared_value: shared i32 = 31
    let reader: EscapingSharedKeyReader = || -> i32 {
        #shared_value read {
            return shared_value + 0
        }
        return 0
    }
    return invokeEscapingSharedKeyReader(reader)
}
```

Observed bootstrap result before repair:

```text
error[E-MOD-2402]: Type annotation incompatible with inferred type
  --> C:/dev/ultraviolet/HelloUltraviolet/Source/Reference/Keys/AcquisitionBlocks.uv:55:5
55 |     let reader: EscapingSharedKeyReader = || -> i32 {
55 |     ^
```

Bootstrap owner:

- `LLVMBootstrap/cursive/src/04_analysis/typing/type_infer.cpp`

Failure analysis:

`SPECIFICATION.md` §16.9.4 permits the shared dependency set for an escaping
closure to be inferred when the closure is checked against an expected closure
type, and §13.11 defines the dependency-set form on closure types. The source
above gives the closure expression an expected closure type through a type
alias. The bootstrap checker only recognized raw `TypeClosure` expected shapes
in the closure-specific checking path. When the expected type was a type alias,
the checker fell through to ordinary synthesis, produced a local capturing
closure without the expected dependency set, and reported an annotation
mismatch.

Required bootstrap behavior:

Expected-type checking for closure expressions must normalize type aliases
before deciding whether the expected type is a closure or function shape. A
type alias that expands to `|| -> R [shared: {...}]` must drive the same
parameter and dependency-set checking as the expanded closure type.

Repair:

- Closure expected-type checking in `type_infer.cpp` now normalizes aliases
  before inspecting the expected type for `TypeFunc` or `TypeClosure`.
- The original expected type remains the recorded/checking target; only the
  shape test uses the normalized type.

Verified bootstrap result after repair:

```text
Visual Studio bootstrap build wrapper: exit=0, rebuilt Cursive.exe
Cursive.exe build HelloUltraviolet --check --target-profile x86_64-win64 --build-progress off --max-errors 5: exit=0, total diagnostic set is seven warnings plus two infos
Cursive.exe build HelloUltraviolet --target-profile x86_64-win64 --build-progress off --max-errors 5: exit=0, total diagnostic set is seven warnings plus two infos
HelloUltraviolet.exe: exit=0, 0-byte stdout/stderr
HelloUltraviolet.exe --audit: exit=0, 0-byte stdout/stderr
Tools/ExtractObligationLedger.py --check: exit=0, obligations=6045
```

## UVBOOT-0062: Unique Place Binding to Const Suspends Instead of Moving

Spec-valid source:

```ultraviolet
internal record BindingPermissionCell {
    internal value: i32

    internal procedure readConst(~) -> i32 {
        return self.value
    }
}

internal procedure suspendedUniqueBindingValue() -> i32 {
    let source: unique BindingPermissionCell = BindingPermissionCell { value: 23 }
    let view: const BindingPermissionCell = source
    return view~>readConst()
}
```

Observed bootstrap result before repair:

```text
error[E-UNS-0107]: Non-`Bitcopy` place expression used as value
  --> C:/dev/ultraviolet/HelloUltraviolet/Source/Reference/Statements/Bindings.uv:65:5
65 |     let view: const BindingPermissionCell = source
65 |     ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
```

Bootstrap owners:

- `LLVMBootstrap/cursive/src/04_analysis/typing/stmt/let_stmt.cpp`
- `LLVMBootstrap/cursive/src/04_analysis/typing/stmt/var_stmt.cpp`

Failure analysis:

`SPECIFICATION.md` §18.2.4 defines `SuspendUniqueBind`: when a binding
initializer is a place, the initializer has `unique` permission, and the binding
type has `const` permission, the compiler suspends the unique path. This is an
accepted binding-state path, not a value copy of a non-`Bitcopy` place.

The bootstrap annotation checker ran ordinary expected-expression checking
before the binding-state rule could apply. That path treated `source` as an
ordinary non-`Bitcopy` place value use and emitted `E-UNS-0107`.

Required bootstrap behavior:

Annotated `let` and `var` binding checks must accept a `unique` place
initializer when the annotated binding type is the same underlying type with
`const` permission. The memory/binding pass then applies
`SuspendUniqueBind`/`DowngradeUniqueBind_inplace` to suspend the unique path.

Repair:

- The annotated binding compatibility helper in both `let_stmt.cpp` and
  `var_stmt.cpp` now recognizes the `unique` place to `const` binding case.
- The existing explicit `move` compatibility for `unique` bindings is
  preserved.

Verified bootstrap result after repair:

```text
Visual Studio bootstrap build wrapper: exit=0, rebuilt Cursive.exe
Cursive.exe build HelloUltraviolet --check --target-profile x86_64-win64 --build-progress off --max-errors 12: exit=0, total diagnostic set is seven warnings plus two infos
Cursive.exe build HelloUltraviolet --target-profile x86_64-win64 --build-progress off --max-errors 12: exit=0, total diagnostic set is seven warnings plus two infos
HelloUltraviolet.exe: exit=0, 0-byte stdout/stderr
HelloUltraviolet.exe --audit: exit=0, 0-byte stdout/stderr
```

## UVBOOT-0065: Binary16 Comparison and Runtime Helper ABI

Spec-valid source:

```ultraviolet
internal procedure primitiveFloatReference() -> bool {
    let half: f16 = 1.5f16
    let single: f32 = 2.5f32
    let double: f64 = 3.5f64
    return half > 1.0f16 &&
        single > 2.0f32 &&
        double > 3.0f64
}
```

Observed bootstrap results before repair:

```text
Cursive.exe build HelloUltraviolet --target-profile x86_64-win64 --build-progress off --max-errors 12: exit=1
lld-link: error: undefined symbol: __extendhfsf2
HelloUltraviolet.exe: exit=1
catalog compiled symbol failed: runDataTypesPrimitivesReference
reference failed: catalogCompiledSymbolsExecute
```

Bootstrap owners:

- `LLVMBootstrap/cursive/runtime/src/compat/rtc_stubs.c`
- `LLVMBootstrap/cursive/src/05_codegen/llvm/emit/ir/ops/binary.cpp`

Failure analysis:

`SPECIFICATION.md` §12.1 defines `f16` as IEEE 754 binary16, and
§24.2/Appendix D map `f16` to LLVM `half` with size and alignment of two
bytes. The source above exercises an ordered comparison between representable
binary16 values, so `1.5f16 > 1.0f16` must evaluate to true.

The bootstrap first failed to link because the CRT-free Windows runtime archive
did not provide the half-float conversion helper referenced by LLVM lowering.
After adding the helper, the program linked but the comparison still evaluated
false. The Windows x64 helper ABI passes the half value through `xmm0`; the
initial helper read a `uint16_t` integer argument and therefore converted the
wrong bits.

Required bootstrap behavior:

The compiler must lower ordered binary16 comparisons to the same IEEE ordered
truth values as the specification: NaN operands are unordered, signed zeroes
compare equal, and finite/infinite non-NaN operands compare by numeric value.
The CRT-free runtime must also expose half conversion helpers with the ABI that
LLVM-generated Windows x64 calls use.

Repair:

- The LLVM binary-operation lowering now emits direct binary16 ordered
  comparison logic for `half` relational operators. It bitcasts operands to
  `i16`, rejects NaN operands, treats `+0` and `-0` as equal, and compares
  non-zero values through an IEEE sortable key.
- The Windows runtime half helpers now separate bit-level conversion from the
  exported ABI wrapper. On x64, `__extendhfsf2` receives the low half lane from
  `xmm0`, and `__truncsfhf2` returns the half bits in `xmm0`.

Verified bootstrap result after repair:

```text
Visual Studio bootstrap build wrapper, target=cursive: exit=0, rebuilt Cursive.exe
Visual Studio bootstrap build wrapper, target=ultraviolet0_rt: exit=0, rebuilt UltravioletRT.lib
Cursive.exe build HelloUltraviolet --check --target-profile x86_64-win64 --build-progress off --max-errors 12: exit=0, total diagnostic set is seven warnings plus two infos
Cursive.exe build HelloUltraviolet --target-profile x86_64-win64 --build-progress off --max-errors 12: exit=0, total diagnostic set is seven warnings plus two infos
HelloUltraviolet.exe: exit=0, 0-byte stdout/stderr
HelloUltraviolet.exe --audit: exit=0, 0-byte stdout/stderr
```

## UVBOOT-0066: Floating Exponentiation Link Inputs

Spec-valid source:

```ultraviolet
internal procedure arithmeticOperatorsReference() -> bool {
    let float_power: f64 = 2.0f64 ** 3.0f64
    return float_power == 8.0f64
}
```

Observed bootstrap result before repair:

```text
Cursive.exe build HelloUltraviolet --target-profile x86_64-win64 --build-progress off --max-errors 20: exit=1
lld-link: error: undefined symbol: pow
>>> referenced by ...HelloUltraviolet_x3a_x3aReference_x3a_x3aExpressions.obj:
>>> (HelloUltraviolet_x3a_x3aReference_x3a_x3aExpressions_x3a_x3aarithmeticOperatorsReference)
```

Bootstrap owner:

- `LLVMBootstrap/cursive/src/01_project/link.cpp`

Failure analysis:

`SPECIFICATION.md` §16.4.1 includes `**` in the arithmetic operator grammar,
§16.4.4 types arithmetic operators over numeric operands, §16.4.5 defines
floating `**` through IEEE 754 pow semantics, and §16.4.6 requires binary
operators to lower through `LowerBinOp`.

The compiler already lowered floating exponentiation to LLVM's pow intrinsic,
which code generation materialized as a `pow` runtime symbol. The Windows final
link used `/NODEFAULTLIB` and provided `msvcrt.lib`, but did not name the UCRT
math import library that satisfies `pow`. The source was accepted by check
mode, and final artifact generation failed only at link time.

Required bootstrap behavior:

Generated artifacts that use SPEC floating exponentiation must include the
platform math runtime inputs required by the compiler's lowering strategy.

Repair:

- Windows link arguments now include `ucrt.lib` alongside the existing CRT and
  ICU import libraries.
- ELF link arguments now include `-lm` before `-lc` so the same lowered pow
  dependency is provided for ELF targets.

Verified bootstrap result after repair:

```text
Visual Studio bootstrap build wrapper, target=cursive: exit=0, rebuilt Cursive.exe
Cursive.exe clean HelloUltraviolet: exit=0, removed build artifacts
Cursive.exe build HelloUltraviolet --check --target-profile x86_64-win64 --build-progress off --max-errors 20: exit=0, total diagnostic set is eight warnings plus two infos
Cursive.exe build HelloUltraviolet --target-profile x86_64-win64 --build-progress off --max-errors 20: exit=0, total diagnostic set is eight warnings plus two infos
HelloUltraviolet.exe: exit=0, 0-byte stdout/stderr
HelloUltraviolet.exe --audit: exit=0, 0-byte stdout/stderr
```

## UVBOOT-0067: Callable Alias and Indirect Closure Call Lowering

Spec-valid source:

```ultraviolet
internal type IntUnaryClosure = |i32| -> i32
internal type MoveIntClosure = |move i32| -> i32

internal procedure applyMoveClosure(closure: MoveIntClosure, value: i32) -> i32 {
    return closure(move value)
}

internal procedure closureCaptureReference() -> bool {
    let base_value: i32 = 3
    let add_captured: IntUnaryClosure = |value: i32| -> i32 value + base_value
    let move_base: i32 = 2
    let move_add_captured: MoveIntClosure = |move value: i32| -> i32 value + move_base

    return add_captured(4) == 7 &&
        applyMoveClosure(move_add_captured, 6) == 8
}

internal procedure pipelineFormsReference() -> bool {
    let base_value: i32 = 3
    let add_captured: IntUnaryClosure = |value: i32| -> i32 value + base_value
    let closure_pipeline: i32 = 4 => add_captured
    return closure_pipeline == 7
}
```

Observed bootstrap results before repair:

```text
Cursive.exe build HelloUltraviolet --check --target-profile x86_64-win64 --build-progress off --max-errors 20: exit=1
error[E-SEM-2538]: Pipeline RHS is not callable
  --> .../ClosuresAndPipelines.uv:98:33
98 |     let closure_pipeline: i32 = 4 => add_captured

Cursive.exe build HelloUltraviolet --target-profile x86_64-win64 --build-progress off --max-errors 20: exit=0
HelloUltraviolet.exe: exit=5

Focused pass-through probe after alias dispatch repair:
probeApplyMoveClosure(move_add_captured, 6): exit=0
direct move-parameter closure call: exit=8
```

Bootstrap owners:

- `LLVMBootstrap/cursive/src/04_analysis/typing/expr/pipeline_expr.cpp`
- `LLVMBootstrap/cursive/src/05_codegen/lower/expr/call.cpp`
- `LLVMBootstrap/cursive/src/05_codegen/lower/expr/closure_expr.cpp`
- `LLVMBootstrap/cursive/src/05_codegen/lower/expr/pipeline_expr.cpp`

Failure analysis:

`SPECIFICATION.md` §16.9 defines closure type syntax and closure invocation
through ordinary call syntax. §16.9.4 types capturing closures as
`TypeClosure`, §16.9.5 evaluates closure calls by applying the closure
environment and code pointer, and §16.9.3/§16.9.4 allow pipeline right-hand
sides whose type is either `TypeFunc` or `TypeClosure`.

The reference source used type aliases for those callable types. The bootstrap
pipeline typechecker and lowering paths checked only the raw type node and did
not normalize aliases before deciding whether the right-hand side was callable.
After pipeline alias normalization, ordinary call lowering still recognized only
raw `TypeClosure`, so `closure(move value)` where `closure` had alias type
`MoveIntClosure` was lowered as a generic indirect function call instead of a
closure call.

The final failing boundary was an indirect closure call through a non-`move`
procedure parameter. Direct invocation of the captured move-parameter closure
returned `8`, while invoking the same closure through
`probeApplyMoveClosure(closure: MoveIntClosure, value: i32)` returned `0`.
Lowering had extracted the closure code pointer but did not register the
env-augmented callable signature needed by LLVM call emission, so the indirect
call did not use the closure code ABI.

Required bootstrap behavior:

Callable type aliases must be normalized before closure/pipeline callable
shape checks in typing and lowering. A call whose callee has alias-normalized
`TypeClosure` must lower as `LowerClosureCall`, using the closure parameter
modes for argument lowering. The extracted closure code pointer must carry an
env-augmented function type so backend call emission can use the correct ABI for
indirect closure calls.

Repair:

- Pipeline typechecking and lowering now normalize callable aliases before
  checking or lowering `TypeFunc`/`TypeClosure` pipeline right-hand sides.
- Closure-expression lowering normalizes callable aliases when deriving
  contextual parameter modes and return types.
- Ordinary call lowering normalizes callable aliases before dispatching to
  `LowerClosureCall`.
- `LowerClosureCall` now lowers arguments through the closure parameter modes
  and registers the extracted code pointer with an env-augmented function type.

Source correction made during diagnosis:

Noncapturing closure literals in `closureParameterFormsReference` were changed
from `TypeClosure` aliases to `TypeFunc` aliases. This was a source correction,
not a bootstrap repair: `SPECIFICATION.md` §16.9.4 types noncapturing closure
expressions as `TypeFunc`, while capturing closures type as `TypeClosure`.

Verified bootstrap result after repair:

```text
Visual Studio bootstrap build wrapper, target=cursive: exit=0, rebuilt Cursive.exe
Cursive.exe clean HelloUltraviolet: exit=0, removed build artifacts
Cursive.exe build HelloUltraviolet --check --target-profile x86_64-win64 --build-progress off --max-errors 20: exit=0, total diagnostic set is eight warnings plus two infos
Cursive.exe build HelloUltraviolet --target-profile x86_64-win64 --build-progress off --max-errors 20: exit=0, total diagnostic set is eight warnings plus two infos
HelloUltraviolet.exe: exit=0, 0-byte stdout/stderr
HelloUltraviolet.exe --audit: exit=0, 0-byte stdout/stderr
python3 Tools/ExtractObligationLedger.py --check: exit=0, obligations=6045
git diff --check: exit=0, with existing CRLF notices only
```

## UVBOOT-0068: Generic Predicate Bounds in Procedure Body Checking

Spec-valid source:

```ultraviolet
internal procedure callReferenceSelect<TFirst; TSecond>(
    first: TFirst,
    second: TSecond
) -> TSecond |: Bitcopy(TFirst) Bitcopy(TSecond) {
    return second
}
```

Observed bootstrap result before repair:

```text
error[E-UNS-0107]: Non-`Bitcopy` place expression used as value
  --> .../Expressions/Calls.uv
```

Bootstrap owners:

- `LLVMBootstrap/cursive/include/04_analysis/typing/context.h`
- `LLVMBootstrap/cursive/include/04_analysis/generics/generic_params.h`
- `LLVMBootstrap/cursive/src/04_analysis/generics/generic_params.cpp`
- `LLVMBootstrap/cursive/src/04_analysis/typing/type_predicates.cpp`
- `LLVMBootstrap/cursive/src/04_analysis/typing/item/procedure_decl.cpp`

Failure analysis:

`SPECIFICATION.md` defines procedure predicate clauses as part of the generic
procedure contract, and value use of a local requires the type to satisfy
`BitcopyType(T)`. The bootstrap checked the predicate clause at call sites, but
the generic procedure body context did not attach direct predicate facts such as
`Bitcopy(TSecond)` to the `TSecond` type-parameter entity. As a result, the
body checker treated `second: TSecond` as a non-`Bitcopy` place even though the
procedure's own predicate clause supplied the required fact.

Repair:

Generic type-parameter binding now accepts the active predicate clause and
attaches direct predicate bounds to the type-parameter entity. `BitcopyType`
consults those predicate bounds when checking an uninstantiated type parameter,
and procedure signature/body contexts bind generic parameters with the
procedure predicate clause in scope.

Verified bootstrap result after repair:

```text
Visual Studio bootstrap build wrapper, target=cursive: exit=0, rebuilt Cursive.exe
Cursive.exe build HelloUltraviolet --check --target-profile x86_64-win64 --build-progress off --max-errors 20: exit=0, total diagnostic set is eight warnings plus two infos
Cursive.exe build HelloUltraviolet --target-profile x86_64-win64 --build-progress off --max-errors 20: exit=0, total diagnostic set is eight warnings plus two infos
```

## UVBOOT-0069: Indirect Closure Call ABI Mode Recovery

Spec-valid source:

```ultraviolet
internal type MoveIntClosure = |move i32| -> i32

internal procedure applyMoveClosure(closure: MoveIntClosure, value: i32) -> i32 {
    return closure(move value)
}

internal procedure closureCaptureReference() -> bool {
    let move_base: i32 = 2
    let move_add_captured: MoveIntClosure = |move value: i32| -> i32 value + move_base
    return applyMoveClosure(move_add_captured, 6) == 8
}
```

Observed bootstrap result before repair:

```text
HelloUltraviolet.exe: exit=1
closure/pipeline return failed: closureCaptureReference.applyMoveClosure(move_add_captured, 6) == 8
```

Bootstrap owners:

- `LLVMBootstrap/cursive/src/05_codegen/llvm/emit/ir/call/direct.cpp`
- `LLVMBootstrap/cursive/src/05_codegen/llvm/llvm_call.cpp`

Failure analysis:

`SPECIFICATION.md` §16.9 types `|move i32| -> i32` as a closure whose ordinary
call syntax consumes the argument by move. The lowering path had the correct
callee type for the indirect closure-code call:
`(move * imm u8, move i32) -> i32`. During LLVM call emission, however, a stale
concrete closure-code signature from another procedure was selected for the
opaque closure-code value. That stale signature marked the `i32` parameter as a
borrowed parameter, so the ABI classifier passed the address of `value` instead
of the moved `i32` value expected by the closure code.

Repair:

Indirect non-symbol calls now reconcile a recovered concrete signature with the
callee value's actual callable type before ABI classification. When the callee
type is `TypeFunc`, the function type's parameter modes and types replace the
recovered signature's corresponding source parameters. When the callee type is
`TypeClosure`, the closure parameter modes are applied after the hidden
environment parameter. LLVM call materialization also loads a by-value argument
from addressable storage when the ABI target is a non-pointer value but the
lowered source value is a pointer.

Verified bootstrap result after repair:

```text
Visual Studio bootstrap build wrapper, target=cursive: exit=0, rebuilt Cursive.exe
Cursive.exe build HelloUltraviolet --target-profile x86_64-win64 --build-progress off --max-errors 20: exit=0, total diagnostic set is eight warnings plus two infos
HelloUltraviolet.exe: exit=0, 0-byte stdout/stderr
```

## UVBOOT-0070: Panic Cleanup Reaches Catch Export Boundaries

Spec-valid source:

```ultraviolet
extern "C-unwind" {
    [[unwind("catch")]]
    public procedure importedSpeculativePanicBoundary() -> i32
}

[[dynamic]]
[[export("C-unwind")]]
[[unwind("catch")]]
[[mangle("importedSpeculativePanicBoundary")]]
public procedure ffiImportedSpeculativePanicBoundaryProvider() -> i32 {
    var values: shared [i32; 2] = [1, 2]

    #values[1usize] speculative write {
        values[4usize] = 99
    } commit {
        values[1usize] = 3
    }

    return 1
}
```

Observed bootstrap result before repair:

```text
HelloUltraviolet.exe: exit=6
```

Bootstrap owners:

- `LLVMBootstrap/cursive/src/05_codegen/checks/checks.cpp`
- `LLVMBootstrap/cursive/src/05_codegen/llvm/emit/ir/control/return.cpp`

Failure analysis:

`SPECIFICATION.md` §16.2.6 and §24.2.1 require a failing dynamic array bounds
check to raise the `Bounds` panic code. `SPECIFICATION.md` §19.2 requires key
blocks to release their acquired paths on every exit mode, including panic.
The exported `C-unwind` catch boundary is the source boundary that converts the
panic into the catch return value after cleanup. The bootstrap emitted a bare
panic follow-up check after the bounds check, so lowering reached the catch
return path before running the cleanup plan for the active speculative key
block.

Repair:

`PanicCheck` now emits an `IRCleanupPanicCheck` carrying the cleanup plan to
the function root, and catch-export return lowering clears the panic state and
returns the ABI zero value after that cleanup path has run.

Verified bootstrap result after repair:

```text
Visual Studio bootstrap build wrapper, target=cursive: exit=0, rebuilt Cursive.exe
Cursive.exe build HelloUltraviolet --target-profile x86_64-win64 --build-progress off --max-errors 20: exit=0
HelloUltraviolet.exe: exit=0, 0-byte stdout/stderr
```

## UVBOOT-0071: Dynamic Fixed-Array Bounds Checks Use Source Length

Spec-valid source:

```ultraviolet
[[dynamic]]
internal procedure dynamicOrderedSameBaseKeyAccessValue(
    first: usize,
    second: usize
) -> i32 {
    var values: shared [i32; 4] = [1, 2, 3, 4]
    var observed: i32 = 0

    #values[first], values[second] dynamic ordered read {
        observed = values[first] + values[second]
    }

    return observed
}
```

Observed bootstrap result before repair:

```text
dynamicOrderedSameBaseKeyAccessValue(0usize, 2usize): exit=6
```

Bootstrap owner:

- `LLVMBootstrap/cursive/src/05_codegen/llvm/emit/ir_instruction_visitor.cpp`

Failure analysis:

`SPECIFICATION.md` §12.3 defines fixed-size array types by their source
length, and §16.2.6 checks dynamic array indexing against `Len(v_b)`. The
bootstrap lowered the specimen with a bounds check against the LLVM storage
shape instead of the source `[i32; 4]` length; disassembly showed the second
index compared against length `1`. The same owner path could also emit a
length `4` check for a source `[i32; 3]` specimen, confirming that the lowered
storage representation rather than the source array type was driving the
check.

Repair:

`IRInstructionVisitor::StaticLengthOf` now uses the visitor's `LookupValueType`
helper, so the emitter's local source type table is consulted before falling
back to lowered storage or derived-value metadata.

Verified bootstrap result after repair:

```text
Visual Studio bootstrap build wrapper, target=cursive: exit=0, rebuilt Cursive.exe
Cursive.exe build HelloUltraviolet --target-profile x86_64-win64 --build-progress off --max-errors 20: exit=0
HelloUltraviolet.exe: exit=0, 0-byte stdout/stderr
```

## UVBOOT-0072: Speculative Non-Write Key Blocks Preserve Semantic Diagnostics

Spec-valid rejected source:

```ultraviolet
public procedure speculativeReadModeReference() -> i32 {
    var shared_value: shared i32 = 1
    var observed: i32 = 0

    #shared_value speculative read {
        observed = shared_value
    }

    return observed
}
```

Observed bootstrap result before repair:

```text
Cursive.exe build HelloUltraviolet/Fixtures/RejectedSource/Keys/SpeculativeReadMode --check --target-profile x86_64-win64 --build-progress off --max-errors 8: exit=1
error[E-CON-0091]: Write to path outside keyed set in speculative block
```

Expected SPEC result:

```text
error[E-CON-0095]: `speculative` without `write` modifier
```

Bootstrap owner:

- `LLVMBootstrap/cursive/src/02_source/parser/stmt/key_block_stmt.cpp`

Failure analysis:

`SPECIFICATION.md` §19.5.4 defines `K-Spec-Write-Required` as the static
semantic rule for `#P speculative M {B}` when `M` is not `write`, and
§19.5.7 assigns that condition to `E-CON-0095`. The bootstrap parser consumed
the `read` mode, emitted a generic parse error, then rewrote the AST mode to
`write`. That bypassed the semantic checker's `E-CON-0095` branch and allowed
later speculative-body purity checking to report `E-CON-0091`.

Repair:

The parser now preserves the parsed or omitted key-block mode. The semantic
checker receives the original `speculative read` shape and emits the SPEC
diagnostic for `K-Spec-Write-Required`.

Verified bootstrap result after repair:

```text
Visual Studio bootstrap build wrapper, target=cursive: exit=0, rebuilt Cursive.exe
Cursive.exe build HelloUltraviolet/Fixtures/RejectedSource/Keys/SpeculativeReadMode --check --target-profile x86_64-win64 --build-progress off --max-errors 8: exit=1
error[E-CON-0095]: `speculative` without `write` modifier
```

## UVBOOT-0073: Directory Module Keyword Paths Emit Module Aggregation Diagnostics

Spec-valid rejected-source specimen:

```text
HelloUltraviolet/Fixtures/RejectedSource/Modules/ReservedModuleKeyword/
  Ultraviolet.toml
  Source/if/Main.uv
```

```ultraviolet
//! Rejected module aggregation specimen for a reserved keyword module path.

public procedure reservedModuleKeywordReference() -> i32 {
    return 1
}
```

Observed bootstrap result before repair:

```text
Cursive.exe build HelloUltraviolet/Fixtures/RejectedSource/Modules/ReservedModuleKeyword --check --target-profile x86_64-win64 --build-progress off --max-errors 20: exit=1
error[E-CNF-0401]: Reserved keyword used as identifier
error[E-MOD-1105]: Module path component is a reserved keyword
```

Expected SPEC result:

```text
error[E-MOD-1105]: Module path component is a reserved keyword
```

Bootstrap owner:

- `LLVMBootstrap/cursive/src/01_project/module_discovery.cpp`

Failure analysis:

`SPECIFICATION.md` §11.5 defines `WF-Module-Path-Reserved` for directory-derived
module path components and §11.5.7 assigns that condition to `E-MOD-1105`.
`SPECIFICATION.md` §7.2 separately defines `Validate-Module-Keyword-Err` for
module-scope name validation, and the bootstrap maps that source-name path to
`E-CNF-0401`. The module-discovery path was emitting both codes from
`ValidateModulePath`, so a source-root child directory named with a reserved
keyword reported the source-identifier diagnostic as well as the module
aggregation diagnostic.

Repair:

`ValidateModulePath` now reports `WF-Module-Path-Reserved` through
`E-MOD-1105` only. Parser and resolver paths that validate reserved keywords in
source identifier positions continue to own `E-CNF-0401`.

Verified bootstrap result after repair:

```text
Visual Studio bootstrap build wrapper, target=cursive: exit=0, rebuilt Cursive.exe
Cursive.exe build HelloUltraviolet/Fixtures/RejectedSource/Modules/ReservedModuleKeyword --check --target-profile x86_64-win64 --build-progress off --max-errors 20: exit=1
error[E-MOD-1105]: Module path component is a reserved keyword
python3 Tools/ExtractObligationLedger.py --check: exit=0, obligations=6045
Cursive.exe build HelloUltraviolet --check --target-profile x86_64-win64 --build-progress off --max-errors 20: exit=0, warnings=10, infos=3
Cursive.exe build HelloUltraviolet --target-profile x86_64-win64 --build-progress off --max-errors 20: exit=0, warnings=10, infos=3
HelloUltraviolet.exe: exit=0, 0-byte stdout/stderr
HelloUltraviolet.exe --audit: exit=0, 0-byte stdout/stderr
```

## UVBOOT-0074: Type Alias Recursive Rule Maps to Core Diagnostic Code

Spec-valid rejected source:

```ultraviolet
public type AliasCycleA = AliasCycleB
public type AliasCycleB = AliasCycleA
```

Observed bootstrap result before repair:

```text
Cursive.exe build HelloUltraviolet/Fixtures/RejectedSource/DataTypes/TypeAliasCycle --check --target-profile x86_64-win64 --build-progress off --max-errors 20: exit=1
error: Internal error: unknown diagnostic id 'TypeAlias-Recursive-Err'
error: Internal error: unknown diagnostic id 'TypeAlias-Recursive-Err'
```

Expected SPEC result:

```text
error[E-TYP-1506]: Type alias cycle detected
```

Bootstrap owner:

- `LLVMBootstrap/cursive/src/04_analysis/typing/typecheck_diag_lookup.h`

Failure analysis:

`SPECIFICATION.md` §12.9 defines `AliasCycle(p)` over the alias graph, and the
internal obligation ledger names the rejecting rule `TypeAlias-Recursive-Err`.
`SPECIFICATION.md` §8.5 owns alias-cycle diagnostics and assigns type alias
cycles to `E-TYP-1506`. The type alias checker returned
`TypeAlias-Recursive-Err`, but the typecheck diagnostic lookup did not map that
rule id to the SPEC code, causing the user-facing diagnostic renderer to emit an
internal unknown-diagnostic failure instead of `E-TYP-1506`.

The public `SPECIFICATION.md` currently spells the rule conclusion as
`TypeAlias-Reultraviolet-Err`; `HelloUltraviolet/Audit/SpecClarificationsNeeded.md`
records that as a public SPEC typo because the internal obligation ledger and
CSV use `TypeAlias-Recursive-Err`.

Repair:

`LookupTypecheckDiagCode` now maps `TypeAlias-Recursive-Err` to `E-TYP-1506`.

Verified bootstrap result after repair:

```text
Visual Studio bootstrap build wrapper, target=cursive: exit=0, rebuilt Cursive.exe
Cursive.exe build HelloUltraviolet/Fixtures/RejectedSource/DataTypes/TypeAliasCycle --check --target-profile x86_64-win64 --build-progress off --max-errors 20: exit=1
error[E-TYP-1506]: Type alias cycle detected
python3 Tools/ExtractObligationLedger.py --check: exit=0, obligations=6045
Cursive.exe build HelloUltraviolet --check --target-profile x86_64-win64 --build-progress off --max-errors 20: exit=0, warnings=10, infos=3
Cursive.exe build HelloUltraviolet --target-profile x86_64-win64 --build-progress off --max-errors 20: exit=0, warnings=10, infos=3
HelloUltraviolet.exe: exit=0, 0-byte stdout/stderr
HelloUltraviolet.exe --audit: exit=0, 0-byte stdout/stderr
```

## UVBOOT-0075: Compile-Time Aggregate And Enum Literalization Reaches Phase 3

Spec-valid accepted source:

```ultraviolet
internal record ComptimeLiteralRecord {
    internal value: usize
    internal flag: bool
}

internal enum ComptimeLiteralEnum {
    Empty
    Count(usize)
    Named {
        value: usize
    }
}

internal modal ComptimeLiteralModal {
    @Ready {
        internal value: usize
    }
}

let literal_array: [usize; 3] = comptime { [2usize, 4usize, 6usize] }
let literal_record: ComptimeLiteralRecord =
    comptime { ComptimeLiteralRecord { value: 43usize, flag: true } }
let literal_enum_unit: ComptimeLiteralEnum =
    comptime { ComptimeLiteralEnum::Empty }
let literal_enum_tuple: ComptimeLiteralEnum =
    comptime { ComptimeLiteralEnum::Count(5usize) }
let literal_enum_record: ComptimeLiteralEnum =
    comptime { ComptimeLiteralEnum::Named { value: 7usize } }
let literal_modal_state: ComptimeLiteralModal@Ready =
    comptime { ComptimeLiteralModal@Ready { value: 11usize } }
```

Observed bootstrap results before repair:

```text
Cursive.exe build HelloUltraviolet --check --target-profile x86_64-win64 --build-progress off --max-errors 20: exit=1
[cursive] codegen failure at C:\Dev\Ultraviolet\LLVMBootstrap\cursive\src\05_codegen\lower\expr\expr_common.cpp:254
[cursive] EnsureCodegenModule: lowering failed for module 'HelloUltraviolet::Reference::Comptime' (resolve_failed=false, codegen_failed=true)
error[E-OUT-0411]: LLVM IR lowering failed

Cursive.exe build HelloUltraviolet --check --target-profile x86_64-win64 --build-progress off --max-errors 20: exit=1
error[E-MOD-2402]: Type annotation incompatible with inferred type
  --> C:/dev/ultraviolet/HelloUltraviolet/Source/Reference/Comptime/CompileTimeForms.uv:75:5
75 |     let literal_enum_tuple: ComptimeLiteralEnum =
```

Failure analysis:

`SPECIFICATION.md` §22.1.5 requires every `CtExpr` to be replaced before
Phase 3 by `CtLiteralize` or a compatible `CtAst` payload. The same section
defines `CtLiteralize` for `CtArray`, `CtRecord`, `CtModalState`, and enum
values with unit, tuple, and record payloads. It also states that expression
constructors outside Chapter 22 use the ordinary child order, scope creation,
pattern binding, control propagation, and operator semantics during
compile-time execution.

The bootstrap already had literalization support for aggregate and enum
compile-time values, but Phase 2 evaluation did not construct all required
`CtValue` forms. Record literals did not evaluate to `CtRecord` or
`CtModalState`, and enum payload constructors arrived from the parser as
`QualifiedApplyExpr` before Phase 3 enum resolution. Those unevaluated
compile-time expressions survived to lowering and triggered `E-OUT-0411`.

After the evaluator repair, tuple-payload enum literalization produced an
`EnumLiteralExpr` with the source-local enum path `ComptimeLiteralEnum`.
Existing `QualifiedApplyExpr` resolution canonicalized enum constructor paths,
but already-materialized `EnumLiteralExpr` nodes only resolved their payload
children. The checker therefore inferred `ComptimeLiteralEnum` instead of the
fully qualified
`HelloUltraviolet::Reference::Comptime::ComptimeLiteralEnum`.

Required bootstrap behavior:

Phase 2 `CtEval` must construct `CtArray`, `CtRecord`, `CtModalState`, and
`CtEnum` values for ordinary source literal constructors whose members are
compile-time evaluable. After literalization, Phase 3 must canonicalize
already-materialized enum literal paths the same way it canonicalizes
qualified enum constructor syntax.

Repair:

- `LLVMBootstrap/cursive/src/03_comptime/eval.cpp` now evaluates record
  literals into `CtRecord` or `CtModalState`, evaluates parser-level
  `QualifiedApplyExpr` enum constructors into tuple or record `CtEnum`
  payloads, and evaluates already-materialized `EnumLiteralExpr` payloads.
- `LLVMBootstrap/cursive/src/04_analysis/resolve/resolve_expr.cpp` now
  canonicalizes already-materialized `EnumLiteralExpr` paths through the
  existing `ResolveEnumUnit`, `ResolveEnumTuple`, and `ResolveEnumRecord`
  paths while preserving normal payload resolution.

Verified bootstrap result after repair:

```text
Visual Studio bootstrap build wrapper, target=cursive: exit=0, rebuilt eval.cpp and Cursive.exe
Visual Studio bootstrap build wrapper, target=cursive: exit=0, rebuilt resolve_expr.cpp and Cursive.exe
Cursive.exe build HelloUltraviolet --check --target-profile x86_64-win64 --build-progress off --max-errors 20: exit=0, warnings=10, infos=3
Cursive.exe build HelloUltraviolet --target-profile x86_64-win64 --build-progress on --max-errors 20: exit=0, warnings=10, infos=3, duration=122.09s
HelloUltraviolet.exe: exit=0, 0-byte stdout/stderr
HelloUltraviolet.exe --audit: exit=0, 0-byte stdout/stderr
```

## UVBOOT-0078: Async Take Lowering Preserves Remaining Count Across Resume

Spec-valid accepted source:

```ultraviolet
public procedure runAsyncCompositionTakeResumeDoneReference() -> bool {
    let taken = asyncCompositionTwoOutputs(84, 85)~>take(1usize)

    if taken is @Suspended(output, _) {
        let resumed = taken~>resume(())
        let resumed_completed: bool = if resumed is @Completed(_) {
            true
        } else {
            false
        }

        return output == 84 && resumed_completed
    }

    return false
}
```

Observed bootstrap result before repair:

```text
Cursive.exe build HelloUltraviolet --target-profile x86_64-win64 --build-progress on --max-errors 20: exit=0, warnings=10, infos=3
HelloUltraviolet.exe: exit=1
catalog compiled symbol failed: runAsyncCompositionCombinatorsReference
catalog compiled symbol failed: runAsyncCompositionFormsReference
reference failed: catalogCompiledSymbolsExecute
reference failed: runAsyncCompositionTakeResumeDoneReference
reference failed: runAsyncCompositionCombinatorRuntimeReference
reference failed: runAsyncCompositionCombinatorsReference
reference failed: runAsyncCompositionFormsReference
program_exit=1
```

Failure analysis:

`SPECIFICATION.md` §21.3.5 defines `take` as a stateful async wrapper
`TakeAsync = <source, remaining>`. `EvalSigma-Take-Resume-Yield` yields the
source output while storing the wrapper state with `remaining - 1`.
`EvalSigma-Take-Resume-Done` then completes when the stored remaining count is
zero, and `EvalSigma-Take-Resume-Source-Complete` completes when the source
completes before the count is exhausted.

The bootstrap lowering for `AsyncCombinatorKind::Take` only inspected the
original count. For nonzero counts it returned the source async value unchanged
after the first yield. Resuming `asyncCompositionTwoOutputs(84, 85)~>take(1)`
therefore resumed the source stream directly and yielded `85` instead of
completing the take wrapper.

Required bootstrap behavior:

The emitted value for `take(n)` must carry wrapper state across suspension.
After each yielded source output, the wrapper must store the decremented
remaining count and route the next resume through the `take` wrapper rather
than directly through the source continuation.

Repair:

- `LLVMBootstrap/cursive/runtime/src/memory/async.c` now defines a take frame
  containing the source async value and remaining count, returns completed unit
  for `take(0)`, forwards source completion/failure, and resumes through the
  wrapper frame when a yielded value leaves remaining output budget.
- `LLVMBootstrap/cursive/runtime/include/cursive_rt.h` and
  `LLVMBootstrap/cursive/runtime/include/cursive_rt_language_symbols.h`
  declare and map the runtime `ultraviolet::runtime::async::take` entry point.
- `LLVMBootstrap/cursive/src/05_codegen/intrinsics/builtins.cpp`,
  `LLVMBootstrap/cursive/include/05_codegen/intrinsics/builtins.h`, and
  `LLVMBootstrap/cursive/src/05_codegen/intrinsics/intrinsics_interface.cpp`
  register the async take runtime symbol.
- `LLVMBootstrap/cursive/src/05_codegen/llvm/emit/ir/call/direct.cpp` now
  lowers the take combinator through the runtime entry point so the wrapper
  state is preserved in emitted programs.

Verified bootstrap result after repair:

```text
Visual Studio bootstrap build wrapper, target=cursive: exit=0, rebuilt runtime async.c, builtins, direct.cpp, and Cursive.exe
Cursive.exe build HelloUltraviolet --check --target-profile x86_64-win64 --build-progress off --max-errors 20: exit=0, warnings=10, infos=3
Cursive.exe build HelloUltraviolet --target-profile x86_64-win64 --build-progress on --max-errors 20: exit=0, warnings=10, infos=3, duration=330.44s
HelloUltraviolet.exe: exit=0, 0-byte stdout/stderr
HelloUltraviolet.exe --audit: exit=0, 0-byte stdout/stderr
```

## UVBOOT-0079: Qualified Modal State Literals And Qualified Record Patterns

Status: repaired in the bootstrap.

Spec-valid source exercised:

```ultraviolet
let opened: QualifiedModalTypes::ModalReference@Open =
    QualifiedModalTypes::ModalReference@Open { value: 29 }
return opened~>readValue()
```

```ultraviolet
let QualifiedDataTypes::RecordReference {
    count: resolved_count,
    is_active
} = reference
```

Observed bootstrap results before repair:

```text
error[E-SRC-0520]: Generic syntax error (unexpected token)
  --> C:/dev/ultraviolet/HelloUltraviolet/Source/Reference/Names/QualifiedResolution.uv:142:5
142 |     return opened.value
```

After the parser accepted the qualified modal-state literal, the resolver then
reported:

```text
error[E-MOD-1301]: Unresolved name: identifier not found in any accessible scope
note: unresolved type path `QualifiedDataTypes`
```

Failure analysis:

`SPECIFICATION.md` defines `modal_state_expr ::= modal_type_ref "@" identifier
"{" field_init_list? "}"`, and `ResolveModalRef` accepts a modal reference
whose base is a resolved `TypePath` or `TypeApply`. A qualified module alias
therefore remains a valid modal-state literal base.

`SPECIFICATION.md` also defines `record_pattern ::= type_path "{" ... "}"`.
The qualified pattern `QualifiedDataTypes::RecordReference { ... }` is a
record pattern because the joined type path names a record. The bootstrap
parser initially treated the first `::` as an enum-pattern separator, and the
resolver only tried the joined record-pattern interpretation after the prefix
had already resolved as a type.

Required bootstrap behavior:

Qualified modal-state record literals must parse as record literals whose
target is a `ModalStateRef`. Qualified record patterns must resolve a joined
type path when the parsed enum-shaped pattern has a record payload and the
prefix is a module path or module alias rather than an enum type.

Repair:

- `LLVMBootstrap/cursive/src/02_source/parser/expr/primary.cpp` now scans a
  full type path before deciding whether an identifier starts a modal-state
  record literal.
- `LLVMBootstrap/cursive/src/04_analysis/resolve/resolve_pattern.cpp` now
  resolves enum-shaped record-payload patterns through the joined record type
  path when the prefix does not resolve as an enum type.
- `LLVMBootstrap/cursive/src/04_analysis/resolve/resolve_expr.cpp`,
  `LLVMBootstrap/cursive/src/04_analysis/resolve/resolve_items.cpp`,
  `LLVMBootstrap/cursive/src/04_analysis/resolve/resolve_module.cpp`, and
  `LLVMBootstrap/cursive/src/04_analysis/resolve/resolve_types.cpp` now
  preserve resolver diagnostic context for these paths.

Source correction:

The first draft read `opened.value` from another module. That is not
SPEC-valid: §13.2.4 defines `ModalFieldVisible` by the declaring modal module.
The reference now observes the constructed state through the public state
method `opened~>readValue()`, while same-module modal field reads remain
covered in `Reference::ModalTypes`.

Verified bootstrap result after repair:

```text
Visual Studio bootstrap build wrapper, target=cursive: exit=0, rebuilt primary.cpp, resolve_pattern.cpp, resolve_expr.cpp, resolve_items.cpp, resolve_module.cpp, resolve_types.cpp, and Cursive.exe
Cursive.exe build HelloUltraviolet --check --target-profile x86_64-win64 --build-progress on --max-errors 20: exit=0, warnings=10, infos=3, duration=162.42s
Cursive.exe build HelloUltraviolet --target-profile x86_64-win64 --build-progress on --max-errors 20: exit=0, warnings=10, infos=3, duration=427.00s
HelloUltraviolet.exe: exit=0
HelloUltraviolet.exe --audit: exit=0
```

## UVBOOT-0080: Slice Bounds Checks Must Use Runtime Fat-Pointer Length

Status: repaired in the bootstrap.

Spec-valid source exercised:

```ultraviolet
var backing: shared [i32; 4] = [1, 2, 3, 4]
let values: shared [i32] = backing[..]
let index: usize = 2usize
var observed: i32 = 0
#values[index], values[index] read {
    observed = values[index] + values[index]
}
return observed
```

Observed bootstrap result before repair:

```text
HelloUltraviolet.exe: exit=6
```

The narrowed corpus printed `debug: runKeysConflictDetectionReference` and
then exited with runtime bounds failure code `6`. Disassembly showed the full
corpus emitting slice bounds comparisons against `1` for `values[index]`, while
the same slice source in an isolated scratch project compared against the
runtime slice length `4`.

Failure analysis:

`SPECIFICATION.md` §12.4 defines array-to-slice coercion and slice indexing:
`Coerce-Array-Slice` permits `TypePerm(p, TypeArray(T, n))` to become
`TypePerm(p, TypeSlice(T))`, while `T-Index-Slice` and `P-Index-Slice` require
only a `usize` index. Once the fixed array is viewed as a slice, bounds checks
must use the slice fat pointer's runtime length. A stale static length from a
different value in the same module cannot constrain the emitted check.

The bootstrap LLVM emitter consulted `StaticLengthOf` before `DynamicLengthOf`
for index, range, and slice-length checks. In the full corpus that let stale
static metadata select length `1` for a `shared [i32]` slice whose runtime
length field was `4`.

Required bootstrap behavior:

Bounds and slice-length checks for runtime slice values must prefer the dynamic
length stored in the fat pointer. Static length is still valid for fixed-array
values and for values whose runtime representation has no dynamic length field.

Repair:

- `LLVMBootstrap/cursive/src/05_codegen/llvm/emit/ir/checks/check_index.cpp`
  now asks `DynamicLengthOf` before falling back to `StaticLengthOf`.
- `LLVMBootstrap/cursive/src/05_codegen/llvm/emit/ir/checks/check_range.cpp`
  applies the same runtime-length preference for range checks.
- `LLVMBootstrap/cursive/src/05_codegen/llvm/emit/ir/checks/check_slice_len.cpp`
  now computes both compared slice lengths from dynamic fat-pointer lengths
  when available, with integer widening to `i64` before comparison.

Verified bootstrap result after repair:

```text
Visual Studio bootstrap build wrapper, target=cursive: exit=0, rebuilt Cursive.exe
Cursive.exe build HelloUltraviolet --check --target-profile x86_64-win64 --build-progress on --max-errors 20: exit=0, warnings=11, infos=3, duration=184.89s
Cursive.exe build HelloUltraviolet --target-profile x86_64-win64 --build-progress on --max-errors 20: exit=0, warnings=11, infos=3, reused=57, rebuilt=2, duration=247.95s
HelloUltraviolet.exe: exit=0
HelloUltraviolet.exe --audit: exit=0
python3 Tools/ExtractObligationLedger.py --check: exit=0, obligations=6045
```

## UVBOOT-0081: Dynamic Indexed Key Safety In Parallel Context Requires Dynamic Mode

Status: repaired in the bootstrap.

Spec-valid rejected-source specimen:

```ultraviolet
public procedure dynamicKeyStaticRequiredReference(context: Context, index: usize) -> i32 {
    var backing: shared [i32; 4] = [1, 2, 3, 4]
    let values: shared [i32] = backing[..]
    return parallel context~>inline() {
        let first: Spawned<i32> = spawn {
            var observed: i32 = 0
            #values[index] write {
                values[index] = 11
                observed = 11
            }
            observed
        }
        let second: Spawned<i32> = spawn {
            var observed: i32 = 0
            #values[index] write {
                values[index] = 13
                observed = 13
            }
            observed
        }
        (wait first) + (wait second)
    }
}
```

Observed bootstrap result before repair:

```text
Cursive.exe build .agents/tmp/DynamicKeyStaticRequiredProbe --check --target-profile x86_64-win64 --build-progress on --max-errors 8: exit=0
```

Failure analysis:

`SPECIFICATION.md` §19.6.4 defines `K-Static-Required`: if key safety is not
statically safe and the access is outside a dynamic context, the program is
rejected. §19.6.5 and §19.6.6 define the `[[dynamic]]` path: incomparable
dynamic indices require runtime ordering and may lower to runtime
synchronization. §19.6.7 assigns `E-CON-0020` to non-statically-provable key
safety outside `[[dynamic]]`, and `I-CON-0011` to runtime synchronization
emitted under `[[dynamic]]`.

The bootstrap treated local absence of same-body key conflicts as a sufficient
static proof for dynamic indexed paths even inside a parallel context. That
accepted two spawned tasks that each wrote `values[index]` through a runtime
index outside `[[dynamic]]`. The local body of each spawned task contains one
keyed path, but the cross-task relationship is not statically disjoint.

Required bootstrap behavior:

Dynamic indexed key paths in a parallel context require a sound static proof
that covers cross-task access. When the compiler has only local body
disjointness, the access is not statically safe outside `[[dynamic]]` and must
emit `E-CON-0020`. The same source under `[[dynamic]]` is valid and must lower
runtime synchronization, surfacing `I-CON-0011`.

Repair:

- `LLVMBootstrap/cursive/src/04_analysis/typing/stmt/key_block_stmt.cpp` now
  carries dynamic-path classification into static-safety classification.
- In parallel contexts, local disjointness is no longer accepted as the
  disjoint-path proof for dynamic keyed paths; the checker therefore routes the
  non-`[[dynamic]]` source to `E-CON-0020` and allows the `[[dynamic]]` source
  to emit runtime synchronization.

Permanent corpus coverage:

- `Fixtures/RejectedSource/Keys/DynamicKeyStaticRequired` rejects with
  `E-CON-0020` for `rule.19.K-Static-Required`.
- `Fixtures/DiagnosticSource/Keys/DynamicKeyRuntimeSyncInfo` compiles with
  `I-CON-0011` for `diagnostics.19.DynamicKeyVerification`.

Verified bootstrap result after repair:

```text
Visual Studio bootstrap build wrapper, target=cursive: exit=0, rebuilt key_block_stmt.cpp and Cursive.exe
Cursive.exe build HelloUltraviolet/Fixtures/RejectedSource/Keys/DynamicKeyStaticRequired --check --target-profile x86_64-win64 --build-progress on --max-errors 8: exit=1, E-CON-0020
Cursive.exe build HelloUltraviolet/Fixtures/DiagnosticSource/Keys/DynamicKeyRuntimeSyncInfo --check --target-profile x86_64-win64 --build-progress on --max-errors 8: exit=0, I-CON-0011
```

## UVBOOT-0082: Static Destructuring Bindings Need Pattern-Typed Value Lookup

Status: repaired in the workspace bootstrap and verified by
`Cursive.exe build HelloUltraviolet --check --target-profile x86_64-win64`.

Reference source:

- `Source/Reference/Modules/Statics.uv`

Spec obligations exercised:

- `grammar.StaticDeclarationSyntax`
- `def.StaticDeclTopLevelItems`
- `req.StaticDeclModuleScopeBindingSemantics`
- `Bind-Static`
- `WF-StaticDecl`
- `ResolveItem-Static`
- `def.24.StaticName`
- `def.24.StaticBindTypes`
- `def.24.StaticBindList`
- `def.24.StaticBinding`
- `rule.24.Emit-Static-Multi`

Spec basis:

- `SPECIFICATION.md` §11.3 defines module-scope `let` and `var` static
  declarations.
- `SPECIFICATION.md` §11.3 defines `Bind-Static` as binding every name in
  `PatNames(pat)`.
- `SPECIFICATION.md` §11.3 requires `WF-StaticDecl` to type the declaration
  pattern against the annotated type.
- `SPECIFICATION.md` §24 defines `StaticBindTypes`, `StaticBindList`,
  `StaticBinding`, and `Emit-Static-Multi` for static declarations whose
  binding pattern introduces multiple names.

Spec-valid specimen:

```ultraviolet
internal let (STATIC_MULTI_LEFT, STATIC_MULTI_RIGHT): (i32, i32) = (5, 6)
internal let STATIC_RUNTIME_INIT_VALUE: i32 = staticInitializerReference()

internal procedure staticInitializerReference() -> i32 {
    return STATIC_REFERENCE_VALUE + STATIC_MULTI_RIGHT
}

internal procedure staticMultiBindingReference() -> bool {
    return STATIC_MULTI_LEFT + STATIC_MULTI_RIGHT == STATIC_REFERENCE_VALUE
}
```

Observed bootstrap result before repair:

```text
Cursive.exe build HelloUltraviolet --check --target-profile x86_64-win64 --build-progress on --max-errors 20: exit=1

error: Static rule failed without assigned diagnostic code: ResolveExpr-Ident-Err
  --> C:/dev/ultraviolet/HelloUltraviolet/Source/Reference/Modules/Statics.uv:10:5
10 |     return STATIC_REFERENCE_VALUE + STATIC_MULTI_RIGHT
10 |     ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

error: Static rule failed without assigned diagnostic code: ResolveExpr-Ident-Err
  --> C:/dev/ultraviolet/HelloUltraviolet/Source/Reference/Modules/Statics.uv:23:5
23 |     return STATIC_MULTI_LEFT + STATIC_MULTI_RIGHT == STATIC_REFERENCE_VALUE
23 |     ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
```

Bootstrap owner:

- `LLVMBootstrap/cursive/src/04_analysis/composite/function_types.cpp`

Failure analysis:

The collection pass already implemented `Bind-Static` by adding every
`PatNames(pat)` name from a static declaration to the module value map.
Expression typechecking then called `LookupModuleStaticInModule` to recover the
value type for a resolved static name. That lookup only recognized
`IdentifierPattern` and `TypedPattern`, so names introduced by a tuple-pattern
static declaration were present in the resolver map but had no value type during
expression typing.

Required bootstrap behavior:

Static value lookup must use the same binding surface as `Bind-Static`.
For a static declaration whose pattern introduces multiple names, lookup should
first identify declarations whose pattern binds the requested name, then type
the pattern against the static annotation and return the requested binding's
pattern-derived type.

Repair:

- `LLVMBootstrap/cursive/src/04_analysis/composite/function_types.cpp` now
  includes the canonical pattern-typing helper.
- `LookupModuleStaticInModule` now filters static declarations through
  `PatNames`, lowers the static annotation once, invokes
  `TypePatternAgainstType`, and returns the matching binding type from the
  pattern-typed binding list.

Permanent corpus coverage:

- `Source/Reference/Modules/Statics.uv` now exercises identifier static
  bindings, tuple-valued statics, tuple-pattern multi-binding statics,
  cross-static initialization through a procedure call, private mutable static
  storage, and runtime reads from destructured static binding names.

Verified bootstrap result after repair:

```text
Visual Studio bootstrap build wrapper, target=cursive: exit=0, rebuilt function_types.cpp and Cursive.exe
Cursive.exe build HelloUltraviolet --check --target-profile x86_64-win64 --build-progress on --max-errors 20: exit=0, 12 warnings, 9 infos, 184.23s
Cursive.exe build HelloUltraviolet --target-profile x86_64-win64 --build-progress on --max-errors 20: exit=0, 12 warnings, 9 infos, 782.70s
HelloUltraviolet/build/bin/HelloUltraviolet.exe: exit=0
HelloUltraviolet/build/bin/HelloUltraviolet.exe --audit: exit=0
python3 Tools/ExtractObligationLedger.py --check: exit=0, PASS obligations=6045
```

## UVBOOT-0084: Static Rule Diagnostic Codes Missing From Typecheck Routing

Status: repaired in the workspace bootstrap for SPEC-assigned codes. Remaining
uncoded Chapter 15 cases are recorded in the SPEC clarification ledger.

Rejected reference sources:

- `Fixtures/RejectedSource/Statements/FrameNoActiveRegion/Source/Main.uv`
- `Fixtures/RejectedSource/Statements/FrameTargetNotActive/Source/Main.uv`
- `Fixtures/RejectedSource/Statements/FrameDiagnostic/Source/Main.uv`
- `Fixtures/RejectedSource/Statements/UnsafeRequiredOperationOwnershipDiagnostic/Source/Main.uv`
- `Fixtures/RejectedSource/Expressions/IfConditionNonBool/Source/Main.uv`
- `Fixtures/RejectedSource/Expressions/TransmuteUnsafe/Source/Main.uv`
- `Fixtures/RejectedSource/Expressions/ControlExpressionDiagnosticOwnership/Source/Main.uv`
- `Fixtures/RejectedSource/Expressions/RawDerefUnsafeRequirement/Source/Main.uv`
- `Fixtures/RejectedSource/Expressions/RawDerefPlaceTypingFamily/Source/Main.uv`
- `Fixtures/RejectedSource/Procedures/MethodLookupAmbiguousDefault/Source/Main.uv`

Spec obligations exercised:

- `rule.18.Frame-NoActiveRegion-Err`
- `rule.18.Frame-Target-NotActive-Err`
- `diag.18.UnsafeRequiredOperationOwnership`
- `diag.16.ControlExpressions`
- `rule.16.Transmute-Unsafe-Err`
- `req.16.ControlExpressionDiagnosticOwnership`
- `rule.16.T-Deref-Raw`
- `rule.16.DerefPlaceTypingFamily`
- `rule.15.LookupMethod-Ambig`

Spec basis:

- `SPECIFICATION.md:4640-4650` assigns `E-MEM-1207`, `E-MEM-1208`, and
  `E-MEM-3030`.
- `SPECIFICATION.md:5834` assigns `E-MOD-1307`.
- `SPECIFICATION.md:12465` assigns `E-TYP-2103`.
- `SPECIFICATION.md:18039` assigns `E-SEM-2526`.

Observed bootstrap result before repair:

```text
error: Static rule failed without assigned diagnostic code: Frame-NoActiveRegion-Err
error: Static rule failed without assigned diagnostic code: Frame-Target-NotActive-Err
error: Static rule failed without assigned diagnostic code: Transmute-Unsafe-Err
error: Static rule failed without assigned diagnostic code: If-Cond-NotBool
error: Static rule failed without assigned diagnostic code: Deref-Raw-Unsafe
error: Static rule failed without assigned diagnostic code: LookupMethod-Ambig
```

Bootstrap owner:

- `LLVMBootstrap/cursive/src/04_analysis/typing/typecheck_diag_lookup.h`

Failure analysis:

The typecheck paths returned static rule labels, and the generated diagnostic
registry retained these labels without concrete codes. For rules whose SPEC
diagnostic surface already assigns a code, the typecheck diagnostic resolver
needed to translate the rule label before emitting the user-facing diagnostic.

Required bootstrap behavior:

SPEC-assigned static-rule diagnostics must emit the SPEC diagnostic code rather
than falling through to the uncoded static-rule path. Cases where the SPEC
requires a diagnostic but does not assign a code must remain explicit uncoded
static diagnostics and be tracked for SPEC clarification.

Repair:

- `typecheck_diag_lookup.h` now maps the affected static rule labels to
  `E-MEM-1207`, `E-MEM-1208`, `E-MEM-3030`, `E-SEM-2526`, `E-TYP-2103`, and
  `E-MOD-1307`.

Spec clarification recorded:

`HelloUltraviolet/Audit/SpecClarificationsNeeded.md` records that
`WF-ProcedureDecl-MissingReturnType`, `ReturnAnnOk`, non-boolean
`WF-Contract`, and `Transmute-Unsafe-Err` need direct code-assignment clarity.

Verified bootstrap result after repair:

```text
Visual Studio bootstrap build wrapper, target=cursive: exit=0, rebuilt typecheck.cpp, record_decl.cpp, item_common.cpp, procedure_decl.cpp, and Cursive.exe
Cursive.exe build HelloUltraviolet/Fixtures/RejectedSource/Statements/FrameNoActiveRegion --check --target-profile x86_64-win64 --build-progress off --max-errors 4: exit=1, error[E-MEM-1207]
Cursive.exe build HelloUltraviolet/Fixtures/RejectedSource/Statements/FrameTargetNotActive --check --target-profile x86_64-win64 --build-progress off --max-errors 4: exit=1, error[E-MEM-1208]
Cursive.exe build HelloUltraviolet/Fixtures/RejectedSource/Statements/UnsafeRequiredOperationOwnershipDiagnostic --check --target-profile x86_64-win64 --build-progress off --max-errors 4: exit=1, error[E-MEM-3030]
Cursive.exe build HelloUltraviolet/Fixtures/RejectedSource/Expressions/IfConditionNonBool --check --target-profile x86_64-win64 --build-progress off --max-errors 4: exit=1, error[E-SEM-2526]
Cursive.exe build HelloUltraviolet/Fixtures/RejectedSource/Expressions/TransmuteUnsafe --check --target-profile x86_64-win64 --build-progress off --max-errors 4: exit=1, error[E-MEM-3030]
Cursive.exe build HelloUltraviolet/Fixtures/RejectedSource/Expressions/RawDerefUnsafeRequirement --check --target-profile x86_64-win64 --build-progress off --max-errors 4: exit=1, error[E-TYP-2103]
Cursive.exe build HelloUltraviolet/Fixtures/RejectedSource/Expressions/RawDerefPlaceTypingFamily --check --target-profile x86_64-win64 --build-progress off --max-errors 4: exit=1, error[E-TYP-2103]
Cursive.exe build HelloUltraviolet/Fixtures/RejectedSource/Procedures/MethodLookupAmbiguousDefault --check --target-profile x86_64-win64 --build-progress off --max-errors 4: exit=1, error[E-MOD-1307]
```

## UVBOOT-0083: Empty Tuple Pattern Must Match Unit Values

Status: repaired in the workspace bootstrap and verified by:

```text
LLVMBootstrap/cursive/build/Release/Cursive.exe build HelloUltraviolet --target-profile x86_64-win64 --build-progress on --max-errors 20
HelloUltraviolet/build/bin/HelloUltraviolet.exe
HelloUltraviolet/build/bin/HelloUltraviolet.exe --audit
```

Reference source:

- `Source/Reference/Patterns/TupleRecordPatterns.uv`

Spec obligations exercised:

- `rule.17.Parse-TuplePatternElems-Empty`
- `rule.17.Pat-Tuple-R`
- `rule.17.Match-Tuple`
- `rule.16.T-Unit-Literal`

Spec basis:

- `SPECIFICATION.md:9020` types the empty tuple expression as `TypePrim("()")`.
- `SPECIFICATION.md:18216-18242` parses `()` in pattern position as
  `TuplePattern([])`.
- `SPECIFICATION.md:18317-18364` defines tuple-pattern typing and runtime
  matching by element count and elementwise binding.

Spec-valid specimen:

```ultraviolet
let unit_value: () = ()
let empty_tuple_hit: bool = if unit_value is () {
    true
} else {
    false
}
```

Observed bootstrap result before repair:

```text
HelloUltraviolet/build/bin/HelloUltraviolet.exe: exit=1

catalog compiled symbol failed: runPatternsTupleRecordPatternsReference
reference failed: catalogCompiledSymbolsExecute
reference failed: runPatternsTupleRecordPatternsReference
```

The focused scratch specimen returned `exit=1` until the empty tuple pattern
was evaluated separately from non-empty tuple aggregate matching.

Bootstrap owner:

- `LLVMBootstrap/cursive/src/05_codegen/llvm/emit/ir/control/if_case.cpp`

Failure analysis:

`IRTuplePattern` lowering required every tuple-pattern scrutinee to lower as an
LLVM struct value with a `TypeTuple` source type. Unit values lower as the
primitive unit type `TypePrim("()")` and carry no LLVM struct payload. The empty
tuple pattern therefore typechecked and lowered but evaluated to false at
runtime.

Required bootstrap behavior:

The empty tuple pattern is the source spelling that matches the unit value. Its
match result does not need to inspect an LLVM aggregate payload; it succeeds
when the normalized scrutinee type is `TypePrim("()")`, and also succeeds for
an internal zero-element `TypeTuple` if such a value reaches pattern lowering.

Repair:

- `LLVMBootstrap/cursive/src/05_codegen/llvm/emit/ir/control/if_case.cpp` now
  handles `IRTuplePattern` with no elements before aggregate tuple extraction.
  It returns true for `TypePrim("()")` and for zero-element `TypeTuple`, and
  keeps the existing struct extraction path for non-empty tuple patterns.

Spec clarification recorded:

`HelloUltraviolet/Audit/SpecClarificationsNeeded.md` records that the SPEC
should add an explicit static rule for `TuplePattern([]) ◁ TypePrim("()")`, or
state a different unit-pattern spelling if this intended reading changes.

Permanent corpus coverage:

- `Source/Reference/Patterns/TupleRecordPatterns.uv` exercises `if unit_value
  is ()` in the executable pattern reference runner.

Verified bootstrap result after repair:

```text
Visual Studio bootstrap build wrapper, target=cursive: exit=0, rebuilt if_case.cpp and Cursive.exe
.agents/tmp/UVPatternProbeImports/build/bin/UVPatternProbeImports.exe: exit=0
LLVMBootstrap/cursive/build/Release/Cursive.exe build HelloUltraviolet --target-profile x86_64-win64 --build-progress on --max-errors 20: exit=0, 14 warnings, 10 infos, 499.07s
HelloUltraviolet/build/bin/HelloUltraviolet.exe: exit=0
HelloUltraviolet/build/bin/HelloUltraviolet.exe --audit: exit=0
python3 Tools/ExtractObligationLedger.py --check: exit=0, PASS obligations=6045
```

## UVBOOT-0085: Async Suspension Frame Slots Miss Stable Binding Names

Status: repaired in the workspace bootstrap and verified by:

```text
Visual Studio bootstrap build wrapper, Config=Release: exit=0
LLVMBootstrap/cursive/build/Release/Cursive.exe build .agents/tmp/AsyncSuspendedProbe --target-profile x86_64-win64 --build-progress off --incremental off --max-errors 20: exit=0
.agents/tmp/AsyncSuspendedProbe/build/bin/AsyncSuspendedProbe.exe: exit=0
LLVMBootstrap/cursive/build/Release/Cursive.exe build HelloUltraviolet --target-profile x86_64-win64 --build-progress on --incremental on --max-errors 30: exit=0
HelloUltraviolet/build/bin/HelloUltraviolet.exe: exit=0
HelloUltraviolet/build/bin/HelloUltraviolet.exe --audit: exit=0
```

Reference source:

- `Source/Reference/Async/StateMachine.uv`
- `Source/Reference/Async/CompositionForms.uv`
- `Source/Reference/Async/CombinatorRuntimeForms.uv`

Spec obligations exercised:

- `rule.21.AsyncYield`
- `rule.21.AsyncResume`
- `rule.21.AsyncSync`
- `rule.21.AsyncComposition`

Spec-valid specimen:

```ultraviolet
internal procedure asyncCompositionUnitSuspends(value: i32)
    -> Async<(), (), i32, bool> {
    yield ()
    return value
}

let suspended: unique Async<(), (), i32, bool> = asyncCompositionUnitSuspends(57)
return if suspended is {
    @Suspended {
        let resumed = suspended~>resume(())
        if resumed is {
            @Completed { value } { value == 57 }
            @Suspended { false }
            @Failed { false }
        }
    }
    @Completed { false }
    @Failed { false }
}
```

Observed bootstrap result before repair:

```text
catalog compiled symbol failed: runAsyncCompositionCombinatorsReference
catalog compiled symbol failed: runAsyncCompositionFormsReference
reference failed: runAsyncCompositionSyncSuspendedUnitReference
reference failed: runAsyncCompositionSyncReference
reference failed: runAsyncCompositionAllSuspendedReference
reference failed: runAsyncCompositionReturnRaceSuspendedReference
reference failed: runAsyncCompositionStreamingRaceResumeReference
reference failed: runAsyncCompositionStreamingRaceReference
reference failed: runAsyncCompositionFilterSkipReference
reference failed: runAsyncCompositionChainSuspendedContinuationReference
reference failed: runAsyncCompositionCombinatorRuntimeReference
reference failed: runAsyncCompositionLoopIterationReference
reference failed: runAsyncCompositionCombinatorsReference
reference failed: runAsyncCompositionFormsReference
```

The focused async-suspension probe returned `exit=1` for manual resume. Its
generated resume function completed with payload `0` instead of the original
parameter value because the async frame did not contain the live parameter slot.

Bootstrap owner:

- `LLVMBootstrap/cursive/include/05_codegen/lower/lower_expr.h`
- `LLVMBootstrap/cursive/src/05_codegen/lower/lower_proc.cpp`
- `LLVMBootstrap/cursive/src/05_codegen/llvm/emit/proc_emit.cpp`
- `LLVMBootstrap/cursive/src/05_codegen/llvm/emit/ir_storage_emit.cpp`

Failure analysis:

Lowered identifier reads use stable binding names to distinguish source
bindings across scopes. Async liveness and frame-slot collection still keyed
parameter and binding slots by source names. After a suspension point, the
resume prelude therefore did not restore values read through stable names, and
some fallback addressable reads materialized zero-initialized storage.

Required bootstrap behavior:

Async frame slot collection must preserve every local or parameter value used
after a suspension point under the same local identity that the lowered body
will read on resume. Source names and stable names for the same binding must
refer to the same restored storage.

Repair:

- Async procedure metadata now records canonical stable slot names plus
  source-name aliases.
- Async resume setup registers all aliases to the same restored local storage.
- Async bind emission reuses an existing stable async slot when a binding's
  stable name is the collected frame slot.
- Async liveness collection treats `IRReadVar` as a use after suspension.

## UVBOOT-0086: Iterator Loop Patterns Bound Source Names After Stable Body Lowering

Status: repaired in the workspace bootstrap and verified by:

```text
Visual Studio bootstrap build wrapper, Config=Release: exit=0
LLVMBootstrap/cursive/build/Release/Cursive.exe build .agents/tmp/AsyncSuspendedProbe --target-profile x86_64-win64 --build-progress off --incremental off --max-errors 20: exit=0
.agents/tmp/AsyncSuspendedProbe/build/bin/AsyncSuspendedProbe.exe: exit=0
LLVMBootstrap/cursive/build/Release/Cursive.exe build HelloUltraviolet --target-profile x86_64-win64 --build-progress on --incremental on --max-errors 30: exit=0
HelloUltraviolet/build/bin/HelloUltraviolet.exe: exit=0
HelloUltraviolet/build/bin/HelloUltraviolet.exe --audit: exit=0
```

Reference source:

- `Source/Reference/Async/CompositionForms.uv`

Spec obligations exercised:

- `rule.16.Lower-Loop-Iter`
- `rule.17.Pat-Ident-R`
- `rule.17.Match-Ident`
- `rule.21.T-Loop-Iter-Async`

Spec-valid specimen:

```ultraviolet
internal procedure asyncCompositionIterates(first: i32, second: i32)
    -> Async<(), (), i32, bool> {
    var total: i32 = 0
    loop value in asyncCompositionTwoOutputs(first, second) {
        total = total + value
    }
    return total
}
```

Observed bootstrap result before repair:

```text
.agents/tmp/AsyncSuspendedProbe/build/bin/AsyncSuspendedProbe.exe: exit=3
```

The generated IR stored each async output into a local named from the source
pattern, while the loop body read the stable binding name. The body addition
therefore lowered as `total + 0` instead of `total + value`.

Bootstrap owner:

- `LLVMBootstrap/cursive/src/05_codegen/lower/expr/loop_iter.cpp`
- `LLVMBootstrap/cursive/src/05_codegen/lower/pattern/ir_pattern.cpp`

Failure analysis:

`LowerLoopIter` registered pattern bindings and lowered the loop body while the
loop scope was active, so body identifier reads correctly used stable binding
names. It then popped the loop scope before lowering the IR pattern used by the
LLVM loop emitter. That IR pattern retained the source name, splitting the
runtime loop-value store from the body read.

Required bootstrap behavior:

The IR pattern that drives iterator value storage must use the same binding
identity as the lowered loop body. Pattern binding identity is part of the
lowered loop contract, not only semantic-analysis bookkeeping.

Repair:

- `LowerLoopIter` now lowers the IR pattern before popping the loop scope.
- IR pattern lowering uses stable binding names for identifier and typed
  patterns when such names are available.
