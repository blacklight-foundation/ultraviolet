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
error: Static rule failed without assigned diagnostic code: Record-Method-RecvSelf-Err
```

Bootstrap owner:

- `LLVMBootstrap/cursive/src/04_analysis/composite/record_methods.cpp`
- `LLVMBootstrap/cursive/src/00_core/generated/static_rule_registry.inc`

Failure analysis:

The record-method receiver checker returned `Record-Method-RecvSelf-Err` as the
diagnostic id but did not trace the SPEC rule at the emission point. The
generated static-rule registry therefore omitted the rule, and the typecheck
diagnostic resolver classified the id as an internal unknown instead of an
uncoded static-rule diagnostic.

Repair:

- `record_methods.cpp` now records `SPEC_RULE("Record-Method-RecvSelf-Err")`
  before returning the diagnostic id.
- `generate_static_rule_registry` regenerated `static_rule_registry.inc`, and
  the Release `cursive` target was rebuilt successfully.

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
error: Static rule failed without assigned diagnostic code: Let-Refutable-Pattern-Err
```

Bootstrap owner:

- `LLVMBootstrap/cursive/src/04_analysis/typing/stmt/stmt_common.cpp`
- `LLVMBootstrap/cursive/src/00_core/generated/static_rule_registry.inc`

Failure analysis:

The statement pattern checker returned `Let-Refutable-Pattern-Err` for a
refutable `let` pattern but traced `Pat-Refutable-Err`. The generated static-rule
registry therefore knew about the traced helper name rather than the emitted
SPEC rule diagnostic id.

Repair:

- `stmt_common.cpp` now records `SPEC_RULE("Let-Refutable-Pattern-Err")` before
  returning that diagnostic id.
- `generate_static_rule_registry.py` regenerated `static_rule_registry.inc`, and
  the Release `cursive` target was rebuilt successfully.

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

Status: open.

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

Observed bootstrap result:

```text
error[E-MOD-1302]: Duplicate declaration in module scope
  --> .../FreeProcedureOverloadResolution/Source/Main.uv:7:8
7 | public procedure selectOverload(value: bool) -> i32 {
```

Bootstrap owner:

- `LLVMBootstrap/cursive/src/04_analysis/resolve/collect_toplevel.cpp`
- `LLVMBootstrap/cursive/src/04_analysis/typing/expr/call.cpp`

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
