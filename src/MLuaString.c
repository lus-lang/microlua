/*
 * MicroLua - MLuaString.c
 * Interned string implementation with per-state string table
 */

#include "MLuaString.h"
#include "MLuaGC.h"

/* The intern-table probe masks instead of taking modulo, which requires the
 * capacity to stay a power of two through every transition: the initial
 * size (a port knob), the *2 growth, and the /2-toward-initial shrink. */
MLUA_STATIC_ASSERT((MLUA_STRING_TABLE_INITIAL_SIZE &
                    (MLUA_STRING_TABLE_INITIAL_SIZE - 1)) == 0,
                   "intern table capacity must be a power of two");

/* ========================================================================== */
/* String Hash (FNV-1a)                                                       */
/* ========================================================================== */

#define FNV_OFFSET_BASIS 2166136261U
#define FNV_PRIME 16777619U

/* One byte's worth of hash mixing. Both variants are left folds, so the
 * incremental concat hashing below works identically with either. */
#if MLUA_HASH_SHIFT_XOR
#define HASH_STEP(h, c) ((h) ^ (((h) << 5) + ((h) >> 2) + (U32)(c)))
#else
#define HASH_STEP(h, c) (((h) ^ (U32)(c)) * FNV_PRIME)
#endif

U32 MLuaStringHash(const char *str, Size len) {
  U32 hash = FNV_OFFSET_BASIS;
  Size i;

  for (i = 0; i < len; i++) {
    hash = HASH_STEP(hash, (U8)str[i]);
  }

  return hash;
}

/* Hash and detect pure ASCII in the same pass over the bytes. */
static U32 StringHashAscii(const char *str, Size len, Bool *allAscii) {
  U32 hash = FNV_OFFSET_BASIS;
  U8 acc = 0;
  Size i;

  for (i = 0; i < len; i++) {
    U8 c = (U8)str[i];
    hash = HASH_STEP(hash, c);
    acc |= c;
  }

  *allAscii = (acc & 0x80U) == 0;
  return hash;
}

/* ========================================================================== */
/* String Table (per-state)                                                   */
/* ========================================================================== */

Bool MLuaStringTableInit(MLuaState *L) {
  Size i;
  Size bytesNeeded;

  if (L->StringTable != NULL) {
    return TRUE; /* Already initialized */
  }

  bytesNeeded = MLUA_STRING_TABLE_INITIAL_SIZE * sizeof(MLuaValue);
  L->StringTable = (MLuaValue *)MLuaAlloc(L, bytesNeeded);

  if (!L->StringTable) {
    return FALSE;
  }

  L->StringTableCap = MLUA_STRING_TABLE_INITIAL_SIZE;
  L->StringTableCount = 0;

  /* Initialize all slots to nil */
  for (i = 0; i < L->StringTableCap; i++) {
    L->StringTable[i] = MLUA_NIL;
  }

  return TRUE;
}

/*
 * Probe for `str`. Returns the slot holding it on a hit (callers detect a
 * hit with IsPtr(*slot)), or the best insert slot on a miss: the first
 * MLUA_FALSE tombstone passed during the probe if any, else the empty slot
 * that ended it. Reusing tombstones keeps probe chains from growing under
 * string churn (the GC tombstones dead entries in place; only a rebuild
 * clears them).
 */
