## 10. Strings, Bytes & Text Handling

This chapter is the definitive reference for Ultraviolet's two built-in byte-sequence types: `string` (§13.6) and `bytes` (§13.7). Both are **modal** built-in types: each has two states — `@Managed` (a heap-owned, growable buffer) and `@View` (a borrowed window over an existing buffer) — and the bare, unannotated form (`string`, `bytes`) is the **modal supertype** of those two states. The whole point of this chapter is to let you read and build byte sequences with confidence, so every operation the specification defines on these types is enumerated here, with its exact signature, its semantics, and a compiling example.

Every keyword, operator, type name, and grammar production in this chapter is reproduced exactly from the specification. Several facts govern everything below and are worth stating up front because they recur throughout:

1. **Their operations are built-in path functions, not record methods.** The specification defines them in `StringBuiltinTable` (§13.6.3) and `BytesBuiltinTable` (§13.7.3) as entries like `string::from` and `bytes::view`. You call them with `path_expr` call syntax (`string::from(source, heap)`), **not** with the `~>` method-call operator. The first parameter — whether named `self`, `source`, or `data` — is supplied positionally like any other argument. (`path_expr ::= type_path "::" identifier`, Appendix B.3.)
2. **Both types are byte sequences; `string` additionally carries a UTF-8 guarantee.** The dynamic model (§13.6.5) makes every `string` and `bytes` value a `List(u8)`. The `string` type's slicing operation is UTF-8-boundary-checked; the `bytes` type imposes no encoding constraint. Both lower to the same machine shape (pointer + length, plus capacity in the managed state).
3. **`@View` borrows; `@Managed` owns.** A `@View` does not allocate, does not own memory, and must not outlive the storage it references. A `@Managed` value owns a heap allocation that is released automatically when it goes out of scope, via the `DropManaged` hook (§13.6.6, §13.7.6).

Cross-references: heap allocation and the `$HeapAllocator` capability are covered in *Memory, Allocation & Regions*; the `Outcome<TValue, TError>` enum type and the `?` propagation operator (`propagate_expr ::= postfix_expr "?"`, §16.8) are covered in *Error Handling & Outcome*; slices (`[u8]`) and scalar indexing are covered in *Arrays, Slices & Indexing*; modal states and the `@State` syntax are covered in *Modal Types*.

---

### 10.1 The Two Type Families at a Glance

#### 10.1.1 Exact Syntax

The §13.6.1 and §13.7.1 productions:

```ebnf
string_type      ::= "string" string_state_opt
string_state_opt ::= ε | "@" "Managed" | "@" "View"

bytes_type      ::= "bytes" bytes_state_opt
bytes_state_opt ::= ε | "@" "Managed" | "@" "View"
```

Appendix B (B.2) restates the same shape with the state factored out:

```ebnf
string_type  ::= "string" ("@" string_state)?
bytes_type   ::= "bytes" ("@" bytes_state)?
string_state ::= "Managed" | "View"
bytes_state  ::= "Managed" | "View"
```

Both `string` and `bytes` are listed in `primitive_type` and `non_union_type` (B.2), so they are first-class types usable anywhere a type is expected, and they may be permission-qualified by the surrounding `type ::= permission? non_permission_type refinement_clause?` rule — e.g. `const string@View`, `unique bytes@Managed`.

#### 10.1.2 The Type Surface

| Written type | State | Meaning | Owns memory? | Size (lowering) |
| --- | --- | --- | --- | --- |
| `string@Managed` | Managed | Heap-owned, growable UTF-8 buffer | Yes (dropped) | `3 × PtrSize` (pointer, length, capacity) |
| `string@View` | View | Borrowed UTF-8 window | No | `2 × PtrSize` (pointer, length) |
| `string` | (none) | Modal supertype of both states | — | `ModalLayout(string)` |
| `bytes@Managed` | Managed | Heap-owned, growable raw-byte buffer | Yes (dropped) | `3 × PtrSize` (pointer, length, capacity) |
| `bytes@View` | View | Borrowed raw-byte window | No | `2 × PtrSize` (pointer, length) |
| `bytes` | (none) | Modal supertype of both states | — | `ModalLayout(bytes)` |

The state set is fixed by the spec: `States(string) = { @Managed, @View }` (§13.6.3) and `States(bytes) = { @Managed, @View }` (§13.7.3). Each state is a subtype of the bare type, per the (WF-String) and (WF-Bytes) subtyping rules:

```text
S ∈ {@Managed, @View}              S ∈ {@Managed, @View}
─────────────────────              ───────────────────
   string@S <: string                 bytes@S <: bytes
```

So you can store a `string@View` or a `string@Managed` where a `string` is expected. To **call operations**, however, you generally need the concrete state the operation's parameter demands (see the surface tables in §10.3.1 and §10.5.1): most read operations require `@View`, mutation requires `unique @Managed`.

#### 10.1.3 The Dynamic Model: Everything Is a Byte Sequence

The dynamic semantics (§13.6.5) define one unifying byte-sequence carrier. There is a single store triple `SB` whose components map managed values to their bytes and capacity:

