# RELEASING.md — how to cut a release

No publish directives were found in the allowed reads:
`KICKOFF-CLEANROOM.md` (full, 151 lines) names no host, repository name,
versioning scheme, license, or upload procedure, and `DECISIONS.md`
leaves the license (D10/O1) plus seven further items OPEN. Every step
below that depends on a missing decision is marked OPEN and must be
resolved by the coordinator before the release proceeds. Do not invent
values.

Prerequisite: every gate in `RELEASE-CHECKLIST.md` passes.

Candidate snapshots (decoder-complete candidate 2026-09-19, frozen
for review until re-cut) are NOT releases: they freeze the tree +
proof matrix for review while §1 identity items stay coordinator-OPEN.
A candidate claims no version, tag, host, or license, and MUST NOT be
published as a release. Checklist status at candidacy (record only;
evidence not shipped in this tree, re-run planned): gates 2 (unit), 3
(selftest), and the decoder halves of 4–5 are GREEN; gate 1 (license),
gate 6 (fuzzers unwired), gate 7 (no perf data), gate 8 (audit), gate
9–10, and the encoder halves of 4–5 are RED.

## 1. Decide the release identity (OPEN)

Coordinator provides:

- OPEN: version number and versioning scheme (e.g. semver — scheme
  itself undecided; nothing in the allowed reads mandates one).
- OPEN: tag name derived from the version (e.g. `vX.Y.Z` — convention
  undecided).
- OPEN: hosting service and repository name/location (no directive
  found; GitHub assumed by nothing).
- OPEN: license (DECISIONS.md D10/O1); `LICENSE`/`NOTICE` must be
  committed before tagging.

Record all four in `DECISIONS.md` and replace the OPEN markers in this
file for the release at hand.

## 2. Prepare the tree

1. Complete `RELEASE-CHECKLIST.md` gates 1–9 on a clean checkout:
   `git status --short` shows nothing unexpected.
2. Move the `CHANGELOG.md` `[Unreleased]` content under a new
   `## [X.Y.Z] — <date>` heading (version/date per §1); keep an empty
   `[Unreleased]` section on top.
3. Update `README.md` if the status line changed (e.g. skeleton wording
   for a first code release).
4. Final verification pass from the repo root:
   `make clean && make unit && make selftest`
   (plus the recorded `smoke`/`full` runs per `RELEASE-CHECKLIST.md`
   gates 4–5 if not already run on this exact tree).

## 3. Tag

1. Commit the release preparation (`CHANGELOG.md`, `README.md` if
   touched, any final gate fixes) on the release branch.
2. Create the annotated tag: `git tag -a <tag> -m "Release <version>"`.
3. Inspect: `git show <tag> --stat` lists exactly the intended files;
   `git tag -l` shows the tag.

OPEN: signed versus unsigned tags — no directive found. Coordinator
decides; if signed, use `git tag -s` and record the key policy in
`DECISIONS.md`.

## 4. Publish

1. Push the branch and tag to the coordinator-named location (§1):
   `git push <remote> <branch> && git push <remote> <tag>`.
2. Upload release artifacts, if any (OPEN: artifact list undecided —
   candidates are a source tarball `lzmesh-<version>.tar.gz` produced
   via `git archive`, and/or prebuilt `liblzmesh.a` plus
   `include/lzmesh.h`; coordinator decides which, if any).
3. Attach or link: `CHANGELOG.md` release section, `docs/TESTING.md`
   verdict evidence locations (per-`--out` `summary.json` files for both
   Apple builds), and the license text.

## 5. After the release

1. Confirm the published tag, artifacts, and rendered `README.md` at the
   publication location.
2. Open a fresh `[Unreleased]` section in `CHANGELOG.md` if §2 consumed
   it.
3. Log the release (version, tag, spec freeze hash, oracle build
   identities, publication URL) as an entry in `docs/CLEANROOM-LOG.md`
   or `DECISIONS.md`, per coordinator direction.

## OPEN summary (all require coordinator decisions)

- Version number and versioning scheme.
- Tag name and signed/unsigned tag policy.
- Hosting service, repository name, and publication location.
- License choice (blocks gate 1; DECISIONS.md D10/O1).
- Upload artifact list (source tarball? prebuilt lib? neither?).
- Verdict-threshold confirmation (O7), bench scope (O5), API shape (O3),
  level representation (O4), lane fill model (O6), spec freeze hash (O2)
  — each blocks its respective checklist gate, not the tagging mechanics.