static MLuaValue *StringTableFind(MLuaState *L, const char *str, Size len,
                                  U32 hash) {
  Size index;
  Size i;
  MLuaValue v;
  MLuaGCHeader *gch;
  MLuaStringHeader *sh;
  const char *existingStr;
  MLuaValue *tombstone = NULL;

  if (L->StringTableCap == 0) {
    return NULL;
  }

  /* Capacity is a power of two (initial size static-asserted at the top of
   * this file, growth and shrink are *2 / /2), so masking replaces the two
   * modulos on this hot probe -- every intern passes through here. */
  index = hash & (L->StringTableCap - 1);

  for (i = 0; i < L->StringTableCap; i++) {
    Size slot = (index + i) & (L->StringTableCap - 1);
    v = L->StringTable[slot];

    if (IsNil(v)) {
      /* Empty slot - string not found; insert into a passed tombstone
       * when one exists (shortens future probes of this chain) */
      return tombstone ? tombstone : &L->StringTable[slot];
    }

    if (!IsPtr(v)) {
      if (!tombstone) {
        tombstone = &L->StringTable[slot];
      }
      continue; /* Tombstoned entry: keep probing */
    }

    /* Check if this is the string we're looking for */
    gch = (MLuaGCHeader *)GetPtr(v);
    sh = MLUA_STRHEADER(gch);

    if (sh->Hash == hash && MLuaStrHeaderLen(sh) == len) {
      existingStr = MLUA_STRDATA(sh);
      if (MemCmp(existingStr, str, len) == 0) {
        /* Found it! */
        return &L->StringTable[slot];
      }
    }
  }

  /* No empty slot left (entries + tombstones fill the table): a tombstone
   * is still a valid insert position */
  return tombstone;
}

static Bool StringTableResize(MLuaState *L) {
  Size oldCap = L->StringTableCap;
  MLuaValue *oldEntries = L->StringTable;
  Size newCap = oldCap * 2;
  Size bytesNeeded = newCap * sizeof(MLuaValue);
  Size i;

  L->StringTable = (MLuaValue *)MLuaAlloc(L, bytesNeeded);
  if (!L->StringTable) {
    L->StringTable = oldEntries;
    return FALSE;
  }

  L->StringTableCap = newCap;
  L->StringTableCount = 0;

  /* Initialize new table to nil */
  for (i = 0; i < newCap; i++) {
    L->StringTable[i] = MLUA_NIL;
  }

  /* Reinsert all entries */
  for (i = 0; i < oldCap; i++) {
    MLuaValue v = oldEntries[i];
    if (!IsNil(v) && IsPtr(v)) {
      MLuaGCHeader *gch = (MLuaGCHeader *)GetPtr(v);
      MLuaStringHeader *sh = MLUA_STRHEADER(gch);
      MLuaValue *slot =
          StringTableFind(L, MLUA_STRDATA(sh), MLuaStrHeaderLen(sh), sh->Hash);
      if (slot && IsNil(*slot)) {
        *slot = v;
        L->StringTableCount++;
      }
    }
  }

  /* Note: old entries are on the heap and will be collected by GC */
  return TRUE;
}

/*
 * Insert strVal into the intern table. `hint` may carry the empty slot a
 * preceding StringTableFind miss returned, saving the re-probe; it is
 * discarded when a resize moves the table. (Allocations between the find
 * and the insert are safe: they never collect, so the backing array cannot
 * have moved.)
 */
static Bool StringTableInsert(MLuaState *L, MLuaValue strVal,
                              MLuaValue *hint) {
  Size threshold;
  MLuaGCHeader *gch;
  MLuaStringHeader *sh;

  /* Check if resize needed */
  threshold = (L->StringTableCap * MLUA_STRING_TABLE_LOAD_FACTOR) / 100;
  if (L->StringTableCount >= threshold) {
    if (StringTableResize(L)) {
      hint = NULL; /* the table was rebuilt; the old probe is meaningless */
    }
    /* On resize failure (transient OOM: the heap may be full of
     * collectable garbage, and allocations never collect) fall through
     * and insert into the CURRENT table -- the load factor leaves it
     * free slots, at degraded probe cost, and the next collection's
     * shrink pass rebuilds it. Refusing the insert instead would hand
     * out an un-interned string and silently break pointer equality
     * (==, table keys); only a genuinely slotless table may fail below.
     * The probe hint stays valid: a failed resize allocates nothing
     * that could move the table. */
  }

  if (!hint) {
    gch = (MLuaGCHeader *)GetPtr(strVal);
    sh = MLUA_STRHEADER(gch);
    hint =
        StringTableFind(L, MLUA_STRDATA(sh), MLuaStrHeaderLen(sh), sh->Hash);
  }

  if (hint && !IsPtr(*hint)) {
    /* A reused tombstone already counts toward the load factor (the count
     * tracks occupied slots and only rebuilds reset it), so only a
     * genuinely empty slot increments it. */
    if (IsNil(*hint)) {
      L->StringTableCount++;
    }
    *hint = strVal;
    return TRUE;
  }

  return FALSE;
}

