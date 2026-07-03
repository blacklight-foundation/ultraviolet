## 29. Complete Grammar Reference

This chapter is the single-stop syntax quick reference for the Ultraviolet language. It reproduces, organized and lightly annotated, the complete grammar from **Appendix B** of the Ultraviolet Language Specification, grouped by area (B.1 through B.14), and summarizes the **Appendix C** AST Form Index as a quick map of language forms.

The grammar is written in EBNF with these meta-conventions, used uniformly throughout the specification:

- `::=` defines a production.
- `|` separates alternatives.
- `*` means zero-or-more, `+` means one-or-more, `?` means optional.
- Quoted text (`"let"`, `"::"`) is a literal terminal — it must appear exactly.
- Parameterized helpers such as `decorated_identifier("@", "result")` constrain
  a token sequence; they do not introduce a combined lexer token.
- `(* ... *)` is an informative side-note, not a terminal.
- Parentheses group, exactly as in regular expressions.

A note on authority: the productions reproduced below are **verbatim** from Appendix B. Where a section is marked "informative shape restatement" by the spec (notably the `*_attribute` productions in B.8, the async forms in B.11, and the FFI verification attributes in B.13), the surface text is still parsed by a more general production — the restatement only documents the validated argument shape. Those notes are reproduced inline.

For deeper treatment of any single area, cross-reference the dedicated chapters: lexical structure (Ch. "Lexical Structure"), types (Ch. "The Type System"), expressions (Ch. "Expressions"), patterns (Ch. "Patterns and Matching"), statements (Ch. "Statements and Blocks"), declarations (Ch. "Declarations and Modules"), contracts (Ch. "Contracts and Invariants"), attributes (Ch. "Attributes"), the key system (Ch. "The Key System"), structured parallelism (Ch. "Structured Parallelism"), asynchronous operations (Ch. "Asynchronous Operations"), metaprogramming (Ch. "Compile-Time Execution and Metaprogramming"), FFI (Ch. "Foreign Function Interface"), and regions (Ch. "Regions and Frames").

---

### 29.1 B.1 — Lexical Grammar

The lexical grammar describes how raw text is normalized into lines and tokens: identifiers, numeric literals, string/char literals, and the keyword-style literals `true`, `false`, `null`, and `()`.

```ebnf
source_file     ::= normalized_line*
normalized_line ::= code_point* line_terminator?
line_terminator ::= "\n"
code_point      ::= (* Unicode scalar except U+000A and prohibited code points *)

identifier     ::= ident_start ident_continue*
ident_start    ::= (* Unicode XID_Start *) | "_"
ident_continue ::= (* Unicode XID_Continue *) | "_"

integer_literal  ::= (decimal_integer | hex_integer | octal_integer | binary_integer) int_suffix?
decimal_integer  ::= dec_digit ("_"* dec_digit)*
hex_integer      ::= "0x" hex_digit ("_"* hex_digit)*
octal_integer    ::= "0o" oct_digit ("_"* oct_digit)*
binary_integer   ::= "0b" bin_digit ("_"* bin_digit)*
int_suffix       ::= "i8" | "i16" | "i32" | "i64" | "i128" | "u8" | "u16" | "u32" | "u64" | "u128" | "isize" | "usize"
dec_digit        ::= "0" | "1" | "2" | "3" | "4" | "5" | "6" | "7" | "8" | "9"
hex_digit        ::= dec_digit | "a" | "b" | "c" | "d" | "e" | "f" | "A" | "B" | "C" | "D" | "E" | "F"
oct_digit        ::= "0" | "1" | "2" | "3" | "4" | "5" | "6" | "7"
bin_digit        ::= "0" | "1"

float_literal ::= decimal_integer "." decimal_integer? exponent? float_suffix?
exponent      ::= ("e" | "E") ("+" | "-")? decimal_integer
float_suffix  ::= "f" | "f16" | "f32" | "f64"

string_literal   ::= '"' (string_char | escape_sequence)* '"'
string_char      ::= (* Unicode scalar except ", \\, or U+000A *)
escape_sequence  ::= "\n" | "\r" | "\t" | "\\" | "\"" | "\'" | "\0" | "\x" hex_digit hex_digit | "\u{" hex_digit+ "}"

char_literal ::= "'" (char_content | escape_sequence) "'"
char_content ::= (* Unicode scalar except ', \\, or U+000A *)

bool_literal ::= "true" | "false"
null_literal ::= "null"
unit_literal ::= "(" ")"

decorated_identifier(d, s) ::= d identifier
(* The parameters d and s constrain the decorator token and following
   identifier lexeme. This notation denotes a token sequence, not a combined
   lexer token. *)
```

**Semantics.** A `source_file` is a sequence of normalized lines; the only line terminator is `"\n"` (`U+000A`). Identifiers follow the Unicode `XID_Start` / `XID_Continue` classes, with `_` admitted in both positions. Numeric literals admit underscore digit separators (`1_000_000`) and an optional width suffix that fixes the literal's type (`255u8`, `3.14f32`). Floats require at least one digit before the `.`; the fractional part, exponent, and suffix are each optional. The integer suffixes are exactly the integer type names (`i8`…`i128`, `u8`…`u128`, `isize`, `usize`); the float suffixes are `f`, `f16`, `f32`, `f64`. The `unit_literal` `()` is both the unit value and, in type position (B.2), the unit type.

```ultraviolet
let frame_index: u32 = 0u32
let max_subticks: usize = 1_024usize
let blend_weight: f32 = 0.5f32
let scaled: f64 = 6.022e23
let mask: u64 = 0xFF_00_FF_00u64
let tab_char: char = '\t'
let label: string = "frame \"start\"\n"
let is_ready: bool = true
```

---

### 29.2 B.2 — Type Grammar

The type grammar covers every type form: permissions, primitives, tuples, arrays, slices, unions, nominal/generic types, opaque and dynamic class types, string/bytes states, safe and raw pointers, function and closure types, generic parameter lists, refinement clauses, and modal state-specific types.

```ebnf
type                ::= permission? non_permission_type refinement_clause?
non_permission_type ::= union_type | non_union_type
non_union_type      ::= primitive_type | tuple_type | array_type | slice_type | function_type | closure_type | safe_pointer_type | raw_pointer_type | string_type | bytes_type | dynamic_type | opaque_type | state_specific_type | nominal_type
permission          ::= "const" | "unique" | "shared"

primitive_type ::= integer_type | float_type | bool_type | char_type | unit_type | never_type | string_type | bytes_type
integer_type   ::= signed_int | unsigned_int | pointer_int
signed_int     ::= "i8" | "i16" | "i32" | "i64" | "i128"
unsigned_int   ::= "u8" | "u16" | "u32" | "u64" | "u128"
pointer_int    ::= "isize" | "usize"
float_type     ::= "f16" | "f32" | "f64"
bool_type      ::= "bool"
char_type      ::= "char"
unit_type      ::= "(" ")"
never_type     ::= "!"

tuple_type       ::= "(" ")" | "(" type ";" ")" | "(" type ("," type)+ ")"
array_type       ::= "[" type ";" expression "]"
slice_type       ::= "[" type "]"
union_type       ::= non_union_type ("|" non_union_type)+

nominal_type        ::= type_path generic_args?
type_path           ::= identifier ("::" identifier)*
opaque_type         ::= "opaque" class_path
dynamic_type        ::= "$" class_path
class_path          ::= type_path

string_type  ::= "string" ("@" string_state)?
bytes_type   ::= "bytes" ("@" bytes_state)?
string_state ::= "Managed" | "View"
bytes_state  ::= "Managed" | "View"

safe_pointer_type ::= "Ptr" "<" type ">" ("@" pointer_state)?
pointer_state     ::= "Valid" | "Null" | "Expired"
raw_pointer_type  ::= "*" raw_pointer_qual type
raw_pointer_qual  ::= "imm" | "mut"

function_type        ::= sparse_function_type | closure_type
sparse_function_type ::= "(" param_type_list? ")" "->" type
closure_type         ::= "|" param_type_list? "|" "->" type closure_deps?
closure_deps         ::= "[" "shared" ":" "{" shared_dep_list? "}" "]"
shared_dep_list      ::= shared_dep ("," shared_dep)*
shared_dep           ::= identifier ":" type
param_type_list      ::= param_type ("," param_type)* ","?
param_type           ::= "move"? type
```

