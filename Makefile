# SPDX-License-Identifier: 0BSD
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
#   clean      remove build artifacts

CC ?= cc
CFLAGS ?= -O2 -std=c11 -Wall -Wextra -Iinclude
AR ?= ar

SRC := $(wildcard src/*.c)
OBJ := $(SRC:.c=.o)
LIB := liblzmesh.a

BATTERY := tests/battery/battery.py
# Oracle probe is built in the battery lane (see docs/TESTING.md oracle
# setup) and found via PATH here — override with ORACLE=.
ORACLE ?= oracle_probe
# Apple build under test, consumed by oracle_probe at run time
# (RELEASE-CHECKLIST gate 4: the default counts as one build).
ORACLE_LIB ?= /usr/lib/libcompression.dylib
export ORACLE_LIB
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

clean:
	rm -f $(OBJ) src/port_cli.o $(LIB) port_cli $(UNIT_BIN)
	rm -rf results tmp-selftest-*

.PHONY: all selftest smoke full unit clean
