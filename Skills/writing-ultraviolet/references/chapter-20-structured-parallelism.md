# Chapter 20: Structured Parallelism

## Load When

Use for parallel blocks, execution domains, capture semantics, spawn, dispatch, cancellation, panic handling, determinism, and nesting.

## Authoring Rules

- Use structured parallel constructs for bounded concurrent work.
- Make captured capabilities, shared data, and keys explicit.
- Use dispatch for partitioned indexed work and specify key mode where shared state is accessed.
- Preserve determinism where the construct requires ordered or reduced results.

## Syntax Forms

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

## Static Semantics

Parallel bodies check capture permissions, key requirements, cancellation tokens, result reduction, and nested parallelism constraints.

## Runtime and Lowering Notes

Parallel constructs lower to structured tasks with cancellation, panic propagation, and deterministic joining rules.

## Diagnostics

Use for invalid domain, capture violation, missing key, invalid dispatch range, invalid reduction, cancellation misuse, and nondeterministic nesting.

## Reference Corpus

- `HelloUltraviolet/Source/Reference/Parallelism/**`

## Spec Fallback

Open `SPECIFICATION.md` chapter `20` for structured parallelism.