```text
ByteSeq  = List(u8)
SB       = ⟨StrBuf, BytesBuf, BytesCap⟩
StrBuf   : string@Managed ⇀ ByteSeq
BytesBuf : bytes@Managed  ⇀ ByteSeq
BytesCap : bytes@Managed  ⇀ usize
ViewBytes : (string@View ∪ bytes@View) → ByteSeq
```

The function that recovers the bytes behind **any** of these values is `ByteSeqOf`, and length is just its size:

```text
ByteSeqOf(SB, v) =
  StrBuf(v)    if v : string@Managed
  BytesBuf(v)  if v : bytes@Managed
  ViewBytes(v) if v : string@View or v : bytes@View

ByteLen(SB, v) = |ByteSeqOf(SB, v)|
```

This is why a `string@View` and a `bytes@View` over the same memory hold the same `ByteSeq`, and why `bytes::view_string` (§10.6) can reinterpret a string view as a bytes view with zero copying.

#### 10.1.4 Lowering

The lowering reinforces the representation. Both managed states are three machine words; both view states are two (§13.6.6, §13.7.6):

```text
StringManagedFields = [⟨pointer, *u8⟩, ⟨length, usize⟩, ⟨capacity, usize⟩]        (size 3 × PtrSize)
StringViewFields    = [⟨pointer, const *u8⟩, ⟨length, usize⟩]                       (size 2 × PtrSize)
BytesManagedFields  = [⟨pointer, *u8⟩, ⟨length, usize⟩, ⟨capacity, usize⟩]          (size 3 × PtrSize)
BytesViewFields     = [⟨pointer, const *u8⟩, ⟨length, usize⟩]                       (size 2 × PtrSize)
```

The view field's element pointer is `const`-qualified — views are read-only windows. Managed values own their allocation and are deallocated on drop:

```text
DropManaged(ManagedString(ptr, _, cap), v_heap) ⇔ HeapDeallocRaw(v_heap, ptr, cap)
DropManaged(ManagedBytes(ptr, _, cap),  v_heap) ⇔ HeapDeallocRaw(v_heap, ptr, cap)
```

You never invoke these drop hooks yourself; a `@Managed` value is cleaned up automatically when it leaves scope. The runtime symbols are `PathSig(["ultraviolet", "runtime", "string", "drop_managed"])` and `PathSig(["ultraviolet", "runtime", "bytes", "drop_managed"])` (§24.6.2).

---

### 10.2 String Types (§13.6)

#### 10.2.1 The Operation Surface

These are exactly the **eight** operations the specification defines on `string`, from `StringBuiltinTable` (§13.6.3). Read each signature literally: the parameter named `self` is the **first positional argument**, not a `~>` receiver. The permission prefix (`const`, `unique`) and the state suffix (`@View`, `@Managed`) are both part of each parameter's type.

| Path function | Parameters (in order) | Returns |
| --- | --- | --- |
| `string::from` | `source: string@View`, `heap: $HeapAllocator` | `Outcome<unique string@Managed, AllocationError>` |
| `string::as_view` | `self: const string@Managed` | `string@View` |
| `string::slice` | `self: const string@View`, `start: usize`, `end: usize` | `string@View` |
| `string::to_managed` | `self: const string@View`, `heap: $HeapAllocator` | `Outcome<unique string@Managed, AllocationError>` |
| `string::clone_with` | `self: const string@Managed`, `heap: $HeapAllocator` | `Outcome<unique string@Managed, AllocationError>` |
| `string::append` | `self: unique string@Managed`, `data: string@View`, `heap: $HeapAllocator` | `Outcome<(), AllocationError>` |
| `string::length` | `self: const string@View` | `usize` |
| `string::is_empty` | `self: const string@View` | `bool` |

The runtime-symbol mapping confirms these are real, callable built-ins: e.g. `BuiltinSym(string::from) = PathSig(["ultraviolet", "runtime", "string", "from"])` (§24.6.2). Any `string::name` that is **not** one of these eight is a static error by `(BuiltinSym-String-Err)`:

```text
(BuiltinSym-String-Err)
StringMethod(method)    method ∉ StringBuiltins
───────────────────────────────────────────────
Γ ⊢ BuiltinSym(method) ⇑
```

There is no `string::push`, `string::concat`, `string::find`, or similar — only the names above.

#### 10.2.2 String Literals and the `string@View` Type

A string literal produces a value of type `string@View` (§13.6.5: `StringLiteralVal(lit) = v ⇔ LiteralValue(lit, TypeString(@View)) = v`). The lexical grammar (§2; identical in Appendix B.1):

```ebnf
string_literal   ::= '"' (string_char | escape_sequence)* '"'
string_char      ::= (* Unicode scalar except ", \, or U+000A *)
escape_sequence  ::= "\n" | "\r" | "\t" | "\\" | "\"" | "\'" | "\0"
                   | "\x" hex_digit hex_digit | "\u{" hex_digit+ "}"
```

