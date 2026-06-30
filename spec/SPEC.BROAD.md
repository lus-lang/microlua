Here is a finalized, comprehensive specification for the MicroLua runtime. This document integrates the "Smallest Possible" compiler architecture and adheres to the strict naming conventions you requested.

### Compiler Architecture Review: The "Zero-Overhead" Strategy

To achieve the smallest footprint, we cannot afford the luxury of distinct compilation stages (Tokenize AST Optimize Emit). We must use a **Single-Pass, Syntax-Directed Translation** architecture.

**1. The Lazy "Pull" Lexer**
The tokenizer should not allocate memory or create a list of token objects. Instead, it functions as a stateful cursor over the source buffer.

- **Mechanism:** The parser calls `microlua_lex_next()`. The lexer skips whitespace/comments and identifies the next token.
- **Zero-Copy:** Identifiers and string literals are not copied. The token returned is simply a struct containing a `type` and a `const char*` pointer into the source code with a `length`. Memory is only allocated if the parser decides this token is a new string constant that must be interned.
- **Keyword Detection:** To avoid large lookup tables, keywords are detected using a "First-Character Switch" followed by a `memcmp`. This is denser than a perfect hash table for small keyword sets.

**2. The Pratt Parser (Expression Engine)**
Recursive descent is excellent for statements (`if`, `while`), but terrible for expressions (`1 + 2 * 3`) because it requires a unique function call for every precedence level, consuming valuable C stack space.

- **Solution:** We use **Pratt Parsing** (Top-Down Operator Precedence) for expressions. A single function, `microlua_parse_expr(precedence)`, loops over tokens. It uses a small lookup table to determine infix and prefix binding powers. This flattens the recursion depth significantly.

**3. On-the-Fly Codegen with Backpatching**
We do not build an Abstract Syntax Tree (AST). Bytecode is emitted immediately as syntax is recognized.

- **Control Flow:** For `if` or `while` loops, the compiler emits a "Jump" instruction with a placeholder offset (0xFFFF). It records the index of this instruction on a simple compiler-internal stack. Once the block is finished and the destination address is known, the compiler seeks back to the placeholder and "patches" the correct jump offset.
- **Function Compilation:** When a `function` keyword is encountered, the compiler recursively invokes itself. It creates a new `Proto` (function prototype) object and pauses compilation of the current function. Once the inner function is done, it is finalized, and a `closure` instruction is emitted into the outer function's bytecode.

---

# MicroLua 1.0 Specification

## 1. Introduction and Philosophy

MicroLua is a strictly embedded scripting runtime derived from Lua 5.1. It is engineered for environments where kilobytes of RAM are a luxury. The primary design goals are determinism, minimal memory footprint, and implementation simplicity. Unlike standard Lua, which balances performance with flexibility, MicroLua prioritizes predictability and size. It removes dynamic features that incur hidden costs (such as metatables and sparse arrays) and replaces them with explicit, optimized alternatives.

## 2. Memory Model

### 2.1 The Moving Heap

The core of MicroLua's efficiency is its memory management. The runtime operates on a single contiguous block of memory managed by a Mark-Compact garbage collector. Unlike traditional allocators that leave gaps (fragmentation) when objects are freed, the MicroLua collector moves live objects together to ensure that all free space is always at the end of the heap. This allows for an extremely fast "bump pointer" allocation strategy, where allocating memory is as simple as incrementing a pointer.

### 2.2 Value Representation

MicroLua uses alignment-based tagging for values. Every value in the system—whether a number, a boolean, or a reference—is pointer-sized (32 bits on 32-bit platforms, 64 bits on 64-bit platforms).

Heap objects are 8-byte aligned, leaving the low 3 bits of pointers always zero. These bits are used for type tagging. Pointers are stored as absolute memory addresses, allowing unlimited heap sizes. The 3-bit tag identifies the value type (pointer, integer, special, short string, or light function). This packed representation eliminates the need for separate "type" and "value" fields.

### 2.3 Object Storage

Heap objects (tables, strings, functions) are prefixed with a compact variable-length header. The header contains essential metadata: the object's type, its garbage collection status (marked, pinned, or read-only), and its size.

Strings are stored as UTF-8 byte sequences. Strings are interned; only one copy of any given string exists in the heap, allowing string equality checks to be performed by a single pointer comparison.

## 3. The Language

MicroLua implements a strict subset of Lua 5.1. The syntax is identical, but the semantics are tightened to prevent common embedded pitfalls.

### 3.1 Data Structures

