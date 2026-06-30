This implementation specification provides the concrete data structures, bytecode instruction set, and algorithmic details required to build **MicroLua 1.0**.

### 1. Data Representation & Memory Layout

MicroLua operates on a single contiguous heap managed by a bump-pointer allocator. Values use platform-specific encoding.

#### 1.1 `MLuaValue` (Platform-Sized)

```c
typedef UPtr MLuaValue;  /* Pointer-sized unsigned integer */
```

**On 64-bit Platforms: NaN-Boxing (LuaJIT-style)**

Doubles are stored directly with full 64-bit precision. Other values are encoded as quiet NaN payloads:

- Bits 63-52: Exponent = 0x7FF (NaN), Bit 51 = 1 (quiet)
- Bits 50-47: Type tag (0-15)
- Bits 46-0: Payload (47 bits, enough for x86-64 canonical pointers)

| NaN Type Tag | Mnemonic    | Payload                                |
| ------------ | ----------- | -------------------------------------- |
| 0            | `NIL`       | (unused)                               |
| 1            | `FALSE`     | (unused)                               |
| 2            | `TRUE`      | (unused)                               |
| 3            | `INT`       | 32-bit signed integer in low bits      |
| 4            | `PTR`       | 47-bit heap pointer                    |
| 5            | `LIGHTFUNC` | Index into registered C function table |
| 6            | `SHORTSTR`  | 3 bytes of UTF-8 data in low bits      |

**On 32-bit Platforms: Alignment-Based Tagging**

All heap objects are 8-byte aligned, leaving the low 3 bits of pointers always zero for type tagging.

| Tag (Low 3 bits) | Mnemonic        | Payload Interpretation                 |
| ---------------- | --------------- | -------------------------------------- |
| `0b000`          | `TAG_PTR`       | Heap pointer (mask low 3 bits)         |
| `0b001`          | `TAG_INT`       | 29-bit signed integer in upper bits    |
| `0b010`          | `TAG_SPECIAL`   | 0=nil, 1=false, 2=true in upper bits   |
| `0b011`          | `TAG_SHORTSTR`  | 3 bytes of UTF-8 data                  |
| `0b100`          | `TAG_LIGHTFUNC` | Index into C function table            |
| `0b101`-`0b111`  | Reserved        | (Numbers are heap-allocated on 32-bit) |

**Heap Object Types (stored in object header):**

- `0x01`: `MLUA_TSTRING` (Long String)
- `0x02`: `MLUA_TTABLE`
- `0x03`: `MLUA_TFUNCTION` (Lua Closure)
- `0x04`: `MLUA_TPROTO` (Function Prototype/Bytecode)
- `0x05`: `MLUA_TUSERDATA`
- `0x06`: `MLUA_TUPVALUE` (Open Upvalue)
- `0x07`: `MLUA_TTHREAD` (Coroutine)

#### 1.2 Object Header (`GCObject`)

Every object on the heap begins with a compact header.

```c
typedef struct {
    uint8_t flags; // [7:ROM][6:Pinned][5:Marked][4:Reserved][3-0: Type]
    // Variable length size field follows immediately
} mlua_GCObject;

```

- **Size Encoding:**
- If `Type` implies fixed size (e.g., `TUPVALUE`, `TPROTO` which has a fixed header before data), size is implicit.
- If `Type` is variable (String, Table), the next 1-4 bytes encode the size using a Varint (LEB128-ish) scheme.

#### 1.3 VM Runtime Arrays

The VM operates on three distinct memory regions per call frame:

| Array                | Access Pattern           | Purpose                                           |
| -------------------- | ------------------------ | ------------------------------------------------- |
| **Evaluation Stack** | LIFO                     | Temporary operand storage. Opcodes pop/push here. |
| **Local Variables**  | Indexed (slot 0, 1, ...) | Named variables declared with `local`.            |
| **Arguments**        | Indexed (arg 0, 1, ...)  | Function parameters passed by caller.             |

**Data Flow Model:** Variables cannot be operated on directly. They must be loaded to the eval stack, operated upon, then stored back.

