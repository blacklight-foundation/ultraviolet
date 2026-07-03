## 15. Procedures, Methods & Overloading

This chapter defines the three core ways Ultraviolet packages executable behavior: free **procedures** declared with the `procedure` keyword (§15.1), **methods** that attach to a type through a **receiver** (§15.2), and the **overloading** rules that govern how a name with several declarations resolves to one of them (§15.3). The constructs here build directly on the type and permission machinery introduced in *Types & Type Constructors* and *Permissions & Ownership*, and they are the foundation on which *Records & Structs*, *Modal Types*, and *Classes & Conformance* layer their behavior. Contract clauses (`|: pre |= post`) are introduced syntactically here — this chapter shows where they attach; how contracts are checked is owned in full by *Contracts* (§15.4).

Every keyword, operator, and type name in this chapter is reproduced exactly from the specification. Several facts are worth stating up front because they recur throughout:

- The **unit type** is written `()` (Appendix B: `unit_type ::= "(" ")"`). The specification's metalanguage refers to it as `TypePrim("()")`. There is **no `unit` keyword**; "returns unit" always means the type `()`. The unit value literal is also `()` (`unit_literal ::= "(" ")"`).
- The **method-call operator** is `~>` (Appendix B postfix grammar: `postfix_suffix ::= "." identifier | "." decimal_integer | "[" expression "]" | "~>" identifier "(" argument_list? ")" | "(" argument_list? ")" | "?"`). Methods are invoked with `receiver~>name(args)`, never with `.`. The `.` postfix is reserved for field access (`.identifier`) and tuple-index access (`.decimal_integer`).
- The three **permissions** are `const`, `unique`, and `shared` (B.2: `permission ::= "const" | "unique" | "shared"`). The default permission, when none is written, is `const`.
- The only **parameter mode** is `move` (B.6: `param_mode ::= "move"`). There is no `copy` parameter mode; `copy` is an argument-passing mode at the call site.

---

### 15.1 Procedure Declarations

A procedure is a named, free-standing unit of behavior. It is a top-level item (B.6: `top_level_item ::= import_decl | using_decl | static_decl | procedure_decl | comptime_procedure_decl | record_decl | enum_decl | modal_decl | class_declaration | type_alias_decl | extern_block | derive_target_decl`).

#### 15.1.1 Exact Syntax

The normative grammar in §15.1.1 is:

```ebnf
procedure_decl ::= attribute_list? visibility? "procedure" identifier generic_params? signature contract_clause? block_expr
signature      ::= "(" param_list? ")" ("->" type)?
param_list     ::= param ("," param)*
param          ::= "move"? identifier ":" type
```

Appendix B (B.6) gives the same production with the trailing-comma and union-return refinements the parser also accepts:

```ebnf
procedure_decl ::= attribute_list? visibility? "procedure" identifier generic_params? signature contract_clause? block_expr
signature      ::= "(" param_list? ")" ("->" return_type)?
param_list     ::= param ("," param)* ","?
param          ::= param_mode? identifier ":" type
param_mode     ::= "move"
return_type    ::= type | union_return
union_return   ::= type ("|" type)+
```

Reading these together, a procedure declaration consists, in order, of:

1. an optional `attribute_list` (one or more `attribute`s; e.g. `#inline`, `#export("...")`, `#test`);
2. an optional `visibility` — one of `public`, `internal`, or `private` (B.6: `visibility ::= "public" | "internal" | "private"`);
3. the keyword **`procedure`**;
4. an `identifier` naming the procedure (`camelCase` per the style guide);
5. optional `generic_params`, e.g. `<TValue>` or `<TValue <: Drawable>` (B.2: `generic_params ::= "<" generic_param_list ">"`; the parameters in `generic_param_list` are separated by `;`, and a bound is written with `<:`);
6. a `signature`: a parenthesized `param_list` followed by an optional `-> return_type` annotation;
7. an optional `contract_clause` (`|: pre |= post`, owned by §15.4); and
8. the `block_expr` body, opened with a same-line brace.

##### Parameters and the `move` mode

Each parameter is `param_mode? identifier ":" type`, and the only `param_mode` is **`move`**. The argument-passing semantics are fixed by the `EvalArgsSigma` family in §15.1.5:

- A parameter **without** `move` (mode `⊥`) is passed **by reference**: the callee binds an `Alias(addr)` to the caller's place (**(EvalArgsSigma-Cons-Ref)**).
- A parameter **with** `move` takes **ownership** of its argument: the callee binds an `Owned(addr)` (**(EvalArgsSigma-Cons-Move-Place)**).

