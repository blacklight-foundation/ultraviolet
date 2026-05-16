# Chapter 21: Async

## Load When

Use for async type, suspension forms, composition forms, async state machines, and async-key integration.

## Authoring Rules

- Use async forms when suspension is semantically part of the operation.
- Release held keys before suspension when the async-key rules require it.
- Use `sync`, `race`, and `all` for composition rather than ad hoc polling.
- Keep cancellation behavior explicit through the relevant token/state.

## Syntax Forms

```ultraviolet
public procedure asyncYieldReleaseReference(value: i32) -> AsyncReferenceComputation {
    let resumed: i32 = yield release value
    return resumed
}

public procedure asyncYieldReleaseFromReference(value: i32) -> AsyncReferenceComputation {
    let resumed: i32 = yield release from asyncSuspendsReference(value)
    return resumed + 1
}
```

```ultraviolet
internal procedure asyncCompositionOutcome(should_fail: bool, value: i32) -> i32 | bool {
    if should_fail {
        return true
    }
    return value
}

internal procedure asyncCompositionMayFail(
    should_fail: bool,
    value: i32
) -> Async<(), (), i32, bool> {
    let result: i32 = asyncCompositionOutcome(should_fail, value)?
    return result
}

public procedure runAsyncCompositionReference() -> bool {
    let all_success: (i32, i32) | bool = all {
        asyncCompositionMayFail(false, 34),
        asyncCompositionMayFail(false, 35),
    }
    let race_success: i32 | bool = race {
        asyncCompositionMayFail(false, 38) -> |value| value + 1,
        asyncCompositionMayFail(false, 40) -> |value| value + 1,
    }
    let all_ok: bool = if all_success is {
        pair: (i32, i32) {
            pair.0 == 34 && pair.1 == 35
        }
        failed: bool {
            false
        }
    }
    let race_ok: bool = if race_success is {
        value: i32 {
            value == 39
        }
        failed: bool {
            false
        }
    }
    return all_ok && race_ok
}
```

## Static Semantics

Async expressions and procedures check suspension points, key-holding state, result types, cancellation behavior, and state-machine validity.

## Runtime and Lowering Notes

Async lowers to state machines with explicit suspension/resume state and key release integration.

## Diagnostics

Use for invalid suspension context, held-key suspension errors, bad composition result, invalid state machine, and cancellation misuse.

## Reference Corpus

- `HelloUltraviolet/Source/Reference/Async/**`

## Spec Fallback

Open `references/SPECIFICATION.md` chapter `21` for async rules.
