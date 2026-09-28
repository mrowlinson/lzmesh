# Standalone clean-room LZMESH port — build notes.
#
# Toolchain: stock cc on macOS or Linux. No configure step, no third-party
# dependencies. Library sources live in src/ (added by implementer lanes);
# public API is include/lzmesh.h (stub until lanes land).
#
# Targets:
#   all        liblzmesh.a + port_cli (needs lane sources; fails loudly without)
#   port_cli   battery CLI: port_cli enc|dec <selector-hex>, stdio byte pipe
#   selftest   battery framework sanity (no codec needed; always runnable)
#   smoke      fast battery tier vs port_cli (needs built port_cli)
#   full       full 77-seed battery tier (needs ORACLE_LIB + port_cli)
#   unit       fast C unit tests over the public API (nonzero exit on FAIL)
#   bench      in-process throughput harness over the pinned corpus
#              (needs bench/corpus/*; see bench/README.md)
#   bench-corpus  regenerate the pinned corpus (byte-identical; fails if not)
#   pgo        profile-guided rebuild (lib + port_cli + bench under build-pgo/)
#   pgo-unit   unit tests built against the PGO lib, then run
#   clean      remove build artifacts (including build-pgo/)

CC ?= cc
CFLAGS ?= -O2 -std=c11 -Wall -Wextra -Iinclude
# P3-N2: LZMESH_SCALAR=1 (make var) forces scalar codec paths (NEON off).
ifdef LZMESH_SCALAR
CFLAGS += -DLZMESH_SCALAR=$(LZMESH_SCALAR)
endif
AR ?= ar