There is **no `copy` parameter mode.** `copy` appears only at the *call site* as an argument-passing mode (B.3: `argument ::= ("move" | "copy")? expression`), where it requests that a copy be materialized for the call. The parameter declaration controls ownership; the argument syntax controls how the value reaches it.

Parameter names must be distinct within one procedure. The well-formedness rule **(WF-ProcedureDecl)** requires `Distinct(ParamNames(params))`; a duplicate is a compile-time error (§15.1.7).

##### Permission annotations on parameter types

A parameter's *type* may carry a permission. Permissions are part of the type grammar, not a separate parameter modifier (B.2):

```ebnf
type       ::= permission? non_permission_type refinement_clause?
permission ::= "const" | "unique" | "shared"
```

So a parameter taking a uniquely-borrowed handle is written `device_handle: unique Device`, and one taking a shared view is `frame: shared Frame`. The permission binds to the type (`TypePerm(p, base)` in the metalanguage); when no permission is written the default `const` applies (`PermOf(ty) = const` when `ty` is not a `TypePerm`). Permission annotations on parameter *types* are orthogonal to the `move` parameter *mode*: `move` decides ownership transfer; the permission decides what the callee may do with the value it sees. The admissibility relation between permissions belongs to *Permissions & Ownership*.

##### Return types, `-> ()`, and the explicit-return rule

The return annotation is optional in the surface grammar. `ProcReturn` (§15.1.3) supplies the effective return type:

```text
ProcReturn(ret_opt) =
  { TypePrim("()")   if ret_opt = ⊥
    ret_opt          otherwise }
```

So an omitted `-> type` yields the effective return type `()` (unit). The static rule **(WF-ProcedureDecl)** ties the body to that return type. With `R = ProcReturn(ret_opt)` and `R_b = BodyReturnType(R)`, the body's type `T_b` must satisfy `T_b <: R_b`, and:

```text
R_b ≠ TypePrim("()") ⇒ ExplicitReturn(body)
```

This is the **explicit-return rule**: any procedure whose effective return type is *not* `()` must end in an explicit `return`. `ExplicitReturn` is defined precisely:

```text
ExplicitReturn(BlockExpr(stmts, tail_opt)) ⇔ tail_opt = ⊥ ∧ stmts ≠ [] ∧ LastStmt(stmts) = ReturnStmt(_)
```

So the body block must have **no trailing tail expression**, and its **last statement must be a `return`**. A non-unit procedure that "falls off the end" with a bare tail expression instead of `return` is ill-formed under **(WF-ProcBody-ExplicitReturn-Err)**. The `return_stmt` is `return expression?` (B.5: `return_stmt ::= "return" expression?`). The style guide reinforces this: *"Write explicit `return` statements in non-`unit` procedures."*

A return type may also be a `union_return` — two or more types joined by `|`, e.g. `-> FrameReply | FrameError`.

> **Spec note — the `ReturnAnnOk` premise.** **(WF-ProcedureDecl)** also carries the premise `ReturnAnnOk(ret_opt)`, where `ReturnAnnOk(ret_opt) ⇔ ret_opt ≠ ⊥`, and **(WF-ProcedureDecl-MissingReturnType)** raises an error when `¬ ReturnAnnOk(ret_opt)`. Read literally, this requires every procedure to write its return annotation explicitly, even a unit-returning one (as `-> ()`). The §15.1.7 diagnostic list correspondingly names "missing explicit return annotations" as a diagnosed condition. To stay safely conforming under the strictest reading, **this handbook writes every procedure with an explicit return annotation**, using `-> ()` for unit procedures. (The conceptual unit default in `ProcReturn` and the relaxed body rule are what make a bare unit procedure plausible, but the spec text does not unambiguously license omitting the annotation; always supplying it removes the ambiguity.)

##### The `main` entry point

The program entry point is a procedure named `main`. Its required shape is fixed by `MainSigOk` (§15.1.4):

```text
MainSigOk(d) ⇔ d = ProcedureDecl(_, vis, `main`, _, _, params, ret_opt, _, _, _, _)
  ∧ vis = `public`
  ∧ params = [⟨mode, name, ty⟩]
  ∧ mode ∈ {⊥, `move`}
  ∧ ContextBundleType(StripPerm(ty))
  ∧ ret_opt = TypePrim("i32")
```

