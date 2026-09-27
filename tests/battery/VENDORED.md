# Vendored battery framework

Source: frozen scaffold copy. Pinned 2026-09-17. Files:

| File | sha256 | Lines |
|---|---|---|
| battery.py | 40584d972fbc1478425fd4bc0ccea0bf02d8b69a2e5fdc7af8dc9213a1e48afa | 340 |
| battery.py (lanes/f6-coverage, corpus-only delta) | a948bcbc6e3cc1af045291dadcb91a1cd0c043493589386485e52d711d33ff39 | 375 |
| oracle_probe.c | ebbe9f426a26c14196bf50ed7740891581c037ad32e93680f360ca60b0e5dcc4 | 104 |
| README.md | b95a0badd1310212cff72068cef166d3a94a88fc319a97cb6d221da87c0c73ac | 49 |

Rules:

- Vendored copy. Scaffold stays frozen upstream; lanes deploy copies.
- Never edit in place. Resync = whole-file copy from scaffold + new pin row.
- Framework is format-agnostic (opaque byte functions, no codec logic).
  Any edit touching comparison/classification invalidates prior campaigns;
  rerun whole tier after resync.
- F6 delta touches corpus generation only (new vectors append after the
  legacy rotation; comparison/classification untouched). Legacy cells keep
  prior verdicts; new cells are additive (see LANE-F6).
- `oracle_probe` builds from the pinned `oracle_probe.c` in this
  directory (needs an Apple libcompression selectable via ORACLE_LIB
  at run time). This repo never links an Apple binary; no Makefile
  target builds oracle_probe here.
