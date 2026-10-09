# RELEASING.md — how to cut a release

No publish directives were found in the allowed reads:
`KICKOFF-CLEANROOM.md` (full, 151 lines) names no host, repository name,
versioning scheme, license, or upload procedure, and `DECISIONS.md`
leaves the license (D10/O1) plus seven further items OPEN. Every step
below that depends on a missing decision is marked OPEN and must be
resolved by the coordinator before the release proceeds. Do not invent
values.

Prerequisite: every gate in `RELEASE-CHECKLIST.md` passes.

Candidate snapshots (decoder-complete candidate 2026-09-19,
`../tmp/portrepo/release/MANIFEST.md`, scratch until re-cut) are NOT releases: they freeze the tree +
proof matrix for review while §1 identity items stayed coordinator-OPEN
at the time (resolved 2026-10-09; see §1).
A candidate claims no version, tag, host, or license, and MUST NOT be
published as a release. Checklist status at candidacy (record only;
evidence not shipped in this tree, re-run planned): gates 2 (unit), 3
(selftest), and the decoder halves of 4–5 are GREEN; gate 1 (license),
gate 6 (fuzzers unwired), gate 7 (no perf data), gate 8 (audit), gate
9–10, and the encoder halves of 4–5 are RED.

## 1. Decide the release identity (RESOLVED 2026-10-09)

Owner-decided for the 1.0.0 release:

- RESOLVED: version number 1.0.0, versioning scheme semver
  (owner decision 2026-10-09).
- RESOLVED: tag convention `vX.Y.Z` (so 1.0.0 tags as `v1.0.0`).
  No tag is created by release-prep work itself.
- RESOLVED: host `github.com/mrowlinson/lzmesh` — verified
  first-hand from the prior public push precedent (cleanroom
  evidence commit `5946fde1a`: lane-4 `3a5f76436` pushed to public
  `mrowlinson/lzmesh` main `9c615ce..9f5cd60`; public checkout
  remote `https://github.com/mrowlinson/lzmesh.git`; tag
  `duet-land6` present there).
- RESOLVED: license 0BSD (DECISIONS.md D10/O1, decided 2026-09-28;
  confirmed in `LICENSE-CHOICE.md` + README "License: 0BSD"; full
  text injected as `LICENSE` at the public-tree root by
  `release/export.sh`).

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

## OPEN summary (coordinator decisions; resolved items marked)

- Version number and versioning scheme: RESOLVED 2026-10-09 = 1.0.0,
  semver (owner decision).
- Tag name and signed/unsigned tag policy: name RESOLVED = `vX.Y.Z`
  convention (owner decision 2026-10-09); signed-vs-unsigned still
  OPEN (see §3).
- Hosting service, repository name, and publication location:
  RESOLVED 2026-10-09 = `github.com/mrowlinson/lzmesh` (prior
  public-push precedent, verified first-hand; see §1).
- License choice: RESOLVED 2026-09-28 = 0BSD (D10/O1); gate 1 unblocked.
- Upload artifact list (source tarball? prebuilt lib? neither?).
- Verdict-threshold confirmation (O7), bench scope (O5), API shape (O3),
  level representation (O4), lane fill model (O6), spec freeze hash (O2)
  — each blocks its respective checklist gate, not the tagging mechanics.
