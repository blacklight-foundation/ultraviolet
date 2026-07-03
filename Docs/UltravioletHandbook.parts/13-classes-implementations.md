## 13. Classes, Implementations & Associated Types

A *class* in Ultraviolet is a nominal abstraction boundary. It declares a set of
required methods, optional default-method bodies, required (abstract) fields,
required abstract states, and associated types that a concrete declaration must
satisfy. It is the language's analog to an interface or trait in other languages,
and it is also the carrier of polymorphism — both static (through generic class
bounds, Chapter 12) and dynamic (through `$Class` dynamic objects, §13.4). Classes
do not own data the way records do; they describe *what a type must provide*. A
concrete `record`, `enum`, or `modal` declares that it satisfies a class with the
`<:` implements clause, and the type checker verifies that every required member is
present with a matching signature.

This chapter covers the class declaration form (spec §14.3), how records, enums, and
modals implement a class (§14.4), associated types (§14.5), and dynamic class objects
with their vtable cost model (§14.6). All grammar is reproduced from the
specification's Appendix B and the §14.3–14.6 inference rules.

> Cross-references: record/enum/modal declaration syntax is covered in **Chapter 11
> (Records, Enums, Modals)**; method receivers, permissions, and the `~>` call form
> are covered in **Chapter 10 (Permissions)** and **Chapter 16 (Expressions)**;
> generic parameters and class bounds in **Chapter 12 (Generics)**; opaque types
> (`opaque Class`) in **spec §14.7**.

### 13.1 Classes (spec §14.3)

#### Exact syntax

A class declaration is a top-level item. The canonical production (Appendix B.6) is:

```ebnf
class_declaration   ::= attribute_list? visibility? "modal"? "class" identifier generic_params? ("<:" superclass_bounds)? "{" class_item* "}"
superclass_bounds   ::= class_bound ("+" class_bound)*
class_item          ::= abstract_procedure | concrete_procedure | abstract_field | abstract_state | associated_type
abstract_procedure  ::= "procedure" identifier signature contract_clause?
concrete_procedure  ::= "procedure" identifier signature contract_clause? block_expr
key_boundary        ::= "#"
abstract_field      ::= attribute_list? visibility? key_boundary? identifier ":" type
abstract_state      ::= "@" identifier "{" field_list? "}"
field_list          ::= abstract_field ("," abstract_field)* ","?
associated_type     ::= "type" identifier ("=" type)?
```

The §14.3.1 grammar gives the same form with the canonical class-item alternatives:

```ebnf
class_decl     ::= attribute_list? visibility? "modal"? "class" identifier generic_params? ("<:" superclass_bounds)? "{" class_body? "}"
class_item     ::= class_method | associated_type | abstract_field | abstract_state
abstract_state ::= "@" identifier "{" abstract_field* "}"
abstract_field ::= attribute_list? visibility? key_boundary? identifier ":" type
```

A class *method* (abstract or concrete) is parsed by the canonical
`ParseMethodSignature` rule shared with record methods (spec §15.2.2). The receiver
is the **first element inside the parameter parentheses**, not part of the bare
`signature` production. The receiver grammar (Appendix B.6) is:

```ebnf
receiver           ::= receiver_shorthand | explicit_receiver
receiver_shorthand ::= "~" | "~!" | "~%"
explicit_receiver  ::= param_mode? "self" ":" type
param_mode         ::= "move"
```

The receiver shorthand maps to a receiver permission (Chapter 10), exactly as for
record methods: `~` is a `const` receiver, `~!` is a `unique` receiver, and `~%` is a
`shared` receiver. An explicit receiver `self : type` MUST use a `Self`-form type —
the static rule **(WF-Class-Method)** requires `SelfTypeClass(ty)`, i.e. `ty` is
`Self` or a permission-qualified `Self` such as `unique Self`. Note that the
shorthand order is `~` / `~!` / `~%`; there is no `~?` form, and the percent sign,
not the bang, denotes `shared`.

#### Semantics

A class introduces the type-level path `Cl ∈ Σ.Classes`. Its body items are:

- **Abstract methods** (`abstract_procedure`): a `procedure` with a signature and no
  block body. Every concrete implementer must supply a matching method. Detected by
  `AbstractClassMethod(m) ⇔ m.body_opt = ⊥`.