The storage rule is normative and load-bearing for `@View` safety (§13.6.5, **String Literal Storage**): for any string literal `lit`, evaluation **MUST** allocate `StringBytes(lit)` in static, read-only storage. The resulting `string@View` references that storage, has length `|StringBytes(lit)|`, and the backing storage **MUST** have static duration and **MUST NOT** be deallocated. So a literal view never dangles — it is always safe to hold and to pass to operations expecting `string@View`.

```ultraviolet
let greeting: string@View = "Hello, Ultraviolet"
let with_escape: string@View = "tab\there, newline\nhere, emoji \u{1F600}"
```

Because the value type is `string@View`, you can pass a literal directly anywhere a `string@View` is required — including as the `source` argument to `string::from`, the `data` argument to `string::append`, and the `data` argument to `bytes::view_string`.

#### 10.2.3 Length and Emptiness

`string::length` returns the **byte length** of the view; `string::is_empty` is true exactly when that length is zero (§13.6.5):

```text
(StringLength)   n = ByteLen(SB, self)         ⊢ StringLength(SB, self) ⇓ n
(StringIsEmpty)  b = (ByteLen(SB, self) = 0)   ⊢ StringIsEmpty(SB, self) ⇓ b
```

Both take `self: const string@View`. A literal already gives you a `string@View`, so the read surface needs no allocation:

```ultraviolet
/// Returns the byte length of `text`, or 0 when it is empty.
procedure describe(text: string@View) -> usize {
    if string::is_empty(text) {
        return 0
    }

    let byte_count: usize = string::length(text)
    return byte_count
}
```

Mind the units: `string::length` is the number of UTF-8 **bytes**, not the number of Unicode scalar values. A literal like `"é"` encoded as two UTF-8 bytes reports `2`.

#### 10.2.4 Constructing an Owned `string@Managed`

There are three ways to obtain a `string@Managed`, all of which allocate and therefore take a `$HeapAllocator` and return an `Outcome`:

- `string::from(source, heap)` — copy a view's bytes into a fresh owned buffer.
- `string::to_managed(self, heap)` — same effect, expressed as a view operation (`self: const string@View`).
- `string::clone_with(self, heap)` — duplicate an existing owned string (`self: const string@Managed`).

The success rules all copy `ByteSeqOf` into a new `StrBuf` entry; the error rules return an `AllocationError` value and leave the store unchanged (§13.6.5):

```text
(StringFrom-Ok)       r = v   SB' = ⟨StrBuf[v ↦ ByteSeqOf(SB, source)], BytesBuf, BytesCap⟩
(StringToManaged-Ok)  r = v   SB' = ⟨StrBuf[v ↦ ByteSeqOf(SB, self)],   BytesBuf, BytesCap⟩
(StringCloneWith-Ok)  r = v   SB' = ⟨StrBuf[v ↦ ByteSeqOf(SB, self)],   BytesBuf, BytesCap⟩
(* the -Err variants: AllocErrorVal(r), SB' = SB *)
```

The returned managed string is `unique` — you receive sole ownership, and it is dropped (deallocated) at end of scope. Use `?` to propagate the allocation error when your procedure itself returns a compatible `Outcome`. Construct the success value as `Outcome::Value(...)` (`Outcome` is a two-variant enum), or rely on implicit introduction and simply `return` the payload in an `Outcome`-typed context (see *Error Handling & Outcome*).

```ultraviolet
/// Copies `source` into a freshly owned UTF-8 buffer.
procedure ownGreeting(
    source: string@View,
    heap: $HeapAllocator
) -> Outcome<unique string@Managed, AllocationError> {
    let owned: unique string@Managed = string::from(source, heap)?
    return Outcome::Value(move owned)
}
```

The `?` is the propagation operator. By `(T-Propagate-Outcome)` (§16.8.4), when the operand has type `Outcome<T_s, E_s>` and the enclosing procedure returns `Outcome<T_r, E_r>` with `E_s <: E_r`, the expression evaluates to `T_s` on `Outcome::Value` and otherwise returns `Outcome::Error(…)` from the enclosing procedure. Here `E_s = E_r = AllocationError`, so `AllocationError <: AllocationError` holds and the propagated value is the `unique string@Managed` success payload. See *Error Handling & Outcome*.

`string::clone_with` is the right tool when you already hold a managed string and need an independent owned copy:

```ultraviolet
/// Produces an independent owned copy of `original`.
procedure duplicate(
    original: const string@Managed,
    heap: $HeapAllocator
) -> Outcome<unique string@Managed, AllocationError> {
    let copy: unique string@Managed = string::clone_with(original, heap)?
    return Outcome::Value(move copy)
}
```

#### 10.2.5 Borrowing a View From an Owned String

`string::as_view` turns a `const string@Managed` into a `string@View` sharing the same bytes — `ByteSeqOf(SB, v) = ByteSeqOf(SB, self)` (§13.6.5, **(StringAsView-Ok)**). The view borrows; it does not copy and does not own. This is the bridge from owned storage to the view-only operations (`length`, `is_empty`, `slice`):

