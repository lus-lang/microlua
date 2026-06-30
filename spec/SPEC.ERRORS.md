Here is the specification for the Error Handling system. Save this as `specs/error_handling.md`.

---

# MicroLua Specification: Soft Error Handling (v1.0)

**Context:** Embedded Runtime (No `setjmp`/`longjmp`/Exceptions)
**Pattern:** Status-Return with Propagation Macros

## 1. Core Principles

To ensure reliability and prevent "sentinel value confusion" (e.g., returning `-1` for error vs. value `-1`), MicroLua adopts strict conventions:

1. **Status is King:** All runtime functions that _can_ fail must return `MLuaStatus`.
2. **Output via Pointers:** Data is returned via "Out Parameters" (`double* out`), not return values.
3. **Macro Propagation:** Error checks are flattened using `M_TRY` to avoid nested `if` blocks.

---

## 2. Type Definitions

### 2.1 Status Codes

Replace ad-hoc integer returns with a formal enumeration.

```c
    MLUA_OK = 0,            // Operation Successful
    MLUA_ERR_RUNTIME,       // Generic Runtime Error (Type mismatch, Index OOB)
    MLUA_ERR_MEMORY,        // Alloc failure / GC out of memory
    MLUA_ERR_SYNTAX,        // Parsing error
    MLUA_ERR_STACK,         // Stack Overflow/Underflow
```

### 2.2 State Augmentation

The `MLuaState` struct holds the error context. This allows `MLUA_ERR_RUNTIME` to carry a specific message string without complex return structures.

```c
struct MLuaState {
    // ... existing fields ...
    const char* error_msg;  // Pointer to static string or internal error buffer
    MLuaStatus status;      // Current "Sticky" status (optional usage)
};

```

---

## 3. Control Flow Macros

These macros are mandatory for internal runtime development to maintain code cleanliness.

### 3.1 `M_TRY(expr)`

Executes an expression (function call). If the result is **not** `MLUA_OK`, it immediately returns the error code to the caller.

```c
#define M_TRY(expr) \
    do { \
        MLuaStatus _s = (expr); \
        if (_s != MLUA_OK) return _s; \
    } while (0)

```

### 3.2 `M_FAIL(L, code, msg)`

Sets the error state and returns the failure code immediately.

```c
#define M_FAIL(L, code, msg) \
    do { \
        (L)->error_msg = (msg); \
        return (code); \
    } while (0)

```

### 3.3 `M_ASSERT(L, cond, code, msg)`

Quick validation helper.

```c
#define M_ASSERT(L, cond, code, msg) \
    do { \
        if (!(cond)) M_FAIL(L, code, msg); \
    } while (0)

```

---

## 4. Implementation Convention

### 4.1 Internal Function Signature

Any internal helper (math, stack logic, table logic) must follow this signature:

**Bad:**

```c
double vm_add(MLuaState* L, double a, double b); // How to signal overflow?

```

**Good:**

```c
MLuaStatus vm_add(MLuaState* L, double a, double b, double* out_result);

```

### 4.2 The "Sticky Error" Pattern (API Layer)

For the public C API (where `void` returns are preferred for usability), functions check `L->status` on entry.

```c
void MLuaPush(MLuaState* L, MLuaValue* v) {
    if (L->status != MLUA_OK) return; // No-op if already crashed

    // ... logic ...
    if (fail) L->status = MLUA_ERR_MEMORY;
}

```

---

## 5. Usage Examples

### 5.1 Deeply Nested Runtime Logic

Scenario: Implementing the `OP_ADD` instruction which relies on type checking and coercion.

**Leaf Function (The work):**

```c
MLuaStatus CoreAdd(MLuaState* L, MLuaValue* a, MLuaValue* b, double* out) {
    if (a->type == MLUA_TNUMBER && b->type == MLUA_TNUMBER) {
        *out = a->data.number + b->data.number;
        return MLUA_OK;
    }
    // Attempt coercion or fail
    return M_FAIL(L, MLUA_ERR_RUNTIME, "Attempt to perform arithmetic on non-numbers");
}

```

**Instruction Handler (The caller):**

```c
MLuaStatus Inst_Add(MLuaState* L) {
    MLuaValue v1, v2;
    // Get top 2 values (Assume MLuaGetValue returns Status)
    M_TRY(MLuaGetValue(L, -2, &v1));
    M_TRY(MLuaGetValue(L, -1, &v2));

    double result;
    // Call leaf function; if it fails, we return immediately.
    M_TRY(CoreAdd(L, &v1, &v2, &result));

    // Success path
    MLuaValue res_val;
    MLuaInitNumber(&res_val, result);
    MLuaPop(L, 2);
    MLuaPush(L, &res_val);

    return MLUA_OK;
}

```

### 5.2 The VM Execution Loop

The top-level loop acts as the final "Catch" block.

```c
MLuaStatus MLuaExecute(MLuaState* L) {
    while (1) {
        MLuaInstruction inst = Fetch(L);
        MLuaStatus status = MLUA_OK;

        switch (inst.opcode) {
            case OP_ADD:
                status = Inst_Add(L);
                break;
            case OP_CALL:
                status = Inst_Call(L);
                break;
            // ...
        }

        // Centralized Error Handling
        if (status != MLUA_OK) {
            if (status == MLUA_ERR_RUNTIME && CanRecover(L)) {
                // If inside pcall, jump to handler
                PerformRecovery(L);
                continue;
            } else {
                // Fatal error, return to host
                return status;
            }
        }
    }
}

```

## 6. Migration Guide

1. **Search/Replace:** Find all instances of `return -1;` in runtime code.
2. **Refactor:** Change signatures from `type func(...)` to `MLuaStatus func(..., type* out)`.
3. **Wrap:** Wrap call sites in `M_TRY(...)`.
4. **Define:** Add the `MLuaStatus` enum to `mlua_core.h`.