> Within `closure_type`, a parameter type whose outermost constructor is `union_type` MUST be parenthesized as `("(" type ")")`.

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

refinement_clause   ::= "|:" "{" predicate_expr "}"

modal_type_name     ::= type_path generic_args?
state_specific_type ::= modal_type_name "@" state_name
state_name          ::= identifier
```

**Semantics and pitfalls of note.**

- A `type` is an optional `permission` (`const`, `unique`, or `shared`), then a non-permission type, then an optional `refinement_clause`. When no permission is written, `const` is the default. Permissions and binding state are covered in Ch. "Permissions and Binding State".
- **The one-element tuple uses a trailing `;` before `)`**: `(T;)` is a single-element tuple type, distinct from `(T)` (a parenthesized type) and `()` (the unit type). The two-or-more form is `(A, B)`, `(A, B, C)`, etc.
- `array_type` `[T; N]` has a *length expression* `N`; `slice_type` `[T]` is unsized.
- `never_type` is the single token `!`.
- `string`/`bytes` carry an optional state tag `@Managed` or `@View`.
- The **safe pointer** type is `Ptr<T>` with optional state `@Valid`, `@Null`, or `@Expired`. The **raw pointer** type is `*imm T` or `*mut T` — note the qualifier (`imm`/`mut`) precedes the pointee type.
- A **sparse function type** is `(P, Q) -> R`. A **closure type** is `|P, Q| -> R` with an optional shared-capture dependency list `[shared: { name: T }]`. Inside a closure type, a union parameter type must be parenthesized.
- **`generic_param_list` is `;`-separated**, not comma-separated: `<TKey; TValue>`. Bounds use `<:` and defaults use `=`. Generic *arguments* (`generic_args`) at use sites are comma-separated: `Map<string, i32>`.
- `state_specific_type` is `ModalName@StateName` (e.g. `Connection@Open`).
- `dynamic_type` is `$ClassPath` (a dynamic class object); `opaque_type` is `opaque ClassPath`.

```ultraviolet
type FrameId = u64

record FramePair {
    public lhs: i32
    public rhs: i32
}

type FrameBuffer = [f32; 256]
type FrameView = [f32]
type Coordinate = (f32, f32, f32)
type SingleSlot = (i32;)

type Reducer = (i32, i32) -> i32
type Predicate = |i32| -> bool [shared: {threshold: i32}]

procedure makeBuffer() -> Ptr<FrameBuffer>@Valid {
    return allocBuffer()
}

type IdMap<TValue> = Map<u64, TValue>
type BoundedIndex = u32 |: { @result < 256u32 }
```

---

### 29.3 B.3 — Expression Grammar

The expression grammar defines operator precedence (highest at the bottom: power, then cast, unary, pipeline, postfix, primary), ranges, literals, closures, control-flow expressions (`if`, `loop`, blocks), and the full primary-expression set including memory, concurrency, async, comptime, and metaprogramming forms.

```ebnf
expression         ::= attributed_expr | unattributed_expr
attributed_expr    ::= attribute_list expression
unattributed_expr  ::= range_expression | logical_or_expr

range_expression    ::= exclusive_range | inclusive_range | from_range | to_range | to_inclusive_range | full_range
exclusive_range     ::= logical_or_expr ".." logical_or_expr
inclusive_range     ::= logical_or_expr "..=" logical_or_expr
from_range          ::= logical_or_expr ".."
to_range            ::= ".." logical_or_expr
to_inclusive_range  ::= "..=" logical_or_expr
full_range          ::= ".."

logical_or_expr     ::= logical_and_expr ("||" logical_and_expr)*
logical_and_expr    ::= comparison_expr ("&&" comparison_expr)*
comparison_expr     ::= bitwise_or_expr (comparison_op bitwise_or_expr)*
comparison_op       ::= "==" | "!=" | "<" | "<=" | ">" | ">="
bitwise_or_expr     ::= bitwise_xor_expr ("|" bitwise_xor_expr)*
bitwise_xor_expr    ::= bitwise_and_expr ("^" bitwise_and_expr)*
bitwise_and_expr    ::= shift_expr ("&" shift_expr)*
shift_expr          ::= additive_expr (shift_op additive_expr)*
shift_op            ::= "<<" | ">>"
additive_expr       ::= multiplicative_expr (additive_op multiplicative_expr)*
additive_op         ::= "+" | "-"
multiplicative_expr ::= power_expr (multiplicative_op power_expr)*
multiplicative_op   ::= "*" | "/" | "%"
power_expr          ::= cast_expr ("**" power_expr)?
cast_expr           ::= unary_expr ("as" type)?

unary_expr     ::= new_expr | unary_operator unary_expr | pipeline_expr
new_expr       ::= "new" unary_expr
unary_operator ::= "!" | "-" | "&" | "*" | "move" | "copy" | "widen"
pipeline_expr  ::= postfix_expr ("=>" postfix_expr)*

postfix_expr   ::= primary_expr postfix_suffix*
postfix_suffix ::= "." identifier | "." decimal_integer | "[" expression "]" | "~>" identifier "(" argument_list? ")" | "(" argument_list? ")" | "?"
```

**Operator structure.** Precedence climbs from `logical_or` (lowest binary) down through bitwise OR, bitwise XOR, bitwise AND, shift, additive, multiplicative, power, and cast. `**` (power) is **right-associative** (`power_expr ::= cast_expr ("**" power_expr)?`). Unary forms are `new`, `!`, `-`, `&` (address-of), `*` (deref), `move`, `copy`, and `widen`. The **pipeline** operator `=>` threads a value left-to-right through `postfix_expr` stages. Postfix suffixes are: field access `.name`, tuple index `.0`, indexing `[i]`, the **bound/dynamic call** `~>method(args)`, ordinary call `(args)`, and the propagation/try operator `?`.

```ebnf
primary_expr ::= literal | identifier_expr | path_expr | tuple_literal | array_literal | record_literal | enum_literal | closure_expr | if_expr | loop_expr | block_expr | move_expr | copy_expr | widen_expr | address_of_expr | null_ptr_expr | transmute_expr | sync_expr | race_expr | all_expr | wait_expr | yield_expr | yield_from_expr | spawn_expr | parallel_block | dispatch_expr | fence_expr | comptime_expr | comptime_if | comptime_loop | quote_expr | quote_type | quote_pattern | type_literal | splice_expr | splice_ident | contract_intrinsic

literal ::= integer_literal | float_literal | string_literal | char_literal | bool_literal | null_literal | unit_literal

identifier_expr ::= identifier
path_expr       ::= type_path "::" identifier

tuple_literal       ::= "(" tuple_expr_elements? ")"
tuple_expr_elements ::= expression ";" | expression ("," expression)+
array_literal       ::= "[" array_segment_list? "]"
array_segment_list  ::= array_segment ("," array_segment)*
array_segment       ::= expression | expression ";" expression
expression_list     ::= expression ("," expression)* ","?
record_literal      ::= identifier "{" field_init_list "}" | state_specific_type "{" field_init_list? "}"
field_init_list     ::= field_init ("," field_init)* ","?
field_init          ::= identifier ":" expression | identifier
enum_literal        ::= type_path "::" identifier variant_args?
variant_args        ::= "(" expression_list ")" | "{" field_init_list "}"