```ultraviolet
/// Reports the byte length of an owned string by borrowing a view.
procedure ownedLength(owned: const string@Managed) -> usize {
    let view: string@View = string::as_view(owned)
    return string::length(view)
}
```

#### 10.2.6 Slicing With UTF-8 Boundary Safety

`string::slice(self, start, end)` produces the sub-view `self[start..end)` and is the only way to take a substring. Its precondition is strict and **encoding-aware** (§13.6.5):

```text
(StringSlice-Ok)
0 ≤ start ≤ end ≤ ByteLen(SB, self)
start and end are valid UTF-8 byte boundaries of ByteSeqOf(SB, self)
ByteSeqOf(SB, v) = ByteSeqOf(SB, self)[start..end)
──────────────────────────────────────────────────
StringSlice(SB, self, start, end) ⇓ v
```

Three conditions must all hold: the offsets are ordered, they are in range, **and** both land on UTF-8 character boundaries. Slicing into the middle of a multi-byte scalar is not permitted — the result is defined only when `start` and `end` are valid boundaries. The resulting view shares the original storage (no allocation), so the UTF-8 guarantee is preserved.

```ultraviolet
/// Returns the first five bytes of `text`, or `text` itself when shorter.
procedure firstFiveBytes(text: string@View) -> string@View {
    let len: usize = string::length(text)
    if len < 5 {
        return text
    }

    let head: string@View = string::slice(text, 0, 5)
    return head
}
```

Because the result is a `string@View` borrowing `text`, it stays valid only as long as the bytes it views do. For literal-backed views this is forever (static storage); for views derived from a `string@Managed`, the slice must not outlive the owner.

#### 10.2.7 Appending to an Owned String

`string::append(self, data, heap)` grows an owned string in place. It requires `self: unique string@Managed` (you must hold the buffer uniquely to mutate it), takes the appended bytes as a `string@View`, and may allocate (§13.6.5):

```text
(StringAppend-Ok)
r = ()
StrBuf' = StrBuf[self ↦ ByteSeqOf(SB, self) ++ ByteSeqOf(SB, data)]
──────────────────────────────────────────────────────────────────
StringAppend(SB, self, data, heap) ⇓ ((), SB')
(* (StringAppend-Err): AllocErrorVal(r), SB' = SB *)
```

It returns `Outcome<(), AllocationError>` — the unit success payload `()` means "the in-place mutation happened." Build up a string by starting from a seed buffer and appending views:

```ultraviolet
/// Builds the line "label: value" in a freshly owned buffer.
procedure buildLine(
    label: string@View,
    value: string@View,
    heap: $HeapAllocator
) -> Outcome<unique string@Managed, AllocationError> {
    var line: unique string@Managed = string::from(label, heap)?
    string::append(line, ": ", heap)?
    string::append(line, value, heap)?
    return Outcome::Value(move line)
}
```

Two things to note. First, `line` is declared with `var`, not `let`, because `append` mutates it through a `unique` parameter. Second, `line` is passed positionally without `move`; because `string::append` does not consume `self` (it returns `Outcome<(), …>`, not the buffer), the `unique` binding is admissible by `PermAdmits(unique, unique)` (§10.4.4) and `line` remains usable for the next call. The literal `": "` is a `string@View` flowing straight into the `data` parameter.

#### 10.2.8 Conversions and Encoding Guarantees

| From → To | Operation | Allocates? |
| --- | --- | --- |
| `string@View` → `string@Managed` | `string::from` or `string::to_managed` | Yes |
| `string@Managed` → `string@View` | `string::as_view` | No (borrow) |
| `string@View` → `string@View` (substring) | `string::slice` | No (sub-view) |
| `string@Managed` → `string@Managed` (copy) | `string::clone_with` | Yes |
| `string@View` → `bytes@View` (reinterpret) | `bytes::view_string` (§10.6) | No |

Every `string` value — managed or view — is guaranteed to hold valid UTF-8. The view operations preserve that guarantee because `slice` enforces boundary alignment and `as_view` shares already-valid bytes. There is no operation in the string surface that forges invalid UTF-8 into a `string`; to handle arbitrary (possibly non-UTF-8) data, use `bytes` (§10.5).

#### 10.2.9 Worked Example: Read-Only Inspection

Putting the read-side surface together — no allocation, just inspect a borrowed string with `is_empty`, `length`, and `slice`:

```ultraviolet
/// Returns the byte length of the prefix `text[0..cut)`, clamped to the
/// full length when `cut` is at or past the end.
procedure prefixLength(text: string@View, cut: usize) -> usize {
    if string::is_empty(text) {
        return 0
    }

    let total: usize = string::length(text)
    if cut >= total {
        return total
    }

    let prefix: string@View = string::slice(text, 0, cut)
    return string::length(prefix)
}
```

This compiles using only `string::is_empty`, `string::length`, and `string::slice`, all of which take `const string@View` (a literal or any borrowed view satisfies that). The caller's `cut` is responsible for being a valid UTF-8 boundary per `(StringSlice-Ok)`.