Bool MLuaStringTableShrink(MLuaState *L) {
  MLuaValue *oldEntries = L->StringTable;
  Size oldCap = L->StringTableCap;
  Size liveCount = 0;
  Size newCap = MLUA_STRING_TABLE_INITIAL_SIZE;
  Size bytesNeeded;
  Size i;

  if (!oldEntries || oldCap <= MLUA_STRING_TABLE_INITIAL_SIZE) {
    return FALSE;
  }

  for (i = 0; i < oldCap; i++) {
    if (IsPtr(oldEntries[i])) {
      liveCount++;
    }
  }

  while (liveCount >= (newCap * MLUA_STRING_TABLE_LOAD_FACTOR) / 100) {
    newCap *= 2;
  }

  if (newCap * 2 >= oldCap) {
    /* No rebuild: the table keeps its tombstones, so the load-factor
     * count must keep counting them. StringTableCount tracks OCCUPIED
     * slots (live + tombstones; tombstone reuse doesn't increment it),
     * and it is already correct here. Overwriting it with the live-only
     * count let a churned table fill to 100% occupied while staying
     * under the resize threshold -- inserts then found no slot, no
     * resize fired, and MLuaStringNew handed out un-interned strings,
     * silently breaking pointer equality (`('k'..i) ~= ('k'..i)`). */
    return FALSE;
  }

  bytesNeeded = newCap * sizeof(MLuaValue);
  L->StringTable = (MLuaValue *)MLuaAlloc(L, bytesNeeded);
  if (!L->StringTable) {
    L->StringTable = oldEntries;
    return FALSE;
  }

  L->StringTableCap = newCap;
  L->StringTableCount = 0;
  for (i = 0; i < newCap; i++) {
    L->StringTable[i] = MLUA_NIL;
  }

  for (i = 0; i < oldCap; i++) {
    MLuaValue v = oldEntries[i];
    if (IsPtr(v)) {
      MLuaGCHeader *gch = (MLuaGCHeader *)GetPtr(v);
      MLuaStringHeader *sh = MLUA_STRHEADER(gch);
      MLuaValue *slot =
          StringTableFind(L, MLUA_STRDATA(sh), MLuaStrHeaderLen(sh), sh->Hash);
      if (slot && IsNil(*slot)) {
        *slot = v;
        L->StringTableCount++;
      }
    }
  }

  return TRUE;
}

/* ========================================================================== */
/* String Creation                                                            */
/* ========================================================================== */

MLuaValue MLuaStringNewShort(const char *str, Size len) {
  if (len > MLUA_SHORTSTR_MAX) {
    return MLUA_NIL;
  }

  return MLuaMakeShortStr(str, len);
}