- **Concrete (default) methods** (`concrete_procedure`): a `procedure` with a body.
  Implementers inherit the body unless they override it. Detected by
  `ConcreteClassMethod(m) ⇔ m.body ≠ ⊥`.
- **Abstract fields** (`abstract_field`): a required named field of a given type. The
  optional `key_boundary` marker `#` marks the field as a key boundary (see the Keys
  chapter). Implementers must have a field of that name whose type is a subtype of the
  required type.
- **Abstract states** (`abstract_state`): a required `@State { … }` shape whose body
  is a list of `abstract_field`s. A class that declares any abstract state may be
  implemented **only by a `modal` type** (§14.4.4); the leading `modal` keyword on the
  class declaration marks this intent.
- **Associated types** (`associated_type`): a named type member, abstract
  (`type Name`) or defaulted (`type Name = Default`), bound by the implementer's body
  (§13.3).

Class declarations introduce no runtime actions by themselves (§14.3.5). Observable
behavior arises only when a concrete method body runs, or when the class participates
in dynamic dispatch (§13.4). The implicit type variable inside a class body is `Self`
(`SelfVar = TypePath([Self])`); the substitution `SubstSelf(T, ty)` replaces `Self`
with the implementing type `T` when checking a particular implementation.

When a method omits its return type, the default is unit: `ReturnType(m) =
TypePrim("()")` when `m.return_type_opt = ⊥`.

#### Well-formedness rules (§14.3.4)

Rule **(WF-Class)** requires, for a class `Cl`:

- `Distinct(MethodNames(Cl))` — no duplicate method names (diagnostic `E-TYP-2500`).
- `Distinct(FieldNames(Cl))` — no duplicate abstract-field names (`E-TYP-2408`).
- `Disjoint(MethodNames(Cl), FieldNames(Cl))` — a method name may not collide with a
  field name.
- `Distinct(Supers(Cl))` and every superclass path resolves:
  `∀ S ∈ Supers(Cl), Γ ⊢ S : ClassPath` (undefined superclass → `E-TYP-2509`).
- Each method is `ClassMethodOK(Cl)` and its body (if any) typechecks via
  `ClassMethodBodyOK`.
- Linearization succeeds: `Γ ⊢ Linearize(Cl) ⇓ L` (a cycle → `Superclass-Cycle`,
  `E-TYP-2508`).

Per-method well-formedness **(WF-Class-Method)** additionally requires: generic params
are well-formed; an explicit receiver uses a `Self`-form type
(`SelfTypeClass(ty)`); the receiver type is well-formed; `self ∉ ParamNames(params)`;
parameter names are distinct; and every parameter type and the return type are
well-formed.

A concrete method body is checked by **(T-Class-Method-Body)**: `self` is bound to
`RecvType(SelfVar, m.receiver)`, the parameters are bound, the body typechecks against
the declared return type, and a non-unit return type requires an explicit `return`
(`ExplicitReturn(body)`) — consistent with the style guide's rule to "write explicit
`return` statements in non-`unit` procedures."

#### Superclasses, linearization, and effective members

A class may list superclasses with `<: A + B + …` (note: superclasses are separated by
`+`, whereas an implements clause on a concrete type separates classes with `,`).
Subtyping propagates by **(T-Superclass)**: `class A <: B` and `T <: A` give `T <: B`.
The set of methods and fields a class actually requires is its *effective* set,
computed by C3-style linearization:

- `Linearize(Cl)` produces an ordered list `[Cl, …]` by merging the linearizations of
  the superclasses (rules **(Lin-Base)**, **(Lin-Ok)**, **(Merge-Step)**). A merge that
  cannot select a consistent head fails (**(Merge-Fail)** → `Superclass-Cycle`,
  `E-TYP-2508`).
- `EffMethods(Cl) = FirstByName(++ ClassMethods(C_i))` over the linearization, keeping
  the first method of each name. If two classes contribute a same-named method with
  *different* `Self`-signatures, `FirstByName` raises **(EffMethods-Conflict)**
  (`E-TYP-2505`; conflicting signatures also surface as `E-UNS-0106`).
- `EffFields(Cl)` is the analogous first-by-name over fields; a same-name field with a
  differing signature raises **(EffFields-Conflict)** (`E-TYP-2505`, also reported as
  `E-TYP-2406`, "conflicting field names from multiple classes").