---

### 10.3 Bytes Types (§13.7)

`bytes` is the raw, **un-encoded** counterpart to `string`. Use it for binary data, protocol framing, hashing input, file contents you do not yet know are text, and any place you must read or assemble individual `u8` values. It has the same `@Managed`/`@View` modality and the same byte-sequence dynamic model.

#### 10.3.1 The Operation Surface

These are exactly the **ten** operations the specification defines on `bytes`, from `BytesBuiltinTable` (§13.7.3):

| Path function | Parameters (in order) | Returns |
| --- | --- | --- |
| `bytes::with_capacity` | `cap: usize`, `heap: $HeapAllocator` | `Outcome<unique bytes@Managed, AllocationError>` |
| `bytes::from_slice` | `data: const [u8]`, `heap: $HeapAllocator` | `Outcome<unique bytes@Managed, AllocationError>` |
| `bytes::as_view` | `self: const bytes@Managed` | `bytes@View` |
| `bytes::as_slice` | `self: const bytes@View` | `const [u8]` |
| `bytes::to_managed` | `self: const bytes@View`, `heap: $HeapAllocator` | `Outcome<unique bytes@Managed, AllocationError>` |
| `bytes::view` | `data: const [u8]` | `bytes@View` |
| `bytes::view_string` | `data: string@View` | `bytes@View` |
| `bytes::append` | `self: unique bytes@Managed`, `data: bytes@View`, `heap: $HeapAllocator` | `Outcome<(), AllocationError>` |
| `bytes::length` | `self: const bytes@View` | `usize` |
| `bytes::is_empty` | `self: const bytes@View` | `bool` |

As with strings, every path lowers to a real runtime symbol — e.g. `BuiltinSym(bytes::view) = PathSig(["ultraviolet", "runtime", "bytes", "view"])` (§24.6.2) — and any `bytes::name` outside this set is a static error by `(BuiltinSym-Bytes-Err)`:

```text
(BuiltinSym-Bytes-Err)
BytesMethod(method)    method ∉ BytesBuiltins
─────────────────────────────────────────────
Γ ⊢ BuiltinSym(method) ⇑
```

There is no `bytes::get`, `bytes::push`, or `bytes::index` — only the ten names above.

#### 10.3.2 The Element Type Is `u8`, Reached Through `as_slice`

A bytes value is a `List(u8)` (§13.7.5). The only operation that exposes an **indexable** form is `bytes::as_slice`, which yields a `const [u8]` (the §13.7.3 signature returns `TypePerm(const, TypeSlice(TypePrim("u8")))`). Indexing that slice with a `usize` yields a `const u8` by `(T-Index-Slice-Perm)` (§16.2.4):

```text
(T-Index-Slice-Perm)
Γ ⊢ e_1 : TypePerm(p, TypeSlice(T))    IndexUsizeExpr(e_2)    BitcopyType(TypePerm(p, T))
──────────────────────────────────────────────────────────────────────────────────────
Γ ⊢ IndexAccess(e_1, e_2) : TypePerm(p, T)
```

With `e_1 : const [u8]` (i.e. `p = const`, `T = u8`) and a `usize` index, the result type is `const u8`. There is therefore **no** direct `bytes[i]` indexing operator; you index through the slice obtained from `as_slice`. Because `u8` is `Bitcopy`, reading an element copies it, so it binds equally well to a `u8` or `const u8` local. This is the canonical "read one byte" path:

```ultraviolet
/// Reads the first byte of a non-empty view.
procedure firstByte(data: const bytes@View) -> u8 {
    let slice: const [u8] = bytes::as_slice(data)
    let b: u8 = slice[0]
    return b
}
```

Indexing rules (§16.2.4): the index must be `usize`. An unsuffixed integer literal like `0` is accepted contextually by `IndexUsizeExpr`; any non-literal index expression must already type as `usize`. A non-`usize` index is the compile-time error `(Index-Slice-NonUsize)` / `E-TYP-1820`. An out-of-bounds index is **not** a compile-time error — it panics at runtime via `(EvalSigma-Index-OOB)`. Always guard with `bytes::length` first when the index is not statically known to be valid.

#### 10.3.3 Length, Emptiness, and Iteration

`bytes::length` and `bytes::is_empty` mirror their string counterparts and take `self: const bytes@View` (§13.7.5):

```text
(BytesLength)   n = ByteLen(SB, self)         ⊢ BytesLength(SB, self) ⇓ n
(BytesIsEmpty)  b = (ByteLen(SB, self) = 0)   ⊢ BytesIsEmpty(SB, self) ⇓ b
```

`bytes` defines **no** built-in iterator of its own. You traverse it by converting to a `[u8]` slice with `bytes::as_slice` and iterating index positions with the range-based `loop`. The loop grammar (B.3) is `loop_expr ::= "loop" loop_condition? loop_invariant? block_expr` with `loop_condition ::= ... | pattern (":" type)? "in" expression`, and the half-open range is `exclusive_range ::= logical_or_expr ".." logical_or_expr` (B.3). Combining length, slice, and indexing gives a complete read traversal:

