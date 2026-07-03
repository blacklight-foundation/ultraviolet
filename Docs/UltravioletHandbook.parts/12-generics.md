## 12. Generics & Parametric Polymorphism

Ultraviolet supports parametric polymorphism through *generic parameters*: a declaration may abstract over one or more **type** parameters and is then *instantiated* at concrete type arguments. The model is **monomorphizing**: every distinct instantiation produces a distinct, fully specialized declaration with its own layout and its own compiled code. There is no type erasure, no runtime type passing, and no hidden boxing — generic parameter declarations, generic argument lists, and class-bound lists have *no* runtime semantics and are eliminated before abstract-machine evaluation (§14.1.5).

This chapter specifies the declaration syntax for generic parameters (§14.1), the inline class bounds (`<:`) that constrain them, parameter defaults, and the rules governing generic procedures and generic types — including type-argument inference and the monomorphization cost model (§14.2).

Generic parameters appear on procedures, records, enums, modals, classes, and type aliases. Every owning declaration form embeds the same `generic_params` grammar at a fixed point in its own production (records in Chapter "Records & Data Aggregation", enums in "Enums", modals in "Modal Types", classes in "Classes & Polymorphism", aliases in "Type Aliases"). The foundational class vocabulary — `Bitcopy`, `Clone`, `Drop`, `FfiSafe`, and `GpuSafe` — connects to the value-semantics, FFI, and GPU chapters and is discharged intrinsically by the compiler (§14.10.4).

> Spec mapping. This chapter covers Specification §14.1 (Generic Parameters and Arguments) and §14.2 (Generic Procedures and Types). Handbook section numbers below (12.1, 12.2, …) are local to the handbook; the parenthetical "§14.x" references point at the governing specification clause.

### 12.1 Generic Parameter and Argument Syntax

#### 12.1.1 Grammar

The canonical productions for the generic-parameter list, a single parameter, and the argument list are (§14.1.1):

```ebnf
generic_params       ::= "<" generic_param (";" generic_param)* ">"
generic_param        ::= identifier ("<:" class_bound ("," class_bound)*)? ("=" type)?
generic_args         ::= "<" type ("," type)* ","? ">"
```

Appendix B (§B.2) factors the same grammar into named helper productions, which are equivalent:

```ebnf
generic_params     ::= "<" generic_param_list ">"
generic_param_list ::= generic_param (";" generic_param)*
generic_param      ::= identifier bound_clause? default_clause?
bound_clause       ::= "<:" class_bound_list
default_clause     ::= "=" type
class_bound_list   ::= class_bound ("," class_bound)*
class_bound        ::= type_path generic_args?
generic_args       ::= "<" type_arg_list ">"
type_arg_list      ::= type ("," type)* ","?
```

Two separator conventions are load-bearing and must not be confused:

- **Generic *parameters* are separated by `;` (semicolon).** Inside `generic_params`, each parameter after the first is introduced by `;`. The parser confirms this: `Parse-TypeParamTail-Cons` advances only on `IsPunc(Tok(P), ";")`.
- **Generic *arguments* and *class bounds* are separated by `,` (comma).** Inside `generic_args` (`type_arg_list`) and inside `class_bound_list`, successive items are separated by `,`. The parser confirms this: `Parse-ClassBoundListTail-Cons` advances only on `IsPunc(Tok(P), ",")`.

A trailing comma in `generic_args` is permitted only where `TrailingCommaAllowed` holds (§5.5). A trailing comma does **not** denote an additional type argument (§14.1.1).

Inline bounds introduced by `<:` are **class bounds**. Foundational requirements such as `Bitcopy`, `Clone`, `Drop`, `FfiSafe`, and `GpuSafe` are built-in classes, so they use the same bound position as user-defined classes: `<TValue <: Bitcopy>`, `<TValue <: FfiSafe>`, and so on.

The `|:` token does not introduce generic constraints. In generic-adjacent syntax it belongs to contracts, invariants, and refinement types, where its operand is a boolean expression or braced predicate.

#### 12.1.2 Parameter Form and Naming

A parsed parameter is the tuple `TypeParam = ⟨name, bounds, default_opt, variance⟩`; a generic-parameter list is `GenericParams = [TypeParam]`; a list of supplied arguments is `GenericArgs = [Type]` (§14.1.3). At parse time the variance field is always `⊥`; the AST representation assigns user-declared parameters invariant variance (see §12.1.3).