`ClassMethodTable(Cl) = EffMethods(Cl)` and `ClassFieldTable(Cl) = EffFields(Cl)` are
exactly the obligations an implementer must satisfy.

#### `class` vs `record` vs `modal`

Per the style guide (`AGENTS.md`, "Type Design"):

- Use **`record`** for plain value data: descriptors, configuration, snapshots, and
  other value-first structures.
- Use **`class`** *only* when shared identity, polymorphism, or reference-oriented
  behavior is actually required — that is, when you need an abstraction over multiple
  concrete types, or dynamic dispatch.
- Use **`modal`** for state-based code where available fields or allowed operations
  differ by lifecycle state. A *modal class* (a class with abstract states) abstracts
  over modal protocols.

A class is the right tool when several concrete types must be used interchangeably
behind a common contract, or when you need a `$Class` dynamic object. It is the wrong
tool for a single concrete data shape — that is a `record`.

#### Worked example: a class with abstract and default methods

```ultraviolet
/// A drawable surface target. Implementers provide a pixel extent and a clear
/// operation; the framed area is supplied as a reusable default.
public class RenderTarget {
    /// Pixel width of the target.
    procedure width(~) -> u32

    /// Pixel height of the target.
    procedure height(~) -> u32

    /// Clear the whole target to the given color.
    procedure clear(~%, color: Rgba8)

    /// Total pixel area. Default reuses width and height; rarely overridden.
    procedure area(~) -> u32 {
        return self~>width() * self~>height()
    }
}
```

Here `width`, `height`, and `clear` are abstract (no body). `area` is a default method
with a body that calls the other two through the `self~>name(...)` method-call form
(Chapter 16). The `~` receiver is `const` (read-only); `~%` is `shared`
(key-mediated access), matching `clear`'s need to write through the key system.

#### Worked example: a class with required fields and a superclass

```ultraviolet
/// Anything with a stable identity number.
public class Identified {
    #id: u64
}

/// A named, identified resource: extends Identified with a display name field.
public class NamedResource <: Identified {
    name: string@View

    /// Default label combines name and id.
    procedure label(~) -> string@Managed {
        return formatLabel(self.name, self.id)
    }
}
```

`#id` is a key-boundary field (the `#` marker). `NamedResource <: Identified` inherits
the `id` field obligation through linearization, so `id` is an effective field of
`NamedResource` (hence `self.id` resolves in `label`), and any implementer of
`NamedResource` must provide both `id` and `name`.

#### Worked example: a modal class (abstract states)

```ultraviolet
/// A connection protocol abstracted over its lifecycle states. Because it
/// declares abstract states, only a `modal` type may implement it.
public modal class Connection {
    @Closed {
        endpoint: string@View,
    }

    @Open {
        endpoint: string@View,
        session_id: u64,
    }

    /// Required of every implementer.
    procedure send(~%, payload: bytes@View) -> SendResult
}
```

The abstract states `@Closed` and `@Open` declare the state shapes an implementer must
provide; their bodies contain only abstract fields. The method `send` is an ordinary
class-level abstract method (class methods are class items, not state members).

### 13.2 Implementations (spec §14.4)

#### Exact syntax

Implementation is declared *at the defining record, enum, or modal* via the implements
clause — there are **no standalone extension/impl blocks** in the language (§14.4.1).
The clause is (Appendix B.6):

```ebnf
implements_clause ::= "<:" class_list
class_list        ::= type_path ("," type_path)*
```

The §14.4.1 grammar states the same clause as
`implements_clause ::= "<:" class_path ("," class_path)*` (where
`class_path ::= type_path`), and also defines the implementing method form whose
override marker is significant:

```ebnf
override_method ::= visibility? "override"? "procedure" identifier signature contract_clause? block
```

The implements clause appears after the type name and generic params on each concrete
declaration (Appendix B.6):

```ebnf
record_decl ::= attribute_list? visibility? "record" identifier generic_params? implements_clause? "{" record_body "}" type_invariant?
enum_decl   ::= attribute_list? visibility? "enum"   identifier generic_params? implements_clause? "{" variant_members? "}" type_invariant?
modal_decl  ::= attribute_list? visibility? "modal"  identifier generic_params? implements_clause? "{" state_block+ "}" type_invariant?
```

Methods on the implementing type are ordinary `method_def`s; a method that replaces a
class *default* must carry the `override` keyword (Appendix B.6):