```ultraviolet
/// XOR-folds every byte of a view into a single checksum.
procedure checksum(data: const bytes@View) -> u8 {
    let slice: const [u8] = bytes::as_slice(data)
    let len: usize = bytes::length(data)

    var acc: u8 = 0u8
    loop index in 0..len {
        let b: u8 = slice[index]
        acc = acc ^ b
    }
    return acc
}
```

`index` types as `usize`, so `slice[index]` is a valid `usize` index yielding `u8`. The `^` operator is bitwise XOR (`bitwise_xor_expr ::= bitwise_and_expr ("^" bitwise_and_expr)*`, B.3), and `0u8` is a `u8` literal (`int_suffix` includes `u8`, B.1). This "`as_slice` then index" pattern is the bit/byte-level workhorse of the `bytes` type.

#### 10.3.4 Constructing Owned Bytes

Three allocating constructors return `unique bytes@Managed` inside an `Outcome` (§13.7.5):

- `bytes::with_capacity(cap, heap)` — a fresh **empty** buffer with reserved capacity `cap' ≥ cap`. Its initial contents are `[]` (length 0), so you grow it with `append`.
- `bytes::from_slice(data, heap)` — copy an existing `const [u8]` into a fresh owned buffer.
- `bytes::to_managed(self, heap)` — copy a `const bytes@View` into a fresh owned buffer.

```text
(BytesWithCapacity-Ok)  r = v   BytesBuf' = BytesBuf[v ↦ []]   BytesCap' = BytesCap[v ↦ cap']   cap' ≥ cap
(BytesFromSlice-Ok)     r = v   BytesBuf' = BytesBuf[v ↦ SliceBytes(data)]
(BytesToManaged-Ok)     r = v   BytesBuf' = BytesBuf[v ↦ ByteSeqOf(SB, self)]
(* the -Err variants: AllocErrorVal(r), SB' = SB *)
```

`with_capacity` is the idiomatic start for incremental building, because it pre-reserves space and starts empty:

```ultraviolet
/// Allocates an empty buffer with at least `reserve` bytes of capacity.
procedure makeBuffer(
    reserve: usize,
    heap: $HeapAllocator
) -> Outcome<unique bytes@Managed, AllocationError> {
    let buf: unique bytes@Managed = bytes::with_capacity(reserve, heap)?
    return Outcome::Value(move buf)
}
```

`from_slice` copies from any `const [u8]`. An array place coerces to a slice by `(Coerce-Array-Slice)`, which fires when the source has type `TypePerm(p, TypeArray(T, n))`; to make that coercion apply, give the array binding an explicit `const` permission so it reads as `const [u8; N]`:

```ultraviolet
/// Builds an owned 4-byte ELF magic header.
procedure magicHeader(
    heap: $HeapAllocator
) -> Outcome<unique bytes@Managed, AllocationError> {
    let header: const [u8; 4] = [0x7Fu8, 0x45u8, 0x4Cu8, 0x46u8]
    let owned: unique bytes@Managed = bytes::from_slice(header, heap)?
    return Outcome::Value(move owned)
}
```

The `const [u8; 4]` array — type `TypePerm(const, TypeArray(u8, 4))` — coerces to `const [u8]` (`TypePerm(const, TypeSlice(u8))`) at the call, satisfying the `data` parameter without a copy of the array's *type identity* (the bytes are copied into the owned buffer by `from_slice`).

#### 10.3.5 Borrowing Views and Slices

`bytes::as_view` borrows a `const bytes@Managed` as a `bytes@View` (shared bytes, no copy; §13.7.5 **(BytesAsView-Ok)**). `bytes::as_slice` exposes a `const bytes@View` as the underlying `const [u8]` (§13.7.5 **(BytesAsSlice-Ok)**). Together they let you go owned → view → slice → individual `u8`:

```ultraviolet
/// Reads the last byte of a non-empty owned buffer.
procedure lastByte(owned: const bytes@Managed) -> u8 {
    let view: bytes@View = bytes::as_view(owned)
    let len: usize = bytes::length(view)
    let slice: const [u8] = bytes::as_slice(view)
    return slice[len - 1]
}
```

The index `len - 1` is `usize - usize`, which is `usize`, so it is an admissible slice index. The view returned by `as_view` (and any slice from it) borrows the owned buffer and must not outlive it.

#### 10.3.6 Appending and Building Bytes

`bytes::append(self, data, heap)` appends a `bytes@View`'s bytes onto a `unique bytes@Managed`, returning `Outcome<(), AllocationError>` (§13.7.5):

```text
(BytesAppend-Ok)
r = ()   BytesBuf' = BytesBuf[self ↦ ByteSeqOf(SB, self) ++ ByteSeqOf(SB, data)]
────────────────────────────────────────────────────────────────────────────────
BytesAppend(SB, self, data, heap) ⇓ ((), SB')
(* (BytesAppend-Err): AllocErrorVal(r), SB' = SB *)
```