The primary data structure remains the Table, but its behavior is bifurcated into a strict Array part and a Hash part. The Array part behaves like a C vector: it must be contiguous. Creating "holes" in a sequence (e.g., assigning index 1 and then index 3 without index 2) is a runtime error. This constraint allows the array part to be implemented as a simple memory block without complex bounds-checking logic for iterations. The Hash part behaves like a standard dictionary for key-value storage.

### 3.2 Object Orientation

The metatable system, a hallmark of standard Lua, is removed to reduce the complexity of the virtual machine. In its place, MicroLua introduces a prototype delegation mechanism via `table.forward`. A table can be explicitly linked to a "forwarding" table. If a lookup for a key fails in the primary table, the runtime automatically searches the forwarding table. This preserves the syntactic sugar of Lua's colon notation (`obj:method()`) and prototype-based inheritance while eliminating the overhead of checking for operator overloading metamethods (like `__add` or `__call`) on every instruction.

### 3.3 Functions and Closures

Lua closures are fully supported. Functions can capture variables from their lexically enclosing scope (upvalues). Unlike standard Lua, C functions are strictly "light": they are raw function pointers and cannot hold upvalues. This simplifies the C API and reduces the memory overhead for registering native callbacks.

## 4. The C API

The MicroLua C interface is designed to be safe in the presence of a moving garbage collector while avoiding dependencies on the standard C library (libc).

### 4.1 Naming and Style

All public API functions and types are prefixed with `MLua` and use PascalCase. For example, the state structure is `MLuaState`, and the function to push a nil value is `MLuaPushNil`.

### 4.2 The GC Reference System

Because the garbage collector may move objects during any allocation, C pointers to heap objects can become invalid without warning. To solve this, MicroLua uses a reference system. C code must not hold raw pointers to internal objects across API calls that might allocate memory. Instead, C code uses `MLuaGCRef`, an opaque handle.

When a C function needs to keep an object alive or accessible, it "pushes" the reference onto a shadow stack using `MLuaPushGCRef`. The runtime ensures that the object pointed to by this reference is updated if it moves. When the reference is no longer needed, it is released with `MLuaPopGCRef`. This explicit management ensures memory safety without the overhead of a global handle table.

### 4.3 Error Handling

The runtime avoids the heavy stack usage of `setjmp` and `longjmp`. Error handling is implemented via return code propagation. Every internal VM function returns a status code. When an error occurs (e.g., type mismatch), the VM halts the current instruction, unwinds the stack frame, and propagates the error code up to the nearest protected call (`pcall`) or to the top-level panic handler.

## 5. The Compilation Pipeline

Compiling source code into bytecode is performed by a streamlined, single-pass translation engine designed to run directly on the embedded target.

### 5.1 The Lexer

The lexer is a lazy, zero-copy state machine. It does not generate a list of token objects. When the parser requests the next token, the lexer reads from the source buffer, skipping whitespace and comments, and returns a transient structure pointing to the start and end of the lexeme in the source. String allocation only occurs when absolutely necessary, such as when finalizing a string literal for the constant table.

### 5.2 The Parser

The parser utilizes a hybrid approach to balance stack usage and code size. Statement parsing (blocks, `if`, `for`) is handled via Recursive Descent, which maps naturally to the grammar's structure. Expression parsing is handled by a Pratt Parser. This approach uses a lookup table for operator precedence and associativity, allowing complex expressions to be parsed iteratively. This prevents stack overflow errors when parsing deeply nested mathematical formulas, a common issue in simple recursive descent parsers.

### 5.3 Code Generation

Bytecode is emitted in the same pass as parsing. The compiler maintains a "backpatching" stack to handle control flow. When a jump instruction is generated (e.g., at the start of an `else` block), its destination is unknown. The compiler emits a placeholder and pushes the instruction's location onto a stack. Once the destination is reached (e.g., the `end` keyword), the compiler seeks back to the placeholder and writes the correct offset.

### 5.4 Bytecode Format

The virtual machine executes a custom stack-based instruction set. Instructions are variable-length to maximize code density. Common operations like `ADD` or `RETURN` are encoded as single bytes. Operations requiring operands, such as loading a constant or jumping, use subsequent bytes for data. This format is significantly more compact than Lua's standard 4-byte register-based instructions, reducing the RAM required to load programs.

## 6. Execution Model

The runtime executes bytecode using a tight interpreter loop. To support executing code directly from Read-Only Memory (ROM), the bytecode loader differentiates between RAM-based and ROM-based functions. If a function is flagged as ROM-resident, the garbage collector ignores it during the mark phase, and the interpreter reads instructions directly from flash memory, bypassing the need to load the program into limited RAM.
