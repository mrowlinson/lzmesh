# docs/

- `CLEANROOM-LOG.md` — append-only session log, one entry per session
  (clean-side format: inputs consulted + sha256 of spec version read).
- `DERIVATION.md` — per-file provenance: what each source file was
  written from (spec version + clauses pointed at, never copied).
- Repo-level decisions live in `../DECISIONS.md`.
- API reference is `../include/lzmesh.h` (stub until lanes land).
