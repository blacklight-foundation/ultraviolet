# Guide: Regions, Frames, and Provenance

## Load When

Use for scoped memory, arena allocation, `region`, `frame`, `^`, provenance tags, lifetime escape checks, and region runtime methods.

## Core Model

- `region` creates a scoped active region.
- `frame` creates a reset scope within the nearest or named active region.
- `^value` allocates in the innermost active region.
- `scratch ^ value` allocates in a named active region.
- Provenance prevents shorter-lived data from escaping into longer-lived locations.

## Source Patterns

```ultraviolet
internal procedure frameAllocationValue() -> i32 {
    var value: i32 = 0
    region as scratch {
        frame {
            let implicit_value: i32 = ^29
            if implicit_value == 29
                value = value + 1
        }

        scratch.frame {
            let explicit_value: i32 = scratch ^ 31
            if explicit_value == 31
                value = value + 2
        }
    }
    return value
}
```

```ultraviolet
internal procedure scopedRegionValue() -> i32 {
    let options: RegionOptions =
        RegionOptions { stack_size: 0usize, name: "scratch" }
    let opened: unique Region@Active = Region::new_scoped(options)
    let allocated: i32 = opened~>alloc(19)
    let freed: Region@Freed = unsafe { opened~>free_unchecked() }
    return allocated
}
```

## Common Fixes

- Add a surrounding `region` before using `^`.
- Use `scratch.frame` when the frame must target a specific region.
- Keep values allocated in an inner region from escaping to outer state.
- Wrap `free_unchecked` in `unsafe` and keep the unsafe boundary local.

## Related Chapters

- `chapter-06-abstract-machine-responsibility-authority-memory.md`
- `chapter-13-modal-and-special-types.md`
- `chapter-18-statements-and-blocks.md`
- `chapter-24-lowering-lifecycle-and-backend.md`
