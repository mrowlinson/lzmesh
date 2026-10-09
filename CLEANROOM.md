# CLEANROOM.md — how this port was built without ever reading Apple code

One page. Behavior and own measurements only — no Apple internals,
no disassembly, no third-party LZMESH code anywhere in this tree.

## The wall

Two sides, one direction. The RE side measured Apple's shipping codec
as a black box (feed bytes in, record bytes out) and wrote down what it
does as behavior prose: numbered SHALL clauses with byte-level proofs.
The implementation side read that prose — and only that prose — plus
the battery CLI contract and its own lane's divergence reports. It
never opened the Apple binary, never read decompiler output, never saw
another lane's work or any spec-side transcript. Blocked on a clause,
a lane filed a one-way spec query instead of guessing.

## Black-box proof

Every behavior claim in this port was proven by executing two CLIs and
comparing bytes: this port's `port_cli` against an Apple oracle adapter
driven through the same stdio contract. Operators never read
implementation source; the 15792-cell gated battery (see README.md)
compares output bytes only. The oracle adapter source ships pinned for
audit (`tests/battery/oracle_probe.c`), no default target builds it,
and this library never links Apple code.

## Hashes, not bytes

Apple artifacts are identified by sha256 + size + source path only —
never committed, never copied, never quoted. The derivation log names
tool versions and directory inventories where the method needs them,
but contains zero Apple bytes, zero addresses, zero disassembly. Any
file that ever held disassembler output stayed in private scratch and
is excluded from this tree by construction (see `release/export.sh`).

## Neutral ground

The public prefix is `lzmesh_`; upstream and Apple-private identifier
shapes are forbidden. No spec prose or constants tables are reproduced
in the sources — per-file provenance points at spec sections from
`docs/DERIVATION.md`, and every session logged its exhaustive inputs
(spec version sha256 included) in `docs/CLEANROOM-LOG.md`.

## Pointers

- `DERIVATION-CLEANROOM.md` — RE-track derivation log (this tree root)
- `docs/SPEC-S9CR.md` — the behavior spec section the port implements
- `docs/CLEANROOM-LOG.md` — per-session input logs (implementation side)
- `docs/DERIVATION.md` — per-file provenance (what each file was written from)
- `README.md` — what the port proves today (byte-claim + perf)