argument_list ::= argument ("," argument)* ","?
argument      ::= ("move" | "copy")? expression

closure_expr       ::= "|" closure_param_list? "|" ("->" type)? closure_body
closure_param_list ::= closure_param ("," closure_param)* ","?
closure_param      ::= "move"? identifier (":" type)?
closure_body       ::= expression | block_expr
```

> Within `closure_expr`, a typed parameter annotation whose outermost constructor is `union_type` MUST be parenthesized as `("(" type ")")`.

```ebnf
if_expr         ::= "if" expression if_tail
if_tail         ::= block_expr ("else" (block_expr | if_expr))?
                  | "is" if_case_pattern block_expr ("else" (block_expr | if_expr))?
                  | "is" "{" if_case+ if_case_else? "}"
if_case         ::= if_case_pattern block_expr
if_case_pattern ::= pattern | ":" type
if_case_else    ::= "else" block_expr
loop_expr       ::= "loop" loop_condition? loop_invariant? block_expr
loop_condition  ::= expression | pattern (":" type)? "in" expression
loop_invariant  ::= "|:" "{" predicate_expr "}"

block_expr ::= "{" statement* expression? "}"

move_expr        ::= "move" place_expr
copy_expr        ::= "copy" unary_expr
widen_expr       ::= "widen" unary_expr
address_of_expr  ::= "&" place_expr
null_ptr_expr    ::= "Ptr" "::" "null" "()"
transmute_expr   ::= "transmute" "<" type "," type ">" "(" expression ")"
```

**Key semantics.**

- **Tuple and array literal element forms mirror the type grammar**: a one-element tuple literal is `(expr;)`; the multi-element form is `(a, b)`. An array segment may be a single `expression` or the repeat form `expr; count`, so `[0; 256]` is "256 copies of 0".
- A `record_literal` is `Name { field: value, ... }`; fields may use the punning shorthand `field` when an in-scope binding has the same name. The state-specific form `Modal@State { ... }` constructs a modal value in a specific state.
- An `enum_literal` is `Type::Variant`, optionally with tuple-style `(...)` or record-style `{...}` payload arguments.
- `argument` and `closure_param` accept the `move`/`copy` modifier to control binding mode at the call/capture site (`closure_param` admits only `move`).
- **`if` has three tails**: a plain boolean `if cond { } else { }`; the single-arm pattern form `if expr is pattern { } else { }`; and the multi-arm match-like form `if expr is { pat1 { } pat2 { } else { } }`. Every tail uses braced `block_expr` bodies — there is no brace-less `if`. An `if_case_pattern` may also be a type test `: SomeType`.
- `loop` takes an optional condition (boolean `expr`, or `pattern in iterable`), an optional `loop_invariant` (`|: { ... }`), and a block body.
- `null_ptr_expr` is the exact form `Ptr::null()`. `transmute_expr` is `transmute<From, To>(value)`. `widen value` widens a record value into the corresponding modal value.

```ultraviolet
procedure classifyFrame(frame: FrameRequest) -> FrameReply {
    let priority: i32 = frame.depth ** 2 + 1

    if frame.is_skip {
        return FrameReply::Skip
    } else if priority > 100 {
        return FrameReply::Defer
    }

    return frame
        => normalizeFrame
        => buildFrameReply
}

procedure sumColumn(values: [i32]) -> i32 {
    var total: i32 = 0
    loop value: i32 in values |: { total >= 0 } {
        total += value
    }
    return total
}

let identity_palette: [f32; 4] = [1.0f32; 4]
let origin: (f32, f32, f32) = (0.0f32, 0.0f32, 0.0f32)
let scale: |i32| -> i32 = |x| -> i32 { x * 2 }
```

---

### 29.4 B.4 — Pattern Grammar

Patterns appear in `let`/`var` bindings, `if ... is`, and `loop ... in`. They cover literals, wildcards, identifiers, typed bindings, tuples, records, enum variants, modal states, and ranges.

```ebnf
pattern                ::= literal_pattern | wildcard_pattern | identifier_pattern | typed_pattern | tuple_pattern | record_pattern | enum_pattern | modal_pattern | range_pattern
literal_pattern        ::= literal
wildcard_pattern       ::= "_"
identifier_pattern     ::= identifier
typed_pattern          ::= ("_" | identifier) ":" type
tuple_pattern          ::= "(" tuple_pattern_elements? ")"
tuple_pattern_elements ::= pattern ";" | pattern ("," pattern)+ ","?
record_pattern         ::= type_path "{" field_pattern_list? "}"
field_pattern_list     ::= field_pattern ("," field_pattern)* ","?
field_pattern          ::= identifier ":" pattern | identifier
enum_pattern                  ::= type_path "::" identifier enum_payload_pattern?
enum_payload_pattern          ::= "(" enum_payload_pattern_elements? ")" | "{" field_pattern_list? "}"
enum_payload_pattern_elements ::= pattern ("," pattern)* ","?
modal_pattern          ::= "@" identifier ("{" field_pattern_list? "}")?
range_pattern          ::= pattern (".." | "..=") pattern
```

**Semantics.**

- `wildcard_pattern` `_` matches and discards. `identifier_pattern` binds the whole value. `typed_pattern` binds (or discards via `_`) with an explicit type annotation: `count: u32` or `_: ErrorCode`.
- The **one-element tuple pattern** again uses the trailing-`;` form: `(x;)`. The multi-element form is `(a, b)` with an optional trailing comma.
- `record_pattern` is `Type { field: subpattern, ... }`; bare `field` is field-pun binding.
- `enum_pattern` is `Type::Variant` with optional `(...)` or `{...}` payload sub-patterns.
- `modal_pattern` `@State` matches a modal value in a given state, optionally destructuring its fields with `@State { field: pat }`.
- `range_pattern` joins two patterns with `..` (exclusive) or `..=` (inclusive).

```ultraviolet
procedure describeCode(code: StatusCode) -> string {
    if code is {
        StatusCode::Ok { return "ok" }
        StatusCode::Retry(attempts: u32) { return "retry" }
        StatusCode::Failure { reason: r } { return r }
        else { return "unknown" }
    }
}

procedure gradeScore(score: u32) -> char {
    if score is {
        90u32..=100u32 { return 'A' }
        80u32..90u32 { return 'B' }
        else { return 'F' }
    }
}

let (frame_id, frame_kind) = decodeHeader(packet)
```

---

### 29.5 B.5 — Statement Grammar

Statements appear inside blocks. They cover bindings, local `using`, assignment and compound assignment, expression statements, control transfers, `defer`, `unsafe` blocks, and (by reference) region/frame/key/comptime statements.

```ebnf
statement ::= binding_stmt | using_local_stmt | assignment_stmt | compound_assign | expr_stmt | return_stmt | break_stmt | continue_stmt | defer_stmt | region_stmt | frame_stmt | unsafe_block | key_block_stmt | comptime_stmt

binding_stmt     ::= ("let" | "var") pattern (":" type)? binding_op expression terminator
using_local_stmt ::= "using" identifier "as" identifier terminator
binding_op       ::= "=" | ":="

assignment_stmt ::= place_expr "=" expression terminator
compound_assign ::= place_expr compound_op expression terminator
compound_op     ::= "+=" | "-=" | "*=" | "/=" | "%="
place_expr      ::= identifier | postfix_expr "." identifier | postfix_expr "[" expression "]"

expr_stmt  ::= expression terminator
terminator ::= ";" | newline
newline    ::= "\n"

return_stmt   ::= "return" expression?
break_stmt    ::= "break" expression?
continue_stmt ::= "continue"