**Naming convention (AGENTS.md).** Generic type parameters use `PascalCase` with a leading `T`: `TValue`, `TState`, `TResource`. The style guide's canonical examples are exactly `TValue`, `TState`, `TResource`. The `T` prefix distinguishes a type parameter from a concrete nominal type (which is also `PascalCase`) at every use site. Use a descriptive role name (`TValue`, `TElement`, `TError`) rather than a bare single letter. When a generic-parameter list, argument list, or signature exceeds the line limit, wrap to one item per line (AGENTS.md "Line Breaking").

```ultraviolet
record Pair<TFirst; TSecond> {
    first: TFirst
    second: TSecond
}
```

Here `Pair` declares two parameters separated by `;`. Within the record body the names `TFirst` and `TSecond` denote the bound type parameters: the binding judgment `BindTypeParams(Γ, params)` extends the environment with `TFirst : P_1, TSecond : P_2` (§14.1.3).

#### 12.1.3 Variance

User-declared generic parameters are **invariant** by default (§14.1.3). There is no user-visible syntax for declaring covariance, contravariance, or bivariance: the variance field of a `TypeParam` for a user declaration is always `Invariant`. Built-in types that have dedicated subtyping rules — notably `Async` — define their variance through those rules rather than through any surface annotation (§14.1.3). The `Variance` set itself is `{Covariant, Contravariant, Invariant, Bivariant}`, but only the built-in subtyping machinery populates the non-invariant positions.

The practical consequence for invariant parameters is exactness at assignment and checking sites. If an expression's stripped type is `TypeApply(path, [S_1, …, S_n])` and the expected stripped type is `TypeApply(path, [T_1, …, T_n])`, then for every position `i` whose variance is invariant (`VarianceOf(path, i) = ` `=`) the checker requires `S_i ≡ T_i` (type equivalence), not merely a subtype relation; otherwise it raises `Chk-Generic-Invariant-Err` (`E-TYP-1520`, §14.2.4). For the built-in directional positions, `Chk-Generic-Variance-Err` (`E-TYP-1521`) applies covariant (`+`) or contravariant (`-`) rules through `VarianceSatisfied`.

#### 12.1.4 Class Bounds (`<:`)

A class bound constrains a parameter to types that implement a named class. Syntactically a bound is `class_bound ::= type_path generic_args?` — a class path optionally applied to its own type arguments. Multiple bounds on one parameter are comma-separated and are **conjunctive**: an instantiation must satisfy *all* of them.

Semantically (§14.1.4), a type argument `A` *satisfies* a parameter's bounds exactly when it is a subtype of each bound:

```text
(T-Constraint-Sat)
∀ B ∈ Bounds, Γ ⊢ A <: B
─────────────────────────────────────
Γ ⊢ A satisfies Bounds
```

Each declared bound must itself name a class. A bound whose path resolves to a non-class type is rejected by `WF-Generic-Param` with diagnostic `E-TYP-2305` ("Class bound references a non-class type"). The bounds of *all* parameters in a list, together with default well-formedness, are checked by:

```text
(WF-Generic-Param)
∀ i ≠ j, name_i ≠ name_j
∀ i, ∀ B ∈ Bounds_i, Γ ⊢ B : ClassPath
DefaultSuffix([P_1, …, P_n])    DefaultRefsOk([P_1, …, P_n])    DefaultWF(Γ, [P_1, …, P_n])
──────────────────────────────────────────────────────────────────────────
Γ ⊢ ⟨P_1; …; P_n⟩ wf
```

The first premise forbids duplicate parameter names (`E-TYP-2304`); the second requires every bound to be a class path; the remaining three govern defaults (§12.1.6).

A class bound declares the *constraint* that the argument type implements the named class. It is checked at every instantiation site, so a generic procedure or type bounded by `<: Eq` can be instantiated only at types that implement `Eq`:

```ultraviolet
record Keyed<TKey <: Eq; TValue> {
    key: TKey
    value: TValue
}
```

`TKey <: Eq` requires the key type to implement the `Eq` class. Instantiating `Keyed<i32, string>` is well-formed because `i32` satisfies `Eq` intrinsically (`EqType(i32)` holds, §14.10.4). Instantiating `Keyed` with a key type that does not implement `Eq` fails the bound check (`E-TYP-2302`).