```ebnf
method_def ::= attribute_list? visibility? "override"? "procedure" identifier generic_params? "(" receiver ("," param_list)? ")" ("->" return_type)? contract_clause? block_expr
```

#### Semantics: what an implementer must provide

For each implemented class `Cl ∈ Implements(T)`, the checker walks
`ClassMethodTable(Cl)` and `ClassFieldTable(Cl)` and verifies each obligation. The
rules in §14.4.4 classify each required method by whether it is abstract or defaulted,
and whether the implementer marks `override`:

- **(Impl-Abstract-Method)** — the class method is abstract (`m.body = ⊥`), the type
  has a method of that name with a matching signature (`SigMatch(T, m', m)`), and that
  method is **not** marked `override`. This is the correct way to satisfy an abstract
  requirement.
- **(Impl-Missing-Method)** — abstract requirement with no matching method on `T` →
  implementation fails (`E-TYP-2503`).
- **(Impl-Sig-Err)** — a method of the right name exists but its signature does not
  match → fails (`E-TYP-2503`).
- **(Override-Abstract-Err)** — the implementing method matches an *abstract*
  requirement but is wrongly marked `override` → fails (`E-TYP-2501`).
- **(Impl-Concrete-Default)** — the class method has a default body and the type
  provides *no* method of that name; the type **uses the default** (no source needed).
- **(Impl-Concrete-Override)** — the class method has a default body, the type provides
  a matching method, and that method **is** marked `override` → the type overrides the
  default.
- **(Override-Missing-Err)** — the type provides a matching method for a *defaulted*
  class method but omits `override` → fails (`E-TYP-2502`).
- **(Impl-Sig-Err-Concrete)** — a method matching a defaulted requirement by name has a
  mismatched signature → fails (`E-TYP-2503`).
- **(Override-NoConcrete)** — a method on `T` is marked `override` but no implemented
  class has a concrete (defaulted) method of that name → fails (`E-UNS-0105`).

The signature-match relation `SigMatch(T, m_impl, m_decl)` requires the receiver types
and parameter signatures to be *equal* after `Self` substitution, and the implementer's
return type to be a subtype of the declared return type
(`recv_i = recv_d ∧ params_i = params_d ∧ Γ ⊢ ret_i <: ret_d`).

Fields are checked by:

- **(Impl-Field)** — required field `f : T_c` is present on `T` as `f : T_i` with
  `T_i <: T_c`.
- **(Impl-Field-Missing)** — required field absent (`E-TYP-2402`).
- **(Impl-Field-Type-Err)** — present but `T_i <: T_c` fails (`E-TYP-2404`).

A class with abstract states may be implemented only by a `modal` type; a non-modal
type attempting it is `E-TYP-2401`. A modal implementer missing a required state is
`E-TYP-2403`; a state missing a required payload field is `E-TYP-2405`.

#### Coherence and the orphan rule (§14.4.4)

Two normative restrictions govern *where* an implementation may be declared:

- **Coherence**: a type **MUST NOT** implement the same class more than once.
  `Distinct(Implements(T))` is required by **(WF-Impl)**; a duplicate triggers
  **(Impl-Coherence-Err)** → `E-TYP-2506`.
- **Orphan rule**: for every implementation `T <: Cl`, at least one of `T` or `Cl`
  MUST be defined in the current assembly. Formally `ImplOrphanOk(T, Cl) ⇔
  SameAssembly(ImplModule(T), CurrentModule(Γ)) ∨ (Cl ∈ dom(Σ.Classes) ∧
  SameAssembly(ClassModule(Cl), CurrentModule(Γ)))`. A violation is
  **(Impl-Orphan-Err)** → `E-TYP-2507`. Source-level implementation evidence is owned
  by the implementing record, enum, or modal declaration; an imported metadata relation
  where both `T` and `Cl` are foreign to the current assembly is rejected with
  `Impl-Orphan-Err`.

The full implementation judgment **(WF-Impl)** ties these together: every implemented
class is itself `ClassOk`, `Implements(T)` is distinct, every pair is orphan-OK, `T`
satisfies `BitcopyDropOk`, and for every class every method obligation is met
(abstract / override / default) and every field is satisfied. When all hold, the
subtyping relation `Γ ⊢ T <: Cl ⇔ Cl ∈ Implements(T) ∧ Γ ⊢ T : ImplementsOk` becomes
available.