defer_stmt   ::= "defer" block_expr
unsafe_block ::= "unsafe" block_expr
```

**Semantics.**

- A `binding_stmt` introduces an immutable (`let`) or mutable (`var`) binding. The pattern may carry a type annotation. The **binding operator** is either `=` (ordinary initialization) or `:=` (the move-binding operator). The statement terminator is a `;` or a newline, so most bindings end with a line break and need no semicolon.
- `using_local_stmt` is the only sanctioned local aliasing form: `using original as alias` (the style guide forbids name-churn shadowing; alias only here when genuinely needed).
- A `place_expr` — the left side of an assignment — is an identifier, a field access, or an index. Compound assignment supports `+= -= *= /= %=`.
- `return`, `break`, and `continue` are statements; `return` and `break` carry an optional value expression.
- `defer block` schedules the block to run on scope exit. `unsafe block` opens an unsafe region; keep it minimal and wrapped in safe APIs (style guide).
- `region_stmt`, `frame_stmt` (B.14), `key_block_stmt` (B.9), and `comptime_stmt` (B.12) are statements too; they are documented in their own sections.

```ultraviolet
procedure accumulate(samples: [i32]) -> i32 {
    var running_total: i32 = 0
    let device_handle: Handle = acquireDevice()
    defer { releaseDevice(device_handle) }

    loop sample: i32 in samples {
        running_total += sample
        if running_total > FRAME_LIMIT {
            break
        }
    }

    return running_total
}
```

---

### 29.6 B.6 — Declaration Grammar

Top-level items: imports, `using`, statics, procedures (including comptime procedures), records, enums, modals, classes, type aliases, extern blocks, and derive targets. This is the structural backbone of every Ultraviolet module.

```ebnf
top_level_item ::= import_decl | using_decl | static_decl | procedure_decl | comptime_procedure_decl | record_decl | enum_decl | modal_decl | class_declaration | type_alias_decl | extern_block | derive_target_decl

import_decl     ::= attribute_list? visibility? "import" module_path ("as" identifier)?
using_decl      ::= attribute_list? visibility? "using" using_clause
using_clause    ::= module_path "::" identifier ("as" identifier)?
                  | module_path "::" using_list
                  | module_path "::" "*"
using_list      ::= "{" using_specifier ("," using_specifier)* ","? "}"
using_specifier ::= identifier ("as" identifier)?
module_path     ::= identifier ("::" identifier)*

static_decl  ::= attribute_list? visibility? ("let" | "var") binding_decl
binding_decl ::= pattern (":" type)? binding_op expression

visibility ::= "public" | "internal" | "private"
```

**Modules and visibility.** `import` brings a module into scope (optionally aliased). `using` brings names in: a single name, a brace list, or the wildcard `::*` (the style guide restricts `using module::*` to internal/implementation modules — never public API). Always write `visibility` explicitly (`public`, `internal`, `private`); the style guide treats visibility as part of the API contract.

```ebnf
procedure_decl ::= attribute_list? visibility? "procedure" identifier generic_params? signature contract_clause? block_expr
signature      ::= "(" param_list? ")" ("->" return_type)?
param_list     ::= param ("," param)* ","?
param          ::= param_mode? identifier ":" type
param_mode     ::= "move"
return_type    ::= type | union_return
union_return   ::= type ("|" type)+

method_def         ::= attribute_list? visibility? "override"? "procedure" identifier generic_params? "(" receiver ("," param_list)? ")" ("->" return_type)? contract_clause? block_expr
receiver           ::= receiver_shorthand | explicit_receiver
receiver_shorthand ::= "~" | "~!" | "~%"
explicit_receiver  ::= param_mode? "self" ":" type
```

**Procedures and methods.** A `procedure_decl` may carry a generic parameter list and/or a `contract_clause` (B.7). A method's first parameter is a **receiver**: the shorthands `~` (`const` self — shared read), `~!` (`unique` self — mutable), and `~%` (`shared` self), or an explicit `self: Type` (optionally `move self: Type`). `override` marks a method that replaces a class default. Return types may be a single type or a `union_return` `A | B`.

```ebnf
record_decl       ::= attribute_list? visibility? "record" identifier generic_params? implements_clause? "{" record_body "}" type_invariant?
record_body       ::= record_member*
record_member     ::= record_field_decl | method_def
record_field_decl ::= attribute_list? visibility? identifier ":" type record_field_init?
record_field_init ::= "=" expression
field_decl        ::= visibility? identifier ":" type
implements_clause ::= "<:" class_list
class_list        ::= type_path ("," type_path)*

enum_decl       ::= attribute_list? visibility? "enum" identifier generic_params? implements_clause? "{" variant_members? "}" type_invariant?
variant_members ::= variant (terminator variant)* terminator?
variant         ::= identifier variant_payload? ("=" integer_literal)?
variant_payload ::= "(" type_list ")" | "{" field_decl_list "}"
type_list       ::= type ("," type)* ","?
field_decl_list ::= field_decl ("," field_decl)* ","?
```

**Records and enums.** A `record` holds fields (with optional `= default` initializers) and methods, may `<:` implement classes, and may end with a `type_invariant`. An `enum` lists variants separated by terminators; each `variant` may carry a tuple payload `(T, U)` or record payload `{ field: T }` and an explicit discriminant `= 3`.

```ebnf
modal_decl        ::= attribute_list? visibility? "modal" identifier generic_params? implements_clause? "{" state_block+ "}" type_invariant?
state_block       ::= "@" state_name "{" state_member* "}"
state_member      ::= state_field_decl | state_method_def | transition_def
state_field_decl  ::= attribute_list? visibility? identifier ":" type
state_method_def       ::= attribute_list? visibility? "procedure" identifier generic_params? state_method_signature contract_clause? block_expr
state_method_signature ::= "(" receiver ("," param_list)? ")" ("->" type)?
transition_def    ::= attribute_list? visibility? "transition" identifier "(" param_list ")" "->" "@" target_state block_expr
target_state      ::= identifier
```

**Modals.** A `modal` declares one or more `@State { ... }` blocks. Each state block holds state-local fields, state methods, and `transition` definitions. A `transition name(params) -> @TargetState { ... }` consumes the source-state value (the receiver `self` is implicit — a `transition_def` takes only a `param_list`, never a `receiver`) and produces a value in the target state — the preferred way to model lifecycle protocols (style guide). Structural requirements such as `Bitcopy`, `Clone`, `Drop`, `FfiSafe`, and `GpuSafe` are generic class bounds.

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

type_alias_decl ::= attribute_list? visibility? "type" identifier generic_params? "=" type

extern_block      ::= attribute_list? visibility? "extern" abi_string? "{" extern_item* "}"
abi_string        ::= string_literal
extern_item       ::= foreign_procedure
foreign_procedure ::= attribute_list? visibility? "procedure" identifier generic_params? signature contract_clause? foreign_contract_clause_list? terminator
```

**Classes, aliases, extern.** A `class` declares abstract and concrete procedures, abstract fields (optionally key-boundary-marked with `#`), abstract states, and associated types. Superclass bounds join with `+`. `modal class` declares a stateful class. A `type` alias may carry generics. `extern` blocks declare foreign procedures (see B.13).

```ultraviolet
public modal Connection {
    @Closed {
        public host: string

        public transition open(socket: Socket) -> @Open {
            return Connection@Open { host: host, socket: socket }
        }
    }

    @Open {
        public host: string
        public socket: Socket

        public procedure send(~, payload: bytes) -> SendResult {
            return writeAll(socket, payload)
        }

        public transition close() -> @Closed {
            return Connection@Closed { host: host }
        }
    }
}

public enum StatusCode {
    Ok = 0
    Retry(u32)
    Failure { reason: string }
}
```

---

### 29.7 B.7 — Contract Grammar

