# Guide: Keys, Shared Access, and Memory Ordering

## Load When

Use for `shared` data, key blocks, key paths, nested release, speculative execution, ordered access, memory-order attributes, fences, async-key, or parallel dispatch key behavior.

## Core Model

- `shared` data is synchronized through key acquisition.
- Reads require read keys; writes require write keys.
- Shared method calls use receiver permission to determine key mode.
- Key acquisition uses acquire semantics; release uses release semantics.
- Shared access ordering defaults to `seqcst` unless overridden.

## Source Patterns

```ultraviolet
internal procedure readAndWriteKeyBlockValue() -> i32 {
    var shared_value: shared i32 = 1
    var observed: i32 = 0
    #shared_value read {
        observed = shared_value + 0
    }
    #shared_value write {
        shared_value = observed + 1
    }
    return shared_value + 0
}
```

```ultraviolet
internal procedure fenceOrderingValue() -> i32 {
    var shared_value: shared i32 = 1
    var observed: i32 = 0
    fence(acquire)
    #shared_value write {
        shared_value = 2
    }
    fence(release)
    #shared_value read {
        observed = shared_value + 0
    }
    fence(seqcst)
    return observed
}
```

## Common Fixes

- Add `#path read` around shared reads.
- Add `#path write` around shared mutations or `~%` calls.
- Use nested `release` when a nested operation must temporarily release a held key.
- Do not put memory-order annotations inside speculative blocks.
- Route dispatch key questions to chapter 20 and async-key suspension to chapter 21.

## Related Chapters

- `chapter-10-permissions-and-binding-state.md`
- `chapter-19-key-system.md`
- `chapter-20-structured-parallelism.md`
- `chapter-21-async.md`