#### Lowering

Implementation-specific method bodies lower exactly as concrete methods on `T`
(§14.4.6). When a required method is satisfied by a class default, lowering reuses the
default body as the dispatch target for that `(type, class, method)` triple — no copy
is materialized in source.

#### Worked example: a record implementing a class

```ultraviolet
/// An in-memory image buffer that can serve as a render target.
public record ImageBuffer <: RenderTarget {
    public width_px: u32
    public height_px: u32
    public pixels: unique bytes@Managed

    /// Satisfies the abstract `width`. No `override`: it fulfils an abstract
    /// requirement, it does not replace a default.
    public procedure width(~) -> u32 {
        return self.width_px
    }

    public procedure height(~) -> u32 {
        return self.height_px
    }

    public procedure clear(~%, color: Rgba8) {
        fillPixels(self.pixels, color)
    }

    /// Replaces the default `area` with a faster path. `override` is REQUIRED
    /// because `area` has a default body in the class.
    public override procedure area(~) -> u32 {
        return self.width_px * self.height_px
    }
}
```

Removing `override` from `area` would fail with `E-TYP-2502`; adding `override` to
`width` would fail with `E-TYP-2501`.

#### Worked example: a modal implementing a modal class

```ultraviolet
/// A TCP connection implementing the Connection protocol class. The states
/// match the class's abstract states by name and payload.
public modal TcpConnection <: Connection {
    @Closed {
        endpoint: string@View,
    }

    @Open {
        endpoint: string@View,
        session_id: u64,

        public procedure send(~%, payload: bytes@View) -> SendResult {
            return writeFrame(self.session_id, payload)
        }
    }

    public transition open(initial_session: u64) -> @Open {
        return TcpConnection@Open { endpoint: self.endpoint, session_id: initial_session }
    }
}
```

The transition body constructs and returns a fresh state value using the modal-state
construction form `modal_type_ref "@" identifier "{" field_init_list? "}"`, i.e.
`TcpConnection@Open { … }`.

### 13.3 Associated Types (spec §14.5)

#### Exact syntax

```ebnf
associated_type ::= "type" identifier ("=" type)?
```

In a **class** body, the optional `= type` is a *default*. In an **implementing**
declaration body (parsed by `Parse-RecordMember-AssociatedType`), the optional
`= type` is the *bound* associated-type body.

#### Semantics

An associated type is a type member supplied by the implementing declaration rather
than at the use site — contrast with generic class parameters (`generic_params`),
which are supplied at use sites (§14.5.4). In a class:

- `type Name` (no `= type`) is **abstract**: every implementation must bind it.
- `type Name = Default` is **concrete-defaulted**: implementations may use the default
  or override it with their own binding.

In a concrete implementing declaration body, an associated-type member is well-formed
**only** in bound form `type Name = Bound` (§14.5.4) — you cannot leave it unbound on
the implementer.

Lookup order for `AssocTypeBinding(T, Cl, name)` is exactly (§14.5.4):

1. the implementation binding from the implementing declaration body
   (`ImplAssocType(T, name)`);
2. the class default from the referenced class (`AssocTypeDefault(Cl, name)`);
3. missing binding.

`A_abs(Cl)` is the set of abstract associated types of `Cl` (those without a default).
An implementation that fails to bind a member of `A_abs(Cl)` triggers
**(Impl-AssocType-Missing)** → the implementation is rejected. Duplicate
associated-type names within a class are `E-TYP-2504`.

Associated types are compile-time only: they introduce no runtime values and no
abstract-machine transitions (§14.5.5), and are erased during type elaboration
(§14.5.6) — there is no per-feature ABI form.

A class alias built with `type Alias = A + B` participates in subtyping by
**(T-Alias-Equiv)**: `Γ ⊢ T <: Alias ⇔ Γ ⊢ T <: A ∧ Γ ⊢ T <: B`.

#### Worked example: abstract and defaulted associated types

```ultraviolet
/// A container abstracted over its element and its index.
public class Sequence {
    /// Abstract: each implementer chooses its element type.
    type Element

    /// Defaulted: implementers may keep usize or override it.
    type Index = usize

    procedure at(~, index: Index) -> Element
    procedure count(~) -> Index
}

/// A fixed array of frame ids binds Element and reuses the default Index.
public record FrameList <: Sequence {
    /// Bound form `type Name = Bound` is mandatory on the implementer.
    type Element = u64

    public ids: [u64]

    public procedure at(~, index: usize) -> u64 {
        return self.ids[index]
    }

    public procedure count(~) -> usize {
        return lengthOf(self.ids)
    }
}
```