Contracts attach preconditions and postconditions to procedures (and foreign procedures), and define type/loop invariants. They use the `|:` introducer and the `|=` separator.

```ebnf
contract_clause    ::= "|:" contract_body
contract_body      ::= precondition_expr "|=" postcondition_expr
                     | "|=" postcondition_expr
                     | precondition_expr
precondition_expr  ::= predicate_expr
postcondition_expr ::= predicate_expr
predicate_expr     ::= logical_or_expr
contract_intrinsic ::= decorated_identifier("@", "result")
                     | decorated_identifier("@", "entry") "(" expression ")"

(* The @ spellings are decorated identifiers. Contract intrinsics parse in any
   primary-expression position; `@result` outside a postcondition and `@entry`
   outside a contract predicate are rejected statically
   (E-SEM-2806, E-CON-0415, E-CON-0416). *)

type_invariant ::= "|:" "{" predicate_expr "}"
loop_invariant ::= "|:" "{" predicate_expr "}"
```

**Semantics.** A `contract_clause` is `|:` followed by one of three bodies: precondition-and-postcondition (`pre |= post`), postcondition-only (`|= post`), or precondition-only (`pre`). A `predicate_expr` is any `logical_or_expr` that evaluates to `bool`. The **contract intrinsics** are `@result` (the return value, valid only in a postcondition) and `@entry(expr)` (the value of `expr` at procedure entry, valid only within a contract predicate). `@result` used outside a postcondition is `E-SEM-2806`; `@entry` is constrained by `E-CON-0415` (no capability-requiring operation in `@entry`) and `E-CON-0416` (no side-effecting operation in `@entry`). Both `type_invariant` and `loop_invariant` are the braced form `|: { predicate }`. Contracts are mandatory wherever a machine-checkable rule can be expressed (style guide), especially on public, cross-module, lifecycle, and FFI surfaces.

```ultraviolet
public procedure withdraw(account: Account, amount: u64) -> Account
|: amount > 0u64 && amount <= account.balance
|= @result.balance == @entry(account.balance) - amount {
    return Account { balance: account.balance - amount }
}

public record RingIndex {
    public value: u32
} |: { @result.value < RING_CAPACITY }
```

---

### 29.8 B.8 — Attribute Grammar

Attributes annotate declarations and expressions. The generic `attribute` production parses all of them; the `*_attribute` productions are informative restatements documenting the validated argument shape for each well-known attribute.

```ebnf
attribute_list ::= attribute+
attribute      ::= "#" attribute_spec
attribute_spec ::= attribute_name ("(" attribute_args? ")")?
attribute_name ::= identifier
                 | "dynamic"
                 | "static"
                 | vendor_prefix "::" identifier
                 | vendor_prefix "::" "dynamic"
                 | vendor_prefix "::" "static"
vendor_prefix  ::= identifier ("::" identifier)*
attribute_args ::= attribute_arg ("," attribute_arg)* ","?
attribute_arg  ::= literal
                 | identifier
                 | identifier ":" literal
                 | identifier ":" identifier
                 | identifier "(" attribute_args? ")"

layout_attribute              ::= "#" "layout" "(" layout_args ")"
layout_args                   ::= layout_kind ("," layout_kind)*
layout_kind                   ::= "C" | "packed" | "align" "(" integer_literal ")" | integer_type
inline_attribute              ::= "#" "inline" ("(" inline_mode ")")?
inline_mode                   ::= "always" | "never" | "default"
cold_attribute                ::= "#" "cold"
deprecated_attribute          ::= "#" "deprecated" ("(" string_literal ")")?
reflect_attribute             ::= "#" "reflect"
dynamic_attribute             ::= "#" "dynamic"
stale_ok_attribute            ::= "#" "stale_ok"
emit_attribute                ::= "#" "emit"
files_attribute               ::= "#" "files"
test_attribute                ::= "#" "test" ("(" test_attribute_args ")")?
test_attribute_args           ::= test_attribute_arg ("," test_attribute_arg)*
test_attribute_arg            ::= "name" ":" string_literal | "covers" "(" string_literal ")"
mangle_attribute              ::= "#" "mangle" "(" ("none" | string_literal) ")"
library_attribute             ::= "#" "library" "(" "name" ":" string_literal ("," "kind" ":" string_literal)? ")"
unwind_attribute              ::= "#" "unwind" "(" string_literal ")"
export_attribute              ::= "#" "export" "(" string_literal ")"
host_export_attribute         ::= "#" "host_export" "(" string_literal ")"
ffi_pass_by_value_attribute   ::= "#" "ffi_pass_by_value"
derive_attribute              ::= "#" "derive" "(" derive_target_list ")"
derive_target_list            ::= identifier ("," identifier)*
```

> The `*_attribute` productions above are informative shape restatements: attributes parse through the generic `attribute` grammar, and each owning section validates its argument shape. The same applies to `ffi_verification_attr` and `ffi_verification_mode` in B.13.

**Semantics.** Every attribute begins with `#` and a name (a bare identifier, the reserved `dynamic`/`static`, or a `vendor::scoped` name). Arguments may be literals, identifiers, `key: value` pairs, or nested `name(...)` groups. The `#dynamic` attribute marks a declaration or expression as requiring runtime verification when static verification is insufficient; its scope is lexical and does not propagate through procedure calls. It is ill-formed on a contract clause (`E-CON-0410`), a type-alias declaration (`E-CON-0411`), or a field declaration (`E-CON-0412`). The style guide names the dynamic directive `[[dynamic]]` informally, but the only spelling the grammar accepts is `#dynamic`.

```ultraviolet
#layout(C, align(16))
public record GpuVertex {
    public position: (f32, f32, f32)
    public uv: (f32, f32)
}

#inline(always)
public procedure clampUnit(value: f32) -> f32 |: { @result >= 0.0f32 && @result <= 1.0f32 } {
    if value < 0.0f32 { return 0.0f32 }
    if value > 1.0f32 { return 1.0f32 }
    return value
}

#test(name: "withdraw reduces balance")
procedure testWithdraw() {
    let after: Account = withdraw(Account { balance: 100u64 }, 40u64)
    assertEqual(after.balance, 60u64)
}
```

---

### 29.9 B.9 — Key System Grammar

The key system expresses fine-grained read/write reservations over paths. Key paths, key blocks (`%read`/`%write`/`%release`/`%speculative write`), and memory fences live here.

```ebnf
key_path_expr ::= key_root key_seg*
key_root      ::= identifier
key_seg       ::= "." key_field | "[" key_index "]"
key_field     ::= key_marker? identifier
key_index     ::= key_marker? expression
key_marker    ::= "#"

key_block_stmt ::= key_block_head key_path_list key_options? block_expr
key_block_head ::= decorated_identifier("%", "read")
                 | decorated_identifier("%", "write")
                 | decorated_identifier("%", "release") key_mode
                 | decorated_identifier("%", "speculative") "write"
key_path_list  ::= key_path_expr ("," key_path_expr)*
key_options    ::= "[" key_option ("," key_option)* ","? "]"
key_option     ::= "ordered"

fence_expr  ::= "fence" "(" fence_order ")"
fence_order ::= "acquire" | "release" | "seqcst"
```

> `key_mode` (used by `%release`) is `"read" | "write"`, shared with B.10.

**Semantics.** A `key_path_expr` names a root identifier followed by field (`.name`) and index (`[i]`) segments; a leading `#` marker on a field or index marks a key boundary. A `key_block_stmt` opens a reservation over one or more paths: `%read` (shared read), `%write` (exclusive write), `%release M paths { }` (temporarily releasing to a different inner mode `M`), or `%speculative write` (speculative write). The `[ordered]` option requests ordered acquisition. A `fence(order)` issues a memory fence with `acquire`, `release`, or `seqcst` ordering. Writing inside a `%read` block is `E-CON-0070`; combining `%speculative` with `%release` is `E-CON-0094`; and `%speculative` not followed by `write` is `E-CON-0095`.

