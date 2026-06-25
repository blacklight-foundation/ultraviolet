## 9. Modal Types & Typestate

A `modal` type is Ultraviolet's mechanism for *typestate*: a single nominal type whose available fields, methods, and operations depend on which lifecycle **state** the value currently occupies. The compiler tracks the state in the type itself, so an operation that is only valid in one phase of a value's life is a *compile error* when attempted in another phase. This chapter specifies how to declare modal types, how state-specific fields, methods, and transitions work, how to construct and match state values, and how to widen a concrete state into a general modal value.

Modal types are a *core* Ultraviolet idiom. The style guide (`AGENTS.md`, "`record`, `class`, and `modal`") is explicit: use `modal` "for state-based code. If behavior, available fields, or allowed operations differ by lifecycle state, model that with `modal` types rather than booleans, comments, or informal conventions." Modal types and contracts are "the preferred way to model protocols, resource states, runtime sessions, imports, cooking phases, and other lifecycle-heavy flows." Reach for `record` for plain value data and `class` only when shared identity or polymorphism is actually required (see the Records and Classes chapters); reach for `modal` whenever a value's legal operations change over its lifetime.

This chapter corresponds to specification §13.1–§13.5. All grammar productions are reproduced verbatim from the specification's feature sections and from Appendix B.

### 9.1 Modal Declarations (§13.1)

#### Syntax

A modal declaration begins with the `modal` keyword and contains zero or more **state blocks**, each introduced by `@` followed by a state name. The §13.1.1 grammar is:

```ebnf
modal_decl        ::= attribute_list? visibility? "modal" identifier generic_params? implements_clause? modal_body type_invariant?
modal_body        ::= "{" state_block* "}"
state_block       ::= "@" identifier "{" state_member* "}"
state_member      ::= state_field_decl | state_method_def | transition_def
modal_type_ref    ::= type_path generic_args?
modal_state_type  ::= modal_type_ref "@" identifier
modal_state_expr  ::= modal_type_ref "@" identifier "{" field_init_list? "}"
```

The consolidated Appendix B form (B.6) shows the same structure with the `state_block+` requirement (at least one state) made explicit:

```ebnf
modal_decl        ::= attribute_list? visibility? "modal" identifier generic_params? implements_clause? "{" state_block+ "}" type_invariant?
```

(The `modal_body` production permits `state_block*`; well-formedness rule `(Modal-NoStates-Err)` then rejects the zero-state case at type-check time. Appendix B folds that requirement into the grammar as `state_block+`.)

And the type-grammar productions (Appendix B.2) for *referring to* a modal type and one of its states:

```ebnf
modal_type_name     ::= type_path generic_args?
state_specific_type ::= modal_type_name "@" state_name
state_name          ::= identifier
```

There are two distinct ways the name of a modal type `M` appears in type position:

- **`M`** — the *general* (widened) modal type. A value of type `M` may be in *any* declared state; you must match it (see §9.6) before touching state-specific members.
- **`M@State`** — a *state-specific* type. The value is statically known to be in exactly `State`, so that state's fields, methods, and transitions are directly available.

#### Naming

By the style-guide naming matrix, modal **types** use `PascalCase` (like `record`, `class`, `enum`), and states are `PascalCase` identifiers written after `@` (e.g. `@Connected`, `@Closed`). The `@` is part of the state syntax, not part of the identifier. State methods and transitions are `camelCase`; public/internal payload fields are `snake_case`; private payload fields are `_snake_case`. The style guide further directs that, inside a `modal`, states be ordered in lifecycle order, and that "within a state, keep transitions and state-specific public behavior near the state fields they govern."

#### Semantics and well-formedness

The static rules (§13.1.4) impose the following on every modal declaration `M`:

- **At least one state.** A modal with zero states is rejected by `(Modal-NoStates-Err)` → `E-TYP-2050`.
- **Distinct state names.** Duplicate state names are rejected by `(Modal-DupState-Err)` → `E-TYP-2051`.
- **No state may share the modal's own name.** A state named the same as the modal type is rejected by `(Modal-StateName-Err)` → `E-TYP-2054`.
- **State-member visibility may not exceed the modal's visibility.** `StateMemberVisOk(M)` requires every payload field, method, and transition to have visibility rank ≤ the modal's; violations raise `(StateMemberVisOk-Err)` → `E-TYP-2063`.
- **Unique payload field names within a state.** Duplicate payload field names in one state are rejected by `(Modal-Payload-DupField)` → `E-TYP-2058`.

A modal may carry **generic parameters** (`generic_params`), an **implements clause** (`implements_clause`, written `<: class_list`), and a **type invariant** (`type_invariant`), exactly like `record` and `enum` declarations. Generic constraints are written as bounds on the relevant type parameters. The type-invariant and implements-clause productions are:

```ebnf
type_invariant   ::= "|:" "{" predicate_expr "}"
implements_clause ::= "<:" class_list
class_list        ::= type_path ("," type_path)*
```

See the Classes & Contracts chapters for type invariants and `<:` bounds in detail; they behave identically on modal types.

#### Worked example: the declaration shape