```
-- Lua: a = b + c
MOVE 1        ; Push local[1] (b) to eval stack
MOVE 2        ; Push local[2] (c) to eval stack
ADD           ; Pop b and c, push result
SETLOCAL 0    ; Pop result into local[0] (a)
```

#### 1.4 Closures and Upvalues (Lazy Migration)

**Objective:** Mutable, shared lexical scoping without heap-allocating all locals.

##### A. UpValue Structure

A mutable handle to a variable with two states:

| State      | Pointer Target             | Behavior                               |
| ---------- | -------------------------- | -------------------------------------- |
| **OPEN**   | Stack slot in parent frame | Fast access to live variable           |
| **CLOSED** | Internal storage           | Variable persists after parent returns |

##### B. Closure Structure

A runtime function instance containing:

- **Prototype**: Pointer to immutable bytecode/constants
- **UpValue List**: Array of `UpValue*` forming the closure's environment

##### C. Lifecycle

**1. Instantiation (`OP_CLOSURE`):**

- Scan prototype's upvalue map
- For parent locals: find or create OPEN UpValue pointing to stack slot
- For parent upvalues: copy pointer from parent's upvalue list

**2. Access (`OP_GETUPVAL` / `OP_SETUPVAL`):**

- Dereference `closure.upvals[index]` → follow internal pointer → read/write
- Writes visible to all closures sharing the upvalue (aliasing)

**3. Scope Termination (`OP_RET` / implicit `CLOSE`):**

- Filter OPEN UpValues pointing to dying stack frame
- Copy values from stack to UpValue internal storage
- Repoint UpValue to internal storage (now CLOSED)
- Remove from global "Open UpValue List"

##### D. Constraints

- **Aliasing:** Multiple closures capturing same local share ONE `UpValue` address
- **Atomicity:** Close before stack pointer decrement (no dangling pointers)

### 2. Virtual Machine Instruction Set (Bytecode)

The VM is a **Pure Stack Machine**. All instructions are **exactly 1 or 2 bytes**.

- **1-byte instructions:** Stack-only operations (arithmetic, logic, table access)
- **2-byte instructions:** Include 8-bit immediate or index (locals, small constants, jumps)

> **CRITICAL:** No instruction exceeds 2 bytes. For indices > 255, use stack-based variants.

#### 2.1 Handling Large Indices

For indices > 255, push the index to the eval stack first, then use an `_S` (stack) variant:

```
-- Load constant #300:
LOADINT 1     ; Push high byte (1)
LOADINT 44    ; Push low byte (44 = 0x2C)
              ; Could also: push 300 via multiple adds, or use LOADK for small ints
LOADK_S       ; Pops index, pushes constants[300]
```

#### 2.2 Opcode Table