```ultraviolet
procedure transferFunds(ledger: Ledger, source: usize, target: usize, amount: u64) {
    %write ledger.accounts[source], ledger.accounts[target] [ordered] {
        ledger.accounts[source].balance -= amount
        ledger.accounts[target].balance += amount
    }
}

procedure readSnapshot(ledger: Ledger, index: usize) -> u64 {
    %read ledger.accounts[index] {
        return ledger.accounts[index].balance
    }
}
```

---

### 29.10 B.10 — Concurrency Grammar

Structured parallelism: `parallel` regions, `spawn` tasks, indexed `dispatch`, and memory-order attributes.

```ebnf
parallel_block       ::= "parallel" domain_expr parallel_option_list? block_expr
domain_expr          ::= expression
parallel_option_list ::= "[" parallel_option ("," parallel_option)* "]"
parallel_option      ::= "cancel" ":" expression
                       | "name" ":" string_literal
                       | "workgroup" ":" dim3_const
                       | "workgroups" ":" dim3_const
dim3_const           ::= "(" expression "," expression "," expression ")"

spawn_expr        ::= "spawn" spawn_option_list? block_expr
spawn_option_list ::= "[" spawn_option ("," spawn_option)* "]"
spawn_option      ::= "name" ":" string_literal
                    | "affinity" ":" expression
                    | "priority" ":" expression

dispatch_expr        ::= "dispatch" pattern "in" range_expression key_clause? dispatch_option_list? block_expr
key_clause           ::= "key" key_path_expr key_mode
key_mode             ::= "read" | "write"
dispatch_option_list ::= "[" dispatch_option ("," dispatch_option)* "]"
dispatch_option      ::= "reduce" ":" reduce_op
                       | "ordered"
                       | "chunk" ":" expression
                       | "workgroup" ":" dim3_const
reduce_op            ::= "+" | "*" | "min" | "max" | "and" | "or" | identifier

memory_order_attribute ::= "#" memory_order
memory_order           ::= "relaxed" | "acquire" | "release" | "acqrel" | "seqcst"
```

**Semantics.** A `parallel domain { }` block runs its body across an execution domain (e.g. `ctx.cpu()`, `ctx.gpu()`), with options for cancellation, naming, and workgroup geometry (`dim3_const` is a 3-tuple). The domain expression must have type `ExecutionDomain` (`E-CON-0102`). `spawn { }` launches a structured task within a `parallel` region (a bare `spawn` outside `parallel` is `E-CON-0101`), with optional name/affinity/priority. `dispatch pat in range { }` runs the body over an index range with an optional `key`-clause reservation, a `reduce:` operator, `ordered`, `chunk:`, and `workgroup:` options. Memory-order attributes (`#relaxed`, `#acquire`, `#release`, `#acqrel`, `#seqcst`) annotate atomic operations.

```ultraviolet
procedure renderTiles(ctx: ExecutionContext, tiles: [Tile]) {
    parallel ctx.cpu() [name: "tile-pass"] {
        dispatch tile_index: usize in 0usize..tiles.length [chunk: 32usize] {
            shadeTile(tiles[tile_index])
        }
    }
}

procedure sumLanes(ctx: ExecutionContext, lanes: [i32]) -> i32 {
    var total: i32 = 0
    dispatch lane: usize in 0usize..lanes.length [reduce: +] {
        total += lanes[lane]
    }
    return total
}
```

---

### 29.11 B.11 — Async Grammar

Asynchronous operations: `wait`, `yield`, `yield from`, `sync`, `race`, and `all`. The async class and async procedure/loop forms are informative restatements of ordinary class/procedure/loop syntax.

```ebnf
(* The following productions are informative shape restatements of the
   built-in Async class declaration and of async procedure and loop usage;
   the normative surface forms are ordinary class, procedure, and loop syntax. *)
async_class  ::= "class" "Async" "<" type_param (";" type_param)* ">"
type_param   ::= identifier ("=" type)?
async_procedure ::= procedure_decl

wait_expr       ::= "wait" expression
yield_expr      ::= "yield" "release"? expression
yield_from_expr ::= "yield" "release"? "from" expression

async_loop ::= "loop" pattern "in" expression block_expr
sync_expr  ::= "sync" expression
race_expr  ::= "race" "{" race_arm ("," race_arm)* ","? "}"
race_arm   ::= expression "->" "|" pattern "|" race_handler
race_handler ::= expression | "yield" expression
all_expr   ::= "all" "{" expression ("," expression)* ","? "}"
```

**Semantics.** `wait expr` awaits an async value. `yield expr` produces a value from an async generator; the optional `release` modifier yields while releasing held resources, and `yield from expr` delegates to another async source. `sync expr` runs an async expression to completion synchronously. `race { a -> |p| handler, ... }` resolves with the first completing arm (each arm binds a pattern and runs a handler, which may itself `yield`). `all { a, b, c }` awaits all listed expressions. The `async_class`, `async_procedure`, and `async_loop` forms are ordinary class/procedure/loop syntax — there is no separate `async` keyword surface. Note the built-in `Async` class type parameters are `;`-separated, matching `generic_param_list`.

```ultraviolet
public procedure fetchFrame(client: Client, frame_id: u64) -> Async<Frame> {
    let response: Response = wait client.request(frame_id)
    return decodeFrame(response)
}

public procedure firstReady(left: Async<Frame>, right: Async<Frame>) -> Frame {
    return race {
        left -> |frame| frame,
        right -> |frame| frame
    }
}

public procedure loadAll(client: Client, ids: [u64]) -> [Frame] {
    return all {
        fetchFrame(client, ids[0]),
        fetchFrame(client, ids[1])
    }
}
```

---

### 29.12 B.12 — Metaprogramming Grammar

Compile-time execution and metaprogramming: `comptime` statements/expressions/`if`/`loop`/procedures, type literals, `quote`/`splice` forms, and derive targets.

```ebnf
comptime_stmt           ::= attribute_list? "comptime" block_expr
comptime_expr           ::= attribute_list? "comptime" "{" expression "}"
comptime_if             ::= "comptime" "if" expression block_expr ("else" (comptime_if | block_expr))?
comptime_loop           ::= "comptime" "loop" pattern (":" type)? "in" expression block_expr
comptime_procedure_decl ::= attribute_list? "comptime" visibility? "procedure" identifier generic_params? signature contract_clause? block_expr

type_literal   ::= "Type" "::" "<" type ">"
quote_expr     ::= "quote" "{" quoted_content "}"
quote_type     ::= "quote" "type" "{" type "}"
quote_pattern  ::= "quote" "pattern" "{" pattern "}"
quoted_content ::= expression | statement | top_level_item
splice_expr    ::= "$" "(" expression ")"
splice_ident   ::= "$" identifier

(* Splice forms parse in any primary-expression position; use outside quoted
   content is rejected statically (E-CTE-0233). Within quoted content,
   `splice_ident` is additionally admitted wherever `identifier` is admitted. *)

derive_target_decl  ::= "derive" "target" identifier "(" "target" ":" "Type" ")" derive_contract_opt block_expr
derive_contract_opt ::= "|:" derive_clause ("," derive_clause)*
derive_clause       ::= "emits" identifier | "requires" identifier
```