```ultraviolet
/// A network connection whose available operations depend on its lifecycle.
public modal Connection {
    @Idle {
        endpoint: string@View

        /// Open the connection on the supplied OS handle.
        public transition open(handle: usize) -> @Open {
            return Connection@Open { endpoint: self.endpoint, handle: handle, bytes_sent: 0 }
        }
    }

    @Open {
        endpoint: string@View
        handle: usize
        bytes_sent: u64

        /// Account for a batch of bytes pushed to the peer.
        public procedure record_sent(~%, count: u64) -> u64 {
            self.bytes_sent = self.bytes_sent + count
            return self.bytes_sent
        }

        /// Close the connection, discarding the live handle.
        public transition close() -> @Closed {
            return Connection@Closed { endpoint: self.endpoint }
        }
    }

    @Closed {
        endpoint: string@View
    }
}
```

Here `Connection` has three states ordered by lifecycle (`@Idle`, `@Open`, `@Closed`). `@Idle` and `@Open` and `@Closed` each carry an `endpoint`; only `@Open` carries a live `handle` and a `bytes_sent` counter. `record_sent` exists *only* in `@Open`. Attempting `record_sent` on a `Connection@Idle` is a compile error — the method is simply not in that state's member set (`E-TYP-2053`).

#### Runtime representation and layout (§13.1.5–§13.1.6)

A concrete state value is represented at runtime as `RecordValue(ModalStateRef(modal_ref, S), io)` — structurally, the payload of state `S` laid out like a `record`. A *widened* general value is `ModalVal(S, v_s)`, carrying the runtime state tag plus the state payload.

Layout follows §13.1.6:

- `layout(M@S) = layout(record { ModalPayload(modal_ref, S) })`. An empty-payload state has `sizeof(M@S) = 0`.
- The general type `M` lowers either as a **niche-optimized** single-payload representation (when exactly one state holds a single-field payload with enough spare niches to encode the empty states) or as a **tagged** `enum`-like representation `enum { S_1(payload_1), …, S_n(payload_n) }` otherwise.
- A useful guarantee for reasoning about copies: `sizeof(M@S) ≤ sizeof(M)` (§13.5.6).

You never write the tag yourself; the compiler manages it. What matters for writing code is that `M@S` is the precise, often smaller, view, and `M` is the general view.

#### Built-in modal types

Several standard types are themselves modal and are useful references for the idiom (§13.1.4). Their state sets, exactly as declared in the specification:

| Built-in modal | States | Notes |
|---|---|---|
| `Region` | `@Active`, `@Frozen`, `@Freed` | Arena allocation; `freeze` / `thaw` / `free_unchecked` are constructor procedures, not transitions. |
| `File` | `@Read`, `@Write`, `@Append`, `@Closed` | I/O handle; `close` transitions each open state to `@Closed`. |
| `DirIter` | `@Open`, `@Closed` | Directory iteration; `close` transitions to `@Closed`. |
| `CancelToken` | `@Active` | Cancellation; methods `cancel`, `is_cancelled`, `wait_cancelled`, `child`. |
| `Spawned<T>` | `@Pending`, `@Ready` | Task spawning (see the Concurrency chapter). `@Ready` carries `value`. |
| `Tracked<T; E>` | `@Pending`, `@Ready` | Tracked async result. `@Ready` carries `value`. |
| `Outcome<TValue; TError>` | *(enum, not modal)* | Fallible result; a two-variant enum with variants `Value(TValue)` / `Error(TError)`. Listed here for cross-reference — see Error Handling. |
| `Async` | (Concurrency chapter) | Declared in Chapter 21; its state set and combinators are not duplicated in §13. |

`Outcome<TValue; TError>` is the canonical fallible-result type returned by string, byte, file, and directory operations (for example `File@Read ~> read_all()` returns `Outcome<unique string@Managed, IoError>`). **It is a two-variant enum, not a modal:** its variants are `Value(TValue)` and `Error(TError)`, constructed as `Outcome::Value(x)` / `Outcome::Error(e)` (or introduced implicitly — see Error Handling) and matched as enum variants. Note that generic *declaration* parameters are `;`-separated (`generic_params ::= "<" generic_param (";" generic_param)* ">"`), while generic *arguments* at use sites are `,`-separated (`generic_args ::= "<" type ("," type)* ","? ">"`). So `Outcome` is declared with `<TValue; TError>` but instantiated as `Outcome<i32, IoError>`.

#### Constructing a state value

A modal state value is built with a **record literal whose type is a `state_specific_type`** (Appendix B.3):

```ebnf
record_literal   ::= identifier "{" field_init_list "}" | state_specific_type "{" field_init_list? "}"
field_init_list  ::= field_init ("," field_init)* ","?
field_init       ::= identifier ":" expression | identifier
```

That is, you write `M@State { field: expr, … }`. Construction rule `(T-Modal-State-Intro)` requires:

- the supplied field names exactly equal the state's payload field set (no missing, no extra: `ModalPayloadNameSet(modal_ref, S) = FieldInitSet(fields)`),
- field names are distinct, and
- each field initializer checks against its declared payload type.

An empty-payload state is constructed as `M@State {}`. The `field_init` shorthand `identifier` (no `: expression`) is the punning form, equivalent to `identifier: identifier` when a local of the same name is in scope.

```ultraviolet
let pending: Connection@Idle = Connection@Idle { endpoint: "wss://host:443" }
let closed: Connection@Closed = Connection@Closed { endpoint: pending.endpoint }
```

