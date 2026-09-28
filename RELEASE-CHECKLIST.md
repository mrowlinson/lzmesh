# RELEASE-CHECKLIST.md — gates before publish

Every box must be checked before the release described in
`docs/RELEASING.md` proceeds. Each gate names its verification command;
run commands from the repository root. Gates are ordered; each must pass
before the next counts (`docs/TESTING.md`).

## 1. License decided and committed

- [ ] Coordinator has named the license (DECISIONS.md D10/O1); `LICENSE`
      (and `NOTICE` if applicable) committed at root; file-header policy
      applied or explicitly waived.
- Verify: `ls LICENSE NOTICE 2>/dev/null; git status --short` shows the
  license files tracked. Release is blocked while `README.md` still says
  OPEN.

## 2. Unit tests green

- [ ] `make unit` builds every `tests/unit/test_*.c` against
      `liblzmesh.a` and each binary exits 0 (XFAIL/XPASS lines never fail
      the gate; run from repo root so fixtures resolve).
- Verify: `make clean && make unit`

## 3. Battery framework sanity

- [ ] `make selftest` passes (python3 only; loopback adapters, no codec).
- Verify: `make selftest` then confirm no `tmp-selftest-*` dirs remain
  (`ls -d tmp-selftest-* 2>/dev/null` prints nothing).

## 4. Smoke tier PASS against two Apple builds

- [ ] `make smoke` verdict PASS with zero divergence cells, crashes,
      timeouts, or skipped cells — run once per Apple build with separate
      `--out` dirs (see `docs/TESTING.md` oracle setup).
- Verify:
  `ORACLE_LIB=<build-1> make smoke` and
  `ORACLE_LIB=<build-2> make smoke` (distinct builds; default
  `/usr/lib/libcompression.dylib` counts as one); each run's
  `summary.json` reads PASS and `findings.jsonl` is empty.

## 5. Full tier PASS against two Apple builds

- [ ] `make full` (77 seeds, selectors `e00,e01,e05,e09`) verdict PASS
      under the same zero-tolerance rule, once per Apple build.
- Verify:
  `ORACLE_LIB=<build-1> make full` and
  `ORACLE_LIB=<build-2> make full`; each run's `summary.json` reads PASS
  and `findings.jsonl` is empty. Single-build-only evidence blocks the
  release. Threshold confirmation OPEN (O7): if the coordinator sets a
  nonzero threshold, record it here and in `docs/TESTING.md` before
  counting this gate.

## 6. Fuzz clean

- [ ] Roundtrip, decode, and determinism fuzzers (planned in
      `docs/TESTING.md`) built and run to their agreed budgets with no
      crash, hang, exit-discipline violation, or nondeterminism.
- Verify: TBD — harness sources, seed corpus location, and
  time/iteration budgets are all OPEN (see `docs/TESTING.md`
  placeholders). Fuzzing never replaces gates 4–5. This gate cannot pass
  until the harnesses exist; record the exact commands here when they do.

## 7. Perf MUSTs recorded

- [ ] `docs/PERF.md` tables filled (encode/decode throughput, encode
      size) with run provenance rows (date, host, compiler plus flags,
      corpus hash, lane commit) for levels 0/1/5/9.
- Verify: `docs/PERF.md` contains no TBD cells for this release. Bench
  scope is OPEN (O5: black-box via `port_cli` versus in-process timing);
  the release MUSTs are: scope decided, corpus hash recorded, and every
  table cell filled or explicitly waived by the coordinator. No
  Apple-version-specific performance claims beyond recorded oracle runs.

## 8. Clean-room audit clean

- [ ] `docs/CLEANROOM-LOG.md` logs every session with the exact spec
      version plus sha256 read; `docs/DERIVATION.md` carries per-file
      provenance; no session read forbidden sources (Apple binary,
      third-party sources, other lanes, spec-side transcripts,
      `research/`, battery framework source).
- [ ] No forbidden identifiers in the tree: no upstream `mesh_*` /
      `MESH_*`, no Apple-private `Msh*` / `msh_*` outside the documented
      naming rule itself (DECISIONS.md D4, `include/lzmesh.h` header
      comment).
- [ ] No path references from `src/`, `tests/`, or `docs/` into
      `research/`; `.gitignore` covers `research/` + `tmp/` (DECISIONS.md
      D8); no decompiler output, Apple binary bytes, or transcript
      material anywhere in the tree.
- Verify: `grep -rn --exclude-dir=.git -E 'msh_|Msh|[^_a-zA-Z]mesh_[a-z]' src include tests bench docs 2>/dev/null`
  returns only the naming-rule mentions; `grep -rn 'research/' src tests
  docs Makefile 2>/dev/null` returns nothing; `git status --short` shows
  no `research/` or `tmp/` entries.

## 9. Docs and changelog consistent

- [ ] `README.md` status line no longer says skeleton (or the release is
      explicitly a skeleton release); `CHANGELOG.md` `[Unreleased]`
      section moved to the release version with date; `docs/RELEASING.md`
      followed step by step.
- Verify: `grep -n 'OPEN\|TBD\|skeleton' README.md CHANGELOG.md
  docs/RELEASING.md` returns nothing unexplained; every remaining OPEN
  has a coordinator waiver recorded in `DECISIONS.md`.

## 10. Release tag and publication location decided

- [ ] Version number, tag name, hosting service, repository name, and
      upload artifact list all named by the coordinator (all OPEN — no
      directive found in the allowed reads).
- Verify: values recorded in `docs/RELEASING.md`; tag exists locally
  (`git tag -l`) before any push.
