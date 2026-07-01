#!/usr/bin/env bash
# OptimFROG conformance suite — bidirectional proof against the reference binaries.
#   A) every .ofr the reference encoder produces, our decoder decodes identically
#   B) every .ofr our encoder produces, the reference decoder decodes losslessly
# Correctness only; runtime is irrelevant by design.
#
# Env knobs: NFILES (corpus files, default 8), PRESETS, BPSLIST, OURDEC=0 to skip our-decoder side.
set -u

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OFR="${OFR:-$HOME/Downloads/OptimFROG_OSX_x64_5100/ofr}"
OFRENC="${OFRENC:-$ROOT/build/ofrenc}"
ODEC="${ODEC:-$ROOT/build/optimfrogcpp}"
REFGEN="${REFGEN:-/tmp/ref_gen}"
DYLIBDIR="${DYLIBDIR:-$ROOT}"          # holds libOptimFROG.0.dylib for ref_gen
CORPUS="${CORPUS:-$HOME/Downloads/WAVTESTS}"
WORK="${WORK:-/tmp/conformance}"
PRESETS="${PRESETS:-0 2 4 7 10 max}"
BPSLIST="${BPSLIST:-16}"
NFILES="${NFILES:-8}"
OURDEC="${OURDEC:-1}"

mkdir -p "$WORK/raw" "$WORK/tmp"
pass=0; fail=0; faillog="$WORK/failures.txt"; : > "$faillog"
note() { echo "FAIL: $*" >> "$faillog"; fail=$((fail+1)); }
ok()   { pass=$((pass+1)); }

st_of() { case "$1" in 8) echo SINT8;; 16) echo SINT16;; 24) echo SINT24;; 32) echo SINT32;; esac; }
cc_of() { [ "$1" = 2 ] && echo STEREO_LR || echo MONO; }

# retry a command up to 3x, succeeding only when the sentinel output file is non-empty.
# retries ONLY on crash/empty output (Rosetta flakiness); a real mismatch still produces
# output and is never hidden by retry.
rtry_out() { local out="$1"; shift; local i; for i in 1 2 3; do rm -f "$out"; "$@" >/dev/null 2>&1; [ -s "$out" ] && return 0; done; return 1; }

refdec() { rtry_out "$2" env DYLD_LIBRARY_PATH="$DYLIBDIR" "$REFGEN" "$1" "$2"; }
ourdec() { rm -f "$WORK/tmp/test.raw"; local i; for i in 1 2 3; do ( cd "$WORK/tmp" && "$ODEC" "$1" >/dev/null 2>&1 ); [ -s "$WORK/tmp/test.raw" ] && { cp -f "$WORK/tmp/test.raw" "$2"; return 0; }; done; return 1; }

# direction A: reference encode (per preset) -> ref + our decode -> must equal original
dirA() {
  local raw="$1" ch="$2" bps="$3" rate="$4" tag="$5" plist="$6"
  local st cc; st=$(st_of "$bps"); cc=$(cc_of "$ch")
  for p in $plist; do
    local of="$WORK/tmp/a.ofr" rr="$WORK/tmp/a_ref.raw" ro="$WORK/tmp/a_our.raw"
    rtry_out "$of" "$OFR" --preset "$p" --encode --raw --channelconfig "$cc" --sampletype "$st" --rate "$rate" "$raw" --output "$of"
    if [ ! -s "$of" ]; then note "A enc $tag p$p (no ofr output)"; continue; fi
    refdec "$of" "$rr"
    if ! cmp -s "$raw" "$rr"; then note "A ref-decode $tag p$p (ref != original)"; else ok; fi
    if [ "$OURDEC" = 1 ]; then
      ourdec "$of" "$ro"
      if ! cmp -s "$rr" "$ro"; then note "A our-decode $tag p$p (ours != ref)"; else ok; fi
    fi
  done
}

# direction B: our encode (default + best) -> reference decode -> must equal original
dirB() {
  local raw="$1" ch="$2" bps="$3" rate="$4" tag="$5" modes="$6"
  for mode in $modes; do
    local of="$WORK/tmp/b.ofr" rr="$WORK/tmp/b_ref.raw"
    case $mode in
      default) rtry_out "$of" env -u OFR_BEST OFR_PRED=1 OFR_ENT=1 "$OFRENC" "$raw" "$of" "$rate" "$ch" "$bps";;
      best)    rtry_out "$of" env OFR_BEST=1 "$OFRENC" "$raw" "$of" "$rate" "$ch" "$bps";;
      ent2)    rtry_out "$of" env OFR_PRED=1 OFR_ENT=2 "$OFRENC" "$raw" "$of" "$rate" "$ch" "$bps";;
      pred3)   rtry_out "$of" env OFR_PRED=3 OFR_ENT=3 "$OFRENC" "$raw" "$of" "$rate" "$ch" "$bps";;
    esac
    if [ ! -s "$of" ]; then note "B enc $tag $mode (no output)"; continue; fi
    refdec "$of" "$rr"
    if ! cmp -s "$raw" "$rr"; then note "B $tag $mode (ref != original)"; else ok; fi
  done
}