**Important restriction:** the runtime-backed built-in modals cannot be record-constructed directly. Constructing `File@S`, `DirIter@S`, `CancelToken@S`, `Spawned@S`, `Tracked@S`, or `Async@S` with a record literal is rejected by `(Record-FileDir-Err)` → `E-TYP-2073`. You obtain those through their constructor procedures (for example `CancelToken::new`, `Region::new_scoped`) and operating-system / runtime facilities, not by hand-writing the payload. (`Outcome` is a two-variant enum, not a modal; construct it as `Outcome::Value(x)` / `Outcome::Error(e)`.)

### 9.2 State Fields (§13.2)

#### Syntax

State fields are the per-state payload. Each state block contains zero or more field declarations:

```ebnf
state_field_decl ::= attribute_list? visibility? key_boundary? identifier ":" type
```

(The Appendix B restatement omits `key_boundary?`; the §13.2.1 production is authoritative and permits the optional `#` key boundary, parsed by `ParseKeyBoundaryOpt`. See the Keys & Concurrency chapter for `#` field key boundaries.)

A field declared in `@Open` exists *only* while the value is `@Open`. A different state with no such field cannot read it, and the field is not part of that state's layout at all.

#### Semantics

Field access `e.f` on a state-specific value is governed by `(T-Modal-Field)` and `(T-Modal-Field-Perm)`:

- The static type of `e` must be `M@S` (optionally permission-qualified). The field `f` must be in `S`'s payload map (`ModalPayloadMap(modal_ref, S)(f) = T`), yielding the field's type `T` (or `P' T` if `e` was `P' M@S`).
- The access must occur **inside the declaring modal's module** (`ModalFieldVisible`), subject to the field's own visibility.

Three failure modes:

- **Field not in this state.** `e.f` where `f` is not in `S`'s payload → `(Modal-Field-Missing)` → `E-TYP-2052`.
- **Field access on a *general* modal value.** If `e : M` (the widened type, not `M@S`), you cannot read state fields directly; you must match first → `(Modal-Field-General-Err)` → `E-TYP-2057`.
- **Field access from outside the declaring module** → `(Modal-Field-NotVisible)` → `E-TYP-2064`.

Field reads/writes lower exactly as ordinary record-payload field accesses over the current state's layout (§13.2.6); there is no special modal field-access machinery.

#### Worked example

```ultraviolet
public modal Download {
    @Active {
        received: u64
        total: u64
    }

    @Complete {
        total: u64
    }
}

/// Reads the `received` field — legal only because `progress` sees `Download@Active`.
public procedure progress(snapshot: Download@Active) -> u64 {
    let pct: u64 = (snapshot.received * 100) / snapshot.total
    return pct
}
```

`snapshot.received` is well-typed because `snapshot : Download@Active` and `received` is in `@Active`'s payload. The same access on a `Download@Complete` would raise `E-TYP-2052`, because `@Complete` has no `received` field. Field access uses `.` (not `~>`, which is reserved for method and transition calls).

### 9.3 State-Specific Methods (§13.3)

#### Syntax

A method that exists only in a given state is written with the `procedure` keyword inside the state block:

```ebnf
state_method_def       ::= attribute_list? visibility? "procedure" identifier generic_params? state_method_signature contract_clause? block_expr
state_method_signature ::= "(" receiver ("," param_list)? ")" ("->" type)?
receiver               ::= "~" | "~!" | "~%" | ("move"? "self" ":" type)
```

`state_method_signature` is the shared method-signature form (§15.2.1). Unlike transitions (§9.4), a state method **may** carry a `contract_clause`.

#### Receiver forms

Modal methods use the **shorthand receiver tokens**, which map to receiver permissions (§15.2.2). The token-to-permission mapping is fixed by the parser:

| Token | Receiver permission | Receiver type | Meaning |
|---|---|---|---|
| `~` | `const` | `const M@S` | Read-only access to the state value. |
| `~%` | `shared` | `shared M@S` | Synchronized read/write access (key-mediated). |
| `~!` | `unique` | `unique M@S` | Exclusive read/write access. |

These mappings are fixed by `RecvType` (§15.2.3):

```text
RecvType(T, ReceiverShorthand(`const`))  = TypePerm(`const`,  T)
RecvType(T, ReceiverShorthand(`unique`)) = TypePerm(`unique`, T)
RecvType(T, ReceiverShorthand(`shared`)) = TypePerm(`shared`, T)
RecvType(T, ReceiverExplicit(mode, ty))  = SubstSelf(T, ty)
```

An explicit receiver (`self: T` or `move self: T`) is also permitted by the grammar, but for modal state methods the shorthand forms are idiomatic and sufficient: the modal supplies `T = ModalSelfType(M, S)` (the current state type `M@S`), so `~` *is* `const M@S`, `~%` *is* `shared M@S`, and `~!` *is* `unique M@S`. Inside the body, the receiver is bound to `self` of that type.

#### Invocation

State methods (and transitions, §9.4) are invoked with the **method-call operator `~>`**, per the postfix grammar (Appendix B.3):

```ebnf
postfix_suffix ::= "." identifier | "." decimal_integer | "[" expression "]"
               | "~>" identifier "(" argument_list? ")" | "(" argument_list? ")" | "?"
```