Because `append` takes its `data` as a `bytes@View`, you produce that view from whatever source you have: a raw slice via `bytes::view`, or a string via `bytes::view_string` (§10.6). Building a length-prefixed frame:

```ultraviolet
/// Builds [length-byte][payload bytes] from a UTF-8 payload.
procedure frameMessage(
    payload: string@View,
    heap: $HeapAllocator
) -> Outcome<unique bytes@Managed, AllocationError> {
    let payload_view: bytes@View = bytes::view_string(payload)
    let payload_len: usize = bytes::length(payload_view)

    var frame: unique bytes@Managed = bytes::with_capacity(payload_len + 1, heap)?

    let header: const [u8; 1] = [payload_len as u8]
    let header_view: bytes@View = bytes::view(header)
    bytes::append(frame, header_view, heap)?
    bytes::append(frame, payload_view, heap)?

    return Outcome::Value(move frame)
}
```

Here `bytes::view(header)` turns a `const [u8; 1]` (coerced to `const [u8]`) into a `bytes@View` with no copy, and `bytes::view_string(payload)` reinterprets the UTF-8 string view as raw bytes — both feeding `append`'s `data` parameter. The `as u8` is a numeric cast (`cast_expr ::= unary_expr ("as" type)?`, B.3); `CastValid` permits `usize → u8` because both are numeric types (§16.5.4). As with `string::append`, `frame` is `var` and is passed without `move` because `append` does not consume it.

---

### 10.4 Bridging String and Bytes

#### 10.4.1 `bytes::view_string` (string → bytes)

The single dedicated conversion is `bytes::view_string` (§13.7.3, §13.7.5):

```text
⟨bytes::view_string, [⟨⊥, data, string@View⟩], bytes@View⟩

(BytesViewString-Ok)
ByteSeqOf(SB, v) = ByteSeqOf(SB, data)
──────────────────────────────────────
BytesViewString(SB, data) ⇓ v
```

It takes a `string@View` and yields a `bytes@View` over **the very same bytes** — a zero-copy reinterpretation, formalized at the value level (§13.7.5) as:

```text
BytesViewFromString(ViewString(ptr, len)) = ViewBytes(ptr, len)
```

This is the only built-in bridge between the families, and it runs in **one direction only**:

- **string → bytes:** always available and free, via `bytes::view_string`. The resulting bytes are exactly the UTF-8 encoding of the string.
- **bytes → string:** **not provided** by the built-in surface. The bytes surface offers no operation that turns arbitrary `bytes` back into a `string`, because that would require a UTF-8 validity proof for which the specification defines no operation. Do not attempt to fabricate a `string` from `bytes`.

A direct, allocation-free way to measure a string's encoded byte length using the bytes surface:

```ultraviolet
/// Returns the number of UTF-8 bytes in `text`.
procedure encodedByteLength(text: string@View) -> usize {
    let as_bytes: bytes@View = bytes::view_string(text)
    return bytes::length(as_bytes)
}
```

This returns the same value `string::length(text)` would, confirming both types measure in bytes.

#### 10.4.2 `bytes::view` (slice → bytes)

The companion low-level identity for raw slices (§13.7.5) is:

```text
BytesViewFromSlice(SliceVal(ptr, len)) = ViewBytes(ptr, len)
```

which is what `bytes::view(data: const [u8])` realizes — a `bytes@View` directly over an existing `[u8]`, again with no copy. This is the entry point for getting binary data you already hold as a slice into the bytes operation surface (for example, to feed it to `bytes::append`).

---

### 10.5 Idioms & Best Practices

Grounded in the Ultraviolet style guide (AGENTS.md) and the spec's ownership/allocation rules:

- **Pass `@View`, return ownership.** Accept `const string@View` / `const bytes@View` parameters for read-only work; this keeps authority narrow (style guide: "Pass only the exact capabilities a procedure or method uses") and avoids forcing the caller to allocate. Hand back `unique …@Managed` only when the callee genuinely produces new owned data.
- **Take `$HeapAllocator` explicitly and only where you allocate.** Every allocating built-in (`string::from`, `string::to_managed`, `string::clone_with`, `string::append`, `bytes::with_capacity`, `bytes::from_slice`, `bytes::to_managed`, `bytes::append`) requires a `$HeapAllocator`. Thread that capability into exactly the procedures that build or grow buffers; do not bundle it into a broad "god context."
- **Propagate allocation failure with `?`.** When your procedure returns a compatible `Outcome<…, E_r>` whose error type is a supertype of `AllocationError` (`E_s <: E_r`, per `(T-Propagate-Outcome)`), use `?` on allocating calls rather than hand-matching `Outcome::Value`/`Outcome::Error`. Reserve explicit `if … is` matching for when you must **recover** from `AllocationError` locally.
- **Pre-size with `bytes::with_capacity`.** When you know the approximate final size, reserve once with `with_capacity` and `append` into it instead of growing from zero — fewer reallocations, and the intent is explicit at the construction site. Remember it starts empty (`BytesBuf[v ↦ []]`).
- **Reach for `bytes` for binary, `string` for text.** Keep the UTF-8 guarantee meaningful: only put validated text in `string`. Use `bytes` for protocol bytes, hashes, and file payloads of unknown encoding, and cross into the byte stream with `bytes::view_string` at the boundary.
- **Index through `as_slice`, not by guessing an operator.** The only defined way to read an individual byte is `bytes::as_slice(view)[index]`. Bind the slice once (`let slice: const [u8] = bytes::as_slice(view)`) and index it in a loop; do not call `as_slice` per element.
- **Annotate array bindings `const` when feeding `from_slice`/`view`.** `(Coerce-Array-Slice)` matches `TypePerm(p, TypeArray(...))`, so a `const [u8; N]` binding coerces cleanly to the `const [u8]` parameter.
- **Name by the style guide.** Locals and parameters are `snake_case` (`payload_view`, `byte_count`); procedures are `camelCase` (`frameMessage`, `encodedByteLength`); predicate procedures read like questions (`isReady`). Do not encode the type in the name.
- **Use `var` only when you mutate.** `string::append` / `bytes::append` mutate through a `unique` receiver, so the bound buffer must be `var`. Everything else (views, slices, lengths) should be `let`.

