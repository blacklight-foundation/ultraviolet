# Chapter 19: Key System

## Load When

Use for shared access, key paths, key acquisition blocks, conflict detection, nested release, speculative execution, dynamic key verification, and memory ordering.

## Authoring Rules

- Use key blocks for `shared` reads and writes.
- Use read keys for shared reads and const receiver calls.
- Use write keys for shared mutation and shared receiver calls.
- Use release when nested logic must temporarily release a held key.
- Use memory-order attributes only on key blocks or shared-access expressions.

## Syntax Forms

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
internal procedure keyBlockDefaultMemoryOrderingValue() -> i32 {
    var shared_value: shared i32 = 1
    var observed: i32 = 0
    [[relaxed]]
    #shared_value read {
        observed = observed + shared_value
    }
    [[acquire]]
    #shared_value read {
        observed = observed + shared_value
    }
    [[release]]
    #shared_value write {
        shared_value = 2
    }
    [[acqrel]]
    #shared_value read {
        observed = observed + shared_value
    }
    [[seqcst]]
    #shared_value write {
        shared_value = 3
    }
    fence(seqcst)
    return observed
}
```

## Static Semantics

Key paths identify synchronized shared storage. Conflict detection rejects incompatible held keys. Dynamic verification covers paths not statically settled.

## Runtime and Lowering Notes

Key acquisition uses acquire semantics and key release uses release semantics. Effective memory ordering is expression attribute, then key-block default, then `seqcst`.

## Diagnostics

Use for malformed key path, missing key, conflicting key modes, invalid release, invalid speculative memory ordering, and dynamic verification failures.

## Reference Corpus

- `HelloUltraviolet/Source/Reference/Keys/**`

## Spec Fallback

Open `SPECIFICATION.md` chapter `19` for key and memory ordering rules.