`FrameList` binds `Element = u64` and inherits `Index = usize` from the class default.
Omitting the `Element` binding would be rejected by **(Impl-AssocType-Missing)**.

### 13.4 Dynamic Class Objects (spec §14.6)

#### Exact syntax

```ebnf
dynamic_type      ::= "$" class_path
dynamic_cast_expr ::= expr "as" dynamic_type
```

A dynamic class type is written `$Class` (the `$` operator before a class path). A
value is converted to a dynamic object with the ordinary cast operator
(`cast_expr ::= unary_expr ("as" type)?`), where `$Class` is the target type:
`expr as $Class`. Method calls on a dynamic value use the ordinary method-call form
`base~>name(args)` from Chapter 16; there is no special dynamic-call surface syntax
(§14.6.1).

#### Semantics: dispatchability

`$Class` is a *fat pointer*: a `data` raw pointer plus a `vtable` raw pointer. Its AST
form is `TypeDynamic(path)`, and its fields are fixed (§14.6.3):

```
DynFields(Cl) = [⟨data, *imm ()⟩, ⟨vtable, *imm VTable⟩]
```

A class may be made dynamic only if it is **dispatchable**. Dispatchability is defined
per method:

```
vtable_eligible(m) ⇔ HasReceiver(m) ∧ ¬HasGenericParams(m) ∧ ¬SelfOccurs(m)
dispatchable(Cl)   ⇔ ∀ m ∈ EffMethods(Cl). vtable_eligible(m)
```

That is, every effective method must (1) have a receiver, (2) have no generic
parameters, and (3) not mention `Self` anywhere in its parameter or return types
(`SelfOccurs` over those types must be false). A method returning `Self` or taking a
`Self` parameter is not vtable-eligible because the concrete `Self` is erased behind
the dynamic object.

Well-formedness of the type:

- **(WF-Dynamic)** — `$p` is well-formed iff `p ∈ dom(Σ.Classes)`.
- **(WF-Dynamic-Err)** — `$p` where `p` is not a class → rejected.

The cast `e as $Cl` is typed by **(T-Dynamic-Form)**: `e` must be a place expression
whose address can be taken (`IsPlace(e)`, `AddrOfOk(e)`), `Cl` must be a class path,
the stripped type of `e` must satisfy `StripPerm(T) <: Cl`, and `Cl` must be
`dispatchable(Cl)`. If the class is not dispatchable, **(Dynamic-NonDispatchable)**
rejects the cast (`E-TYP-2541`; a non-eligible procedure called on `$` is
`E-TYP-2540`; a generic procedure that blocks dispatch is `E-TYP-2542`).

#### Method resolution and dispatch

A call `base~>name(args)` where `base : $Cl` is typed by **(T-Dynamic-MethodCall)**:
the class method `m = LookupClassMethod(Cl, name)` is found, the caller permission must
admit the method's receiver permission (`PermAdmits(P_caller, P_method)` — Chapter 10),
the receiver and arguments must be well-typed, and the result type is `ReturnType(m)`.
If the name does not resolve, **(LookupClassMethod-NotFound)** rejects the call.

`PermAdmits(P_caller, P_method)` holds for exactly: `(const, const)`, `(shared, const)`,
`(shared, shared)`, `(unique, const)`, `(unique, shared)`, and `(unique, unique)`. So a
`const` dynamic value can call only `~` (const) methods, whereas a `unique` one can
call any.

At runtime, the value form is `Dyn(Cl, RawPtr(imm, addr), T)`, where `T` is the
*hidden concrete type*. Dispatch selects the target body via `Dispatch(T, Cl, name)`
(§14.6.5):

- if `T` has a matching method (`MethodByName(T, name) = m'` with `SigMatch`), dispatch
  to `m'`;
- otherwise, if the class method has a default body (`m.body ≠ ⊥`), dispatch to the
  default;
- otherwise dispatch is `⊥` (no target) — caught statically by the implementation rules
  so it cannot occur for a well-formed program.