> Operators are not class-bound dispatch. The built-in `==` / `!=` operators are typed by `T-Compare-Eq`, which requires the *intrinsic* predicate `EqType(T)` on the operand type — it does **not** fire on a bare type parameter merely because that parameter carries a `<: Eq` bound. Inside a generic body, a type parameter `TKey` is a type variable, not an `EqType`, so `someKey == otherKey` does not typecheck through the `<: Eq` bound. A `<: Eq` bound makes the *instantiated* type usable wherever `Eq` is demanded (including the language's intrinsic equality on concrete `EqType` instantiations); it does not turn the operator into bounded dispatch on the parameter itself. Constrain generic bodies to operations the spec actually licenses; do not assume an operator resolves through a bound.

#### 12.1.5 Foundational Class Bounds

`Bitcopy`, `Clone`, `Drop`, `FfiSafe`, and `GpuSafe` are built-in foundational classes. They are ordinary class names in generic bound syntax, but their satisfaction is defined by intrinsic compiler judgments instead of user-written `implements` declarations:

- **`T <: Bitcopy`** means `BitcopyType(T)` holds. A `unique`-permission type is never `Bitcopy`, and no type may be both `Bitcopy` and `Drop`.
- **`T <: Clone`** means `CloneType(T)` holds. Built-in bit-copyable types are cloneable, and a record may also satisfy `Clone` through an exact `clone` method.
- **`T <: Drop`** means `DropType(T)` holds. A type satisfies it through built-in owning string/bytes types or an exact `drop` method.
- **`T <: FfiSafe`** means `FfiSafeType(T)` checks successfully. Generic FFI-safe aggregates must bound every stored type parameter with `FfiSafe` or a subclass of it.
- **`T <: GpuSafe`** means `GpuSafeType(T)` checks successfully. Generic GPU-safe aggregates must bound every stored type parameter with `GpuSafe` or a subclass of it.

These are class bounds, so they sit directly on the constrained parameter:

```ultraviolet
record Buffer<TElement <: Bitcopy> {
    data: [TElement]
    length: usize
}
```

`Buffer` constrains `TElement` to be bit-copyable. Instantiating `Buffer<f32>` is well-formed because `f32` satisfies `Bitcopy`; an attempt to instantiate `Buffer` with a type for which `BitcopyType` fails raises `E-TYP-2302`.

Multiple foundational requirements over more than one parameter use the same generic-parameter list syntax as other bounds:

```ultraviolet
procedure copyPair<TFirst <: Bitcopy; TSecond <: Bitcopy>(
    first: TFirst,
    second: TSecond
) -> Pair<TFirst, TSecond> {
    return Pair { first: first, second: second }
}
```

Because the bound follows the parameter it constrains, the source spelling remains local: a constraint on `TFirst` is written next to `TFirst`, a constraint on `TSecond` next to `TSecond`, and a contract on a return value remains in the procedure's contract clause.

#### 12.1.6 Parameter Defaults (`= type`)

A parameter may declare a default type with `= type` (`default_clause ::= "=" type`). Defaults are subject to three well-formedness conditions (§14.1.4):

- **`DefaultSuffix`** — defaulted parameters must form a suffix of the list: once a parameter has a default, every later parameter must also have one (`∀ i < j. params[i].default_opt ≠ ⊥ ⇒ params[j].default_opt ≠ ⊥`). A non-suffix default is rejected (§14.1.7).
- **`DefaultRefsOk`** — a default may only mention parameters that appear *earlier* in the list (`TypeParamsIn(T_i, params) ⊆ {params[j].name | j < i}`). A default that refers to a later parameter is rejected (§14.1.7).
- **`DefaultWF`** — each default, evaluated under the bindings of the parameters preceding it, must be a well-formed type and must itself satisfy that parameter's bounds.

When a use site supplies fewer arguments than parameters, the missing trailing arguments are filled in left-to-right from the defaults, substituting already-resolved arguments into each default (`DefaultArgs`, §14.1.4):

```text
DefaultArgs(params, args) = args' ⇔ params = [P_1, …, P_n] ∧ args = [A_1, …, A_k] ∧ k ≤ n ∧
  (∀ i ≤ k. A_i' = A_i) ∧
  (∀ i ∈ k+1..n. P_i.default_opt = T_i ∧ A_i' = TypeSubst([A_1'/P_1.name, …, A_{i-1}'/P_{i-1}.name], T_i)) ∧
  args' = [A_1', …, A_n']
```

If `k > n` (too many arguments) or a missing parameter has no default, `DefaultArgs` is undefined (`⊥`) and the use site is ill-formed (§14.1.7); the catalogued wrong-arity diagnostic in this family is `E-TYP-2303` ("Wrong number of type arguments").

```ultraviolet
record RingBuffer<TElement; TIndex = usize> {
    data: [TElement]
    head: TIndex
    tail: TIndex
}
```

`TIndex` defaults to `usize`. A use of `RingBuffer<f64>` is elaborated by `DefaultArgs` to `RingBuffer<f64, usize>`. Writing `RingBuffer<f64, u32>` overrides the default explicitly. Because defaults must form a suffix, a hypothetical `RingBuffer<TElement = i32; TIndex>` (a defaulted parameter followed by a non-defaulted one) would be rejected by `DefaultSuffix`.

#### 12.1.7 No Const / Value Generic Parameters

`generic_param` admits exactly an identifier, an optional `<:` bound clause, and an optional `= type` default (`generic_param ::= identifier bound_clause? default_clause?`). There is **no** const-generic or value-generic parameter form: a generic parameter is always a *type* parameter. Array sizes are written with an ordinary expression inside the array type, `array_type ::= "[" type ";" expression "]"` (§B.2), not as a generic value parameter. Do not attempt to parameterize a declaration over an integer or other value — the grammar provides no production for it, and such syntax will not parse.

#### 12.1.8 No Runtime Semantics

Generic-parameter declarations, generic-argument lists, and class-bound lists have **no runtime semantics**. They are eliminated before abstract-machine evaluation (§14.1.5). All of their force is discharged statically during well-formedness checking and monomorphization (§12.2.7).

### 12.2 Generic Procedures and Generic Types

#### 12.2.1 Grammar

```ebnf
generic_procedure ::= "procedure" identifier generic_params? signature contract_clause? block
generic_call      ::= callee generic_args "(" arg_list? ")"
generic_type_use  ::= type_path generic_args
```

The generic-parameter list is not parsed by a standalone "generic declaration" parser; instead each owning declaration form invokes `ParseGenericParamsOpt` at the appropriate point in its own grammar before its body-specific parser (§14.2.2). From Appendix B (§B.6):

```ebnf
procedure_decl  ::= attribute_list? visibility? "procedure" identifier generic_params? signature contract_clause? block_expr
record_decl     ::= attribute_list? visibility? "record" identifier generic_params? implements_clause? "{" record_body "}" type_invariant?
enum_decl       ::= attribute_list? visibility? "enum" identifier generic_params? implements_clause? "{" variant_members? "}" type_invariant?
modal_decl      ::= attribute_list? visibility? "modal" identifier generic_params? implements_clause? "{" state_block+ "}" type_invariant?
type_alias_decl ::= attribute_list? visibility? "type" identifier generic_params? "=" type
```

Note the fixed ordering for nominal types: `generic_params` precedes `implements_clause` (`<: class_list`). A generic *type use* is just a type path applied to arguments via `nominal_type ::= type_path generic_args?` (§B.2).

#### 12.2.2 Declaring a Generic Procedure

A generic procedure is well-formed when its parameter list is well-formed and, under the environment extended with the type-parameter bindings, both its signature and body are well-formed (§14.2.4):

```text
(WF-Generic-Proc)
Γ ⊢ ⟨P_1, …, P_n⟩ wf    Γ' = Γ, T_1 : P_1, …, T_n : P_n    Γ' ⊢ signature wf    Γ' ⊢ body wf
────────────────────────────────────────────────────────────────────────────────────────────────────────
Γ ⊢ `procedure` f⟨P_1, …, P_n⟩(...) → R {…} wf
```

```ultraviolet
procedure swap<TValue <: Bitcopy>(first: TValue, second: TValue) -> Pair<TValue, TValue> {
    return Pair { first: second, second: first }
}
```

`swap` abstracts over one parameter `TValue`, requires it to be `Bitcopy` so the values can be duplicated, and returns a `Pair` with the two fields swapped. The bound follows the parameter it constrains. The record literal `Pair { … }` is a bare-identifier literal (record literals carry no type arguments); its `TValue` instantiation is supplied by the declared return type `Pair<TValue, TValue>` during return-expression checking (`T-Return-Value` checks the return expression against the return type, and `T-Record-Literal-ExpectedApply` resolves the type arguments from that expected type).

#### 12.2.3 Calling a Generic Procedure: Explicit Type Arguments

The explicit form supplies type arguments between the callee and the argument list:

```ebnf
generic_call ::= callee generic_args "(" arg_list? ")"
```

A call site is recognized as a type-argument call when the post-callee token can begin generic arguments, those arguments parse, and the next token is `(` (`CallTypeArgsStart`, §14.2.2). The typing rule resolves the callee to a generic procedure, fills defaults, substitutes, and checks bounds and the arguments (§14.2.4):

```text
(T-Generic-Call)
GenericCalleeProc(callee) = ProcedureDecl(_, _, _, gen_params_opt, params, ret_opt, _, _, _, _)
params_gen = TypeParamsOpt(gen_params_opt)    params_gen = [P_1, …, P_n]
DefaultArgs(params_gen, [A_1, …, A_k]) = [A_1', …, A_n']
θ = [A_1'/P_1.name, …, A_n'/P_n.name]
params_θ = [⟨mode_j, TypeSubst(θ, T_j)⟩ | ⟨mode_j, x_j, T_j⟩ ∈ params]
∀ i ∈ 1..n. Γ ⊢ A_i' satisfies Bounds(P_i)
Γ; R; L ⊢ ArgsOk_T(params_θ, args)
──────────────────────────────────────────────────────────────────────────────────────────────────────
Γ; R; L ⊢ CallTypeArgs(callee, [A_1, …, A_k], args) : ProcReturn(ret_opt)[θ]
```

The result type is the procedure's return type with `θ` applied. If `DefaultArgs` is undefined (wrong arity), the call is ill-formed (`Generic-Call-ArgCount-Err`); the wrong-arity diagnostic code in this family is `E-TYP-2303`.

```ultraviolet
let swapped: Pair<i32, i32> = swap<i32>(10, 20)
```

`swap<i32>(10, 20)` binds `TValue = i32`, checks `i32 <: Bitcopy` (which holds), and yields `Pair<i32, i32>`.

#### 12.2.4 Type-Argument Inference

Type arguments **may** be omitted at a generic-procedure call site, but **only** when inference succeeds (§14.2.4). Inference introduces a fresh type variable per parameter, substitutes them through the parameter and return types, collects constraints from the actual argument types (and, if present, the expected result type `T_exp`), solves them, and then fills any still-unsolved variables from defaults (§14.2.4):

```text
(GenericCallInference)
GenericCalleeProc(callee) = ProcedureDecl(_, _, _, gen_params_opt, params, ret_opt, _, _, _, _)
params_gen = TypeParamsOpt(gen_params_opt)    params_gen = [P_1, …, P_n]
FreshTypeArgs(params_gen) = [X_1, …, X_n]    θ_var = [X_1/P_1.name, …, X_n/P_n.name]
params_i = [⟨mode_j, TypeSubst(θ_var, T_j)⟩ | ⟨mode_j, x_j, T_j⟩ ∈ params]
R_i = TypeSubst(θ_var, ProcReturn(ret_opt))    |params_i| = |args|
C_args = {(ArgType(params_i[j], args[j]), ParamType(params_i[j])) | j ∈ 1..|args|}
C_ret = {(R_i, T_exp)}  if T_exp_opt = T_exp ;  ∅ otherwise
Γ ⊢ Solve(C_args ∪ C_ret) ⇓ θ_s
raw_args = [θ_s(X_1), …, θ_s(X_n)]
InferTypeArgs(params_gen, raw_args) = [A_1, …, A_n]    θ = [A_1/P_1.name, …, A_n/P_n.name]
∀ i ∈ 1..n. Γ ⊢ A_i satisfies Bounds(P_i)
──────────────────────────────────────────────────────────────────────────────────────────────────────
Γ; R; L ⊢ GenericCallInference(callee, args, T_exp_opt) ⇓ [A_1, …, A_n]
```

`InferTypeArgs` accepts a solved variable as the argument directly; for any variable the solver left unsolved, it falls back to that parameter's default (substituting the already-resolved arguments). A parameter that is neither solved nor defaulted leaves inference stuck:

```text
InferTypeArgs(params_gen, raw_args) = args' ⇔ params_gen = [P_1, …, P_n] ∧ raw_args = [R_1, …, R_n] ∧
  (∀ i ∈ 1..n.
    ((SolvedType(R_i) ∧ A_i = R_i) ∨
     (¬SolvedType(R_i) ∧ P_i.default_opt = D_i ∧ A_i = TypeSubst([A_1/P_1.name, …, A_{i-1}/P_{i-1}.name], D_i)))) ∧
  args' = [A_1, …, A_n]
```

A well-typed omitted-type-argument call is elaborated to `CallTypeArgs(callee, inferred_args, args)` before monomorphic-call elaboration (§14.2.4). If inference cannot determine the arguments, the compiler reports `E-TYP-2301` ("Type arguments cannot be inferred; explicit instantiation required") and the arguments must be supplied explicitly.

```ultraviolet
// TValue is inferred as i32 from the argument types.
let swapped: Pair<i32, i32> = swap(10, 20)
```

Because both arguments are `i32`, `Solve` binds the fresh variable to `i32` and inference yields `swap<i32>(10, 20)`. When inference is genuinely ambiguous — for example, the parameter appears only in the return type and no expected type is available — supply the arguments explicitly with `swap<i32>(…)`.

#### 12.2.5 Generic Records and Other Nominal Types

A generic nominal type is declared by placing `generic_params` after its name, and is *used* by applying type arguments. The well-formedness rule for an applied type fills defaults, substitutes, and checks each argument's well-formedness and bounds (§14.2.4):

```text
(WF-Apply)
T = TypeApply(p, args)    p ∈ dom(Σ.Types)    params_gen = TypeParamsOf(p)
DefaultArgs(params_gen, args) = args'    θ = [args'_i / params_gen[i].name]
∀ i, Γ ⊢ args'_i wf    ∀ i, Γ ⊢ args'_i satisfies Bounds(params_gen[i])
──────────────────────────────────────────────────────────────────────────────────────────────────────
Γ ⊢ T wf
```

A generic nominal type **must** be applied to its arguments wherever it appears in *type* position. Naming a generic type path *without* arguments in type position is ill-formed:

```text
(WF-Path-Generic-Err)
T = TypePath(p)    p ∈ dom(Σ.Types)    TypeParamsOf(p) ≠ []
────────────────────────────────────────────────────────────────
Γ ⊢ T wf ⇑
```

This is the diagnostic for "use of a generic nominal declaration without required arguments" (§14.2.7). Supplying the wrong number of arguments triggers the wrong-arity error (`WF-Apply-ArgCount-Err`; code `E-TYP-2303`).

> Record and enum *literals* take no type arguments. A generic type carries its arguments only in *type* position (annotations, return types, field types, `let` bindings). The construction *expression* uses the bare declaration name and infers the type arguments from the expected type:
> - `record_literal ::= identifier "{" field_init_list "}"` — a single bare identifier (`|path| = 1`); writing `Stack<i32> { … }` does **not** parse.
> - `enum_literal ::= type_path "::" identifier variant_args?` where `type_path` has no `generic_args`; writing `Outcome<string, string>::Ok(…)` does **not** parse.
> In both cases the type arguments are supplied by the expected type at the use site (`T-Record-Literal-ExpectedApply` for records; for enums, the literal's `EnumLiteral(path, …)` is checked against the expected applied enum type, which fixes the arguments).

A complete worked generic record with a constrained parameter, a default, methods, and instantiation:

```ultraviolet
record Stack<TElement <: Clone; TIndex = usize> {
    data: [TElement]
    count: TIndex

    procedure push(~%, value: TElement) -> () {
        self.data[self.count] = value
        self.count += 1
    }

    procedure top(~) -> TElement {
        return self.data[self.count - 1]
    }
}

procedure makeIntStack() -> Stack<i32> {
    let storage: [i32] = [0, 0, 0, 0]
    let stack: Stack<i32> = Stack { data: storage, count: 0 }
    return stack
}
```

`Stack<i32>` (a *type*, in the return position and the `let` annotation) is elaborated by `DefaultArgs` to `Stack<i32, usize>`. The `<TElement <: Clone>` bound is checked at every application: `i32 <: Clone` holds (`i32` is `Bitcopy`, hence `Clone`), so `Stack<i32>` is well-formed. The construction `Stack { data: storage, count: 0 }` is a bare-identifier record literal; its type arguments come from the `let stack: Stack<i32>` annotation via `T-Record-Literal-ExpectedApply`. The receivers follow `receiver_shorthand ::= "~" | "~!" | "~%"`: `push` uses `~%` (write-key receiver, mutating) and `top` uses `~` (read-key receiver).

A generic enum follows the same shape:

```ultraviolet
enum Outcome<TValue; TError> {
    Ok(TValue)
    Err(TError)
}

procedure firstNonEmpty(line: string) -> Outcome<string, string> {
    if line == "" {
        return Outcome::Err("empty line")
    }
    return Outcome::Ok(line)
}
```

Each enum-literal use names the variant through a plain `type_path "::" identifier` (`Outcome::Err(…)`, `Outcome::Ok(…)`) with **no** type arguments on the path; the `TValue`/`TError` instantiation is fixed by the declared return type `Outcome<string, string>` when each `return` expression is checked against it. The empty-string comparison `line == ""` is licensed because `EqType(string)` holds (`TypeString` is an `EqType`).

#### 12.2.6 Generic Type Aliases

A type alias may carry generic parameters and expands to its right-hand type after substitution (`type_alias_decl ::= … "type" identifier generic_params? "=" type`, §B.6):

```ultraviolet
type IndexPair<TIndex> = Pair<TIndex, TIndex>
```

`IndexPair<usize>` denotes `Pair<usize, usize>`. Here `Pair<TIndex, TIndex>` is a *type* (a `nominal_type` with `generic_args`), which is the correct form for a generic type in type position — unlike a record *literal*, a type reference does carry its arguments. Alias parameters obey the same well-formedness rules as any other generic parameters, and alias bodies must not form a cycle.

#### 12.2.7 Monomorphization Model and Cost

Generics are realized by **monomorphization**, not erasure (§14.2.5–§14.2.6):

- `CallTypeArgs` is elaborated to a monomorphic `Call` after substitution. `TypeApply(path, args)` denotes the *specialized* declaration obtained by applying the elaborated substitution to the generic declaration named by `path` (§14.2.5).
- **Distinct monomorphic instantiations are distinct declarations with distinct layouts** (§14.2.5). `Stack<i32>` and `Stack<f64>` are two separate types with independent memory layout. For an instantiated nominal type, `sizeof` and `alignof` are those of its substituted body (§14.2.6).
- Lowering produces a specialized declaration `D[A_1/T_1, …, A_n/T_n]` for each concrete instantiation; calls to generic procedures lower to **direct static calls** to the specialized instantiation, and each distinct instantiation lowers independently (§14.2.6).

The cost implications follow directly:

- **No runtime overhead per call.** There is no vtable indirection, no type-descriptor passing, and no boxing for generic dispatch — each call is a static call to a specialized function. Generic abstraction is zero-cost at runtime.
- **Code-size and compile-time cost scale with the number of distinct instantiations.** Every distinct type-argument tuple produces its own compiled body and its own layout. Many instantiations of a large generic body multiply code size and compilation work.
- **Recursion is bounded.** Implementations MUST reject infinite monomorphization recursion (`E-TYP-2307`), and the **maximum instantiation depth is 128** (§14.2.6); exceeding it raises `E-TYP-2308`. A generic procedure or type that, when specialized, requires specializing itself at an ever-growing type is rejected rather than looping.

Because generic class methods are monomorphized rather than dispatched dynamically, **a class method that carries a generic parameter list is not vtable-eligible and MUST NOT be invoked through a dynamic class object** (`$`-dispatch), per §14.2.4 (`E-TYP-2542`). Keep methods you intend to call through `$ClassName` non-generic.

### Idioms & Best Practices

- **Prefix every type parameter with `T` and give it a role name.** Use `TValue`, `TElement`, `TKey`, `TError`, `TState`, `TResource` — `PascalCase` with a leading `T` (AGENTS.md). This keeps type parameters visually distinct from concrete types at every mention.
- **Separate parameters with `;`, arguments and bounds with `,`.** This is the single most common syntax mistake. `<TFirst; TSecond>` declares two parameters; `<i32, f64>` is an argument list; `<: Eq, Hash` is a comma-separated bound list.
- **Use `<:` for generic constraints.** Use a class bound when you need an instantiation to implement a user-defined class (`Eq`, `Hash`, `Iterator`, an associated method) or satisfy a built-in foundational class (`Bitcopy`, `Clone`, `Drop`, `FfiSafe`, `GpuSafe`).
- **Do not assume operators dispatch through bounds.** `==`/`!=` require the intrinsic `EqType` on the operand type, not a `<: Eq` bound on a type parameter. Constrain generic bodies to operations the spec licenses for type-variable operands; rely on the bound to make the *instantiated* type acceptable where the class is demanded.
- **Constrain no more than necessary.** Add a bound only when the declaration actually relies on it. Over-constraining narrows the valid instantiations without benefit; under-constraining is caught by the checker (`E-TYP-2302`).
- **Construct generic records and enums with bare names.** Write `Stack { … }` and `Outcome::Ok(x)`, and let the expected type (annotation, return type, field type) supply the type arguments. Never put `<…>` on a record-literal head or an enum-literal path.
- **Prefer inference at call sites, but annotate when it clarifies.** Omit type arguments when they are unambiguously inferable from the actual arguments or the expected type; supply them explicitly when the parameter appears only in the return type or when the reader benefits from seeing the instantiation.
- **Keep defaults a trailing suffix that references only earlier parameters.** Order parameters so defaulted ones come last (`<TElement; TIndex = usize>`), and write defaults that mention only parameters to their left.
- **Mind the monomorphization budget.** A large generic body instantiated at many types multiplies code size and compile time. Factor type-independent work into non-generic helpers and keep the generic surface thin where instantiation counts are high.
- **Wrap long parameter, argument, and signature lists one item per line** when they exceed the column limit (AGENTS.md "Line Breaking").

### Pitfalls & Diagnostics

- **Using `,` to separate parameters, or `;` to separate arguments.** `<TFirst, TSecond>` is wrong (parameters use `;`); `<i32; f64>` is wrong (arguments use `,`). Match the grammar exactly.
- **Putting a bound away from the parameter it constrains.** Write `<TValue <: Bitcopy>`, not a detached clause after the signature or declaration header. A `<:` bound whose path is not a class is rejected with `E-TYP-2305`.
- **Putting type arguments on a record or enum literal.** `Stack<i32> { … }` and `Outcome<string, string>::Err(…)` do not parse: record-literal heads are a single bare identifier, and enum-literal paths carry no generic arguments. Use `Stack { … }` / `Outcome::Err(…)` with an expected type that fixes the arguments.
- **Using `|:` for generic constraints.** `|:` introduces contracts, invariants, and refinement predicates. It does not constrain generic type parameters; put those constraints in the parameter list with `<:`.
- **Assuming `==` works on a `<: Eq` parameter.** Operators require the intrinsic `EqType`/`OrdType`, not the class bound; a bare type parameter is never an `EqType`. The bound governs instantiation, not in-body operator typing.
- **Bound not satisfied by the argument.** Instantiating a parameter with a type that fails a `<:` bound raises `E-TYP-2302` — for example, putting a `Drop` type into `Buffer<TElement <: Bitcopy>`.
- **`Bitcopy`/`Drop` conflict.** No type is simultaneously `Bitcopy` and `Drop` (`BitcopyDrop-Conflict`); a type that satisfies both is itself ill-formed (`E-TYP-2621`, "Type satisfies both `BitcopyType` and `DropType`"). A `<T <: Bitcopy>` bound therefore excludes every `Drop` type at instantiation (`E-TYP-2302`).
- **Wrong number of type arguments.** Supplying too many, or too few without defaults to cover the gap, makes `DefaultArgs` undefined and yields a wrong-arity error (`Generic-Call-ArgCount-Err` for calls, `WF-Apply-ArgCount-Err` for applied types); the catalogued code in this family is `E-TYP-2303` ("Wrong number of type arguments").
- **Using a generic nominal type without arguments in type position.** Mentioning `Stack` (no `<…>`) where a type is required is ill-formed (`WF-Path-Generic-Err`); always apply the type, e.g. `Stack<i32>`.
- **Inference failure.** When type arguments cannot be inferred (e.g. the parameter is return-only and no expected type is present), the compiler raises `E-TYP-2301`; supply the arguments explicitly.
- **Duplicate parameter names.** Two parameters with the same name in one list raise `E-TYP-2304`.
- **Non-suffix or forward-referencing defaults.** A defaulted parameter followed by a non-defaulted one, or a default that names a later parameter, is rejected (`DefaultSuffix` / `DefaultRefsOk`; §14.1.7).
- **Invariance surprises.** User parameters are invariant: `Stack<Derived>` is not usable where `Stack<Base>` is expected. A mismatch at an invariant position raises `E-TYP-1520`; a directional built-in position raises `E-TYP-1521`.
- **Infinite or too-deep monomorphization.** A generic that specializes itself at ever-larger types is rejected: `E-TYP-2307` (infinite recursion) or `E-TYP-2308` (depth limit 128 exceeded).
- **Generic method through `$`-dispatch.** A class method with a generic parameter list is not vtable-eligible and cannot be called on a dynamic class object (`E-TYP-2542`). Relatedly, generic parameters are forbidden in `extern` procedure signatures (`E-TYP-2306`) and on `#host_export` procedures (`E-TYP-2634`, `HostExport-Generic-Err`).