A conforming `main` is therefore **`public`**, takes exactly **one** parameter whose type (after stripping any permission) is the platform context-bundle type — optionally `move` — and returns **`i32`**. `main` MUST NOT be generic (`MainGeneric(d)` is rejected by **(Main-Generic-Err)**). For an executable project, exactly one `main` is required: a project with none is rejected by **(Main-Missing)**, more than one by **(Main-Multiple)**, and a wrong signature by **(Main-Signature-Err)**. The style guide records that `main` keeps its mandated name even though procedures are otherwise `camelCase` (a documented naming exception), and that the *file* may be `Main.uv` while the *entry procedure* inside remains `main`.

#### 15.1.2 Semantics

**Static.** Per **(WF-ProcedureDecl)**: type parameters are bound first (`Γ_g = BindTypeParams(Γ, params_gen)`), parameter names are checked distinct, every parameter type is checked well-formed in `Γ_g`, then a fresh scope is pushed and all parameter bindings are introduced (`IntroAll(Γ_0, ParamBinds(params))`). The body is then checked against the effective return type, and the explicit-return obligation applies.

**Dynamic.** A call binds argument values to parameters and runs the body in a fresh block scope. Rule **(ApplyProcSigma)** (§15.1.5):

```text
BindParams(proc.params, vec_v) = binds
BlockEnter(σ, binds) ⇓ (σ_1, scope)
Γ ⊢ EvalBlockBodySigma(proc.body, σ_1) ⇓ (out, σ_2)
BlockExit(σ_2, scope, out) ⇓ (out', σ_3)
────────────────────────────────────────────────
Γ ⊢ ApplyProcSigma(proc, vec_v, σ) ⇓ (ReturnOut(out'), σ_3)
```

`ReturnOut` normalizes the outcome: a tail value `Val(v)` and an explicit `Ctrl(Return(v))` both surface as `Val(v)`; `Ctrl(Panic)` and `Ctrl(Abort)` propagate. How each argument is evaluated for its parameter mode (by-reference vs. by-ownership, with `move` / `copy` / `ref` passing) is governed by the `EvalArgsSigma` family in §15.1.5 and connects to the borrow rules in *Permissions & Ownership*.

#### 15.1.3 Worked Example — a free procedure

```ultraviolet
/// Builds a frame reply for the given request.
///
/// Skips presentation when the caller has already marked the frame stale.
public procedure buildFrame(request: shared FrameRequest) -> FrameReply {
    if request.is_stale
        return FrameReply::Skip

    let reply: FrameReply = renderFrame(request)
    return reply
}
```

This procedure is `public`, names its single parameter `request` in `snake_case`, takes it under the `shared` permission, and returns `FrameReply`. Because the effective return type is not `()`, the body ends in an explicit `return` on every path, satisfying `ExplicitReturn`. The single-statement `if` branch omits braces, which the style guide permits when the result stays immediately legible. The enum literal `FrameReply::Skip` uses `::` (B.3: `enum_literal ::= type_path "::" identifier variant_args?`), not `.`.

A unit-returning procedure writes `-> ()` and needs no `return`:

```ultraviolet
/// Logs a frame-skip event for diagnostics.
internal procedure recordSkip(frame_index: usize) -> () {
    diagnostics::emit(SkipEvent { frame_index: frame_index })
}
```

A `move` parameter takes ownership of its argument:

```ultraviolet
/// Consumes a request and returns the package it produces.
public procedure consumeRequest(move request: FrameRequest) -> Package {
    let package: Package = request~>intoPackage()
    return package
}
```

#### 15.1.4 Diagnostics (§15.1.7)

Diagnostics are defined for: missing explicit return annotations (**(WF-ProcedureDecl-MissingReturnType)**); duplicate parameter names (`¬ Distinct(ParamNames(params))`); non-unit procedures without an explicit trailing `return` (**(WF-ProcBody-ExplicitReturn-Err)**); and bodies whose result type does not match the declared return type (`¬ (T_b <: R_b)`). For `main`, the entry-point checks raise `Main-Missing`, `Main-Multiple`, `Main-Generic-Err`, and `Main-Signature-Err`.

---

### 15.2 Methods and Receivers

A method is a procedure that is attached to a type and dispatched through a **receiver**. Methods appear as members of records (B.6: `record_member ::= record_field_decl | method_def`), of classes (§14.3), and inside modal-state blocks as state methods (B.6: `state_member ::= state_field_decl | state_method_def | transition_def`; §13.3). They share one syntactic form.

#### 15.2.1 Exact Syntax

The §15.2.1 grammar:

```ebnf
method_def              ::= visibility? "override"? "procedure" identifier generic_params? "(" receiver ("," param_list)? ")" ("->" type)? contract_clause? block_expr
receiver                ::= "~" | "~!" | "~%" | ("move"? "self" ":" type)
state_method_signature  ::= "(" receiver ("," param_list)? ")" ("->" type)?
```

