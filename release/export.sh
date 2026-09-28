#!/bin/bash
# SPDX-License-Identifier: 0BSD
# export.sh — deterministic public-tree exporter (lanes/export-tool).
#
# usage: export.sh <lane-4-sha> <outdir>
#
# Builds the public tree (mrowlinson/lzmesh layout) from the given commit
# of THIS checkout into an empty outdir:
#   1. git archive <sha> port/ -> outdir (strip top level; tracked files
#      only, so worktree build outputs never enter).
#   2. Belt-and-braces excludes: research/ tmp/ results/ *.a port_cli
#      *.o *.profraw *.profdata tmp-selftest-* __pycache__.
#   3. Inject root LICENSE (0BSD, byte-exact, embedded below).
#   4. Root README.md = archived port/README.md, which on the export
#      branch is the committed copy of tmp/export-tool/README.md (the
#      refreshed public README). Sourced from the commit, not scratch,
#      so the export is a pure function of <sha>.
#   5. Inject root .gitignore: static template (verbatim b8788aa lines)
#      + unit-binary list generated from archived tests/unit/test_*.c.
# Prints: file count + tree sha256. Re-runnable byte-identical.
set -euo pipefail

if [ $# -ne 2 ]; then
  echo "usage: $0 <lane-4-sha> <outdir>" >&2
  exit 2
fi
SHA=$1
OUT=$2

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
case "$OUT" in
  /*) ;;
  *) OUT="$(pwd)/$OUT" ;;
esac
cd "$ROOT"
if ! git cat-file -e "$SHA^{commit}" 2>/dev/null; then
  echo "export.sh: unknown commit: $SHA" >&2
  exit 2
fi

if [ -e "$OUT" ] && [ -n "$(ls -A "$OUT")" ]; then
  echo "export.sh: outdir not empty: $OUT" >&2
  exit 2
fi
mkdir -p "$OUT"

git archive "$SHA" port | tar -x -C "$OUT" --strip-components=1

# Excludes (none are tracked today; enforced so a future tracked
# research/ or tmp/ can never leak into the public tree).
rm -rf "$OUT/research" "$OUT/tmp" "$OUT/results"
find "$OUT" \( -name '*.a' -o -name 'port_cli' -o -name '*.o' \
  -o -name '*.profraw' -o -name '*.profdata' -o -name 'tmp-selftest-*' \
  -o -name '__pycache__' \) -prune -exec rm -rf {} +

# LICENSE — byte-exact 0BSD text (cf. public b8788aa LICENSE).
cat > "$OUT/LICENSE" <<'EOF'
Copyright (C) 2026 by Michael Rowlinson

Permission to use, copy, modify, and/or distribute this software for
any purpose with or without fee is hereby granted.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
PERFORMANCE OF THIS SOFTWARE.
EOF

# README.md arrives via the archive (port/README.md); it must exist.
if [ ! -f "$OUT/README.md" ]; then
  echo "export.sh: archived tree lacks port/README.md" >&2
  exit 1
fi

# .gitignore: static template + generated unit-binary list.
{
  printf '%s\n' \
    '# Public-tree ignores (DECISIONS.md D8: research/ + tmp/ from first commit).' \
    'research/' \
    'tmp/' \
    'tmp-selftest-*/' \
    '' \
    '# Build artifacts.' \
    '*.o' \
    '*.a' \
    'port_cli' \
    'results/' \
    '' \
    '# Python droppings.' \
    '__pycache__/' \
    '' \
    '# Built unit-test binaries (sources are test_*.c).'
  ( cd "$OUT/tests/unit" && ls test_*.c | sed 's/\.c$//; s/^/tests\/unit\//' )
  printf '%s\n' '!tests/unit/test_*.c'
} > "$OUT/.gitignore"

NFILES=$(find "$OUT" -type f | wc -l)
TREESHA=$(cd "$OUT" && find . -type f -exec sha256sum {} + | sort -k2 | sha256sum | cut -d' ' -f1)
echo "files: $NFILES"
echo "tree-sha256: $TREESHA"