**Semantics.** `comptime { }` runs a block at compile time; `comptime { expr }` is the expression form; `comptime if`/`comptime loop` are compile-time control flow; `comptime procedure` declares a compile-time-evaluable procedure (the `comptime` keyword precedes `visibility`). A `type_literal` is `Type::<T>` — a first-class value naming a type. The **quote** forms capture syntax: `quote { ... }` (expressions, statements, or items), `quote type { ... }`, and `quote pattern { ... }`. **Splices** interpolate into quoted content: `$(expr)` or `$ident`; using a splice outside quoted content is `E-CTE-0233`. A `derive target Name(target: Type) |: emits X, requires Y { ... }` defines a derive macro target whose contract lists what it emits and requires; the parameter name must literally be `target` and its type literally `Type`. Quote/splice forms are valid only in compile-time contexts.

```ultraviolet
comptime if TARGET_BITS == 64u32 {
    public type Word = u64
} else {
    public type Word = u32
}

comptime public procedure makeAccessor(field_name: Type) -> CodeFragment {
    return quote {
        public procedure get() -> i32 {
            return self.$(field_name)
        }
    }
}

derive target Serialize(target: Type) |: emits SerializeImpl, requires FieldList {
    return buildSerializeImpl(target)
}
```

---

### 29.13 B.13 — FFI Grammar

The foreign function interface: extern blocks, foreign procedures, FFI verification attributes, and foreign contract clauses.

```ebnf
extern_block                 ::= attribute_list? visibility? "extern" abi_string? "{" extern_item* "}"
abi_string                   ::= string_literal
extern_item                  ::= foreign_procedure
foreign_procedure            ::= attribute_list? visibility? "procedure" identifier generic_params? signature contract_clause? foreign_contract_clause_list? terminator

ffi_verification_attr        ::= "#" ffi_verification_mode
ffi_verification_mode        ::= "static" | "dynamic"
foreign_contract             ::= "|:" decorated_identifier("@", "foreign_assumes") "(" predicate_expr ")"
                               | "|:" decorated_identifier("@", "foreign_ensures") "(" ensures_predicate ")"
ensures_predicate            ::= predicate_expr
                               | decorated_identifier("@", "error") ":" predicate_expr
                               | decorated_identifier("@", "null_result") ":" predicate_expr
foreign_contract_clause_list ::= foreign_contract+
```

**Semantics.** An `extern "ABI" { ... }` block declares foreign procedures with no bodies (the `foreign_procedure` ends with a `terminator`, not a block). Each may carry ordinary contracts plus **foreign contracts**: `|: @foreign_assumes(pred)` documents what the foreign side requires of inputs, and `|: @foreign_ensures(pred)` documents what it guarantees — including the `@error: pred` and `@null_result: pred` ensures variants for failure paths. The verification mode attributes `#static` / `#dynamic` select compile-time vs. runtime contract checking at the FFI boundary (`#static` is interpreted only in foreign-contract contexts). The style guide mandates isolating FFI to thin boundary modules and wrapping foreign handles in safe, contract-bearing APIs.

```ultraviolet
public extern "C" {
    #static
    public procedure uv_open_device(device_index: u32) -> Ptr<RawDevice>@Valid
    |: @foreign_assumes(device_index < MAX_DEVICES)
    |: @foreign_ensures(@null_result: device_index >= ACTIVE_DEVICES)

    public procedure uv_close_device(device: Ptr<RawDevice>@Valid) -> i32
    |: @foreign_ensures(@result == 0i32)
}
```

---

### 29.14 B.14 — Region Grammar

Regions and frames: region statements with options and aliases, Region modal allocation, and frame statements.

```ebnf
region_stmt  ::= "region" region_opts? region_alias? block_expr
region_opts  ::= "(" expression ")"
region_alias ::= "as" identifier
frame_stmt   ::= "frame" block_expr | identifier "." "frame" block_expr
```

**Semantics.** A `region (opts) as name { }` opens a memory region; the optional `(expr)` configures it, and `as name` binds a handle for explicit region-targeted allocation. `new value` allocates `value` within the current scoped region, including anonymous regions and frames. The method call `region_handle~>alloc(value)` allocates `value` within the named region and yields a value of the same type carrying that region's provenance. A `frame { }` statement opens a default frame scope; `handle.frame { }` opens a frame on a specific allocator handle. Regions and frames give scoped, deterministic deallocation without per-object lifetime tracking.

```ultraviolet
procedure buildScene(descriptor: SceneDescriptor) -> SceneSummary {
    region as scene_arena {
        let node_table: NodeTable = new NodeTable { count: 0u32 }
        let node_table_ptr: Ptr<NodeTable>@Valid = &node_table

        frame {
            populateNodes(node_table_ptr, descriptor)
        }

        return summarize(node_table_ptr)
    }
}
```

---

### 29.15 Appendix C — AST Form Index (Quick Map)

Appendix C is informative: it catalogs which specification section *owns* each canonical AST form, so you can jump from a syntactic form to its normative definition. The tables below reproduce that ownership map.

#### 29.15.1 C.1 Item Forms

| AST Item Form      | Canonical Owner                                                         |
| ------------------ | ---------------------------------------------------------------------- |
| `ImportDecl`       | `11.1 Import Declarations`                                              |
| `UsingDecl`        | `11.2 Using Declarations`                                              |
| `ExternBlock`      | `11.4 Extern Block Shell` and Chapter `23` for extern/export semantics |
| `StaticDecl`       | `11.3 Static Declarations`                                             |
| `ProcedureDecl`    | `15.1 Procedure Declarations`                                          |
| `CtProc`           | `22.1 Compile-Time Forms`                                              |
| `DeriveTargetDecl` | `22.5 Derive Targets and Contracts`                                    |
| `RecordDecl`       | `12.6 Records`                                                         |
| `EnumDecl`         | `12.7 Enums`                                                           |
| `ModalDecl`        | `13.1 Modal Declarations`                                              |
| `ClassDecl`        | `14.3 Classes`                                                         |
| `TypeAliasDecl`    | `12.9 Type Aliases`                                                    |

#### 29.15.2 C.2 Type Forms

| AST Type Form                                                                                              | Canonical Owner                                                                               |
| --------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------- |
| `TypePerm`                                                                                                 | `10. Permissions and Binding State`                                                           |
| `TypePrim`                                                                                                 | `12.1 Primitive Types`                                                                        |
| `TypeTuple`                                                                                                | `12.2 Tuples`                                                                                 |
| `TypeArray`                                                                                                | `12.3 Arrays`                                                                                 |
| `TypeSlice`                                                                                                | `12.4 Slices`                                                                                 |
| `TypeRange`, `TypeRangeInclusive`, `TypeRangeFrom`, `TypeRangeTo`, `TypeRangeToInclusive`, `TypeRangeFull` | `12.5 Ranges`                                                                                 |
| `TypeUnion`                                                                                                | `12.8 Union Types`                                                                            |
| `TypePath`, `TypeApply`                                                                                    | owner determined by resolved declaration kind; generic-application rules in `14.1` and `14.2` |
| `TypeFunc`                                                                                                 | `13.10 Function Types`                                                                        |
| `TypeClosure`                                                                                              | `13.11 Closure Types`                                                                         |
| `TypePtr`                                                                                                  | `13.8 Safe Pointer Types`                                                                     |
| `TypeRawPtr`                                                                                               | `13.9 Raw Pointer Types`                                                                      |
| `TypeString`                                                                                               | `13.6 String Types`                                                                           |
| `TypeBytes`                                                                                                | `13.7 Bytes Types`                                                                            |
| `TypeModalState`                                                                                           | `13.1 Modal Declarations`                                                                     |
| `TypeDynamic`                                                                                              | `14.6 Dynamic Class Objects`                                                                  |
| `TypeOpaque`                                                                                               | `14.7 Opaque Types`                                                                           |
| `TypeRefine`                                                                                               | `14.8 Refinement Types`                                                                       |

#### 29.15.3 C.3 Expression, Pattern, and Statement Families

