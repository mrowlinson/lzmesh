# PERF-RESULTS.md — performance results

Moved verbatim from the README.md "Current numbers" performance block
on 2026-10-09 (unify-1.0: deficits out of README, into docs). Only the
CHANGELOG.md link below was fixed for the docs/ location. Older
per-wave detail: [CHANGELOG.md](../CHANGELOG.md) gate history +
`WAVES.md`.

Performance — decode gaps at land6 `6b7ff34dd`, measured 2026-10-09
(direct Apple-vs-port bed, n=70, in-process both sides; all three
comparators agree on direction 8/8). Gap = how much slower the port
is; SEP = separated bands (firm), OV = overlapping (noisy):

| cell | gap | letter |
|------|-----|--------|
| text L0 dec | +35.9% | SEP |
| mixed L0 dec | +48.3%* | SEP, core +35–39% |
| mixed L1 dec | +38.9% | SEP |
| text L9 dec | +33.2% | OV |
| text L5 dec | +30.2% | OV |
| mixed L9 dec | +18.9%* | OV, core +13.5–16.7% |
| mixed L5 dec | +15.2%* | OV, core +12–18% |
| text L1 dec | +22.2% | OV (cross-bed mush, no claim) |

*Pooled center storm-inflated; excursion-aware core quoted.* Each
side decodes its own bytes (outputs byte-identical on 9/12 prescope
cells, within 3 B on the 3 pre-existing mixed diffs, so timing inputs
are size-matched).

Encode: no Apple-vs-port matrix exists at or after the land2 tip, so
no current encode gap is claimed. The last full enc+dec absolute
matrix (R28, on the R27 ship tip `enc 3fec8aa3 / dec 5a147a81`, Sep
2026) predates lands 1–6 and is stale — its tables now live in
[CHANGELOG.md](../CHANGELOG.md). Encode moved since (land2 tL5e +12.0%
SEP vehicle-vs-stock at `0902aee1e`, 2026-10-07) but has not been
re-measured against Apple. Optimization frontier, honestly labeled.