Appendix B (B.6) gives the fully-decorated form, splitting the receiver into shorthand and explicit alternatives and admitting a leading `attribute_list`:

```ebnf
method_def         ::= attribute_list? visibility? "override"? "procedure" identifier generic_params? "(" receiver ("," param_list)? ")" ("->" return_type)? contract_clause? block_expr
receiver           ::= receiver_shorthand | explicit_receiver
receiver_shorthand ::= "~" | "~!" | "~%"
explicit_receiver  ::= param_mode? "self" ":" type
```

A method declaration is, in order: an optional `attribute_list`; an optional `visibility`; an optional **`override`** marker (used when a class default is being overridden — see *Classes & Conformance*); the keyword **`procedure`** (methods use the *same* keyword as free procedures); the method `identifier`; optional `generic_params`; a parenthesized signature whose **first position is the receiver**, optionally followed by `, param_list`; an optional `-> return_type`; an optional `contract_clause`; and the body block.

Two structural facts distinguish a method from a free procedure:

- The receiver occupies the **first slot inside the parentheses**, and the rest of the parameter list — if any — follows after a comma: `"(" receiver ("," param_list)? ")"`.
- `method_def` carries only `contract_clause?` after the signature. Generic-parameter requirements are written in the method's `generic_params` list.

#### 15.2.2 Receiver forms and their meaning

There are four receiver spellings: three **shorthand** receivers built from `~`, and one **explicit** `self` receiver.

| Receiver            | Kind      | Receiver permission     | Meaning                                                      |
| ------------------- | --------- | ----------------------- | ----------------------------------------------------------- |
| `~`                 | shorthand | `const`                 | Reads `self` through a `const` (immutable) borrow.          |
| `~!`                | shorthand | `unique`                | Borrows `self` uniquely — exclusive, mutating access.       |
| `~%`                | shorthand | `shared`                | Borrows `self` as a `shared` (aliasable) receiver.          |
| `move? self : type` | explicit  | from the written type   | Names the receiver `self` with its exact (permission) type. |

The parsing rules map the tokens directly (§15.2.2): `~` → `ReceiverShorthand(const)` (**(Parse-Receiver-Short-Const)**), `~!` → `ReceiverShorthand(unique)` (**(Parse-Receiver-Short-Unique)**), `~%` → `ReceiverShorthand(shared)` (**(Parse-Receiver-Short-Shared)**). The explicit form (**(Parse-Receiver-Explicit)**) requires the identifier to be literally `self`, then a `:` colon, then a type, with an optional leading `move`.

The receiver permission determines the type the receiver presents (§15.2.3):

```text
RecvType(T, ReceiverShorthand(`const`))  = TypePerm(`const`, T)
RecvType(T, ReceiverShorthand(`unique`)) = TypePerm(`unique`, T)
RecvType(T, ReceiverShorthand(`shared`)) = TypePerm(`shared`, T)
RecvType(T, ReceiverExplicit(mode, ty))  = SubstSelf(T, ty)
```

The shorthands carry **no** parameter mode (`RecvMode(ReceiverShorthand(_)) = ⊥`): the receiver is *borrowed*, not moved. Only the explicit form can carry a mode (`RecvMode(ReceiverExplicit(mode, _)) = mode`), so a method that consumes its receiver by value is written with an explicit `move self`.

##### Mutable (unique) receivers

The `~!` receiver is the **mutable receiver**: it borrows `self` under the `unique` permission, which admits mutation and excludes aliasing (see *Permissions & Ownership*). A method that mutates the object it is called on must use `~!` (or an explicit `self: unique T`). A `~` receiver, being `const`, cannot mutate `self`; attempting to assign through a `const` path is the standard `const`-path mutation error.

##### Explicit `self` receivers must be `Self`

When you write `self : type`, the type must be the enclosing type or a permission-qualified form of it. **(Recv-Explicit)** requires `SelfType(R, ty)`, where:

```text
SelfType(R, ty) ⇔ ty = Self_R ∨ ∃ p. ty = TypePerm(p, Self_R)
```

with `Self_R = TypePath(RecordPath(R))`. So inside a record `Session`, an explicit receiver must be one of `self : Session`, `self : const Session`, `self : unique Session`, `self : shared Session`, or `move self : Session`. Any other receiver type is rejected by **(Record-Method-RecvSelf-Err)** (§15.2.7: "explicit receivers whose type is not `Self` or a permission-qualified `Self`"). The shorthands `~`, `~!`, `~%` are concise spellings of `self : const Self`, `self : unique Self`, and `self : shared Self`, and are the preferred form.

