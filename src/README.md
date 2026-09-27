# src/ — port sources (implementer lanes write these)

Empty until lanes land. Expected contents:

- `*.c` implementing `include/lzmesh.h`, written from the final spec
  prose only (see lane brief READ-ALLOW; coordinator names spec path).
- `port_cli.c` — battery CLI behind the contract in
  `tests/battery/README.md`: `port_cli enc|dec <selector-hex>`,
  stdio byte pipe, exit 0/10, DECODE_SIZE honored, deterministic.

Rules: spec prose in, code out. No Apple binary linkage, no third-party
codec code, no decompiler output (forbidden to possess, not just commit).
Build: `make` at repo root. Stock cc, macOS-tested (Linux build
unrun, expected-trivial).