`LookupMethod(T, name)` (used for static name resolution) succeeds when the type has the
method directly, or when there is *exactly one* class default of that name; zero or more
than one defaults make the call unresolved.

#### Vtable layout and cost model (§14.6.6)

The dynamic object is exactly two pointers wide:

```
Layout-DynamicClass:  DynLayout(Cl) ⇓ ⟨2 × PtrSize, PtrAlign, DynFields(Cl)⟩
Size-DynamicClass:    sizeof($Cl)  = 2 × PtrSize
Align-DynamicClass:   alignof($Cl) = PtrAlign
```

The vtable holds one entry per vtable-eligible effective method, in linearization
order:

```
VTableEligible(Cl) = [ m ∈ EffMethods(Cl) | vtable_eligible(m) ]
VTable-Order:  VTable(T, Cl) ⇓ [sym_1, …, sym_k]
VSlot-Entry:   VSlot(Cl, method) ⇓ i      (* the slot index of method *)
```

Each vtable slot's symbol is resolved by `DispatchSym(T, Cl, name)`: it points at the
implementer's mangled method when `T` provides a matching one
(**(DispatchSym-Impl)**), or at the default-implementation body otherwise
(**(DispatchSym-Default-None)**, **(DispatchSym-Default-Mismatch)**). Constructing the
dynamic value packs the data pointer with the type-specific vtable
(**(Lower-Dynamic-Form)**, `DynPack`). A dynamic call lowers to an indexed indirect
call plus a panic check:

```
Lower-DynCall:  LowerDynCall(base, name, args) ⇓ SeqIR(CallVTable(base, i, args), PanicCheck)
```

**Cost model.** A static (monomorphized, generic-bound) call resolves to a direct call
with no indirection. A dynamic call costs: the two-pointer object
(size `2 × PtrSize`), one vtable load, and one indirect call through slot `i`. There is
no runtime type test on the happy path — the slot index is fixed at compile time by
`VSlot`. Choose `$Class` only when the set of concrete types is genuinely open at
runtime; prefer generic class bounds (Chapter 12) for closed, performance-sensitive
code.

#### Worked example: dynamic dispatch over `$RenderTarget`

```ultraviolet
/// Clears whichever target is supplied, dispatched dynamically at runtime.
public procedure clearAny(target: $RenderTarget, color: Rgba8) {
    target~>clear(color)
}

public procedure demoDynamic(image: ImageBuffer) {
    let dynamic_target: $RenderTarget = image as $RenderTarget
    clearAny(dynamic_target, Rgba8::Black)
}
```

`RenderTarget` is dispatchable: `width`, `height`, `area`, and `clear` all have
receivers, take no generic parameters, and mention no `Self` in their signatures. The
cast `image as $RenderTarget` is valid because `ImageBuffer <: RenderTarget` and `image`
is an addressable place (an identifier). The call `target~>clear(color)` dispatches
through the vtable to `ImageBuffer::clear`. Note the enum-variant access form
`Rgba8::Black` uses `::`, the canonical path separator for enum literals.

A class with a method like `procedure clone(~) -> Self` would **not** be dispatchable —
`SelfOccurs` would be true for the return type — and `as $Class` would be rejected by
**(Dynamic-NonDispatchable)** (`E-TYP-2541`).

### Idioms & Best Practices

- **Reach for `class` only when polymorphism or shared identity is real.** Per the
  style guide, default to `record` for value data and `modal` for lifecycle state. A
  class earns its keep when multiple concrete types must be used interchangeably or
  when you need `$Class` dynamic dispatch.
- **Prefer static over dynamic.** Use generic class bounds (`<T <: RenderTarget>`,
  Chapter 12) for closed, hot-path polymorphism — it monomorphizes to direct calls.
  Reserve `$Class` for genuinely open runtime sets, consistent with the guide's "use
  `[[dynamic]]` only when the intended semantics are truly dynamic."
- **`override` is a precise signal, not decoration.** Mark `override` exactly when
  replacing a class *default* body; omit it when fulfilling an *abstract* requirement.
  The checker enforces both directions (`E-TYP-2501` / `E-TYP-2502`), so the keyword
  always tells the reader whether a default exists.
- **Use default methods to factor shared behavior** (like `area` over
  `width`/`height`), and keep the genuinely type-specific operations abstract. Default
  bodies call other required members through `self~>name(...)`.
