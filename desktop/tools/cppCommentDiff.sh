#!/bin/sh
# Second opinion on a C++ comment sweep, from the compiler rather than a hand-rolled
# scanner: preprocess each file at <gitRef> and in the working tree with comments dropped
# (-fpreprocessed leaves includes and macros alone), then diff. Raw strings, digit
# separators and trigraphs become the preprocessor's problem, not ours.
#
# Usage: desktop/tools/cppCommentDiff.sh <gitRef> <file...>   (exits 1 on any difference)
#
# Needs a real gcc: Apple's clang-as-gcc rejects -fpreprocessed, and this exits 2 rather
# than compare two empty files. The portable twin is tools/commentOnlyDiff.mjs.
set -eu

ref=${1:?usage: cppCommentDiff.sh <gitRef> <file...>}
shift
[ $# -gt 0 ] || { echo "usage: cppCommentDiff.sh <gitRef> <file...>" >&2; exit 2; }

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# Pick a compiler that takes the flags AND really drops the comment — never trust a run
# whose driver quietly emitted nothing.
printf 'int a = 1; // note\n' > "$work/probe.cpp"
cc=
for c in gcc c++ g++-15 g++-14 g++-13; do
  command -v "$c" >/dev/null 2>&1 || continue
  out=$("$c" -fpreprocessed -dD -E -P -x c++ "$work/probe.cpp" 2>/dev/null) || continue
  case "$out" in
    *note*) continue ;;                        # comments survived: not a strip
    *"int a = 1;"*) cc=$c; break ;;
  esac
done
[ -n "$cc" ] || { echo "cppCommentDiff: no compiler here takes -fpreprocessed; use tools/commentOnlyDiff.mjs" >&2; exit 2; }

# Comment-free text, blank lines and edge whitespace dropped — a removed comment must not
# register as a change of its own.
strip() { # <file> <out>
  "$cc" -fpreprocessed -dD -E -P -x c++ "$1" > "$2.raw"
  sed -e 's/[[:space:]]*$//' -e 's/^[[:space:]]*//' -e '/^$/d' "$2.raw" > "$2"
}

status=0
for f in "$@"; do
  git show "$ref:./$f" > "$work/then.cpp"
  strip "$work/then.cpp" "$work/then.txt"
  strip "$f" "$work/now.txt"
  if diff -q "$work/then.txt" "$work/now.txt" >/dev/null; then
    echo "  OK       $f"
  else
    status=1
    echo "  CHANGED  $f"
    diff "$work/then.txt" "$work/now.txt" | head -20 | sed 's/^/             /'
  fi
done
exit $status
