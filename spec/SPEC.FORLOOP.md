Here is the comprehensive specification for the MicroLua Loop Semantics. You can save this file as `specs/vm_loop_semantics.md`.

---

# MicroLua Specification: Stack-Based Loop Semantics

**Version:** 1.0.0
**Status:** Draft
**Context:** Embedded Stack Machine (1/2-byte instruction width constraint)

## 1. Architectural Overview

MicroLua utilizes a **Hybrid Stack-Loop Model**. Unlike standard CIL (which uses generic comparisons and branches) or Standard Lua (which uses register-based macro-instructions), MicroLua offloads loop state into **Reserved Shadow Locals**.

### Design Constraints

1. **Instruction Width:** All loop control instructions are strictly **2 Bytes** (`[Opcode] [Base_Local_Index]`).
2. **Argument Passing:** Variable data (Loop Bounds, Step, Jump Targets) is passed via the **Operand Stack** prior to opcode execution.
3. **State Persistence:** Internal loop state is stored in a contiguous block of **4 Local Variable Slots** to ensure the Operand Stack remains clean for the loop body.

---

## 2. Memory Layout (Shadow Locals)

The compiler must reserve **4 consecutive local slots** for every active loop in the scope. These slots are invisible to the user but accessible by the VM loop instructions.

### A. Numeric Loop Layout (`for i=...`)

| Slot Offset  | Name        | Type      | Description                                                  |
| ------------ | ----------- | --------- | ------------------------------------------------------------ |
| `[Base + 0]` | **Index**   | `Number`  | The current iterator value.                                  |
| `[Base + 1]` | **Limit**   | `Number`  | The loop boundary condition.                                 |
| `[Base + 2]` | **Step**    | `Number`  | The increment value.                                         |
| `[Base + 3]` | **Loop PC** | `Address` | Absolute instruction pointer address of the loop body start. |

### B. Generic Loop Layout (`for k,v in...`)

| Slot Offset  | Name        | Type       | Description                                                  |
| ------------ | ----------- | ---------- | ------------------------------------------------------------ |
| `[Base + 0]` | **Func**    | `Function` | The iterator function (e.g., `next`).                        |
| `[Base + 1]` | **State**   | `Any`      | The invariant state (e.g., the table).                       |
| `[Base + 2]` | **Control** | `Any`      | The control variable (previous key).                         |
| `[Base + 3]` | **Loop PC** | `Address`  | Absolute instruction pointer address of the loop body start. |

---

## 3. Numeric For Loops

### 3.1 Setup Phase: `NLOOP_PREP`

Initializes the loop state and handles the initial condition check.

- **Format:** `[OP_NLOOP_PREP] [u8 Base_Index]` (2 Bytes)
- **Stack Requirement:** 5 items.
- _Top_ `[Body_Target, Exit_Target, Step, Limit, Start]` _Bottom_

**Operational Logic:**

1. **Pop** 5 values from the stack.
2. **Type Check:** Ensure `Start`, `Limit`, and `Step` are numbers.
3. **Store State:**

- `Locals[Base]   = Start`
- `Locals[Base+1] = Limit`
- `Locals[Base+2] = Step`
- `Locals[Base+3] = Body_Target` (Absolute Address)

4. **Pre-Loop Condition Check:**

- If `(Step > 0 AND Start > Limit)` OR `(Step < 0 AND Start < Limit)`:
- **Jump** to `Exit_Target`.

