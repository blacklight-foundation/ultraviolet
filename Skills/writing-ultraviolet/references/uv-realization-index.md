# Ultraviolet Realization Index

This file gives syntax scaffolds with holes. Text inside `<<...>>` is template
notation to fill before producing UV source. Use `scripts/spec_search.py` for
the exact grammar or builtin signature behind any form.

## Procedure Shell

Cards: Form, Flow, Envelope.

```text
<<visibility>> procedure <<name>>(<<parameters>>) -> <<ReturnType>> {
    <<local_bindings>>
    <<behavior>>
    return <<result>>
}
```

Fill order:

1. Choose visibility and procedure name.
2. Bind only the parameters and capabilities used by the body.
3. Fill local bindings.
4. Compose behavior.
5. End non-`unit` bodies with a procedure-level `return`.

Spec search terms: `procedure_decl`, `ExplicitReturn`, `parameter`, `return`.

## Local Binding

Cards: Form, Envelope, Change.

```text
let <<name>>: <<Type>> = <<expression>>
var <<name>>: <<Type>> = <<expression>>
```

Fill order:

1. Pick `let` for stable bindings and `var` for values updated in the owning
   block.
2. Write the narrow type and permission.
3. Initialize from an in-scope expression.

Spec search terms: `let`, `var`, `binding`, `permission`.

## Record Shape

Cards: Form, Access, Envelope.

```text
<<visibility>> record <<TypeName>> {
    <<field_visibility>> <<field_name>>: <<FieldType>>
}

let <<value_name>>: <<TypeName>> = <<TypeName>> {
    <<field_name>>: <<field_expression>>
}
```

Fill order:

1. Declare the data fields that carry the invariant.
2. Construct with every required field named.
3. Use field access when deconstructing a known record value.

Spec search terms: `record_decl`, `record_literal`, `field`, `record_body`.

## Enum Choice Shape

Cards: Form, Access, Flow.

```text
<<visibility>> enum <<TypeName>> {
    <<Variant>>
    <<VariantWithPayload>>(<<PayloadType>>)
    <<RecordVariant>> { <<field_name>>: <<FieldType>> }
}

return if <<value>> is {
    <<TypeName>>::<<Variant>> {
        <<result_expression>>
    }
    <<TypeName>>::<<VariantWithPayload>>(<<payload_name>>) {
        <<result_expression>>
    }
    <<TypeName>>::<<RecordVariant>> { <<field_name>>: <<binding_name>> } {
        <<result_expression>>
    }
}
```

Fill order:

1. Name variants by domain states or cases.
2. Bind payloads only inside the matching branch.
3. Return a common result shape from every branch.

Spec search terms: `enum_decl`, `variant`, `if_expr`, `if_case`.

## Modal Lifecycle Shape

Cards: Form, Change, Flow, Envelope.

```text
<<visibility>> modal <<Name>> {
    @<<InitialState>> {
        <<fields_or_methods>>

        <<visibility>> transition <<transition_name>>(<<parameters>>) -> @<<NextState>> {
            return <<Name>>@<<NextState>> {
                <<state_field>>: <<expression>>
            }
        }
    }

    @<<NextState>> {
        <<visibility>> <<state_field>>: <<FieldType>>

        <<visibility>> procedure <<method_name>>(~) -> <<ReturnType>> {
            return self.<<state_field>>
        }
    }
}
```

Fill order:

1. Name states by lifecycle reality.
2. Put state-specific fields in the state that owns them.
3. Return the next concrete state from transitions.
4. Keep effect authority in the transition or method that performs the effect.

Spec search terms: `modal_decl`, `state_block`, `transition`, `ModalSelfType`.

## Conditional Choice

Cards: Flow, Form, Envelope.

```text
if <<condition>> {
    <<selected_behavior>>
} else {
    <<alternate_behavior>>
}
```

Expression form:

```text
let <<result>>: <<Type>> = if <<condition>> {
    <<value_when_true>>
} else {
    <<value_when_false>>
}
```

Fill order:

1. Put the decision predicate in `<<condition>>`.
2. Fill both branches with the same statement or expression obligation.
3. Use expression form when the branch result feeds another pattern.

Spec search terms: `if_expr`, `if_tail`, `block_expr`.

## Pattern Choice

Cards: Access, Flow, Envelope.

```text
if <<subject>> is <<Pattern>> {
    <<behavior_using_pattern_bindings>>
} else {
    <<fallback_behavior>>
}
```

Multi-case expression:

```text
return if <<subject>> is {
    <<PatternA>> {
        <<result_a>>
    }
    <<PatternB>> {
        <<result_b>>
    }
    else {
        <<fallback_result>>
    }
}
```

Fill order:

1. Choose the subject value.
2. Choose each pattern and binding name.
3. Keep pattern bindings inside the case body.
4. Return a common result shape.

Spec search terms: `if_case_pattern`, `pattern`, `if_case`, `case`.

## Repeat Shape

Cards: Flow, Change, Envelope.

Predicate loop:

