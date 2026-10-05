# LANE-R27-FLEX9 (memo-assist-beds: H2 FRESH-ephemeral census bed)

lane: r27-flex9 | base 2b7238fd4 | branch lanes/r27-r27-flex9 | ZERO Air bytes.
adopt: CLAIM-r27-flex9-memo-assist (H2 uncovered; pool-rank verified, dispatcher LOGGED).

## mission

Execute R28-road H2 (DROP-r27-memo-r28road1): fresh-shard-only -S delta
census, no Air. Bed-only: report deltas to memo, no ship claim. Method:
H5/traincorp rig rebuilt from pins (flex7 stage1b.sh + h3count.py via
git-show into own lane dir, md5-verified).

## stage1: control-A (DONE, GREEN, ckpt caf349fb5)

- enc-union.c 3860c43c EXACT / profdata 98cfccfe == flex7 EXACT /
  census 1455/165/120/109/347/33 == memo/flex7 EXACT / PGO-IDENT 24/24 /
  warnings=2 (standing benign).
- finding: profdata pins diverge by rig lineage (memo 5fb4522e vs
  flex7/me 98cfccfe); census binds. DROP-r27-flex9-h21.

## stage2: H2 fresh arm (DONE, verdict EPHEMERAL-SENSITIVE)

- spec: SLA-default (Q1 unanswered 16min at run time): seeds 21-24 (744
  files, 2.56MB, 8 empty skipped) + H5-verbatim (-n 3, boost kept).
  pre-reg BINDING; deviation arm owed if ANSWER-r27-flex9-1 deviates.
- B: profdata 74c7005d. h3 1505/152/121/110/432/32.
  whole 58231/9836/5175/6924/9784/1828. PGO-IDENT 24/24, warnings=2.
- C (supplemental volume control): profdata cbf4fc10. h3 1488/159/126/109/375/33.
  whole 51459/8676/4624/6119/9188/2029. PGO-IDENT 24/24, warnings=2.
- B-A: h3 +50/-13/+1/+1/+85/-1; whole +6823/+1374/+473/+805/+555/-189.
- C-A: h3 +33; whole +51. B-C residual: h3 +17; whole +6772, bl -201.
- verdict per pre-reg (|+50| >= 30): EPHEMERAL-SENSITIVE. whole-file +13%
  ~100% freshness; action outside h3_split. DROP-r27-flex9-h22.

## Q/A (2/0 at DONE; answers memo-side, R28 or late-R27 — flex3 precedent)

- QUESTION-r27-flex9-1 (bed spec) — routed; PRELIM overdue, memo down
  (dispatcher BREACH-NOTICE; SLA-default run = CORRECT proceed-on-proofs).
- QUESTION-r27-flex9-2 (verdict consumption + R28 arm spec) — routed; may lag.
- owe: deviation arm IF ANSWER-1 deviates from 21-24/H5-verbatim (R28/late-R27).

## cites (used/ignored)

- unionprice1 (price+census; USED) / traincorp1 (H5 method+guardrail; USED) /
  flex7-layenc1/4 (rig pins + tripwire pattern; USED) / flex1-union1
  (vehicle; USED) / r28road1 (H2 spec; USED) / decleg1 (decode-side;
  IGNORED: no encode transfer) / flex2-e1fold1 (E1-const-fold;
  IGNORED: no H2 transfer).