##### Receiver permission at the call site

A call `base~>name(args)` checks that the **caller's** permission on `base` admits the **method's** receiver permission. **(T-Record-MethodCall)** (§15.2.4) requires `PermAdmits(P_caller, P_method)`, where `P_method = RecvPerm(R_rec, m.receiver)` and `P_caller` is the permission the caller holds on `base` (`RecvBaseType(base, RecvMode(m.receiver)) = P_caller R_rec`). Calling a `~!` (unique) method through a binding that does not admit `unique` is a receiver-permission error (§15.2.7: "receiver-permission mismatches at call sites").

#### 15.2.3 How methods attach to types

- **Records.** `Methods(R) = [ m | m ∈ R.members ∧ m is MethodDecl ]`. The obligation **(WF-Record-Methods)** requires `Distinct(MethodNames(R))` (no duplicate method names within a record — otherwise **(Record-Method-Dup)**) and that every method is `MethodOK` and `MethodBodyOK`. The body is checked with `self` and the parameters in scope (`IntroAll(Γ_0, [⟨self, T_self⟩] ++ ParamBinds_T(Self_R, m.params))`), and the same explicit-return obligation as procedures applies via **(T-Record-Method-Body)**: `R_b ≠ TypePrim("()") ⇒ ExplicitReturn(m.body)`. The method form **(WF-Record-Method)** additionally requires `self ∉ ParamNames(params)` — you cannot also declare a parameter named `self` — and `Distinct(ParamNames(params))` for the remaining parameters.
- **Classes.** Class methods reuse the same receiver and parameter forms; class-owned additions (abstract vs. concrete methods, `override`, default-method conformance) are defined in §14.3 — see *Classes & Conformance*.
- **Modal types.** State methods inside `@State { … }` blocks use `state_method_signature`, which is the same `"(" receiver ("," param_list)? ")" ("->" type)?` shape; modal-state additions are defined in §13.3 — see *Modal Types*. Transitions are a *separate* member form (`transition_def`) that uses the `transition` keyword, not `procedure`, and are not methods (B.6: `transition_def ::= attribute_list? visibility? "transition" identifier "(" param_list ")" "->" "@" target_state block_expr`).

#### 15.2.4 Semantics

`MethodParamsDecl(T, m) = [⟨RecvMode(m.receiver), self, RecvType(T, m.receiver)⟩] ++ m.params` makes the receiver the implicit first parameter named `self`. Method lowering (§15.2.6) states this directly: *"Methods lower as procedures whose first lowered parameter is the receiver."*

At runtime, **(ApplyMethodSigma)** (§15.2.5) binds `self` to the receiver value and the remaining arguments to the parameters, then runs the body exactly like a procedure:

```text
m = MethodTarget(v_self, name)    ¬ IsTransition(m)
BindParams(RecvParams(base, name), [v_arg] ++ vec_v) = binds
BlockEnter(σ, binds) ⇓ (σ_1, scope)
Γ ⊢ EvalBlockBodySigma(m.body, σ_1) ⇓ (out, σ_2)
BlockExit(σ_2, scope, out) ⇓ (out', σ_3)
────────────────────────────────────────────────
Γ ⊢ ApplyMethodSigma(base, name, v_self, v_arg, vec_v, σ) ⇓ (ReturnOut(out'), σ_3)
```

The receiver is evaluated by the `EvalRecvSigma` rules: a `move` receiver evaluates `base` to a value (**(EvalRecvSigma-Move)**); a borrowing receiver (`mode = ⊥`) takes the address of `base` and binds an alias (**(EvalRecvSigma-Ref)**). At the call site, whether the receiver is moved is determined by `RecvArgMode(base)`, which is `move` exactly when `base` is a `MoveExpr` and `⊥` otherwise; correspondingly **(T-Record-MethodCall)** requires `RecvArgOk(base, RecvMode(m.receiver))`, i.e. a `move`-receiver method must be invoked on a `move`'d place.

#### 15.2.5 Worked Example — a method with a mutable receiver

```ultraviolet
/// A bounded frame counter.
public record FrameCounter {
    public count: usize
    public limit: usize

    /// Returns whether the counter has reached its limit.
    public procedure isAtLimit(~) -> bool {
        return self.count >= self.limit
    }

    /// Advances the counter by one, saturating at the limit.
    public procedure advance(~!) -> () {
        if self.count < self.limit
            self.count += 1
    }

    /// Consumes the counter and returns the final count.
    public procedure finish(move self: FrameCounter) -> usize {
        return self.count
    }
}
```

