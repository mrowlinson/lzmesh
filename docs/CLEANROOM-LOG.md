# CLEANROOM-LOG.md — clean (implementation) side, append-only

One entry per session. Written at session start (inputs), appended at end
(outputs). Log the sha256 of the exact spec version read — most valuable
line in the file. Omit nothing from INPUTS CONSULTED.

```markdown
## Session <date>-<id> · side: IMPL (clean)
- agent: <name>, fresh session, no prior transcript, not a fork/resume
- started: <ISO>   ended: <ISO>
- INPUTS CONSULTED (exhaustive):
  - <final spec path> <version>, PART A ONLY  (sha256 <hash>)
  - tests/battery/README.md, contract section only
  - <own-lane divergence reports / vectors, if any>
- INPUTS EXPLICITLY NOT CONSULTED: research/**, vendor/**, port/**,
  any disassembly, any upstream source, any spec-side transcript,
  other lanes' dirs, battery framework source
- OUTPUTS: <files written>
- spec-queries filed: <ids or none>
```

(No sessions logged yet.)

## Session 2026-09-18-portgap2 · side: IMPL (clean)
- agent: portgap-2, fresh session, no prior transcript, not a fork/resume
- started: 2026-09-18T15:10:00-04:00   ended: 2026-09-18T16:25:00-04:00
- INPUTS CONSULTED (exhaustive):
  - NO spec read (no spec path, no sha256; fix derived from measurements only)
  - tests/battery/README.md, contract section only
  - RESUME.md (lane positions / conventions only)
  - tmp/portgap/GAP-RUN-1.md (PARTIAL-1..4, prior worker's measurements)
  - tmp/portgap/dg1/findings-decgap.jsonl + summary-decgap.json (40 gaps)
  - tmp/portgap/gaps2.json (835 mutants) + triage/verify/sweep/tok scripts
  - tmp/portgap/dbg/dec_fix.c + built CLIs (adopted fix text + behavior)
  - tmp/encbatt/ENC-BATT-R14.md + ENC-BATT-R15.md (measurements only:
    3,957 ENC_DIFF, triple-negative, no cheap encoder gap)
  - tmp/portrepo/src/lzmesh_dec.c (file edited) + tmp/portrepo/Makefile
  - own generated outputs: gap2s*.out, dg2-verify.log, gap2confirm.py,
    results/decgap1-smoke (smoke PASS), /tmp/unit-decgap1.log (unit exit 0)
- INPUTS EXPLICITLY NOT CONSULTED: research/**, vendor/**, port/**,
  any disassembly, any upstream source, any spec-side transcript,
  other lanes' dirs (except the two named encbatt measurements above),
  battery framework source, FINDINGS.md, MAILBOX.md, main LZMESH
- OUTPUTS: tmp/portrepo/src/lzmesh_dec.c (12 edits: bs_end lane cap +
  read-through + lit-run-clamp); rebuilt port_cli + liblzmesh.a;
  tmp/portgap/GAP-RUN-2.md + gap2s1/s2/s4/s8/s9/s11/s12.py +
  gap2confirm.py + probe outputs + results/decgap1-smoke
- spec-queries filed: none

## Session 2026-09-18-portgap3 · side: IMPL (clean)
- agent: portgap-3, fresh session, no prior transcript, not a fork/resume
- started: 2026-09-18T16:40:00-04:00   ended: 2026-09-18T18:15:00-04:00
- INPUTS CONSULTED (exhaustive):
  - NO spec read (no spec path, no sha256; fix derived from measurements only)
  - tests/battery/README.md, contract section only
  - RESUME.md (lane positions / conventions only)
  - tmp/portgap/GAP-RUN-2.md (PARTIAL-1..6 + FINAL, prior worker's measurements)
  - tmp/portgap/GAP-RUN-1.md (PARTIAL-1..4)
  - tmp/portgap/dg2-verify.log (9 gaps) + gap2confirm.py + gap2s1/s2/s3/s4/s8/s9/s11.py + outs + verify2.py
  - tmp/portgap/dbg/dec_fix.c + dec_tok.c + built CLIs (adopted fix/trace text + behavior)
  - tmp/portrepo/src/lzmesh_dec.c (file edited) + tmp/portrepo/src/port_cli.c + tmp/portrepo/Makefile
  - tmp/portrepo/docs/CLEANROOM-LOG.md (template + prior entry)
  - own generated outputs: gap3loc.py, dbg/dec_gap3.c + gap3_cli traces, gap3ceil2.py/json,
    gap3fit.py, gap3dump.py + gap3traces/, gap3verify.py, gap3verify2.py, gap3full.py,
    dg3-verify.log (full gate 0 gaps), unit-fix2.log, results/decgap3-smoke (smoke PASS)
- INPUTS EXPLICITLY NOT CONSULTED: research/**, vendor/**, port/**,
  any disassembly, any upstream source, any spec-side transcript,
  other lanes' dirs, battery framework source, FINDINGS.md, MAILBOX.md, main LZMESH
- OUTPUTS: tmp/portrepo/src/lzmesh_dec.c (fix #2: lit-run bound tokc+round32up(litc)-1);
  rebuilt port_cli + liblzmesh.a; tmp/portgap/GAP-RUN-3.md + gap3*.py/json/logs +
  dbg/dec_gap3.c + gap3_cli + dec_fix2.c + fix2_cli + gap3traces/ + results/decgap3-smoke
- spec-queries filed: none

## Session 2026-09-18-portgap4 · side: IMPL (clean)
- agent: portgap-4, fresh session, no prior transcript, not a fork/resume
- started: 2026-09-18T18:38:00-04:00   ended: 2026-09-18T20:27:39-04:00
- INPUTS CONSULTED (exhaustive):
  - NO spec read (no spec path, no sha256; validation-only run, zero source edits)
  - tests/battery/README.md, contract section only (+ ORACLE_LIB paragraph)
  - RESUME.md (lane positions / conventions only)
  - tmp/portgap/GAP-RUN-3.md (FULL: adopted fix #1/#2 rules + 9->0 gate as baseline)
  - tmp/portgap/gap3loc.py (adopted mutant-set generation pattern for gap4scan/gap4full;
    NOTE: outside lane allow-list, listed here for honesty — measurement code only)
  - tmp/portrepo/tests/battery/battery.py (read CLI args/contract; ran UNMODIFIED)
  - tmp/portrepo/tests/battery/oracle_probe.c (read; compiled UNMODIFIED for
    sim-arm64 + tvos-sim targets)
  - tmp/portrepo/Makefile + ios/Makefile.ios + ios/NOTES.md + docs/BUILD.md (build wiring)
  - tmp/portrepo/docs/CLEANROOM-LOG.md (template + prior entries)
  - tmp/portrepo/src/*.c (COMPILED only for host/ios/tvos/watch/macosx64 targets,
    never read) + include/lzmesh.h (compile -I only)
  - tmp/portrepo/tests/unit/test_*.c (COMPILED+RUN only; read test_m19_cli_cap.c
    header + system() call site SOLELY to diagnose its iOS-build failure)
  - Apple system/SDK paths for build+run ONLY: Xcode 27.0 SDKs (iphoneos,
    iphonesimulator, appletvos, appletvsimulator, watchos, watchsimulator,
    macosx), sim runtime roots (iOS 26.5 DMG vol + iOS/tvOS 27.0 cryptex
    mounts), vtool/nm/xcrun/dyld metadata, loaded-UUID logs
  - own generated outputs: tmp/portgap/sim/* (probes, wrappers, batch driver,
    scan/smoke/unit scripts, build trees, summaries/logs)
- INPUTS EXPLICITLY NOT CONSULTED: research/**, vendor/**, port/**,
  any disassembly, any upstream source, any spec-side transcript,
  other lanes' dirs (except gap3loc.py noted above), FINDINGS.md, MAILBOX.md,
  main LZMESH, tmp/iostarget/**, tmp/twobuild/**, tmp/simparity/**
- OUTPUTS: tmp/portgap/GAP-RUN-4.md (PARTIAL-1..5 + FINAL); tmp/portgap/sim/
  (oracle_probe_sim, oracle_probe_tvos, sim_oracle_{26,27,tvos}.sh,
  batch_oracle.c + batch_oracle_{sim,tvos} + gap4batch.py + gap4batchval.py +
  gap4bdbg.py, gap4smoke.py + smoke{26,27,tvos,27full} summaries/logs,
  gap4scan.py + gap4full.py + summaries, gap4units.sh + units/ + logs,
  Makefile.tvos, ios-build/ + ios6probe/ + tvos-build/ + watch-build/ +
  macosx64/); this log entry. NO portrepo source/build edits. NO commits.
- spec-queries filed: none (finding for parent, not a query: Apple decoder
  refusal is caller-buffer-size sensitive, e01 blob 000108000075ff exact->ok
  vs +1024->refused; battery contract pins +1024 so gates are unaffected)

## Session 2026-09-19-portgap5 · side: IMPL (clean)
- agent: portgap-5, fresh session, no prior transcript, not a fork/resume
- started: 2026-09-19T01:30:00-04:00   ended: 2026-09-19T02:35:00-04:00
- INPUTS CONSULTED (exhaustive):
  - NO spec read (no spec path, no sha256; release-readiness run:
    rebuild + re-verify + package + doc-sync, zero codec logic edits)
  - tests/battery/README.md, contract section only (+ ORACLE_LIB paragraph)
  - RESUME.md (lane positions / conventions only)
  - tmp/portgap/GAP-RUN-4.md (FULL: adopted baseline + 4-build/platform
    results for doc sync; zero redo of run-4's scans)
  - tmp/portrepo/Makefile + BUILD.md + README.md + CHANGELOG.md +
    RELEASE-CHECKLIST.md + docs/{BUILD,TESTING,API,PERF,RELEASING}.md +
    ios/NOTES.md + src/README.md + bench/README.md + docs/README.md
    (files read for doc sync; BUILD/README/CHANGELOG/API/RELEASING/PERF
    edited for sync only)
  - tmp/portrepo/include/lzmesh.h (read + edited: provisional
    LZMESH_VERSION_* 0.0.0-dev marker only, no API change)
  - tmp/portrepo/src/*.c (COMPILED only for host target, never read;
    lzmesh_enc.c untouched — encbatt owns it)
  - tmp/portrepo/tests/unit/test_*.c (COMPILED+RUN only via make unit)
  - tmp/portrepo/tests/battery/battery.py (EXECUTED unmodified via
    make smoke/selftest + direct argv; --help read; source never read)
  - tmp/portgap/gap3verify2.py + gap3full.py (EXECUTED blind, outputs
    only; sources never read — NOTE: outside lane allow-list, listed
    here for honesty — measurement code only)
  - Apple system paths for build+run ONLY: stock cc/ar, host
    /usr/lib/libcompression via oracle_probe (default ORACLE_LIB)
  - own generated outputs: release/ (tarball + sha256 + MANIFEST +
    evidence/), tmp/portgap/GAP-RUN-5.md + gap5-*.log,
    /tmp/portgap5-*.log (matrix logs)
- INPUTS EXPLICITLY NOT CONSULTED: research/**, vendor/**, port/**,
  any disassembly, any upstream source, any spec-side transcript,
  other lanes' dirs, battery framework source, FINDINGS.md, MAILBOX.md,
  main LZMESH, tmp/iostarget/**, tmp/twobuild/**, tmp/simparity/**
- OUTPUTS: tmp/portrepo/include/lzmesh.h (version-marker comment +
  4 macros); rebuilt port_cli + liblzmesh.a + 35 unit binaries;
  tmp/portrepo/release/ (candidate tarball 116 entries + sha256 +
  MANIFEST.md + 10 evidence files); doc syncs (BUILD.md, docs/BUILD.md,
  docs/TESTING.md, docs/API.md, docs/PERF.md, docs/RELEASING.md,
  README.md, CHANGELOG.md); tmp/portgap/GAP-RUN-5.md; this log entry.
  results/ restored post-clean (25 sibling dirs from backup, 2 fresh
  kept). NO commits. Encoder untouched.
- spec-queries filed: none (no version/tag/host/license invented:
  RELEASING §1 + D10/O1 stay coordinator-OPEN; candidate snapshot is
  explicitly NOT a release)