MLuaValue MLuaStringNew(MLuaState *L, const char *str, Size len) {
  U32 hash;
  Bool ascii;
  MLuaValue *existing;
  MLuaGCHeader *gch;
  MLuaStringHeader *sh;
  char *data;
  Size headerSize;
  Size totalDataSize;
  MLuaValue result;

  /*
   * 'len' is always the exact byte length — callers measure with StrLen
   * themselves. (An auto-detect on len==0 would make genuine empty strings
   * impossible: a non-NUL-terminated buffer would be read out of bounds.)
   */

  /* Use short string for small byte strings. */
  if (len <= MLUA_SHORTSTR_MAX) {
    return MLuaStringNewShort(str, len);
  }

  if (len > (Size)MLUA_STR_LEN_MASK) {
    L->ErrorMsg = "string too long";
    return MLUA_NIL;
  }

  /* Initialize string table if needed */
  if (L->StringTableCap == 0) {
    if (!MLuaStringTableInit(L)) {
      L->ErrorMsg = "out of memory";
      return MLUA_NIL;
    }
  }

  /* Compute hash (and the ASCII flag, in the same pass) */
  hash = StringHashAscii(str, len, &ascii);

  /* Check if string already exists (a miss returns an insert slot, which
   * may hold nil or a tombstone -- only IsPtr means a hit) */
  existing = StringTableFind(L, str, len, hash);
  if (existing && IsPtr(*existing)) {
    return *existing; /* Return existing interned string */
  }

  /* Allocate new string */
  headerSize = sizeof(MLuaStringHeader);
  if (len > (Size)-1 - headerSize - 1) {
    L->ErrorMsg = "string too long";
    return MLUA_NIL;
  }
  totalDataSize = headerSize + len + 1; /* +1 for null terminator */

  gch = MLuaAllocObjectNC(L, OBJTYPE_STRING, totalDataSize);
  if (!gch) {
    /* Callers propagate the nil sentinel; without ErrorMsg it would pass
     * VM_CHECK_NIL and flow onward as an ordinary nil value. */
    L->ErrorMsg = "out of memory";
    return MLUA_NIL;
  }

  sh = MLUA_STRHEADER(gch);
  sh->Hash = hash;
  sh->Length = (U32)len | (ascii ? MLUA_STR_ASCII_BIT : 0);

  data = (char *)MLUA_STRDATA(sh);
  MemCpy(data, str, len);
  data[len] = '\0';

  result = MakePtr(gch);

  /* Insert into string table, reusing the probe from the dedup miss. A
   * failed insert (heap AND table full) must raise: returning the string
   * un-interned would silently break pointer equality (==, table keys). */
  if (!StringTableInsert(L, result, existing)) {
    L->ErrorMsg = "out of memory";
    return MLUA_NIL;
  }

  return result;
}

/* ========================================================================== */
/* String Operations                                                          */
/* ========================================================================== */

/*
 * Rotating buffers for short-string data access.
 *
 * Short strings (<=3 bytes) live inside the value word, so MLuaStringData
 * must materialize their bytes somewhere. A single static buffer would
 * alias any two short strings whose data pointers are alive at once
 * (e.g. MLuaStringCompare, table.concat's separator vs elements), so we
 * rotate through four buffers. Contract: a returned pointer stays valid
 * until the fourth subsequent MLuaStringData call on a short string.
 */
#define SHORTSTR_BUFFERS 4
static char ShortStrBuffer[SHORTSTR_BUFFERS][MLUA_SHORTSTR_MAX + 1];
static unsigned ShortStrBufferIdx;

Size MLuaStringLen(MLuaValue v) {
  MLuaGCHeader *gch;
  MLuaStringHeader *sh;

  if (IsShortStr(v)) {
    return MLuaShortStrLen(v);
  }

  if (!IsString(v)) {
    return 0;
  }

  gch = (MLuaGCHeader *)GetPtr(v);
  sh = MLUA_STRHEADER(gch);
  return MLuaStrHeaderLen(sh);
}

Bool MLuaStringIsAscii(MLuaValue v) {
  if (IsShortStr(v)) {
    Size len = MLuaShortStrLen(v);
    U8 acc = 0;
    if (len > 0) {
      acc |= (U8)GetShortStrChar0(v);
    }
    if (len > 1) {
      acc |= (U8)GetShortStrChar1(v);
    }
    if (len > 2) {
      acc |= (U8)GetShortStrChar2(v);
    }
    if (len > 3) {
      acc |= (U8)GetShortStrChar3(v);
    }
    if (len > 4) {
      acc |= (U8)GetShortStrChar4(v);
    }
    return (acc & 0x80U) == 0;
  }

  if (!IsString(v)) {
    return FALSE;
  }

  return MLuaStrHeaderAscii(MLUA_STRHEADER((MLuaGCHeader *)GetPtr(v)));
}