`isAtLimit` uses the `const` shorthand `~` because it only reads `self`; it returns `bool` and therefore ends in an explicit `return`. Its predicate name follows the style guide's `camelCase` rule for boolean methods. `advance` uses the mutable receiver `~!` so it may write `self.count`; it returns `()` and so needs no `return`. `finish` uses an explicit `move self: FrameCounter` receiver to consume the value — `FrameCounter` is a permission-qualified-free `Self`, satisfying `SelfType`. Calls go through `~>`:

```ultraviolet
var counter: FrameCounter = FrameCounter { count: 0, limit: 3 }
counter~>advance()
let done: bool = counter~>isAtLimit()
let total: usize = move counter~>finish()
```

The record literal `FrameCounter { count: 0, limit: 3 }` initializes **every** field, as required by **(T-Record-Literal)** (`FieldInitSet(fields) = FieldNameSet(R)`); omitting a field would raise `Record-FieldInit-Missing`. `counter` is declared with `var` because `advance` needs a `unique` (mutable) borrow, and the moving call `move counter~>finish()` consumes the value: here `move counter` is the receiver place and `~>finish()` is the postfix method call applied to it, so the receiver is a `MoveExpr` as `RecvArgOk` requires for a `move self` method.

#### 15.2.6 Diagnostics (§15.2.7)

Diagnostics are defined for: explicit receivers whose type is not `Self` or a permission-qualified `Self` (**(Record-Method-RecvSelf-Err)**); duplicate method names within a type (**(Record-Method-Dup)**); receiver-permission mismatches at call sites (`¬ PermAdmits(P_caller, P_method)`); invalid receiver passing mode (`¬ RecvArgOk(base, mode)`); and direct user calls to the destructor protocol.

---

### 15.3 Overloading

Overloading is the presence of more than one visible declaration sharing a name; *resolution* is the static process that selects exactly one of them for a given call. Per §15.3.1, **overloading introduces no new surface syntax** — overloads are just ordinary procedure or method declarations that happen to share a name. Per §15.3.6, resolution is complete *before* lowering, which consumes only the selected procedure or method symbol; per §15.3.5, *no runtime overload search is performed* (`When LookupMethod(T, name) = m, execution uses that unique method body.`).

#### 15.3.1 Free-procedure overload resolution

§15.3.4 specifies the algorithm verbatim. For a free call whose callee names an overload set `O`:

```text
1. Candidate selection: retain procedures in O whose parameter count equals the argument count.
2. Type filtering: eliminate candidates for which any argument is incompatible with the
   corresponding parameter under the call-argument compatibility rules of §16.3.4.
3. Exact-match preference: if multiple candidates remain, retain those with the maximal
   number of exact argument-type matches.
4. Genericity preference: if both generic and non-generic candidates remain, retain only
   the non-generic candidates.
5. Constraint specificity: if multiple generic candidates remain, retain only those whose
   bounds are pointwise at least as specific as every remaining
   alternative, with at least one strict improvement.
6. If exactly one candidate remains, that candidate is selected.
7. If no candidate remains, the call is ill-formed with E-SEM-3031.
8. If multiple candidates remain after all preference stages, the call is ill-formed with E-SEM-3030.
```

Read the stages as successive filters: first by arity, then by argument compatibility, then by a sequence of *preferences* (more exact matches beat fewer; non-generic beats generic; a strictly more-constrained generic beats a less-constrained one). The process terminates with exactly one survivor (success), zero survivors (`E-SEM-3031`, no matching overload), or more than one (`E-SEM-3030`, ambiguous).

A distinct rule governs the *declarations themselves*, independent of any call (§15.3.4):

> Two visible overloads with the same name MUST NOT have identical parameter-mode/type signatures after generic-parameter erasure. Such a declaration set is ill-formed with `E-SEM-3032`.

So overloads must differ in their parameter-mode/type signature after erasing generic parameters; two declarations that erase to the same signature are a **duplicate-signature** error, caught at the declaration site regardless of whether any call is ambiguous. (`ResolvedCallee(call)` is the declaration id chosen by resolution, including the selected generic substitution; lowering then mangles that selected declaration. ABI-facing names are outputs of the selected declaration and are not inputs to overload resolution.)

#### 15.3.2 Method and inherited-default resolution

Method dispatch does not search an overload set the way free calls do; it resolves a single method by name through `LookupMethod` (§15.3.3):

