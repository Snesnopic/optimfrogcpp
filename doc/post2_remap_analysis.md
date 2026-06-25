# post_type=2 value-remap table (tonal/synthetic signals only)

Active only when a channel's transform flag (obj+0x9a global, obj+0x98+ch per-channel) == 1.
Triggered by signals with few distinct values relative to their range (pure sine, ramp). NEVER by
real audio (so the 24/24 standard-preset matrix already passes on realistic sources).

## Functions (all decompiled)
- init `FUN_00017c40` (post=2 vtable[0]): per channel reads A (signext 16b → obj+0x24+ch*4),
  B (signext 16b → obj+0x1c+ch*4), then a half-split flag bit (→ obj+0x98+ch; sets obj+0x9a if any set).
  If flag==0: copy A→obj+0xc+ch*4, B→obj+0x14+ch*4 (identity min/max). [THIS is what we already do.]
  If flag==1: read 4-bit idx (range>>4, cap 0xf); read more via FUN_00016bf0 ×2 + ldexp DAT_196e8(2^52)
  to form a double scale; transform index uVar10 = idx; call `FUN_00017f00(scale, obj, ch, uVar10,
  obj+0x38+ch*0x18, rc)` to build the present-value bitmap. After the per-channel loop, if obj+0x9a:
  call `FUN_000189f0(obj, ch, flag[ch])` per channel to build the inverse map (obj+0x68) + final min/max.
- bitmap builder `FUN_00017f00(scale, obj, ch, transform_idx, mapstruct=obj+0x38+ch*0x18, rc)`:
  A=obj+0x24, B=obj+0x1c. alloc candidate bitmap [A..B] (FUN_00019030). For v in [A..B]:
  cand = transform[transform_idx](scale, v); if A<=cand<=B mark candbitmap[cand]=1.
  Then 4 adaptive ctxs: c0=FUN_00002bd0(2,0x8000), c1(2), c2(2), c3(3,0x8000).
  For v in [A..B]: prevPresent = map[v-1] (0 if oob); nextCand = candbitmap[v+1].
  if candbitmap[v]==1: map[v]=1.
  else: decode bit b from c0 → map[v]=b. if (map[v]!=0 && (nextCand||prevPresent)):
    - prev==0 && nextCand==1: decode from c1; if symbol hits top, clear map[v+1] (and skip it).
    - prev==1 && nextCand==0: decode from c2; if top, clear map[v-1].
    - prev==1 && nextCand==1: decode ternary from c3; cases clear map[v+1] or map[v-1].
  (This refines which candidates are actually present using run/neighbor context.)
- inverse builder `FUN_000189f0(obj, ch, transform_flag)`: alloc inverse table obj+0x68 over [A..B]
  (FUN_00018e70). fill out-of-[min,max] with 0xfffffff. If flag==0: identity table[v]=v, min/max=A/B.
  If flag==1: walk map bitmap (obj+0x38); table[0]=0; for i in 1..B if map[i]==1: table[count++]=i
  (positive side); for i=-1..A if map[i]==1: table[-count2--]=i (negative side). Sets obj+0x14=count-1
  (max index), obj+0xc=min index. → dense index → sparse original value.
- apply `FUN_00018cb0(obj, dest, count)`: if obj+0x9a && count: for each sample/ch:
  dest[i] = table[ch][ dest[i] ]; throw if == 0xfffffff. (table at *(obj+0x68+ch*0x18))

## transforms (DAT_21bd0[idx], double scale, int v) → int candidate
- 0,6 (FUN_00017b70): floor(v*scale)
- 1 (FUN_00017b90): ceil(v*scale)
- 2 (FUN_00017bb0): v<0 ? ceil(v*scale) : floor(v*scale)   (toward zero)
- 3 (FUN_00017be0): v<0 ? floor(v*scale) : ceil(v*scale)    (away from zero)
- 4 (FUN_00017c10): floor(v*scale + 0.5)                     (DAT_19770 = 0.5)
- 5 (FUN_00017c30): lrint(v*scale)                           (round to nearest even)

## Helpers
FUN_00019030(struct, lo, hi): alloc int[] indexed [lo..hi], struct[0]=base-lo (so [v] valid),
struct[2]=lo, struct+0x14=hi, struct[1]=refcount. FUN_00018e70 = same for the inverse (int table).
FUN_00016bf0 = read bits helper (range-coded). DAT_196e8 = 2^52 via ldexp? (scale formed as
(long)(hi<<32|lo) / 2^52). transform_idx (uVar10) is the 4-bit idx read before the scale.

## Implementation notes
- Reuses FastCtx-style adaptive trees (FUN_00002bd0/df0) already ported.
- Our current OFR_PostProcessor only handles flag==0 (the common case). Adding flag==1 requires faithfully
  porting FUN_00017c40's flag-1 branch + FUN_00017f00 + FUN_000189f0 + FUN_00018cb0 + transforms (~400-500 lines).
- Integer + a few doubles (the scale + transforms); verify scale formation (FUN_00016bf0/ldexp) via LLDB.
- Test: original sine-source files (mono_p1, mono_p2) trigger flag==1 and currently FAIL.