const char *MLuaStringData(MLuaValue v) {
  MLuaGCHeader *gch;
  MLuaStringHeader *sh;

  if (IsShortStr(v)) {
    char *buf = ShortStrBuffer[ShortStrBufferIdx];
    Size len = MLuaShortStrLen(v);
    Size i;
    ShortStrBufferIdx = (ShortStrBufferIdx + 1) % SHORTSTR_BUFFERS;
    for (i = 0; i < len; i++) {
      switch (i) {
      case 0:
        buf[i] = GetShortStrChar0(v);
        break;
      case 1:
        buf[i] = GetShortStrChar1(v);
        break;
      case 2:
        buf[i] = GetShortStrChar2(v);
        break;
      case 3:
        buf[i] = GetShortStrChar3(v);
        break;
      default:
        buf[i] = GetShortStrChar4(v);
        break;
      }
    }
    buf[len] = '\0';
    return buf;
  }

  if (!IsString(v)) {
    return "";
  }

  gch = (MLuaGCHeader *)GetPtr(v);
  sh = MLUA_STRHEADER(gch);
  return MLUA_STRDATA(sh);
}

Bool MLuaStringEqual(MLuaValue a, MLuaValue b) {
  /* For interned strings, pointer equality is sufficient */
  /* For short strings, value equality is sufficient */
  return a == b;
}

int MLuaStringCompare(MLuaValue a, MLuaValue b) {
  const char *sa;
  const char *sb;
  Size lenA;
  Size lenB;
  Size minLen;
  int cmp;

  if (a == b) {
    return 0;
  }

  sa = MLuaStringData(a);
  sb = MLuaStringData(b);
  lenA = MLuaStringLen(a);
  lenB = MLuaStringLen(b);

  minLen = (lenA < lenB) ? lenA : lenB;
  cmp = MemCmp(sa, sb, minLen);

  if (cmp != 0) {
    return cmp;
  }

  /* Strings are equal up to minLen; shorter one is "less" */
  if (lenA < lenB)
    return -1;
  if (lenA > lenB)
    return 1;
  return 0;
}

MLuaValue MLuaStringConcat(MLuaState *L, MLuaValue a, MLuaValue b) {
  const char *sa;
  const char *sb;
  Size lenA;
  Size lenB;
  Size totalLen;
  char *buf;
  MLuaValue result;

  sa = MLuaStringData(a);
  sb = MLuaStringData(b);
  lenA = MLuaStringLen(a);
  lenB = MLuaStringLen(b);
  totalLen = lenA + lenB;

  /* Allocate temporary buffer on heap for concatenation */
  buf = (char *)MLuaAllocNC(L, totalLen + 1);
  if (!buf) {
    L->ErrorMsg = "out of memory";
    return MLUA_NIL;
  }

  MemCpy(buf, sa, lenA);
  MemCpy(buf + lenA, sb, lenB);
  buf[totalLen] = '\0';

  result = MLuaStringNew(L, buf, totalLen);

  /* Note: buf will be collected by GC eventually */
  return result;
}