| Byte                                   | Mnemonic     | Size | Stack Effect     | Description                               |
| -------------------------------------- | ------------ | ---- | ---------------- | ----------------------------------------- |
| **Constants**                          |
| `00`                                   | `NOP`        | 1    | —                | No operation                              |
| `01`                                   | `LOADNIL`    | 1    | `→ nil`          | Push nil                                  |
| `02`                                   | `LOADTRUE`   | 1    | `→ true`         | Push true                                 |
| `03`                                   | `LOADFALSE`  | 1    | `→ false`        | Push false                                |
| `04`                                   | `LOADINT B`  | 2    | `→ int`          | Push signed 8-bit integer B               |
| `05`                                   | `LOADK B`    | 2    | `→ val`          | Push constants[B]                         |
| `06`                                   | `LOADK_S`    | 1    | `idx → val`      | Pop idx, push constants[idx]              |
| **Locals**                             |
| `10`                                   | `GETLOCAL B` | 2    | `→ val`          | Push locals[B]                            |
| `11`                                   | `SETLOCAL B` | 2    | `val →`          | Pop to locals[B]                          |
| `12`                                   | `GETLOCAL_S` | 1    | `idx → val`      | Pop idx, push locals[idx]                 |
| `13`                                   | `SETLOCAL_S` | 1    | `idx val →`      | Pop idx and val, store val to locals[idx] |
| **Arguments**                          |
| `14`                                   | `GETARG B`   | 2    | `→ val`          | Push args[B]                              |
| `15`                                   | `SETARG B`   | 2    | `val →`          | Pop to args[B]                            |
| **Upvalues**                           |
| `16`                                   | `GETUPVAL B` | 2    | `→ val`          | Push upvalue[B].value                     |
| `17`                                   | `SETUPVAL B` | 2    | `val →`          | Pop to upvalue[B]                         |
| **Globals** (key on stack)             |
| `18`                                   | `GETGLOBAL`  | 1    | `key → val`      | Pop key, push \_G[key]                    |
| `19`                                   | `SETGLOBAL`  | 1    | `key val →`      | Pop key and val, \_G[key] = val           |
| **Stack Manipulation**                 |
| `1A`                                   | `POP B`      | 2    | `vals →`         | Pop B values                              |
| `1B`                                   | `DUP`        | 1    | `a → a a`        | Duplicate top                             |
| `1C`                                   | `SWAP`       | 1    | `a b → b a`      | Swap top two                              |
| **Tables**                             |
| `20`                                   | `NEWTABLE`   | 1    | `→ tbl`          | Push new empty table                      |
| `21`                                   | `GETTABLE`   | 1    | `t k → v`        | v = t[k]                                  |
| `22`                                   | `SETTABLE`   | 1    | `t k v →`        | t[k] = v                                  |
| `23`                                   | `APPEND`     | 1    | `t v →`          | t[#t+1] = v                               |
| **Logic**                              |
| `30`                                   | `NOT`        | 1    | `v → bool`       | Push (not v)                              |
| `31`                                   | `EQ`         | 1    | `a b → bool`     | Push (a == b)                             |
| `32`                                   | `LT`         | 1    | `a b → bool`     | Push (a < b)                              |
| `33`                                   | `LE`         | 1    | `a b → bool`     | Push (a <= b)                             |
| `34`                                   | `NEQ`        | 1    | `a b → bool`     | Push (a ~= b)                             |
| **Arithmetic**                         |
| `40`                                   | `ADD`        | 1    | `a b → res`      | a + b                                     |
| `41`                                   | `SUB`        | 1    | `a b → res`      | a - b                                     |
| `42`                                   | `MUL`        | 1    | `a b → res`      | a \* b                                    |
| `43`                                   | `DIV`        | 1    | `a b → res`      | a / b (float)                             |
| `44`                                   | `IDIV`       | 1    | `a b → res`      | a // b (int)                              |
| `45`                                   | `MOD`        | 1    | `a b → res`      | a % b                                     |
| `46`                                   | `POW`        | 1    | `a b → res`      | a ^ b                                     |
| `47`                                   | `UNM`        | 1    | `a → res`        | -a                                        |
| `48`                                   | `LEN`        | 1    | `a → int`        | #a                                        |
| **Bitwise**                            |
| `50`                                   | `BAND`       | 1    | `a b → res`      | a & b                                     |
| `51`                                   | `BOR`        | 1    | `a b → res`      | a \| b                                    |
| `52`                                   | `BXOR`       | 1    | `a b → res`      | a ~ b                                     |
| `53`                                   | `SHL`        | 1    | `a b → res`      | a << b                                    |
| `54`                                   | `SHR`        | 1    | `a b → res`      | a >> b                                    |
| `55`                                   | `BNOT`       | 1    | `a → res`        | ~a                                        |
| **Control Flow** (8-bit signed offset) |
| `60`                                   | `JMP B`      | 2    | —                | PC += (I8)B                               |
| `61`                                   | `JMPF B`     | 2    | `v →`            | If falsy: PC += (I8)B                     |
| `62`                                   | `JMPT B`     | 2    | `v →`            | If truthy: PC += (I8)B                    |
| `63`                                   | `LOOP B`     | 2    | —                | PC -= B (backward jump, unsigned)         |
| `64`                                   | `JMP_S`      | 1    | `off →`          | Pop offset, PC += off (signed 16-bit)     |
| `65`                                   | `LOOP_S`     | 1    | `off →`          | Pop offset, PC -= off (unsigned 16-bit)   |
| **For Loops**                          |
| `66`                                   | `FORPREP B`  | 2    | `i l s →`        | Init numeric for, jump +B                 |
| `67`                                   | `FORLOOP B`  | 2    | `→ idx?`         | Step loop, if continue: jump -B           |
| `68`                                   | `TFORPREP B` | 2    | —                | Init generic for, jump +B                 |
| `69`                                   | `TFORLOOP B` | 2    | —                | Check iter result, jump -B or exit        |
| `6A`                                   | `TFORCALL B` | 2    | —                | Call iterator, B = nvar                   |
| **Functions**                          |
| `70`                                   | `CLOSURE B`  | 2    | `→ func`         | Create closure from proto[B]              |
| `71`                                   | `CLOSURE_S`  | 1    | `idx → func`     | Pop idx, create closure from proto[idx]   |
| `72`                                   | `CALL B`     | 2    | `fn args → rets` | Call with B args                          |
| `73`                                   | `RET B`      | 2    | `vals →`         | Return B values                           |
| `74`                                   | `RET0`       | 1    | —                | Return 0 values                           |
| `75`                                   | `RET1`       | 1    | `val →`          | Return 1 value                            |
| `76`                                   | `VARARG B`   | 2    | `→ vals`         | Push B varargs                            |
| `77`                                   | `TAILCALL B` | 2    | `fn args →`      | Tail call with B args                     |
| **String**                             |
| `80`                                   | `CONCAT B`   | 2    | `strs → str`     | Concatenate top B values                  |

#### 2.3 Design Notes

**Stack-Based Variants (`_S`):** When B operand insufficient (>255), push index to stack first, use `_S` variant. This keeps all instructions ≤2 bytes.

**Short Return Forms:** `RET0` and `RET1` are 1-byte optimizations for common cases.

**LOOP vs JMP:** `LOOP` uses unsigned offset for backward jumps (0-255 bytes back), `JMP` uses signed offset for forward/backward (±127).

---

### 3. C API & State Structures

#### 3.1 `MLuaState`

```c
struct MLuaState {
    /* Memory Management */
    U8 *HeapBase;     /* Start of contiguous heap */
    Size HeapSize;    /* Total size */
    Size HeapTop;     /* Current bump pointer offset (byte index) */

    /* Execution Context */
    MLuaValue *Stack; /* Value stack */
    Size StackSize;   /* Stack capacity */
    Size StackTop;    /* Current stack pointer (index) */

    /* Globals & Registry */
    MLuaValue Globals;  /* Global Environment (_G) */
    MLuaValue Registry; /* Registry Table */

    /* GC References */
    MLuaGCRef *GCRefHead; /* Linked list of active GC refs */

    /* Error Handling */
    void (*Panic)(MLuaState *); /* Panic callback */
};
```

#### 3.2 GC Reference (Safety Mechanism)

```c
/* Opaque handle for C clients */
typedef struct MLuaGCRef {
    struct MLuaGCRef *Next;  /* Linked list */
    struct MLuaGCRef *Prev;  /* Doubly linked for O(1) removal */
    MLuaValue Value;         /* The actual value (updated by GC) */
} MLuaGCRef;

/* API */
void MLuaPushGCRef(MLuaState *L, MLuaGCRef *ref, MLuaValue val);
void MLuaPopGCRef(MLuaState *L, MLuaGCRef *ref);
```

---

### 4. Compiler Algorithms

#### 4.1 The Pratt Parser Loop (Expressions)

To avoid deep recursion, we use a loop with precedence levels.

```c
// Pseudo-implementation of Expression Parsing
void parse_expr(Compiler* C, int min_precedence) {
    // 1. Prefix Phase (Integers, Unary ops, Grouping)
    Token token = lex_next(C);
    switch (token.type) {
        case TK_INT: emit_loadint(C, token.ival); break;
        case TK_NAME: emit_getlocal_or_global(C, token.str); break;
        case TK_MINUS:
            parse_expr(C, PREC_UNARY); // Recurse with high precedence
            emit_op(C, OP_UNM);
            break;
        // ...
    }

    // 2. Infix Phase (Binary ops)
    while (1) {
        Token op = lex_peek(C);
        int op_prec = get_precedence(op.type);

        if (op_prec < min_precedence) break;

        lex_next(C); // Consume op

        // Right-associative handling (e.g., concatenation)
        int next_min_prec = (op.type == TK_CONCAT) ? op_prec : op_prec + 1;

        parse_expr(C, next_min_prec); // Parse Right-Hand Side
        emit_binop(C, op.type);
    }
}

```

#### 4.2 Backpatching (Control Flow)

Statements like `if` and `while` use a simple stack-based patcher.

1. **`if <cond> then`**:

- Emit `JMPFALSE 0xFFFF`.
- Push current bytecode index to `patch_stack`.

2. **`else`**:

- Emit `JMP 0xFFFF` (jump over the else block).
- Read index from `patch_stack`.
- Rewrite the instruction at that index to jump to `current_pc`.
- Push the new `JMP` index to `patch_stack`.

3. **`end`**:

- Pop index from `patch_stack`.
- Rewrite instruction to jump to `current_pc`.

---

### 5. Garbage Collection: Mark-Compact (Lisp-2 Algorithm)

Since we cannot allocate extra memory for "forwarding pointers" during GC, we reuse the object header or data fields.

#### Phase 1: Mark

- Start from Roots: `l_registry`, `l_globals`, `stack` (VM stack), and the linked list of `microlua_GCRef` (C stack).
- Traverse graph. Set `flags |= MARKED` bit in header.

#### Phase 2: Compute Addresses (The Trick)

We need to calculate where objects _will_ go.

- `free_ptr = heap_base`
- Iterate linearly through the entire heap (using `size` in header to skip).
- If `obj.marked`:
- Calculate `new_addr = free_ptr`.
- **Store `new_addr` in the object.** _Critically, we overwrite the first 3 bytes of the object's data payload with `new_addr`. We must assume the object header has a bit `MOVED` to indicate this._
- `free_ptr += obj.size`.

#### Phase 3: Update References

- Traverse all Roots and all Live Objects again.
- For every pointer `p` found:
- Look at the object at `p`.
- Read the `new_addr` we stored in Phase 2.
- Update `p = new_addr`.

#### Phase 4: Move

- Iterate linearly through the heap again.
- If `obj.marked`:
- `memmove(new_addr, curr_addr, obj.size)`.
- Unset `MARKED` and `MOVED` bits.
- Restore the data we overwrote (if possible? _Actually, Lisp-2 usually requires an extra field. For MicroLua, a better approach is the **Threaded Compaction** or simply using a **break-table** if RAM allows. Given constraints, a **Two-Finger** compactor handles fixed-size blocks best, but we have var-size._
- **Revision for MicroLua:** Use **Mark-Sweep-Compact**.
- Only compact if fragmentation is high (panic mode).
- Otherwise, use a free-list for the variable-sized blocks (standard Lua 5.1).
- **Strict Constraint override:** If you _must_ have strict Mark-Compact (no fragmentation ever), you need a "Forwarding Pointer" in the header. We will reserve 24 bits in the header for this during GC.

**Final Header Revision for Compaction:**
During GC, the `Type` and `Flags` (8 bits) are preserved. The `Size` field (if variable) is complex to overwrite.

- **Solution:** We force all variable-size objects to have a minimum data size of 4 bytes. We store the forwarding pointer in the **Data Payload** of the object during Phase 2. This is safe because we don't need the data during the Update References phase, only the pointers _inside_ the data (which we can still parse if we are careful) or we parse the old heap.

---

### 6. Standard Library Subset

#### 6.1 `math`

- `abs`, `floor`, `ceil`, `min`, `max`, `sin`, `cos`, `tan` (wrappers around embedded libm or hardware instructions).
- `random`: Implementation of a tiny PRNG (e.g., Xorshift32) to avoid libc `rand`.

#### 6.2 `table`

- `getn` (# operator), `insert`, `remove` (only for array part).
- `forward(t, proto)`: The custom API.

#### 6.3 `string`

- `byte`, `char`, `sub`, `len`.
- `find`, `match`, `gsub`: Implemented via a minimal pattern matcher (no full regex engine).