- Else:
- **Push** `Start` value back onto the stack (to be consumed by the user's `i` assignment).
- **Fallthrough** to the next instruction.

### 3.2 Iteration Phase: `NLOOP_STEP`

Increments the index and loops back if boundaries are not met.

- **Format:** `[OP_NLOOP_STEP] [u8 Base_Index]` (2 Bytes)
- **Stack Requirement:** None.

**Operational Logic:**

1. **Load** `Index`, `Limit`, `Step` from `Locals[Base...Base+2]`.
2. **Increment:** `Index = Index + Step`.
3. **Update:** Store `Index` back to `Locals[Base]`.
4. **Boundary Check:**

- If `(Step > 0 AND Index <= Limit)` OR `(Step < 0 AND Index >= Limit)`:
- **Push** `Index` onto the Operand Stack (for next iteration's assignment).
- **Load** `Target` from `Locals[Base+3]`.
- **Jump** to `Target`.

- Else:
- **Fallthrough** (Loop terminates).

---

## 4. Generic For Loops

### 4.1 Setup Phase: `GLOOP_SETUP`

Moves the iterator triad into storage.

- **Format:** `[OP_GLOOP_SETUP] [u8 Base_Index]` (2 Bytes)
- **Stack Requirement:** 4 items.
- _Top_ `[Body_Target, Func, State, Control]` _Bottom_

**Operational Logic:**

1. **Pop** 4 values.
2. **Store State:**

- `Locals[Base]   = Func`
- `Locals[Base+1] = State`
- `Locals[Base+2] = Control`
- `Locals[Base+3] = Body_Target`

3. **Fallthrough** to the loop head.

### 4.2 Invocation Phase: `GLOOP_CALL`

Pushes the iterator arguments onto the stack to prepare for a standard `CALL`. This typically sits at the start of the loop head.

- **Format:** `[OP_GLOOP_CALL] [u8 Base_Index]` (2 Bytes)
- **Stack Requirement:** None.

**Operational Logic:**

1. **Push** `Locals[Base]` (Func).
2. **Push** `Locals[Base+1]` (State).
3. **Push** `Locals[Base+2]` (Control).
4. _Note:_ This instruction is immediately followed by `OP_CALL`.

### 4.3 Iteration Phase: `GLOOP_STEP`

Analyzes return values and decides whether to continue.

- **Format:** `[OP_GLOOP_STEP] [u8 Base_Index]` (2 Bytes)
- **Stack Requirement:** Variable (Results from `OP_CALL`).
- _Top_ `[Val_N, ..., Val_1]` _Bottom_

**Operational Logic:**

1. **Peek** at `Val_1` (The first return value).

- _Note:_ The VM must know how many values were returned by the previous `CALL`.

2. **Check:**

- **If `Val_1` is `nil`:**
- **Pop** all return values.
- **Fallthrough** (Loop terminates).

- **If `Val_1` is NOT `nil`:**
- **Update:** `Locals[Base+2] = Val_1` (Update Control Variable).
- **Preserve Stack:** Leave `Val_1...Val_N` on the stack (these are `k, v`).
- **Load** `Target` from `Locals[Base+3]`.
- **Jump** to `Target`.

---

## 5. Bytecode Implementation Examples

### Example 1: Numeric Loop (`for i = 1, 5 do print(i) end`)

Assuming `Locals[0..3]` are reserved for shadow state, and `Locals[4]` is user variable `i`.

```assembly
; -- 1. PUSH ARGUMENTS --
LDC_NUM 1           ; Start
LDC_NUM 5           ; Limit
LDC_NUM 1           ; Step
PUSH_LABEL EXIT     ; Exit Target Address
PUSH_LABEL BODY     ; Body Target Address

; -- 2. INITIALIZE --
NLOOP_PREP 0        ; Setup Locals[0-3]. If loop runs, Push Start (1).

BODY:
    ; -- 3. USER CODE --
    STLOC 4         ; Assign Top-of-Stack to user 'i'
    GETGLOBAL "print"
    LDLOC 4
    CALL 1, 0       ; print(i)

    ; -- 4. ITERATE --
    NLOOP_STEP 0    ; Incr index. If <= 5, Push index & Jump to BODY.

EXIT:
    ; Loop finished

```

### Example 2: Generic Loop (`for k,v in pairs(t)`)

Assuming `Locals[0..3]` for shadow state, `Locals[4]` for `k`, `Locals[5]` for `v`.

```assembly
; -- 1. GET ITERATOR --
GETGLOBAL "pairs"
GETGLOBAL "t"
CALL 1, 3           ; Returns: Func, State, Control

; -- 2. PUSH METADATA --
PUSH_LABEL BODY     ; Body Target Address

; -- 3. INITIALIZE --
GLOOP_SETUP 0       ; Pop 4 items. Fill Locals[0-3].

BODY:
    ; -- 4. PREPARE CALL --
    GLOOP_CALL 0    ; Push Func, State, Control
    CALL 2, -1      ; Call iterator (2 args, variable results)

    ; -- 5. CHECK & LOOP --
    GLOOP_STEP 0    ; Check result 1.
                    ; If nil -> Exit.
                    ; If valid -> Update Control, Jump to START_ASSIGN.

START_ASSIGN:
    ; Stack has [v, k]
    STLOC 5         ; Pop v -> Loc[5]
    STLOC 4         ; Pop k -> Loc[4]

    ; ... User Code ...

    JUMP BODY       ; Unconditional jump back to header

EXIT:
    ; Loop finished

```

## 6. Safety & Edge Cases

1. **Stack Balance:** The loop instructions guarantee the stack is balanced _relative to the loop body_. `NLOOP_PREP` consumes setup args and provides the first loop value. `NLOOP_STEP` provides subsequent loop values.
2. **Type Safety:** `NLOOP_PREP` must throw a runtime error if `Start`, `Limit`, or `Step` are not coercible to numbers.
3. **Zero Step:** If `Step` is 0, the loop is mathematically infinite. The VM should allow this (standard Lua behavior), but the developer should be warned.
4. **Shadow Overwrites:** The Compiler **must** ensure the User never writes to `Locals[Base...Base+3]`. These indices should be marked "Reserved" in the compiler's symbol table during the loop's scope.