- **Bind associated types instead of over-parameterizing.** When a type member is fixed
  by the implementer (an element type, an index type), an associated type keeps use
  sites clean; reserve generic class parameters for things the *caller* chooses.
- **Honor receiver permissions.** Use `~` (const) for read-only methods, `~%` (shared)
  for key-mediated access, `~!` (unique) for exclusive mutation; the call site's
  permission must satisfy `PermAdmits` (Chapter 10). Declare the narrowest receiver that
  works.
- **Keep abstract APIs small and contract-rich.** The guide's "keep APIs small,
  explicit, and stable" applies doubly to classes: every abstract method is an
  obligation imposed on every implementer. Attach `contract_clause`s to class methods
  where pre/postconditions are expressible.
- **Always write visibility explicitly** on classes, their members, and implementing
  methods; document public classes with `//!`/`///` (style guide, "Imports and
  Visibility", "Comments and Documentation").

### Pitfalls & Diagnostics

- **Forgetting `override` on a default replacement** → `E-TYP-2502` ("Missing
  `override` on concrete procedure replacement"). **Adding `override` to an abstract
  fulfilment** → `E-TYP-2501` ("`override` used on abstract procedure implementation").
  **`override` with no default to replace** → `E-UNS-0105` (**Override-NoConcrete**).
- **Signature mismatch** between the implementing method and the required one →
  `E-TYP-2503` ("Type does not implement required procedure ... or has incompatible
  signature"). Remember `SigMatch` requires receiver and parameter equality and
  return-type subtyping after `Self` substitution — a wrong receiver permission or an
  extra parameter breaks it.
- **Missing required member.** Absent method → `E-TYP-2503` (**Impl-Missing-Method**);
  absent field → `E-TYP-2402`; field with wrong type → `E-TYP-2404`; unbound abstract
  associated type → **Impl-AssocType-Missing**.
- **Implementing a modal class with a non-modal type** → `E-TYP-2401`. A modal
  implementer missing a required state → `E-TYP-2403`; a state missing a required
  payload field → `E-TYP-2405`.
- **Implementing the same class twice** → `E-TYP-2506` (**Impl-Coherence-Err**).
  **Neither type nor class local** (orphan rule) → `E-TYP-2507` (**Impl-Orphan-Err**).
- **Superclass mistakes:** undefined superclass path → `E-TYP-2509`; a superclass cycle
  (or an unsatisfiable linearization merge) → `E-TYP-2508` (**Superclass-Cycle**);
  same-named members with conflicting signatures coming from different superclasses →
  `E-TYP-2505` (**EffMethods-Conflict** / **EffFields-Conflict**), with conflicting
  method signatures also surfacing as `E-UNS-0106` and conflicting field names as
  `E-TYP-2406`.
- **Duplicate names in a class:** duplicate method → `E-TYP-2500`; duplicate abstract
  field → `E-TYP-2408`; duplicate abstract state → `E-TYP-2409`; duplicate associated
  type → `E-TYP-2504`. A method name colliding with a field name violates
  `Disjoint(MethodNames, FieldNames)`.
- **Dynamic-object pitfalls:** casting to a non-dispatchable class (any effective
  method that lacks a receiver, is generic, or mentions `Self`) → `E-TYP-2541`
  (**Dynamic-NonDispatchable**); calling a non-vtable-eligible procedure on a `$` value
  → `E-TYP-2540`; a generic class procedure blocking dispatch → `E-TYP-2542`. Casting to
  `$X` where `X` is not a class fails **(WF-Dynamic-Err)**.
- **Self-returning methods kill dispatchability.** A method like
  `procedure withCapacity(~, n: usize) -> Self` makes the whole class non-dispatchable.
  If you need both a `Self`-returning factory and dynamic dispatch, split the
  construction surface out of the dynamic class.
- **Non-place dynamic cast.** `expr as $Class` requires `expr` to be an addressable
  place (`IsPlace` ∧ `AddrOfOk`); casting a temporary that has no address is rejected.
  Bind it to a `let`/`var`, or cast a parameter or field, so the operand is an
  identifier or other place expression.
- **Wrong tokens.** The receiver shorthand is `~` / `~!` / `~%` (`%` = shared, `!` =
  unique); superclasses are joined with `+`, but an implements `class_list` uses `,`;
  enum literals and other paths use `::`, never `.`.