| AST Family                                                                  | Canonical Owner                                  |
| --------------------------------------------------------------------------- | ------------------------------------------------ |
| Non-concurrency expressions other than key blocks                           | `16. Expressions`                                |
| Key-path and key-block expression and statement forms                       | `19. The Key System`                             |
| `ParallelExpr`, `SpawnExpr`, `DispatchExpr`                                  | `20. Structured Parallelism`                     |
| `WaitExpr`, `YieldExpr`, `YieldFromExpr`, `SyncExpr`, `RaceExpr`, `AllExpr` | `21. Asynchronous Operations`                    |
| `CtExpr`, `CtStmt`, `CtIf`, `CtLoopIter`, `Type::<...>`, quote, splice forms | `22. Compile-Time Execution and Metaprogramming` |
| All pattern forms                                                           | `17. Patterns`                                   |
| All statement forms other than key-system-owned statements                  | `18. Statements and Blocks`                      |

---

### 29.16 Cross-Section Token Cautions

These are the easily-confused tokens drawn directly from the productions above. Internalize them — they are the most common sources of code that fails to parse.

- **One-element tuples use a trailing `;`**: the tuple *type* is `(T;)` and the tuple *literal* and *pattern* are `(expr;)` / `(pat;)`. `(T)` is just a parenthesized type, and `()` is the unit type/value.
- **Generic *parameter* lists are `;`-separated** (`<TKey; TValue>`), but generic *argument* lists are `,`-separated (`Map<K, V>`).
- **`|:` is overloaded by position**: it introduces a `contract_clause` (B.7), a `refinement_clause`/`type_invariant`/`loop_invariant` (braced, B.2/B.7), and the foreign-contract clauses (B.13). Read the following token sequence (`{`, the `@foreign_assumes` decorator spelling, or a bare predicate expression) to disambiguate.
- **Raw pointer qualifier precedes the type**: `*imm T` and `*mut T`, not `*T imm`.
- **`Ptr::null()` is the exact null-pointer expression**; `Ptr<T>@Null` is the corresponding type state.
- **`..` family**: `..` exclusive, `..=` inclusive — used in both `range_expression` and `range_pattern`.
- **Receiver shorthands** are exactly `~` (`const` self), `~!` (`unique` self), `~%` (`shared` self); the bound/dynamic call suffix is `~>method(args)`. Transitions take no receiver — their `self` is implicit.
- **Key-block heads carry the leading `%`**: `%read`, `%write`, `%release read|write`, `%speculative write`.
- **The dynamic attribute is `#dynamic`**; the dynamic *type* is `$ClassPath`. The style guide's informal `[[dynamic]]` is not a grammar form.

---

### 29.17 Idioms & Best Practices

- **State belongs in `modal` types, not booleans.** When available fields, allowed operations, or valid transitions differ by lifecycle phase, model the phases as `@State` blocks and move between them with `transition name(...) -> @Target`. The style guide names this the preferred way to model protocols, resource states, sessions, imports, and cooking phases. Order states in lifecycle order, and keep each state's transitions and methods next to the fields they govern.
- **Express machine-checkable rules as contracts and invariants.** If a rule about range, state, ownership, lifetime, authority, or sequencing fits a `predicate_expr`, write it in a `contract_clause`, `type_invariant`, or `loop_invariant` rather than a comment or ad-hoc runtime check. Public, cross-module, lifecycle, and FFI surfaces should be especially strict. Use `@result` and `@entry(...)` to relate post-state to entry-state precisely.
- **Always write visibility explicitly** (`public`, `internal`, `private`) on every declaration the grammar allows it on. Visibility is part of the API contract, not optional decoration.
- **Keep `using module::*` out of public API.** The wildcard `using_clause` form is permitted only in internal/implementation modules; in public-facing code import exact names, and alias only with `using ... as ...` when it removes a real collision or genuinely improves clarity.
- **Use `record` for data, `class` only for shared identity/polymorphism, `modal` for state.** Match the declaration form to the role; do not reach for a `class` where a `record` suffices.
- **Keep `unsafe`, `#dynamic`, and `extern` boundaries thin and local.** Treat them as deliberate boundary tools: wrap unsafe operations and foreign handles in safe APIs that re-establish project invariants, and document ownership, lifetime, and thread affinity at every unsafe boundary.
- **Narrow capabilities.** Pass only the parameters a procedure uses; do not thread "god context" objects. Where several capabilities travel together at a real boundary, define a narrow projected context type.
- **Prefer newline terminators.** The `terminator` is `;` or newline; reserve `;` for the rare justified case of multiple small statements on one line, or where surrounding syntax requires it.
- **Quote/splice only in compile-time contexts.** `quote`, `quote type`, `quote pattern`, and the splice forms are valid only inside `comptime` machinery; design metaprogramming so splices always sit within quoted content.

---

### 29.18 Pitfalls & Diagnostics

The following diagnostics are drawn directly from the spec text accompanying these productions. They are the static errors most directly tied to mis-writing the grammar in this chapter.

- **Contract intrinsics out of place.** `@result` used outside a postcondition is `E-SEM-2806`. Inside `@entry(...)`, a capability-requiring operation is `E-CON-0415` and a side-effecting operation is `E-CON-0416`. Only write `@result` in the postcondition half (`|= ...`) and `@entry(...)` within a contract predicate, keeping its argument pure.
- **Splices outside quoted content.** A `splice_expr` `$(...)` or `splice_ident` `$ident` used outside `quote { ... }`/`quote type`/`quote pattern` content is `E-CTE-0233`.
- **`spawn` outside `parallel`.** A `spawn_expr` that is not lexically inside a `parallel_block` is `E-CON-0101`. The parallel domain expression must have type `ExecutionDomain` (`E-CON-0102`), and parallel/dispatch geometry options must type-check as the right kinds (`E-CON-0103`).
- **Key-block mode and head misuse.** A write inside a `%read` block is `E-CON-0070`. Changing mode in a nested block without a `%release` head is `E-CON-0012`. A `%release` whose target mode equals the outer mode is `E-CON-0018`. Combining `%speculative` and `%release` heads is `E-CON-0094`, and `%speculative` not followed by `write` is `E-CON-0095`.
- **`#dynamic` placement.** `#dynamic` is ill-formed on a contract clause (`E-CON-0410`), a type-alias declaration (`E-CON-0411`), and a field declaration (`E-CON-0412`). When all proofs already succeed statically, `#dynamic` produces only a warning (`W-CON-0401`).
- **Union types in closure positions must be parenthesized.** Inside a `closure_type` parameter list or a typed `closure_param`, a parameter type whose outermost constructor is `union_type` MUST be written `(A | B)`. Omitting the parentheses violates the closure productions.
- **One-element tuple confusion.** Writing `(T)` where you meant a one-element tuple yields a parenthesized type, not a tuple; the tuple form requires the trailing `;` (`(T;)`). The same applies to literals and patterns.
- **Wrong separator in generic parameter lists.** Using `,` instead of `;` inside `generic_params` (`<A, B>` for declarations) does not parse as a parameter list — declarations require `<A; B>`; only `generic_args` at use sites use `,`.
- **Receiver vs. transition.** `~`/`~!`/`~%` are receiver shorthands valid only as the first slot of a `method_def`/`state_method_def`; a `transition_def` accepts only a `param_list` and never a receiver shorthand — its `self` is implicit (`transition close() -> @Closed`).
- **Brace-less `if`.** Every `if` tail uses a braced `block_expr`. There is no `if cond statement` form — write `if cond { statement }`.
- **`[[dynamic]]` is not grammar.** The style guide refers to the dynamic directive as `[[dynamic]]`, but only `#dynamic` parses. Using `#dynamic` to bypass correct static conformance or to paper over weak type modeling is a design defect even where it parses.
