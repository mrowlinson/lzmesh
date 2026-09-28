# LICENSE-CHOICE.md — publish license: DECIDED 0BSD (memo history below)

Status: DECIDED 2026-09-28 (owner directive): 0BSD (DECISIONS.md
D10/O1 resolved). The options + recommendation below are the
pre-decision memo, kept verbatim as history; the Apache-2.0
recommendation is SUPERSEDED (see Decision).

## Context

- Clean-room C port of Apple's LZMESH codec, written from spec prose
  only. Zero Apple code; neutral identifiers (`lzmesh_`, no `Msh*`/
  `msh_*`/`mesh_*`).
- Zero dependencies; no third-party code vendored except the internal
  battery framework copy (project-internal, not shipped to users).
- No spec prose reproduced in the repo (D11); derivation chain lives in
  clean-room logs, not in shipped files.
- Name note: `LZMESH`/`COMPRESSION_LZMESH` is Apple's public algorithm
  name (used descriptively); no trademark grant implied by any option
  below beyond nominative use.

## Options

1. Apache-2.0 — permissive; express patent grant + retaliation clause
   (matters for a codec: implementers get patent peace on contributed
   claims); NOTICE-file convention fits the 10 §10.2.3 NOTICE+LICENSE
   layout. Compatible with GPLv3, one-way compatible issues with GPLv2
   only (not our problem to solve). Most corporate-friendly default.
2. BSD-3-Clause — permissive; matches Apple's own LZFSE precedent
   (Apple's open codec ships BSD-3-Clause), so license stacking beside
   Apple codec code stays familiar. No express patent grant (implied
   only). Shortest, simplest compliance story.
3. MIT — permissive; simplest text; same patent posture as BSD (none
   express). Fine but adds nothing over BSD-3-Clause here.
4. Dual MIT/Apache-2.0 — Rust-ecosystem norm, lets downstream pick.
   Slightly more header boilerplate; unfamiliar to C consumers.
5. MPL-2.0 / LGPL — weak copyleft. File-level (MPL) or library-level
   (LGPL) share-alike on the codec itself. Protects the port from
   proprietary forks but cuts embeddability (firmware, games, app
   vendors routinely refuse); expect slower adoption.
6. GPL — strong copyleft. Rules out proprietary embedding entirely.
   Incompatible with the stated goal (independent interoperable port).

Not options: public-domain dedications (patent posture unclear, some
jurisdictions reject); proprietary/commercial (contradicts project
goal); no-license (all-rights-reserved by default — the current state,
which blocks every downstream use).

## Decision (2026-09-28, owner directive)

- License: 0BSD — maximally free, no share-back, not GPL-family.
- Source-file marker: `SPDX-License-Identifier: 0BSD` (headers
  applied across the tree).
- `LICENSE` (full 0BSD text) ships at the public-tree root, injected
  by `release/export.sh` at export time.
- No `NOTICE` file needed.
- Note: the memo below preferred Apache-2.0 for its express patent
  grant; the owner chose 0BSD instead (maximal freedom, no
  attribution stack). The patent-posture tradeoff stands as analyzed.

## Recommendation (SUPERSEDED 2026-09-28 — kept as history)

SUPERSEDED 2026-09-28 by owner directive (0BSD decided above).
Original text follows, verbatim:

Apache-2.0, single license. Reasons: permissive (max adoption),
express patent grant (codec-appropriate, BSD/MIT lack it), NOTICE
convention matches the guide layout, corporate-legal pre-approved in
most shops. Fallback if the coordinator prefers Apple symmetry:
BSD-3-Clause (LZFSE precedent), accepting the weaker patent posture.

## On decision (checklist for whoever applies it)

Applied 2026-09-28 against the decided license: `LICENSE` injected at
the export root by `release/export.sh`, 0BSD SPDX headers applied, no
`NOTICE` (not needed under 0BSD). Original checklist follows, verbatim:

- Add `LICENSE` (full text) + `NOTICE` (attribution: clean-room port,
  no Apple code, algorithm-name attribution) at repo root.
- Add one-line SPDX header to every shipped source file
  (e.g. `SPDX-License-Identifier: Apache-2.0`); headers stay out until
  then per D10.
- Confirm battery vendored copy is test-only and either same-licensed
  or excluded from release tarballs; record in VENDORED.md.
- No other action: no contributors to re-license, no vendored
  third-party code in the shipped path.
