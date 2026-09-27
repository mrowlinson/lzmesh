# PERF.md — method + results (shell)

No numbers yet — including at decoder-complete candidacy (2026-09-19):
no perf runs have been made, so all tables below stay TBD and
RELEASE-CHECKLIST gate 7 is RED. Tables are filled by perf runs once
the bench scope is settled (OPEN, DECISIONS.md O5: black-box via
`port_cli` vs in-process timing).

## Method (pending O5)

- Inputs: fixed corpus + sizes TBD (expected to reuse the battery size
  ladder and pattern rotation; exact set fixed at first perf run and
  recorded here).
- Levels: 0, 1, 5, 9, encode and decode separately.
- Metrics: throughput (MiB/s) and ratio where applicable; machine,
  toolchain, and flags recorded per run.
- Each run records: date, host, compiler + flags, corpus hash, lane
  commit. No Apple version-specific performance claims beyond the
  recorded oracle runs.

## Results

### Encode throughput (MiB/s) — TBD

| Corpus | Level 0 | Level 1 | Level 5 | Level 9 |
|--------|---------|---------|---------|---------|
| TBD | — | — | — | — |

### Decode throughput (MiB/s) — TBD

| Corpus | Port | Oracle (27 build) |
|--------|------|-------------------|
| TBD | — | — |

### Encode size (bytes out per corpus input) — TBD

| Corpus | Level 0 | Level 1 | Level 5 | Level 9 | Oracle (27 build) |
|--------|---------|---------|---------|---------|-------------------|
| TBD | — | — | — | — | — |

## Placeholders

- Bench scope + harness location (`bench/` wiring).
- Corpus definition + hash.
- All table cells.
- Run provenance rows (date/host/toolchain/lane).
