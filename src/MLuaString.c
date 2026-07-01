/*
 * MicroLua - MLuaString.c
 * Interned string implementation with per-state string table
 */

#include "MLuaString.h"
#include "MLuaGC.h"

/* ========================================================================== */
/* String Hash (FNV-1a)                                                       */
/* ========================================================================== */

#define FNV_OFFSET_BASIS 2166136261U
#define FNV_PRIME 16777619U

U32 MLuaStringHash(const char *str, Size len) {
  U32 hash = FNV_OFFSET_BASIS;
  Size i;

  for (i = 0; i < len; i++) {
    hash ^= (U8)str[i];
    hash *= FNV_PRIME;
  }

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

static MLuaValue *StringTableFind(MLuaState *L, const char *str, Size len,
                                  U32 hash) {
  Size index;
  Size i;
  MLuaValue v;
  MLuaGCHeader *gch;
  MLuaStringHeader *sh;
  const char *existingStr;

  if (L->StringTableCap == 0) {
    return NULL;
  }

  index = hash % L->StringTableCap;

  for (i = 0; i < L->StringTableCap; i++) {
    Size slot = (index + i) % L->StringTableCap;
    v = L->StringTable[slot];

    if (IsNil(v)) {
      /* Empty slot - string not found, but could insert here */
      return &L->StringTable[slot];
    }

    if (!IsPtr(v)) {
      continue; /* Shouldn't happen, but skip */
    }

    /* Check if this is the string we're looking for */
    gch = (MLuaGCHeader *)GetPtr(v);
    sh = MLUA_STRHEADER(gch);

    if (sh->Hash == hash && (Size)sh->Length == len) {
      existingStr = MLUA_STRDATA(sh);
      if (MemCmp(existingStr, str, len) == 0) {
        /* Found it! */
        return &L->StringTable[slot];
      }
    }
  }

  return NULL; /* Table full? Shouldn't happen with proper resizing */
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
          StringTableFind(L, MLUA_STRDATA(sh), (Size)sh->Length, sh->Hash);
      if (slot && IsNil(*slot)) {
        *slot = v;
        L->StringTableCount++;
      }
    }
  }

  /* Note: old entries are on the heap and will be collected by GC */
  return TRUE;
}

static Bool StringTableInsert(MLuaState *L, MLuaValue strVal) {
  Size threshold;
  MLuaGCHeader *gch;
  MLuaStringHeader *sh;
  MLuaValue *slot;

  /* Check if resize needed */
  threshold = (L->StringTableCap * MLUA_STRING_TABLE_LOAD_FACTOR) / 100;
  if (L->StringTableCount >= threshold) {
    if (!StringTableResize(L)) {
      return FALSE;
    }
  }

  gch = (MLuaGCHeader *)GetPtr(strVal);
  sh = MLUA_STRHEADER(gch);

  slot = StringTableFind(L, MLUA_STRDATA(sh), sh->Length, sh->Hash);
  if (slot && IsNil(*slot)) {
    *slot = strVal;
    L->StringTableCount++;
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
    L->StringTableCount = liveCount;
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
          StringTableFind(L, MLUA_STRDATA(sh), (Size)sh->Length, sh->Hash);
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

  if (len > (Size)0xFFFFFFFFU) {
    return MLUA_NIL;
  }

  /* Initialize string table if needed */
  if (L->StringTableCap == 0) {
    if (!MLuaStringTableInit(L)) {
      return MLUA_NIL;
    }
  }

  /* Compute hash */
  hash = MLuaStringHash(str, len);

  /* Check if string already exists */
  existing = StringTableFind(L, str, len, hash);
  if (existing && !IsNil(*existing)) {
    return *existing; /* Return existing interned string */
  }

  /* Allocate new string */
  headerSize = sizeof(MLuaStringHeader);
  if (len > (Size)-1 - headerSize - 1) {
    return MLUA_NIL;
  }
  totalDataSize = headerSize + len + 1; /* +1 for null terminator */

  gch = MLuaAllocObject(L, OBJTYPE_STRING, totalDataSize);
  if (!gch) {
    return MLUA_NIL;
  }

  sh = MLUA_STRHEADER(gch);
  sh->Hash = hash;
  sh->Length = (U32)len;

  data = (char *)MLUA_STRDATA(sh);
  MemCpy(data, str, len);
  data[len] = '\0';

  result = MakePtr(gch);

  /* Insert into string table */
  StringTableInsert(L, result);

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
  return sh->Length;
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
  buf = (char *)MLuaAlloc(L, totalLen + 1);
  if (!buf) {
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

  if (L->StringTableCap == 0) {
    if (!MLuaStringTableInit(L)) {
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
  gch = MLuaAllocObject(L, OBJTYPE_STRING,
                        sizeof(MLuaStringHeader) + totalLen + 1);
  if (!gch) {
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
    if (IsShortStr(vals[0])) {
      hash = MLuaStringHash(data, firstLen); /* <= 3 bytes */
    } else {
      hash = MLUA_STRHEADER((MLuaGCHeader *)GetPtr(vals[0]))->Hash;
    }
    for (k = firstLen; k < totalLen; k++) {
      hash ^= (U8)data[k];
      hash *= FNV_PRIME;
    }
    sh->Hash = hash;
  }
  sh->Length = (U32)totalLen;
  result = MakePtr(gch);

  /*
   * Dedup against the intern table using the now-contiguous bytes. Concat
   * results are usually unique (a miss that inserts); on the rare hit the
   * freshly built object is simply left for the collector, preserving the
   * "equal contents => identical pointer" invariant.
   */
  slot = StringTableFind(L, data, totalLen, sh->Hash);
  if (slot && !IsNil(*slot)) {
    return *slot;
  }
  StringTableInsert(L, result);
  return result;
}