MLuaValue MLuaStringConcatMany(MLuaState *L, const MLuaValue *vals, int count) {
  Size totalLen = 0;
  Size off = 0;
  int i;
  MLuaGCHeader *gch;
  MLuaStringHeader *sh;
  char *data;
  MLuaValue result;
  MLuaValue *slot;

  for (i = 0; i < count; i++) {
    totalLen += MLuaStringLen(vals[i]);
  }

  /* Short result: assemble into a tiny buffer and return a short
   * string (short strings are never interned). */
  if (totalLen <= MLUA_SHORTSTR_MAX) {
    char tmp[MLUA_SHORTSTR_MAX];
    Size t = 0;
    for (i = 0; i < count; i++) {
      const char *s = MLuaStringData(vals[i]);
      Size n = MLuaStringLen(vals[i]);
      Size j;
      for (j = 0; j < n; j++) {
        tmp[t++] = s[j];
      }
    }
    return MLuaStringNewShort(tmp, totalLen);
  }

  if (totalLen > (Size)MLUA_STR_LEN_MASK) {
    L->ErrorMsg = "string too long";
    return MLUA_NIL;
  }

  if (L->StringTableCap == 0) {
    if (!MLuaStringTableInit(L)) {
      L->ErrorMsg = "out of memory";
      return MLUA_NIL;
    }
  }

  /*
   * Allocate the result object up front and copy the operands straight into
   * its data with (vectorizable) MemCpy -- no temporary buffer and no second
   * copy (unlike routing through MLuaStringNew). Each operand's bytes are
   * consumed immediately, so a short string's rotating data buffer cannot
   * alias across operands. No GC runs inside an allocation (safepoint model),
   * so the operand pointers stay valid throughout.
   */
  gch = MLuaAllocObjectNC(L, OBJTYPE_STRING,
                            sizeof(MLuaStringHeader) + totalLen + 1);
  if (!gch) {
    L->ErrorMsg = "out of memory";
    return MLUA_NIL;
  }
  sh = MLUA_STRHEADER(gch);
  data = (char *)MLUA_STRDATA(sh);

  for (i = 0; i < count; i++) {
    const char *s = MLuaStringData(vals[i]);
    Size n = MLuaStringLen(vals[i]);
    MemCpy(data + off, s, n);
    off += n;
  }
  data[totalLen] = '\0';

  /*
   * Hash incrementally. FNV-1a is a left fold, so
   *   hash(a || b) == fold(hash(a), b's bytes).
   * Start from the FIRST operand's hash (already stored for a long string,
   * O(1); cheaply recomputed for a <=3-byte short string) and fold only the
   * bytes contributed by the remaining operands. For `s = s .. x` this folds
   * just x's few bytes instead of rehashing all of s, collapsing the O(n^2)
   * hashing of a build loop to O(n) -- and yields the exact same hash value,
   * so interning is unaffected.
   */
  {
    Size firstLen = MLuaStringLen(vals[0]);
    Size k;
    U32 hash;
    U8 acc = 0;
    Bool ascii;
    if (IsShortStr(vals[0])) {
      hash = StringHashAscii(data, firstLen, &ascii); /* a few bytes */
    } else {
      hash = MLUA_STRHEADER((MLuaGCHeader *)GetPtr(vals[0]))->Hash;
      ascii = MLuaStrHeaderAscii(MLUA_STRHEADER((MLuaGCHeader *)GetPtr(vals[0])));
    }
    for (k = firstLen; k < totalLen; k++) {
      U8 c = (U8)data[k];
      hash = HASH_STEP(hash, c);
      acc |= c;
    }
    sh->Hash = hash;
    /* ASCII iff the first operand was and the folded bytes stayed below
     * 0x80 -- same incremental structure as the hash. */
    if (ascii && (acc & 0x80U) != 0) {
      ascii = FALSE;
    }
    sh->Length = (U32)totalLen | (ascii ? MLUA_STR_ASCII_BIT : 0);
  }
  result = MakePtr(gch);

  /*
   * Dedup against the intern table using the now-contiguous bytes. Concat
   * results are usually unique (a miss that inserts); on the rare hit the
   * freshly built object is simply left for the collector, preserving the
   * "equal contents => identical pointer" invariant.
   */
  slot = StringTableFind(L, data, totalLen, sh->Hash);
  if (slot && IsPtr(*slot)) {
    return *slot;
  }
  if (!StringTableInsert(L, result, slot)) {
    L->ErrorMsg = "out of memory"; /* un-interned strings must not escape */
    return MLUA_NIL;
  }
  return result;
}