So a call is written `receiver ~> methodName(args)`. The `.` operator accesses fields; `~>` calls methods and transitions.

#### Semantics

By `(T-Modal-Method)`:

- `e : P_caller M@S` (the caller's permission), the method `m` is looked up in state `S` (`LookupStateMethod`), and
- the caller permission must **admit** the receiver permission: `PermAdmits(P_caller, P_method)`. The admissibility relation (§10) is exactly:

| Caller permission | May call `~` (`const`) | May call `~%` (`shared`) | May call `~!` (`unique`) |
|---|---|---|---|
| `const` | Yes | No | No |
| `shared` | Yes | Yes | No |
| `unique` | Yes | Yes | Yes |

- the method must be visible in the current scope (`StateMemberVisible`), and the arguments must check against the parameter list.

The call's result type is the method's declared return type.

Failure modes:

- **Receiver-permission mismatch** (for example calling a `~!` method through a `const` value) → `(Modal-Method-RecvPerm-Err)`, code `(MethodCall-RecvPerm-Err)` → `E-TYP-1605`.
- **No such method in this state** → `(Modal-Method-NotFound)` → `E-TYP-2053`.
- **Method not visible from this scope** → `(Modal-Method-NotVisible)` → `E-TYP-2064`.
- **Duplicate method names within one state** → `(StateMethod-Dup)` → `E-TYP-2061`.

A method body type-checks (`T-Modal-Method-Body`) with `self` bound to `RecvType(ModalSelfType(M, S), receiver)` and must `return` a value of the declared return type when that type is not `()`.

#### Worked example

```ultraviolet
public modal Counter {
    @Open {
        _value: u64

        /// Read-only inspection: `~` is a `const` receiver.
        public procedure value(~) -> u64 {
            return self._value
        }

        /// Mutating bump: `~%` is a `shared` receiver (key-mediated write).
        public procedure add(~%, amount: u64) -> u64 {
            self._value = self._value + amount
            return self._value
        }
    }

    @Sealed {
        _value: u64

        public procedure value(~) -> u64 {
            return self._value
        }
    }
}
```

`value` exists in both states; `add` exists only in `@Open`. A caller holding a `const Counter@Open` may call `value` but not `add`. Note the private field naming `_value` per the style guide (`_snake_case` for private instance fields).

### 9.4 Transitions (§13.4)

A **transition** is the only way a modal value moves from one state to another. It consumes the source state value and produces a target state value.

#### Syntax

```ebnf
transition_def ::= attribute_list? visibility? "transition" identifier "(" param_list ")" "->" "@" identifier block_expr
```

Key syntactic points:

- The keyword is **`transition`** (a reserved keyword), not `procedure`.
- The return is written **`-> @TargetState`** — the `@` plus a state name, *not* a full type. The target must be a state of the same modal.
- There is **no receiver token** in the transition grammar. The receiver is **fixed**: a transition always takes `self` by `unique`, moved. Per `TransitionSig` (§13.4.3): `recv = unique M@S_src`, `mode = move`, `ret = M@S_tgt`.
- **A transition takes no `contract_clause`.** The `transition_def` production ends at `block_expr`; there is no `contract_clause?` slot. Preconditions that you would express as a contract belong on a state *method* (which validates) or are enforced structurally by the source state itself. (See the Pitfalls section for how to reconcile this with the style guide's preference for contracts on lifecycle transitions.)

#### Semantics

A transition's effective signature (§13.4.3) for source state `S_src`:

```text
TransitionSig(M, S_src, t).recv   = unique M@S_src
TransitionSig(M, S_src, t).params = t.params
TransitionSig(M, S_src, t).ret    = M@(t.target)
TransitionSig(M, S_src, t).target = t.target
TransitionSig(M, S_src, t).mode   = move
```

By `(T-Modal-Transition)`, to call `e_self ~> t(args)`:

- `e_self` must be a `unique M@S_src` value (the source state),
- the transition `t` is looked up in `S_src`, must be visible, and its arguments must check, and
- the receiver is **moved** (`RecvArgOk(e_self, move)`): the source value is consumed.

The call's result type is `M@S_tgt`. The transition body (`T-Modal-Transition-Body`) type-checks with `self : unique M@S_src` plus the parameters, and **must produce a value of `M@S_tgt`** — typically by constructing a fresh `M@S_tgt { … }` record literal and returning it. Lowering (§13.4.6) does not mutate a tag in place; it runs the body, which *constructs and returns* a fresh target-state value.

Failure modes:

- **Source not a `unique` state value.** Calling a transition on a non-`unique` receiver or a non-state type → `(Transition-Source-Err)` → `E-TYP-2056`.
- **Target not declared by the modal.** `-> @Bogus` where `Bogus` is not a state → `(Transition-Target-Err)` → `E-TYP-2059`.
- **Body doesn't yield the target state.** The body's value is not `M@S_tgt` → `(Transition-Body-Err)` → `E-TYP-2055`.
- **Transition not visible** → `(Transition-NotVisible)` → `E-TYP-2064`.
- **Duplicate transition names within a state** → `(Transition-Dup)` → `E-TYP-2062`.
- **A transition and a state method in the same state share a name** → `(StateMember-Name-Conflict)` → `E-TYP-2065`.

Because the receiver is consumed, after `let next = value ~> close()`, the original `value` binding is no longer usable; the new state lives in `next`. Transition names are `camelCase` (style guide: "Transitions | `camelCase` | `beginPlayback`, `finishImport`, `enterEditor`").

#### Worked example

```ultraviolet
public modal Door {
    @Locked {
        key_id: u64

        /// Unlock the door with the presented key (validation lives in the caller
        /// or in a state method; a transition itself carries no contract clause).
        public transition unlock() -> @Unlocked {
            return Door@Unlocked { key_id: self.key_id }
        }
    }

    @Unlocked {
        key_id: u64

        public transition lock() -> @Locked {
            return Door@Locked { key_id: self.key_id }
        }

        public transition openWide() -> @Open {
            return Door@Open { key_id: self.key_id }
        }
    }

    @Open {
        key_id: u64

        public transition shut() -> @Unlocked {
            return Door@Unlocked { key_id: self.key_id }
        }
    }
}
```

Each transition consumes its `unique` receiver and returns a freshly constructed target-state value. Driving the lifecycle:

```ultraviolet
public procedure cycle(start: unique Door@Locked) -> Door@Locked {
    let opened: Door@Open = start ~> unlock() ~> openWide()
    let relocked: Door@Locked = opened ~> shut() ~> lock()
    return relocked
}
```

Each `~>` consumes its receiver and yields the next state, so the chain is a pipeline through the state machine. `start` is moved into `unlock`; `opened` is moved into `shut`; the result types are checked at every link. The receiver must be `unique`, which is why `cycle` takes `unique Door@Locked`.

### 9.5 Modal Widening (§13.5)

A concrete state value `M@S` can be *widened* to the general modal type `M`, discarding the static knowledge of which state it is in. This is necessary whenever you need to store, return, or collect modal values whose state is not statically uniform (for example a value whose state depends on a runtime branch).

#### Syntax

```ebnf
widen_expr ::= "widen" unary_expr
```

`widen` is a reserved keyword and a unary prefix operator (Appendix B.3: `unary_operator ::= "!" | "-" | "&" | "*" | "move" | "widen"`).

#### Semantics

By `(T-Modal-Widen)` and `(T-Modal-Widen-Perm)`:

- `widen e` where `e : M@S` produces `M` (the general modal type).
- If `e : P' M@S`, then `widen e : P' M` (permission is preserved through widening).

Runtime (§13.5.5): `UnOp("widen", v) ⇓ ModalVal(S, v)` — the value gains its runtime state tag, becoming a general modal value. When the layout is **niche-compatible** the widen may be representation-preserving; otherwise lowering materializes the tagged general representation (§13.5.6).

Failure / warning modes:

- **`widen` on a non-modal operand** → `(Widen-NonModal)` → `E-TYP-2071`.
- **`widen` on an already-general modal value** (it is already `M`, not `M@S`) → `(Widen-AlreadyGeneral)` → `E-TYP-2072`.
- **Implicit subsumption to a non-niche-compatible general type** is rejected as `(Chk-Subsumption-Modal-NonNiche)` → `E-TYP-2070`. Where the layouts are not niche-compatible, widening must be explicit; you cannot rely on an implicit `M@S` → `M` coercion.
- **Large non-niche payload copy.** Widening a state whose payload exceeds `WIDEN_LARGE_PAYLOAD_THRESHOLD_BYTES = 256` bytes when the layout is not niche-compatible emits the warning `(Warn-Widen-LargePayload)` → `W-SYS-4010`, because the general representation requires copying a large payload.

#### Worked example

```ultraviolet
public modal Job {
    @Queued { id: u64 }
    @Running { id: u64, worker: u32 }
    @Done { id: u64, exit_code: i32 }
}

/// Returns a general `Job` whose state is decided at runtime.
public procedure classify(id: u64, is_running: bool, worker: u32) -> Job {
    if is_running {
        let running: Job@Running = Job@Running { id: id, worker: worker }
        return widen running
    }

    let queued: Job@Queued = Job@Queued { id: id }
    return widen queued
}
```

Both branches produce different concrete state types (`Job@Running`, `Job@Queued`), so neither alone can be the return type. `widen` lifts each to the common general type `Job`, which is the declared return type. The caller of `classify` receives a `Job` and must match on it (next section) to do anything state-specific. Note that a state block can list several fields on one line separated by commas (`id: u64, worker: u32`); the `state_member*` body accepts each `state_field_decl` in sequence.

### 9.6 Matching and Dispatch on Modal State (§13.1.5, §16.7, Chapter 17)

A general modal value `M` carries an unknown state, so you cannot read its fields or call its state methods directly (`E-TYP-2057`). To act on it you must **match** it, which narrows the value to a concrete state inside each arm.

#### Modal patterns

The pattern that matches a state is a **modal pattern** (§17.3.1, Appendix B.4):

```ebnf
modal_pattern      ::= "@" identifier ("{" field_pattern_list? "}")?
field_pattern_list ::= field_pattern ("," field_pattern)* ","?
field_pattern      ::= identifier ":" pattern | identifier
```

So `@Running` matches the `@Running` state, and `@Running { id, worker }` additionally binds the payload fields. A bare `@State` (no brace block) matches the state without destructuring.

#### The `if ... is { ... }` form

Modal dispatch uses the multi-arm `if ... is { ... }` expression (§16.7.1):

```ebnf
if_expr         ::= "if" expression if_tail
if_tail         ::= block_expr ("else" (block_expr | if_expr))?
                  | "is" if_case_pattern block_expr ("else" (block_expr | if_expr))?
                  | "is" "{" if_case+ if_case_else? "}"
if_case         ::= if_case_pattern block_expr
if_case_pattern ::= pattern | ":" type
if_case_else    ::= "else" block_expr
```

Each `if_case` is a pattern immediately followed by a `block_expr` (no `=>`, no comma between arms). Inside an arm, the scrutinee is narrowed to the matched state, so its fields and state methods become available.

By `(T-IfCase-Modal)`, an `if e is { … }` over `e : M` is well-typed when each arm body has the common result type and the match is **exhaustive**: either an irrefutable case is present, an `else` block is present, or the covered states equal `States(M)`. A non-exhaustive modal match with no `else` is rejected by `(IfCase-Modal-NonExhaustive)` → `E-TYP-2060`. Exhaustiveness counts a state as covered only when the pattern actually covers the state (`CoversState`): a `@S` pattern with no payload subpatterns, or one whose payload subpatterns are all irrefutable, covers `@S`; a refutable payload subpattern does not contribute coverage.

#### Worked example: exhaustive dispatch

```ultraviolet
/// Returns the worker id for any `Job`, using a sentinel for non-running states.
public procedure worker_of(job: Job) -> u32 {
    return if job is {
        @Queued { id } {
            0
        }
        @Running { id, worker } {
            worker
        }
        @Done { id, exit_code } {
            0
        }
    }
}
```

All three states of `Job` are covered, so no `else` is required. Inside `@Running { id, worker }`, the bindings `id` and `worker` are the destructured payload. If the `@Done` arm were omitted and there were no `else`, the compiler would raise `E-TYP-2060`. Each arm is a pattern directly followed by a `{ … }` block; the block's trailing expression is the arm's value.

#### Single-arm narrowing

For the common "do something only in one state" case, use the single-arm `if ... is` form, optionally with `else`:

```ultraviolet
public procedure isRunning(job: Job) -> bool {
    if job is @Running { id, worker } {
        return true
    } else {
        return false
    }
}
```

This is the typestate equivalent of an `if`/`else` on a boolean flag — except the state and its payload are part of the type, so the `@Running`-only field `worker` is reachable only inside the narrowing arm.

### 9.7 A Complete Worked Lifecycle

The following self-contained example models a request/response protocol as a modal type, exercising every feature in this chapter: state fields, a `const` state method, a mutating `shared` state method with a contract, transitions with parameters, construction, the `~>` call operator, widening, and exhaustive matching. Strings are kept as `string@View` payloads (a string literal is a `string@View`), and all derived values are primitives, so every line type-checks against the specification without relying on any library string-formatting facility.

```ultraviolet
//! Demonstration of a modal protocol lifecycle.

/// A single HTTP-style exchange. Its legal operations follow the protocol's
/// lifecycle: you may add headers only before sending, write body bytes only
/// while streaming, and read the status only once a response exists.
public modal Exchange {
    @Building {
        method: string@View
        target: string@View
        _header_count: u32

        /// Record a header. Mutates in place, so the receiver is `~%` (shared).
        public procedure addHeader(~%) -> u32 {
            self._header_count = self._header_count + 1
            return self._header_count
        }

        /// Commit the request and begin streaming its body.
        public transition send(content_length: u64) -> @Streaming {
            return Exchange@Streaming {
                target: self.target,
                remaining: content_length
            }
        }
    }

    @Streaming {
        target: string@View
        remaining: u64

        /// Bytes still owed by the body contract.
        public procedure bytesRemaining(~) -> u64 {
            return self.remaining
        }

        /// Write a body chunk; the contract guards against overflowing the body.
        public procedure writeChunk(~%, count: u64) -> u64
            |: count <= self.remaining |= true {
            self.remaining = self.remaining - count
            return self.remaining
        }

        /// Finish the body and receive a response with a status code.
        public transition complete(status: u16) -> @Responded {
            return Exchange@Responded { target: self.target, status: status }
        }
    }

    @Responded {
        target: string@View
        status: u16

        public procedure isSuccess(~) -> bool {
            return self.status >= 200 && self.status < 300
        }
    }
}

/// Build, send, stream, and finish an exchange, returning a general value.
public procedure runExchange(body_length: u64) -> Exchange {
    var building: Exchange@Building = Exchange@Building {
        method: "POST",
        target: "/ingest",
        _header_count: 0
    }
    let _headers: u32 = building ~> addHeader()

    var streaming: Exchange@Streaming = building ~> send(body_length)
    let _left: u64 = streaming ~> writeChunk(body_length)

    let responded: Exchange@Responded = streaming ~> complete(200)
    return widen responded
}

/// Inspect any `Exchange`, in whatever state it happens to be, returning a
/// numeric status: 0 while building, the remaining-byte count while streaming,
/// or the HTTP status once responded.
public procedure statusCode(exchange: Exchange) -> u64 {
    return if exchange is {
        @Building { method, target, _header_count } {
            0
        }
        @Streaming { target, remaining } {
            remaining
        }
        @Responded { target, status } {
            status as u64
        }
    }
}
```

Walking the lifecycle:

1. `Exchange@Building { … }` constructs the initial state with a record literal whose type is the `state_specific_type`.
2. `building ~> addHeader()` calls a `~%` (shared) state method that mutates the header count. The `var` binding holds a `unique`-capable value, which admits the `shared` receiver.
3. `building ~> send(...)` is a transition: it consumes `building` (moved) and yields `Exchange@Streaming`. After this, `building` is spent.
4. `streaming ~> writeChunk(...)` invokes a contract-guarded mutating method (`|: count <= self.remaining |= true`); `streaming ~> complete(200)` transitions to `Exchange@Responded`.
5. `widen responded` lifts the concrete `Exchange@Responded` to the general `Exchange` so `runExchange` can return a value whose state is uniform in the type but not statically pinned by callers.
6. `statusCode` matches the general value exhaustively over all three states, narrowing inside each arm to access state-specific fields, and casts the `u16` status to `u64` with `as` so every arm shares one result type.

The crucial property: `writeChunk` is unreachable on an `Exchange@Building`, `addHeader` is unreachable on an `Exchange@Responded`, and `isSuccess` is unreachable until a response exists. None of these are runtime checks — they are *type* errors at the call site if attempted in the wrong state.

### 9.8 Modal vs. Booleans and Enums — When to Use Which

This is the central design decision the modal feature exists to solve. The style guide is directive here: "Prefer state encoded in types over state encoded in booleans," and "Prefer compile-time safety and structural constraints over convention-based usage."

Use a **`modal`** when *the set of legal operations or available data changes across a value's lifetime*:

- A resource handle that can be open or closed (`File`, sockets, arenas).
- A protocol or session with phases where each phase exposes different methods.
- A builder / import / cooking pipeline where later phases require earlier ones to be complete.
- Anything you would otherwise guard with "is this initialized yet?" booleans plus defensive runtime checks.

The modal makes the illegal states *unrepresentable* and the illegal operations *uncallable*, replacing scattered "if connected then … else panic" checks with type-level guarantees.

Use an **`enum`** when you have a closed set of *alternatives that all support the same operations* and are distinguished by data, not by lifecycle — for example `enum Color { Red, Green, Blue }` or a message variant where every variant is handled the same way. Enums carry per-variant payloads but do not change the *operations* available on the value, and an `enum` value does not move between variants via consuming transitions.

Use a plain **boolean field** only for a genuinely orthogonal yes/no fact that does not gate which methods exist (for example `is_verbose`, `has_focus`). The moment a boolean starts gating "you may only call X if this flag is true," that is a typestate, and the style guide expects a `modal`.

A practical heuristic: if you find yourself writing a comment like "only valid after `open()` is called," or a runtime guard that fails when a flag is wrong, the rule is machine-checkable and the style guide ("State and Validation") requires expressing it in the type — make it a `modal` and move the operation into the state where it is legal.

### 9.9 Idioms & Best Practices

- **Order states in lifecycle order.** The style guide ("Member Ordering") mandates: "In `modal` types, order states in lifecycle order." Place `@Building` before `@Streaming` before `@Responded`, not alphabetically.
- **Keep transitions next to the fields they govern.** "Within a state, keep transitions and state-specific public behavior near the state fields they govern." Declare each state's payload first, then its methods, then its transitions.
- **Name transitions as verbs in `camelCase`.** `open`, `send`, `complete`, `close`, `beginPlayback`. Reserve `procedure` (state methods) for operations that do not change state; reserve `transition` for moves between states.
- **Choose the narrowest receiver.** Use `~` (`const`) for pure inspection, `~%` (`shared`) for synchronized mutation, and `~!` (`unique`) only when exclusive access is genuinely required. A `const`/`shared` caller can still reach your read paths because `PermAdmits` lets stronger permissions call weaker receivers.
- **Construct target states explicitly in transition bodies.** A transition body must end by returning `M@TargetState { … }`. Carry forward exactly the fields the target state needs, and drop fields that no longer apply (for example a live `handle` that is meaningless after `@Closed`).
- **Widen at the boundary, match at the consumer.** Keep values in their concrete `M@S` type as long as the state is statically known; `widen` only when you must erase the state (to return, store, or collect heterogeneous states), and use `if … is { … }` to re-narrow on the consuming side.
- **Put preconditions on validating state methods, not on transitions.** Transitions cannot carry a `contract_clause`. When a state move has a value precondition, model it as a `~%`/`~!` state method that holds the contract and performs the check (or rejects via an `Outcome`), and only allow the transition to be reached once that method has confirmed the invariant. Where the precondition is purely "you must be in state X," the source-state type already enforces it structurally.
- **Prefer `modal` and contracts to model protocols and resource states.** Per the style guide, these "are the preferred way to model protocols, resource states, runtime sessions, imports, cooking phases, and other lifecycle-heavy flows."
- **Document public states and members.** Public types, methods, and transitions take `///` documentation covering purpose, preconditions, postconditions, and capability expectations; public modules take `//!`.
- **Reach for the built-in result type and modals.** Return `Outcome<TValue; TError>` (a two-variant enum) from fallible operations and let callers match `Outcome::Value` / `Outcome::Error`; use `File`, `DirIter`, `Region`, and `CancelToken` rather than re-modeling their lifecycles, and obtain them through their constructor procedures (record construction of those runtime modals is rejected by `E-TYP-2073`).

### 9.10 Pitfalls & Diagnostics

The §13 diagnostics for modal types and widening, together with the modal-match exhaustiveness diagnostic from Chapter 17, and the mistakes that trigger them:

| Code | Trigger | Fix |
|---|---|---|
| `E-TYP-2050` | `modal` declared with zero states. | Every modal needs at least one `@State { … }` block (`Modal-NoStates-Err`). |
| `E-TYP-2051` | Two states with the same name. | Rename one state; state names must be distinct (`Modal-DupState-Err`). |
| `E-TYP-2052` | `e.f` where `f` is not in the current state's payload. | The field belongs to a different state; match/transition first (`Modal-Field-Missing`). |
| `E-TYP-2053` | Calling a method not present in the current state. | The method exists in another state; the value is in the wrong state here (`Modal-Method-NotFound`). |
| `E-TYP-2054` | A state named the same as the modal type. | A state may not equal the modal's own name (`Modal-StateName-Err`). |
| `E-TYP-2055` | A transition body that does not yield the declared target state. | The body's final value must be `M@TargetState { … }` (`Transition-Body-Err`). |
| `E-TYP-2056` | Calling a transition on a non-`unique` or non-state receiver. | Transitions consume `unique M@S_src`; own the value and ensure it is in the source state (`Transition-Source-Err`). |
| `E-TYP-2057` | Reading a field on a *general* `M` value. | You must `if e is @State { … }` to narrow before field access (`Modal-Field-General-Err`). |
| `E-TYP-2058` | Two payload fields with the same name in one state. | Field names within a state must be unique (`Modal-Payload-DupField`). |
| `E-TYP-2059` | `-> @State` naming a state the modal does not declare. | Use a declared target state (`Transition-Target-Err`). |
| `E-TYP-2060` | Non-exhaustive `if e is { … }` over a general modal with no `else`. | Cover every state of `M`, add an `else` block, or add an irrefutable case (`IfCase-Modal-NonExhaustive`). |
| `E-TYP-2061` | Duplicate method names within one state. | Rename one method (`StateMethod-Dup`). |
| `E-TYP-2062` | Duplicate transition names within one state. | Rename one transition (`Transition-Dup`). |
| `E-TYP-2063` | A state member more visible than the modal. | Lower the member's visibility to ≤ the modal's (`StateMemberVisOk-Err`). |
| `E-TYP-2064` | Accessing a state field/method/transition from outside the modal's module, or below required visibility. | Access from within the declaring module, or widen the member's visibility (`Modal-Field-NotVisible`, `Modal-Method-NotVisible`, `Transition-NotVisible`). |
| `E-TYP-2065` | A state method and a transition in the same state share a name. | State-method and transition names must be disjoint per state (`StateMember-Name-Conflict`). |
| `E-TYP-1605` | Calling a `~!`/`~%` method through a `const`/`shared` value. | The caller permission must admit the receiver (`MethodCall-RecvPerm-Err`); obtain a stronger permission. |
| `E-TYP-2070` | Implicit subsumption of `M@S` to a non-niche-compatible `M`. | Write `widen` explicitly (`Chk-Subsumption-Modal-NonNiche`). |
| `E-TYP-2071` | `widen` on a non-modal operand. | Only modal `M@S` values can be widened (`Widen-NonModal`). |
| `E-TYP-2072` | `widen` on an already-general `M`. | The value is already widened; remove the redundant `widen` (`Widen-AlreadyGeneral`). |
| `E-TYP-2073` | Record-constructing a built-in runtime modal state (`File@S`, `DirIter@S`, `CancelToken@S`, `Spawned@S`, `Tracked@S`, `Async@S`). | Use the type's constructor procedures and runtime facilities, not a record literal (`Record-FileDir-Err`). |
| `W-SYS-4010` | Widening a non-niche state whose payload exceeds 256 bytes (a large copy). | Acceptable if intended; otherwise keep the value concrete, or restructure the payload to be niche-compatible (`Warn-Widen-LargePayload`). |

Three further conceptual pitfalls worth calling out:

- **Forgetting that transitions consume the receiver.** After `let next = value ~> close()`, the old `value` binding is spent (the receiver is moved). Reusing it is a move/use-after-move error from the permission system, not a modal-specific diagnostic — but it is the most common surprise. Capture the result and continue from `next`.
- **Trying to "flip" a state in place.** There is no in-place tag mutation. A state change is always a transition that constructs and returns a fresh target-state value (§13.4.6). Model the change as a `transition`, not as field assignment.
- **Putting a `contract_clause` on a `transition`.** The transition grammar (`transition_def ::= … "->" "@" identifier block_expr`) has no contract slot, so `transition open() -> @Open |: … |= …` does not parse. Move the contract onto a validating state method, or rely on the source-state type to enforce the sequencing precondition.

Related chapters: **Permissions & Binding State** (`const`/`shared`/`unique`, receiver admissibility via `PermAdmits`, move semantics), **Records & Classes** (the `<:` implements clause and type invariants shared with modals), **Pattern Matching & `if … is`** (modal patterns and exhaustiveness, Chapter 17), and the **Concurrency / Async** chapter (the built-in `Spawned`, `Tracked`, `Async`, and `CancelToken` modals).