SRC := $(wildcard src/*.c)
OBJ := $(SRC:.c=.o)
LIB := liblzmesh.a

BATTERY := tests/battery/battery.py
# Prebuilt oracle stays in tmp scratch (never committed here); override with ORACLE=.
ORACLE ?= ../tmp/portrepo/oracle_probe
PORT ?= ./port_cli

all: $(LIB) port_cli

$(LIB): $(OBJ)
	@if [ -z "$(LIBOBJ)" ]; then \
		echo "no lane sources yet: src/*.c empty (implementers add them)"; \
		exit 1; \
	fi
	$(AR) rcs $@ $(LIBOBJ)

LIBOBJ := $(filter-out src/port_cli.o,$(OBJ))
port_cli: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(LIBOBJ) src/port_cli.o

src/%.o: src/%.c include/lzmesh.h
	$(CC) $(CFLAGS) -c -o $@ $<

# Always runnable: loopback adapters, zero codec involved.
selftest:
	python3 $(BATTERY) --selftest

# Needs built port_cli. Oracle build selectable via ORACLE_LIB env.
smoke: port_cli
	python3 $(BATTERY) --oracle $(ORACLE) --port $(PORT) \
		--out results/smoke --tier smoke

full: port_cli
	python3 $(BATTERY) --oracle $(ORACLE) --port $(PORT) \
		--out results/full --tier full --selectors e00,e01,e05,e09

# Stage-6 unithuff: every tests/unit/test_*.c builds to a same-dir binary
# linked against the port library; each binary exits nonzero on any FAIL
# (XFAIL/XPASS lines never fail the gate). Run from the repo root so the
# e00-vector fixtures resolve under tests/unit/vectors/.
UNIT_SRC := $(wildcard tests/unit/test_*.c)
UNIT_BIN := $(UNIT_SRC:.c=)
unit: $(LIB) $(UNIT_BIN)
	set -e; for t in $(UNIT_BIN); do ./$$t; done

tests/unit/test_%: tests/unit/test_%.c $(LIB)
	$(CC) $(CFLAGS) -o $@ $< $(LIB)

BENCH := bench/bench
BENCH_REPS ?= 7
CORPUS := $(wildcard bench/corpus/*.bin)

$(BENCH): bench/bench.c $(LIB)
	$(CC) $(CFLAGS) -o $@ $< $(LIB)

# In-process timing (O5): one TSV row per rep sample on stdout.
bench: $(LIB) $(BENCH)
	@if [ -z "$(CORPUS)" ]; then \
		echo "no corpus: run 'make bench-corpus' first"; exit 1; fi
	./$(BENCH) -n $(BENCH_REPS) $(CORPUS)

bench-corpus:
	python3 bench/mkcorpus.py bench/corpus
	python3 bench/mkcorpus.py --check bench/corpus

# PGO (profile-guided optimization) — opt-in, build-only, no source change.
#   make pgo [PGO_TRAIN_REPS=3]   lib + port_cli + bench, profile-optimized
#   make pgo-unit                 unit tests built against the PGO lib, then run
# Pipeline (all outputs under $(PGODIR)/; the default build tree is untouched):
#   1. instrumented build (-fprofile-instr-generate) in $(PGODIR)/gen
#   2. train = bench binary over the PINNED corpus ($(CORPUS)), -n
#      $(PGO_TRAIN_REPS); deterministic — same corpus + fixed seeds as
#      `make bench`, so profiles are reproducible, not workload-fitted,
#      PLUS an L0-only boost run (-l 0, -n $(PGO_L0_BOOST_REPS)): L0-dec
#      is ~1% of a uniform profile (sub-ms vs ~100ms L9-enc) and without
#      the boost PGO mislays its path (-9% L0-dec, W19); the boost
#      restores L0 parity with no other cell hurt (measured n=35)
#   3. llvm-profdata merge -> $(PGODIR)/pgo.profdata
#   4. profile-use rebuild (-fprofile-instr-use) -> $(PGODIR)/liblzmesh.a,
#      $(PGODIR)/port_cli, $(PGODIR)/bench
# Expect exactly one warning at step 4: port_cli.c has no profile data
# (training drives bench, not the CLI). Harmless; kept visible on purpose.
# Byte-identity: PGO must not change output bytes — verify with the full
# battery against $(PGODIR)/port_cli before trusting any PGO binary.
# Toolchain: Apple clang PGO; llvm-profdata found via xcrun (override
# PROFDATA= on other toolchains).
PGODIR ?= build-pgo
PGO_TRAIN_REPS ?= 3
PGO_L0_BOOST_REPS ?= 200
PROFDATA ?= $(shell (xcrun --find llvm-profdata 2>/dev/null || command -v llvm-profdata) 2>/dev/null)
PGO_PROFDATA := $(abspath $(PGODIR))/pgo.profdata
PGO_GEN := -fprofile-instr-generate
PGO_USE := -fprofile-instr-use=$(PGO_PROFDATA)
PGO_LIB := $(PGODIR)/liblzmesh.a
PGO_CLI := $(PGODIR)/port_cli
PGO_BENCH := $(PGODIR)/bench

$(PGODIR)/gen/%.o: src/%.c include/lzmesh.h
	@mkdir -p $(PGODIR)/gen $(PGODIR)/prof
	$(CC) $(CFLAGS) $(PGO_GEN) -c -o $@ $<

$(PGODIR)/bench-gen: $(PGODIR)/gen/lzmesh_dec.o $(PGODIR)/gen/lzmesh_enc.o bench/bench.c
	$(CC) $(CFLAGS) $(PGO_GEN) -o $@ $(PGODIR)/gen/lzmesh_dec.o $(PGODIR)/gen/lzmesh_enc.o bench/bench.c

$(PGO_PROFDATA): $(PGODIR)/bench-gen
	@if [ -z "$(CORPUS)" ]; then \
		echo "no corpus: run 'make bench-corpus' first"; exit 1; fi
	@if [ -z "$(PROFDATA)" ]; then \
		echo "no llvm-profdata: install Xcode CLT or set PROFDATA="; exit 1; fi
	rm -f $(PGODIR)/prof/*.profraw
	LLVM_PROFILE_FILE="$(abspath $(PGODIR))/prof/full-%p.profraw" \
		"$(abspath $(PGODIR))/bench-gen" -n $(PGO_TRAIN_REPS) $(CORPUS) > /dev/null
	LLVM_PROFILE_FILE="$(abspath $(PGODIR))/prof/l0-%p.profraw" \
		"$(abspath $(PGODIR))/bench-gen" -n $(PGO_L0_BOOST_REPS) -l 0 $(CORPUS) > /dev/null
	"$(PROFDATA)" merge -o $@ $(PGODIR)/prof/*.profraw

$(PGODIR)/use/%.o: src/%.c include/lzmesh.h $(PGO_PROFDATA)
	@mkdir -p $(PGODIR)/use
	$(CC) $(CFLAGS) $(PGO_USE) -c -o $@ $<

$(PGO_LIB): $(PGODIR)/use/lzmesh_dec.o $(PGODIR)/use/lzmesh_enc.o
	$(AR) rcs $@ $(PGODIR)/use/lzmesh_dec.o $(PGODIR)/use/lzmesh_enc.o

$(PGO_CLI): $(PGO_LIB) $(PGODIR)/use/port_cli.o
	$(CC) $(CFLAGS) $(PGO_USE) -o $@ $(PGODIR)/use/lzmesh_dec.o $(PGODIR)/use/lzmesh_enc.o $(PGODIR)/use/port_cli.o

$(PGO_BENCH): $(PGO_LIB) bench/bench.c
	$(CC) $(CFLAGS) $(PGO_USE) -o $@ $(PGODIR)/use/lzmesh_dec.o $(PGODIR)/use/lzmesh_enc.o bench/bench.c

pgo: $(PGO_LIB) $(PGO_CLI) $(PGO_BENCH)

PGO_UNIT_BIN := $(patsubst tests/unit/%.c,$(PGODIR)/unit/%,$(UNIT_SRC))
$(PGODIR)/unit/test_%: tests/unit/test_%.c $(PGO_LIB)
	@mkdir -p $(PGODIR)/unit
	$(CC) $(CFLAGS) $(PGO_USE) -o $@ $< $(PGO_LIB)

pgo-unit: pgo $(PGO_UNIT_BIN)
	set -e; for t in $(PGO_UNIT_BIN); do ./$$t; done

clean:
	rm -f $(OBJ) src/port_cli.o $(LIB) port_cli $(UNIT_BIN) $(BENCH)
	rm -rf results tmp-selftest-* $(PGODIR)

.PHONY: all selftest smoke full unit bench bench-corpus pgo pgo-unit clean
