# Guide: Async, Parallelism, and Cancellation

## Load When

Use for structured parallelism, execution domains, spawn, dispatch, capture, cancellation, panic handling, async suspension, async composition, and async-key integration.

## Core Model

- Parallel and async forms are structured, scoped, and key-aware.
- Captures must preserve permission and authority rules.
- Shared data access in parallel or async code still uses keys.
- Suspension interacts with held-key state.
- Cancellation is a resource/lifecycle concern, usually represented through modal token state.

## Source Patterns

```ultraviolet
internal procedure nestedSpawnReference(context: Context) -> i32 {
    return parallel context~>inline() {
        let first: Spawned<i32> = spawn {
            10
        }
        let second: Spawned<i32> = spawn {
            11
        }
        (wait first) + (wait second)
    }
}
```

```ultraviolet
internal procedure keyedDispatchReference(context: Context) -> i32 {
    let partition_key: shared i32 = 7
    return parallel context~>inline() {
        dispatch index in 0usize..4usize key partition_key read [reduce: +, ordered] {
            index as i32
        }
    }
}
```

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

## Common Fixes

- Add explicit key mode to dispatch that touches shared state.
- Release keys before suspension when async-key rules require it.
- Keep capabilities captured by parallel bodies explicit.
- Use structured composition forms instead of ad hoc task ownership.

## Related Chapters

- `chapter-19-key-system.md`
- `chapter-20-structured-parallelism.md`
- `chapter-21-async.md`
