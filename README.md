# Ultraviolet

[![Language Status](https://img.shields.io/badge/status-alpha-orange.svg)](#)
[![License](https://img.shields.io/badge/license-Apache%202.0-blue.svg)](LICENSE.md)

Ultraviolet is a general-purpose systems programming language designed for **explicit authority**, **predictable execution behavior**, and **source code legibility** practical for both machine generation and human review.

The repository contains the language specification, bootstrap compiler, runtime libraries, developer tooling, and the conformance verification suite.

---

## Design Philosophy

Ultraviolet is structured around a small set of foundational rules that eliminate hidden costs and ambient risks:

1. **One Correct Way**: Each semantic operation has exactly one accepted source form, unless alternate syntax changes capability, authority, ownership, synchronization, ABI behavior, or diagnostics.
2. **Local Reasoning**: Mutability, authority, synchronization, movement, suspension, and dynamic checks are fully visible from the local syntax and the referenced procedure or type signature.
3. **Explicit over Implicit**: Source constructs never conceal observable side effects, allocations, memory copy operations, synchronization blocks, suspension points, or unsafe behaviors.
4. **Static by Default**: Strict static compilation checks are the default; dynamic verification, dispatch, allocation, copying, and foreign trust boundaries require explicit opt-in.

---

## Language Highlights & Examples

### 1. Capability-Based Authority (No Ambient Effects)
External side effects (I/O, network, system commands) require an explicit capability handle, typically introduced via the program's `Context` parameter.

```ultraviolet
public procedure main(ctx: Context) -> i32 {
    let io: $IO = ctx.io

    // Explicit dynamic effect invocation using the capability call operator (~>)
    let write_result: Outcome<(), IoError> = io~>write_file(
        "output.txt",
        bytes::view_string("Hello, Ultraviolet!")
    )

    return if write_result is {
        @Value { 0 }
        @Error { 1 }
    }
}
```

### 2. Modal Types (First-Class Typestate)
State machines, connection protocols, and resource lifecycles are modeled directly using state-specific fields, procedures, and transitions.

```ultraviolet
public modal Connection {
    @Closed {
        public transition connect(address: string@View) -> @Connected {
            // Transitions connection into the @Connected state
            return Connection@Connected { address: address }
        }
    }

    @Connected {
        public address: string@View

        public procedure send(~, data: bytes@View) -> bool {
            // Sending is only valid and accessible in the @Connected state
            return true
        }

        public transition disconnect() -> @Closed {
            return Connection@Closed {}
        }
    }
}
```

### 3. Scoped Memory (Arena & Frame Allocators)
Arena memory management is built directly into the language surface. Frames allow cheap, nested allocations that are automatically reclaimed upon scope exit.

```ultraviolet
public procedure processData() -> i32 {
    var sum: i32 = 0
    region as scratch {
        // Allocate a value directly into the scratch region arena
        let temp_array: Ptr<[i32; 3]> = scratch ^ [1, 2, 3]

        // Scope-bound frame block allocates on a stack-like structure
        frame {
            let local_val: i32 = ^10
            sum = local_val + temp_array[0]
        } // Frame allocation is instantly reclaimed here
    } // Entire scratch region is reclaimed here
    return sum
}
```

### 4. Concurrency
Shared memory across execution domains is governed by a **Static Key System** built directly into Ultraviolet's type system. Rather than relying on runtime mutexes or unsafe manual synchronization, shared variables (`shared T`) require static key acquisition.

* **Key Propagation**: Passing a `shared` parameter propagates key authority up the call stack, avoiding inline block overhead.
* **Key Acquisition**: Precise access boundaries can be declared using `%read` and `%write` blocks.
* **Structured Concurrency**: Spawning asynchronously is managed with the `spawn` and `wait` primitives within execution domains (e.g. `parallel context~>cpu()`), and loops are parallelized using `dispatch` blocks.
* **Async Task States**: Async tasks are compiled into modal state machines with explicit, zero-cost suspension and resumption.

```ultraviolet
// Key authority is propagated statically via the 'shared' parameter signature
public procedure worker(counter: shared i32) -> i32 {
    var observed: i32 = 0
    %write counter {
        counter = counter + 1
        observed = counter
    }
    return observed
}

public procedure runConcurrency(context: Context) -> i32 {
    var counter: shared i32 = 0

    // Structured parallel execution domain (CPU threadpool)
    return parallel context~>cpu() {
        // Spawn concurrent tasks
        let task_a: Spawned<i32> = spawn [name: "worker-a"] {
            worker(counter)
        }
        let task_b: Spawned<i32> = spawn [name: "worker-b"] {
            worker(counter)
        }

        // Statically structured await
        (wait task_a) + (wait task_b)
    }
}
```

---

## Project Organization

The repository is organized into three major sections:

* [Docs/](Docs/): Contains the formal [Language Specification](Docs/SPECIFICATION.md), the source of truth for syntax, static/dynamic semantics, and ABI rules.
* [Bootstrap/Ultraviolet/](Bootstrap/Ultraviolet/): The C++ implementation of the bootstrap compiler, runtime libraries, and linker integrations.
* [HelloUltraviolet/](HelloUltraviolet/): A standalone conformance corpus and test reference surface containing physical fixtures, rejected/accepted specimens, and diagnostic audits.

---

## Building the Compiler from Source

The bootstrap compiler is written in C++ and built using CMake.

### Prerequisites
* **Windows**: Visual Studio 2022 (with C++ support) & CMake 3.25+
* **Linux**: GCC/Clang, Make or Ninja, & CMake 3.25+

### Build Steps

1. Configure the project:
   ```bash
   cd Bootstrap/Ultraviolet
   cmake --preset windows-release  # Use 'linux-release' on Linux
   ```

2. Build and stage the release package:
   ```bash
   cmake --build --preset windows-release-package  # Use 'linux-release-package' on Linux
   ```

Upon completion, the compiled command-line compiler is staged as `uv.exe` (Windows) or `uv` (Linux) under the preset build directory:
* Windows: `Bootstrap/Ultraviolet/build/windows/out/uv.exe`
* Linux: `Bootstrap/Ultraviolet/build/linux/out/uv`

---

## Getting Started with Ultraviolet Projects

Once the `uv` compiler is on your path, you can create and build projects.

### 1. Initialize a Project
Create a new directory and initialize a minimal Ultraviolet project structure:
```bash
uv init my_project
cd my_project
```
This generates the standard layout:
```text
Ultraviolet.toml
Source/
  Main.uv
```

### 2. Build the Project
To compile your project, you must explicitly specify a **Target Profile** using the `--target-profile` CLI flag (or configure it in the `Ultraviolet.toml` manifest under `[toolchain].target_profile`):

```bash
uv build --target-profile x86_64-win64  # Target Profile Options: x86_64-win64, x86_64-sysv, aarch64-aapcs64
```

### 3. Run Project Tests
Ultraviolet supports native unit tests defined using `#test` blocks inside your code. Run them using:
```bash
uv test --target-profile x86_64-win64
```

---

## Conformance & Conformance Testing

The [HelloUltraviolet](HelloUltraviolet/) directory is the release conformance surface for compiler and runtime behaviors. It acts as an integration-level gate before compiler updates are merged.

Run the full conformance suite using:
```bash
# Build the conformance suite
uv build HelloUltraviolet --target-profile x86_64-win64

# Run the test suite (from the repository root directory)
./Build/Binary/HelloUltraviolet.exe
./Build/Binary/HelloUltraviolet.exe --audit
```

---

## License

Ultraviolet is licensed under the Apache License, Version 2.0. See [LICENSE.md](LICENSE.md) for details.
