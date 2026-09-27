# DERIVATION.md — per-file provenance

One row per committed source file: what it was written from. Point at
spec sections; never copy spec text or constants tables here.

| File | Written from | Notes |
|---|---|---|
| `src/lzmesh_dec.c` | cleanroom-authored; behavior from spec prose only (file header: "SPECFINAL.md only") | spec sections cited inline (S2.x etc.); black-box battery verified |
| `src/lzmesh_enc.c` | cleanroom-authored; behavior from spec prose only (file header: "SPECFINAL.md only") | spec sections cited inline (S1.x/S5.x etc.); black-box battery verified |
| `src/port_cli.c` | cleanroom-authored from the CLI contract in `tests/battery/README.md` | stdio plumbing only; no codec logic |
| `include/lzmesh.h` | cleanroom-authored; shape follows Apple's public buffer API + spec-settled behavior | naming rule (`lzmesh_` prefix) stated in-file |
| `tests/unit/test_*.c` (38 files) | cleanroom-authored over the public API only | e00-vector tests consume black-box oracle bytes + manifest cells (see `UNITLOG.md`) |
| `tests/unit/vectors/` (25 `.bin` + `manifest.txt`) | black-box functional data: test inputs + recorded oracle output bytes | per-file traceability in `manifest.txt`; no Apple library bytes, no Mach-O |
| `tests/battery/battery.py`, `tests/battery/oracle_probe.c`, `tests/battery/README.md` | vendored pinned copy of the frozen scaffold | sha256 pins in `tests/battery/VENDORED.md`; never edited in place |
| `Makefile`, `BUILD.md`, root docs, `docs/` shell, `bench/README.md`, `src/README.md`, `tests/*/README.md` | port directives recorded in `DECISIONS.md` (D1–D14) | skeleton; zero spec clauses copied in |
| `docs/CLEANROOM-LOG.md` | session log written by clean-side sessions | append-only; inputs + spec-version sha256 per entry |
| `docs/SPEC-S9CR.md` | port-track handoff draft from black-box-vs-oracle filings (dated 2026-09-22, in-file) | normative SHALL clauses for the port; owed items don't gate |
| `docs/API.md`, `docs/PERF.md`, `docs/TESTING.md`, `docs/BUILD.md`, `docs/RELEASING.md` | cleanroom-authored from `include/lzmesh.h` + `DECISIONS.md` + recorded run evidence | `API.md` states its two sources in-file; `PERF.md` tables TBD (no runs yet) |

Dates: files landed during the Stage-6 blind build and the
2026-09-18/19 gap-closure sessions logged in `docs/CLEANROOM-LOG.md`;
no per-file landing date is on record — none is invented here.