```text
loop <<condition>> <<optional: |: { <<invariant>> }>> {
    <<iterated_behavior>>
}
```

Iterator loop:

```text
loop <<pattern>><<optional: : <<Type>>>> in <<source>> <<optional: |: { <<invariant>> }>> {
    <<iterated_behavior>>
}
```

Loop expression:

```text
let <<result>>: <<ResultType>> = loop <<optional_condition>> {
    if <<done_condition>> {
        break <<result_expression>>
    }

    <<progress_behavior>>
}
```

Fill order:

1. Choose predicate loop, iterator loop, or loop expression.
2. Bind the focus value or condition.
3. Fill progress behavior.
4. Add invariant when it expresses the preserved correctness rule.
5. Use `break value` when the loop expression produces a result.

Spec search terms: `loop_expr`, `loop_condition`, `loop_invariant`, `break`.

## Accumulator Shape

Cards: Change, Flow, Form.

```text
var <<accumulator>>: <<AccumulatorType>> = <<initial_value>>

loop <<focus_pattern>> in <<source>> {
    <<accumulator>> = <<combine_expression>>
}

return <<finish_expression>>
```

Fill order:

1. Choose the accumulator type that represents the final result.
2. Initialize before the loop.
3. Update inside the loop.
4. Return the final expression at procedure body level.

Spec search terms: `assignment`, `loop_expr`, `return`, `var`.

## Method Call Shape

Cards: Form, Access, Envelope.

```text
let <<result>>: <<ResultType>> = <<receiver>>~><<method_name>>(<<arguments>>)
```

Fill order:

1. Bind the receiver with the permission required by the method.
2. Pass only the arguments and capabilities the method declares.
3. Bind or return the result according to the caller's next pattern.

Spec search terms: `method call`, `receiver`, `CallExpr`, `postfix_expr`.

## String View Shape

Cards: Form, Access.

```text
let <<length_name>>: usize = string::length(<<text_view>>)
let <<slice_name>>: string@View = string::slice(<<text_view>>, <<start>>, <<end>>)
```

String builtins use ordinary call and method-call rules.

Spec search terms: `StringBuiltinSig`, `string::length`, `string::slice`,
`StringBuiltins`.

## Bytes Inspection Shape

Cards: Access, Flow, Change.

```text
let <<bytes_view>>: bytes@View = bytes::view_string(<<text_view>>)
let <<data>>: const [u8] = bytes::as_slice(<<bytes_view>>)
let <<byte_value>>: u8 = <<data>>[<<index>>]
```

Fill order:

1. Convert text view to a bytes view.
2. Convert bytes view to a const byte slice.
3. Index the slice with a `usize` index in range.

Spec search terms: `BytesBuiltinSig`, `bytes::view_string`, `bytes::as_slice`,
`index_expr`, `E-UNS-0102`.

## Dynamic Verification Shape

Cards: Envelope, Flow, Change.

```text
[[dynamic]]
<<visibility>> procedure <<name>>(<<parameters>>) -> <<ReturnType>> {
    <<behavior_that_requires_runtime_verification>>
    return <<result>>
}
```

Fill order:

1. Place `[[dynamic]]` on the smallest declaration or expression that owns the
   runtime-verified operation.
2. Keep the runtime-verified behavior inside that lexical scope.
3. Preserve the same return obligations as ordinary procedures.

Spec search terms: `[[dynamic]]`, `dynamic scope`, `E-UNS-0102`.

## Effect Boundary Shape

Cards: Envelope, Form, Flow.

```text
<<visibility>> procedure <<name>>(<<capability>>: <<CapabilityType>>, <<input>>: <<InputType>>) -> <<OutcomeType>> {
    let <<outcome>>: <<OutcomeType>> = <<capability>>~><<operation>>(<<input>>)
    return <<outcome>>
}
```

Fill order:

1. Put the capability in the signature.
2. Perform the effect through the capability or context receiver.
3. Return the success or failure value defined by the API.

Spec search terms: `capability`, `context`, `effect`, `method call`.

## Unsafe Wrapper Shape

Cards: Envelope, Flow, Form.

```text
<<visibility>> procedure <<safe_name>>(<<parameters>>) -> <<ReturnType>> {
    <<validate_or_bind_inputs>>

    unsafe {
        <<boundary_operation>>
    }

    return <<safe_result>>
}
```

Fill order:

1. Put the public contract on the safe procedure.
2. Keep boundary behavior local to the block.
3. Return a value whose type re-establishes the caller-facing invariant.

Spec search terms: `unsafe`, `ffi`, `foreign`, `contract`.

## Composition Checklist

- Construct outputs feed Deconstruct, Transform, Remember, and Effect inputs.
- Deconstruct bindings feed Choose, Transform, and Accumulate cases.
- Choose branches feed Construct, Effect, Transform, or explicit return.
- Repeat bodies commonly compose Deconstruct, Choose, Transform, and Accumulate.
- Effect outputs usually feed Choose, Remember, or Transform.
- Coordinate joins feed Accumulate, Construct, or Remember.
