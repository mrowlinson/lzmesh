# Battery lane — CLI contract + run instructions

## CLI contract (BOTH sides implement exactly this)

```
<cli> enc <selector-hex>    stdin: raw input bytes     stdout: encoded bytes
<cli> dec <selector-hex>    stdin: encoded bytes       stdout: decoded bytes
```

- `selector-hex`: e.g. `e05`. Lowercase hex, no `0x`.
- `dec` receives `DECODE_SIZE` env: expected output length + 1024 slack. Size destination buffer from it.
- Exit 0: success, stdout holds exactly the output bytes, nothing else.
- Exit 10: codec returned 0 (clean refusal). Stdout ignored.
- Any other exit / signal / stderr traceback: harness failure, counts as CRASH against that side.
- Deterministic: same input + selector → same bytes, every run.
- No stdout chatter. No network. No reads outside argv/stdin/env.

Oracle side: build `oracle_probe` from `oracle_probe.c` (`clang -O2 -o oracle_probe oracle_probe.c`).
`ORACLE_LIB` env selects Apple build under test. Default `/usr/lib/libcompression.dylib`.

Port side: each implementer lane ships `<lane>/port_cli` meeting the contract above, built from lane source via lane BUILD notes.

## Runs

```
python3 battery.py --selftest                                        # framework sanity, no codec
python3 battery.py --oracle ./oracle_probe --port lanes/impl-a/port_cli \
  --out lanes/battery/results/impl-a --tier smoke                     # fast gate
python3 battery.py --oracle ./oracle_probe --port lanes/impl-a/port_cli \
  --out lanes/battery/results/impl-a --tier full --selectors e00,e01,e05,e09
```

Second Apple build: same command with `ORACLE_LIB=<other build>`, separate `--out`. Verdict needs both (see VERDICT.md).

## Outputs (per --out dir)

- `findings.jsonl`: one row per divergence (bucket, check, input id, sizes, shas).
- `summary.json`: cell count, bucket histogram, seconds, verdict PASS/FAIL.
- `vectors/`: failing input bytes only (name: `<input>-<selector>.bin`). Apple/port outputs NOT stored (reproducible by re-run).

## Corpus

Tier smoke: 9 sizes × 2 patterns. Tier full: ~90 sizes (0–64 dense, 2^k±1, large to 256 KiB) × rotating patterns (run/period/alphabet/random/textlike/sparse), 77 seeds. Deterministic from seed; same seed → same bytes, any machine.

F6 addition (full tier only, smoke unchanged): +33 inputs/seed. `*-singlepox`: the 8 E2-fixed cells (n27p12, n31p16-19, n33p18-19, n34p19, bg00/pox41) every seed. `*-sparsepos`: 25/seed rotating (n,pos,bg,pox) slice over n16-40 (bg 00/FF/42, pox 01/41/FF/7F). Legacy rotation bytes untouched (append-only, no rng consumed).

## Rules for battery operator

- Black-box only. Never read lane source. Execute CLIs, compare bytes.
- Never edit framework mid-campaign. Rerun whole tier after any harness change.
- Delete `tmp-selftest-*` dirs after selftest.