---

### 10.6 Pitfalls & Diagnostics

- **`@View` does not own — mind lifetimes.** A `string@View` / `bytes@View` borrows. A view obtained from a `@Managed` value (via `as_view`, `slice`, or `as_slice`) must not outlive that owner; once the managed buffer is dropped (`DropManaged` → `HeapDeallocRaw`), the view dangles. The exception is literal-backed string views, which reference static, never-deallocated storage and are always safe to hold.
- **Slicing must hit UTF-8 boundaries.** `string::slice` is defined only when `start` and `end` are valid UTF-8 byte boundaries within range (`(StringSlice-Ok)`). Slicing through a multi-byte scalar is outside the rule. Compute boundaries from real text positions; do not slice on arbitrary byte offsets you have not validated.
- **Length is bytes, not characters.** Both `string::length` and `bytes::length` count **bytes** (`ByteLen = |ByteSeqOf|`). Treating `string::length` as a character or column count is wrong for any non-ASCII text. There is no built-in scalar-count operation in this surface.
- **Construct `Outcome` with the enum variant form.** The success value is `Outcome::Value(v)` and the failure value is `Outcome::Error(e)` (`Outcome` is a two-variant enum). You can also rely on implicit introduction and `return` the payload directly in an `Outcome`-typed context; the older modal record-literal form (`@Value{…}` / `@Error{…}`) is not used for `Outcome`.
- **Brace control-flow bodies.** The grammar requires `if`/`loop` bodies to be `block_expr` (`if_tail ::= block_expr …`; `(Parse-If-Expr)` parses the body with `ParseBlock`). Write `if cond { return x }`, not `if cond return x`.
- **Don't use `~>` on these built-ins.** `string`/`bytes` operations are path-call functions (`string::length(view)`), not record methods. Writing `view~>length()` does not resolve — `~>` (`method_call_expr ::= postfix_expr "~>" identifier "(" argument_list? ")"`) targets method definitions, and these built-ins are not methods. Use `path::name(self, …)` with the receiver as the first positional argument.
- **Misspelled built-in paths are hard errors.** `(BuiltinSym-String-Err)` and `(BuiltinSym-Bytes-Err)` reject any `string::name` / `bytes::name` outside the eight/ten defined operations. There is no `string::push`, `bytes::get`, or `string::concat`.
- **`append` needs a `unique` receiver.** `string::append` and `bytes::append` require `self: unique …@Managed`. Passing a `const` binding fails permission admissibility — `PermAdmits(const, unique)` does not hold (§10.4.4) — producing a permission error in the `E-TYP-1601`–`E-TYP-1605` family. Bind the buffer with `var` and full `unique` ownership.
- **Slice indexing is `usize` and bounds-checked.** A non-`usize`, non-literal index is rejected at compile time (`(Index-Slice-NonUsize)`, `E-TYP-1820`). An out-of-bounds index does **not** error at compile time; it panics at runtime via `(EvalSigma-Index-OOB)`. Guard with `bytes::length` before indexing a computed position.
- **Allocation can fail — don't ignore the `Outcome`.** Constructors and `append` return `Outcome<…, AllocationError>`; the `Outcome::Error` payload is an `AllocationError`. Discarding the result or assuming `Outcome::Value` throws away real failure information. Propagate with `?` or match explicitly with `if … is`.
- **There is no bytes → string operation.** If you need text out of `bytes`, the built-in surface does not provide it; do not invent one. Keep data in `bytes` until you have an externally validated path to text. (string → bytes is the only defined direction, via `bytes::view_string`.)
- **`with_capacity` gives an empty buffer.** `bytes::with_capacity` reserves space but starts at length 0 (`BytesBuf[v ↦ []]`). Indexing it before appending panics; reserve, then `append`, then read.