```text
LookupMethod(T, name) = m ⇔ MethodByName(T, name) = m
LookupMethod(T, name) = m ⇔ MethodByName(T, name) = ⊥ ∧ |ClassDefaults(T, name)| = 1 ∧ m ∈ ClassDefaults(T, name)
LookupMethod(T, name) = ⊥ ⇔ MethodByName(T, name) = ⊥ ∧ (|ClassDefaults(T, name)| = 0 ∨ |ClassDefaults(T, name)| > 1)
```

A method declared directly on `T` (`MethodByName`) wins. Otherwise, if exactly one **class default** — an inherited concrete method from an implemented class (`ClassDefaults(T, name) = { m | ∃ Cl ∈ Implements(T). m ∈ ClassMethodTable(Cl) ∧ m.name = name ∧ m.body ≠ ⊥ }`, §14.3) — carries that name, it is used. If a name resolves to **zero** methods, the call is ill-formed under **(LookupMethod-NotFound)**; if it resolves to **more than one** inherited default, it is ill-formed under **(LookupMethod-Ambig)** — the caller must disambiguate. Because record/class method names must be `Distinct` within a single type (§15.2.4), **there is no in-type method overloading by name**; method-level overloading arises only through multiple inherited defaults, and is resolved (or rejected as ambiguous) by `LookupMethod`.

#### 15.3.3 Worked Example — a free-procedure overload set

```ultraviolet
/// Renders a frame from a fully-specified request.
public procedure renderFrame(request: shared FrameRequest) -> FrameReply {
    return composeFrame(request)
}

/// Renders a frame from a request together with an explicit budget.
public procedure renderFrame(request: shared FrameRequest, budget: TimeBudget) -> FrameReply {
    return composeFrameWithin(request, budget)
}
```

These two declarations form the overload set `renderFrame`. They differ in arity, so candidate selection (stage 1) alone distinguishes them: `renderFrame(req)` selects the first, `renderFrame(req, budget)` selects the second. Their parameter signatures are not identical after generic erasure, so `E-SEM-3032` does not apply. Adding a third `renderFrame(request: shared FrameRequest)` with the same single-parameter signature would trigger `E-SEM-3032` at the declaration site.

#### 15.3.4 Diagnostics (§15.3.7)

| Code         | Severity | Detection    | Condition                           |
| ------------ | -------- | ------------ | ----------------------------------- |
| `E-SEM-3030` | Error    | Compile-time | Ambiguous overload resolution       |
| `E-SEM-3031` | Error    | Compile-time | No matching overload found          |
| `E-SEM-3032` | Error    | Compile-time | Duplicate signature in overload set |

Method-lookup diagnostics for missing methods and ambiguous inherited-default resolution remain defined by **(LookupMethod-NotFound)** and **(LookupMethod-Ambig)**.

---

### Idioms & Best Practices

- **Name procedures and methods in `camelCase`.** `bootSession`, `buildPackage`, `extractFrame` (style guide, Naming Matrix). Boolean-returning procedures and methods use a predicate `camelCase` name: `isReady`, `hasFocus`, `canPresent`, `shouldReload`. Parameters and local variables are `snake_case` (`config_path`, `frame_delta`). Generic type parameters are `PascalCase` with a `T` prefix (`TValue`, `TState`).
- **Always write explicit `return` in non-`()` procedures.** This is both a style-guide rule and a hard well-formedness requirement (`ExplicitReturn`). The body must end in `return expr`, with no trailing tail expression.
- **Write the return annotation explicitly, including `-> ()` for unit.** This conforms to the strictest reading of `ReturnAnnOk` and keeps the signature self-documenting. Reserve the value-returning forms for procedures that genuinely produce a value.
- **Prefer the receiver shorthands.** Use `~` for read-only methods, `~!` when the method mutates `self`, and `~%` for shared-borrow methods. Reserve explicit `self : type` for what the shorthands cannot express — chiefly `move self : T` to consume the receiver.
- **Pick the receiver permission deliberately.** A `~` (const) method documents that it does not mutate; choosing `~!` only when mutation is real keeps call sites flexible, since a `const` caller can still invoke `const` methods but not `unique` ones (`PermAdmits`).
- **Annotate parameter types with the narrowest permission that works.** Pass `shared Frame` or `const Config` rather than handing over `unique` or ownership unless the callee truly needs it. The style guide's capability-passing rule — *"Pass only the exact capabilities a procedure or method uses"* — is the same principle applied to permissions.
- **Use `move` parameters and `move self` only when ownership transfer is intended.** A `move` parameter consumes its argument; reach for it when the procedure stores, destroys, or otherwise takes responsibility for the value.
- **Keep overloads genuinely distinct.** Overloads should differ in arity or argument types in a way a reader can see at the call site. Lean on arity differences first; they resolve in stage 1 without relying on the preference stages.
- **Wrap long signatures one item per line.** When a signature exceeds 100 columns, break to one parameter per line, with the closing `)` and `-> Type` on their own line (style guide, Line Breaking).
- **Document the public surface.** Every `public` procedure and method needs a `///` doc comment covering purpose, important pre/postconditions, ownership or capability expectations, and notable failure modes (style guide, Documentation Comments).