run_raw() { # path ch bps rate tag plist bmodes
  dirA "$1" "$2" "$3" "$4" "$5" "$6"
  dirB "$1" "$2" "$3" "$4" "$5" "$7"
}

echo "=== preparing corpus (raw) ==="
python3 "$ROOT/tests/conformance/gen_synthetic.py" "$WORK/synth" >/dev/null

# convert N corpus files to raw at their native (16-bit stereo assumed for the bulk)
i=0
if [ -d "$CORPUS" ]; then
  for w in "$CORPUS"/*.wav; do
    [ -f "$w" ] || continue
    fmt=$(ffprobe -hide_banner -loglevel error -show_entries stream=sample_fmt,channels -of csv=p=0 "$w" 2>/dev/null)
    [ "$fmt" = "s16,2" ] || continue
    r="$WORK/raw/corpus${i}__2ch__16bps.raw"
    ffmpeg -y -i "$w" -f s16le -ac 2 -ar 44100 "$r" >/dev/null 2>&1
    i=$((i+1)); [ $i -ge "$NFILES" ] && break
  done
fi
echo "corpus raws: $i ; synthetic: $(ls "$WORK/synth" | wc -l | tr -d ' ')"

# SYNTH_PRESETS: applied to small synthetic files (cheap → exhaustive).
# CORPUS_PRESETS: applied to large real-audio files. HEAVYPRESETS (slow) are
# only run on the first HEAVYN corpus files to bound wall-clock.
SYNTH_PRESETS="${SYNTH_PRESETS:-$PRESETS}"
CORPUS_PRESETS="${CORPUS_PRESETS:-$PRESETS}"
HEAVYPRESETS="${HEAVYPRESETS:-10 max}"
HEAVYN="${HEAVYN:-9999}"
SYNTH_BMODES="${SYNTH_BMODES:-default best ent2 pred3}"
CORPUS_BMODES="${CORPUS_BMODES:-default best ent2 pred3}"
CORPUS_BMODES_LIGHT="${CORPUS_BMODES_LIGHT:-default ent2 pred3}"

is_heavy() { case " $HEAVYPRESETS " in *" $1 "*) return 0;; *) return 1;; esac; }

echo "=== running (synth: $SYNTH_PRESETS ; corpus: $CORPUS_PRESETS heavy[$HEAVYPRESETS]<=${HEAVYN}f ; bps: $BPSLIST) ==="
ci=0
for bps in $BPSLIST; do
  for r in "$WORK"/synth/*__*bps.raw "$WORK"/raw/*__*bps.raw; do
    [ -f "$r" ] || continue
    base=$(basename "$r")
    fbps=$(echo "$base" | sed -E 's/.*__([0-9]+)bps\.raw/\1/')
    [ "$fbps" = "$bps" ] || continue
    fch=$(echo "$base" | sed -E 's/.*__([0-9]+)ch__.*/\1/')
    bmodes="$SYNTH_BMODES"
    case "$r" in
      */raw/*)  # corpus: drop heavy presets/modes past HEAVYN files
        plist=""; for p in $CORPUS_PRESETS; do
          if is_heavy "$p" && [ "$ci" -ge "$HEAVYN" ]; then continue; fi
          plist="$plist $p"
        done
        if [ "$ci" -ge "$HEAVYN" ]; then bmodes="$CORPUS_BMODES_LIGHT"; else bmodes="$CORPUS_BMODES"; fi
        ci=$((ci+1));;
      *) plist="$SYNTH_PRESETS";;
    esac
    run_raw "$r" "$fch" "$bps" 44100 "$base" "$plist" "$bmodes"
  done
done

echo "=== RESULTS ==="
echo "PASS=$pass FAIL=$fail"
if [ "$fail" -gt 0 ]; then echo "--- failures (first 30) ---"; head -30 "$faillog"; fi
[ "$fail" -eq 0 ]