### Pitfalls & Diagnostics

- **Falling off the end of a non-unit body.** A procedure or method whose effective return type is not `()` must end in an explicit `return`; a trailing tail expression is **not** accepted. This is **(WF-ProcBody-ExplicitReturn-Err)** (§15.1.7: "non-unit procedures without an explicit trailing `return`"). Fix: replace the tail expression with `return expr`.
- **Body type does not match the declared return.** The body's type must be a subtype of the effective return type (`T_b <: R_b`). A mismatch is a compile-time error (§15.1.7, "bodies whose result type does not match the declared return type").
- **Duplicate parameter names.** `Distinct(ParamNames(params))` is required; reusing a name is an error (§15.1.7). The same applies to method parameters, plus the extra rule that no parameter may be named `self` (**(WF-Record-Method)**).
- **Invoking a method with `.` instead of `~>`.** Method calls use the `~>` operator (`receiver~>name(args)`). The `.` postfix accesses fields and tuple indices only.
- **Explicit receiver that is not `Self`.** Writing `self : SomeOtherType` violates `SelfType` and is rejected by **(Record-Method-RecvSelf-Err)** (§15.2.7). The receiver type must be `Self` or a permission-qualified `Self`.
- **Receiver-permission mismatch at the call site.** Calling a `~!` (unique) method through a binding that does not admit `unique` fails on `¬ PermAdmits(P_caller, P_method)` (§15.2.7: "receiver-permission mismatches at call sites"). Declare the binding `var` / give it a `unique` borrow, or make the method's receiver `~` if it does not actually mutate.
- **Mutating through a `~` receiver.** A `const` receiver cannot mutate `self`; assignment to a field of a `~` receiver is the standard `const`-path mutation error. Switch the receiver to `~!`.
- **Invoking a `move self` method without `move`.** A `move`-receiver method requires the receiver to be a `MoveExpr` (`RecvArgOk(base, move) ⇔ ∃ p. base = MoveExpr(p)`). Write `move base~>name(args)`. Calling it on a non-moved place is an invalid-receiver-passing-mode error (§15.2.7).
- **Duplicate method names within a type.** Record/class method names must be `Distinct`; a clash is **(Record-Method-Dup)** (§15.2.7). There is no name-based method overloading inside a single type.
- **Omitting a field in a record literal.** A record literal must initialize every field (`FieldInitSet(fields) = FieldNameSet(R)`); a default initializer on a field does **not** make it omissible in a literal — defaults apply only to zero-argument default construction (`DefaultConstructible`). Omitting a field raises `Record-FieldInit-Missing`.
- **Duplicate overload signatures.** Two same-named visible procedures whose parameter-mode/type signatures erase to the same thing are `E-SEM-3032`, caught at the declaration site.
- **Ambiguous or unmatched overloaded call.** A call that survives with multiple candidates is `E-SEM-3030`; one that survives with none is `E-SEM-3031`. Disambiguate by adjusting argument types or arity, or by adding/removing a more specific overload.
- **Ambiguous or missing inherited default method.** A method name that resolves to more than one class default is **(LookupMethod-Ambig)**; one that resolves to none is **(LookupMethod-NotFound)**. Declare the method directly on the type or qualify the intended class.
- **A bad `main`.** An executable must have exactly one `public main` taking one context-bundle parameter (optionally `move`) and returning `i32`, and it must not be generic. Violations surface as `Main-Missing`, `Main-Multiple`, `Main-Generic-Err`, or `Main-Signature-Err`.

---

*Related chapters:* *Permissions & Ownership* (the `const` / `unique` / `shared` permissions and the `move` semantics that parameter modes and receivers rely on); *Records & Structs* (how `method_def` members compose a record, and the record-literal field-completeness rule); *Classes & Conformance* (§14.3 — `override`, abstract vs. concrete class methods, and class-default resolution feeding `LookupMethod`); *Modal Types* (§13.3 — state methods and the separate `transition` form); *Contracts* (§15.4 — the `contract_clause` that attaches to the declarations shown here).
