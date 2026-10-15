# Adaptive Multi-Error FEC Link Layer - build file
#
# Portable: GNU make 3.81+ (macOS default) and 4.x (Ubuntu/WSL), gcc or clang.
#
#   make               build everything into bin/
#   make test          build + run all unit tests and the integration test
#   make demo          one-terminal demo, TOLL_PLAZA profile
#   make demo-hospital one-terminal demo, HOSPITAL_IMAGING profile
#   make demo-arq      one-terminal demo, Stop-and-Wait ARQ (uncoded + CRC, retransmit)
#   make demo-harq     one-terminal demo, Hybrid ARQ (FEC first, retransmit on CRC fail)
#   make demo-adaptive adaptive code ladder on a toll -> hospital -> toll channel schedule
#   make dashboard     live web dashboard (http://127.0.0.1:8050/) + the adaptive demo
#   make compare       hamming74 vs bch157 on the same seed, side by side
#   make bench         in-process benchmark sweep -> results/bench.csv
#   make plots         results/bench.csv -> results/*.png (needs matplotlib)
#   make sanitize      all unit tests + integration tests built with ASan + UBSan (build/asan)
#   make clean         remove build outputs

CC      ?= cc
PYTHON  ?= python3
CFLAGS  ?= -O2 -g
CFLAGS  += -std=c11 -Wall -Wextra -Wpedantic -Iinclude -MMD -MP \
           -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE -D_DARWIN_C_SOURCE
LDLIBS  += -lm

# Output directories (make sanitize re-runs this Makefile with BUILD=build/asan BINDIR=build/asan/bin)
BUILD   ?= build
BINDIR  ?= bin
export BIN = $(BINDIR)
export BUILD

# Library: every src/*.c is a module of the FEC/link library.
LIB_SRCS  := $(wildcard src/*.c)
LIB_OBJS  := $(patsubst src/%.c,$(BUILD)/obj/%.o,$(LIB_SRCS))
LIB       := $(BUILD)/libfeclink.a

# Programs: src/apps/<name>.c -> bin/<name>, tools/bench.c -> bin/bench
APP_SRCS  := $(wildcard src/apps/*.c)
APPS      := $(patsubst src/apps/%.c,$(BINDIR)/%,$(APP_SRCS))
ifneq ($(wildcard tools/bench.c),)
APPS      += $(BINDIR)/bench
endif

# Unit tests: tests/test_<name>.c -> build/tests/test_<name>
TEST_SRCS := $(wildcard tests/test_*.c)
TEST_BINS := $(patsubst tests/%.c,$(BUILD)/tests/%,$(TEST_SRCS))

# Demo / benchmark knobs (override on the command line, e.g. make demo COUNT=50)
COUNT   ?= 20
DELAY   ?= 250
SEED    ?= 42
CODE    ?= hamming74
CCOUNT  ?= 1000
FRAMES  ?= 5000
SCHEDULE ?= toll:2000,hospital:2000,toll:2000
SEEDS   ?= 20
ASCHED  ?= toll:60,hospital:80,toll:100

.PHONY: all test sanitize unit-test integration-test demo demo-hospital demo-arq demo-harq demo-adaptive dashboard compare bench plots clean

all: $(APPS)

$(BUILD)/obj/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/obj/apps/%.o: src/apps/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/obj/tools/%.o: tools/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(LIB): $(LIB_OBJS)
	@mkdir -p $(dir $@)
	rm -f $@
	ar rcs $@ $^

$(BINDIR)/%: $(BUILD)/obj/apps/%.o $(LIB)
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) $< $(LIB) $(LDLIBS) -o $@

$(BINDIR)/bench: $(BUILD)/obj/tools/bench.o $(LIB)
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) $< $(LIB) $(LDLIBS) -o $@

$(BUILD)/tests/%: tests/%.c tests/testlib.h $(LIB)
	@mkdir -p $(BUILD)/tests
	$(CC) $(CFLAGS) $< $(LIB) $(LDLIBS) -o $@

# Failure-injection helper (sends malformed UDP packets), used by tests/test_failure.sh
$(BUILD)/tests/inject: tests/inject.c $(LIB)
	@mkdir -p $(BUILD)/tests
	$(CC) $(CFLAGS) $< $(LIB) $(LDLIBS) -o $@

test: unit-test integration-test

unit-test: $(TEST_BINS)
	@set -e; for t in $(TEST_BINS); do echo "== $$t"; ./$$t; done
	@echo "All unit tests passed."

integration-test: all $(BUILD)/tests/inject
	@if [ -f tests/test_integration.sh ]; then sh tests/test_integration.sh; fi
	sh tests/test_failure.sh
	@if command -v $(PYTHON) >/dev/null 2>&1; then $(PYTHON) tests/test_dashboard.py; fi

demo: all
	sh tools/demo.sh --profile toll --code $(CODE) --seed $(SEED) --count $(COUNT) --delay-ms $(DELAY)

demo-hospital: all
	sh tools/demo.sh --profile hospital --code $(CODE) --seed $(SEED) --count $(COUNT) --delay-ms $(DELAY)

demo-arq: all
	sh tools/demo.sh --profile toll --strategy arq --seed $(SEED) --count $(COUNT) --delay-ms $(DELAY)

demo-harq: all
	sh tools/demo.sh --profile toll --strategy harq --code $(CODE) --seed $(SEED) --count $(COUNT) --delay-ms $(DELAY)

demo-adaptive: all
	sh tools/demo.sh --strategy adaptive --schedule $(ASCHED) --window 16 --seed $(SEED) \
		--count 240 --delay-ms 40 --rtt-ms 10

dashboard: all
	sh tools/dashboard.sh --strategy adaptive --schedule $(ASCHED) --window 16 --seed $(SEED) \
		--count 240 --delay-ms 120 --rtt-ms 10

compare: all
	CODES="hamming74 bch157" sh tools/compare.sh --profile toll --seed $(SEED) --count $(CCOUNT)

bench: all
	@mkdir -p results
	./bin/bench --profile toll --frames $(FRAMES) --seed $(SEED) --out results/bench.csv
	./bin/bench --profile hospital --frames $(FRAMES) --seed $(SEED) --out results/bench_hospital.csv
	./bin/bench --strategies --schedule $(SCHEDULE) --frames $(FRAMES) --seed $(SEED) \
		--trace results/adaptive_trace.csv --out results/bench_strategy.csv
	for r in 5 20 50; do ./bin/bench --strategies --schedule $(SCHEDULE) --frames $(FRAMES) \
		--seed $(SEED) --rtt-ms $$r --out results/bench_rtt_$$r.csv >/dev/null || exit 1; done
	./bin/bench --strategies --seeds $(SEEDS) --schedule $(SCHEDULE) --frames $(FRAMES) --seed $(SEED) \
		--out results/bench_strategy_seeds.csv | tail -1

plots:
	$(PYTHON) tools/plot.py results/bench.csv
	@if [ -f results/bench_hospital.csv ]; then $(PYTHON) tools/plot.py results/bench_hospital.csv --suffix _hospital; fi
	@if [ -f results/bench_strategy.csv ]; then $(PYTHON) tools/plot.py results/bench_strategy.csv; fi
	@if [ -f results/adaptive_trace.csv ]; then $(PYTHON) tools/plot.py results/adaptive_trace.csv; fi
	@if [ -f results/bench_rtt_5.csv ]; then $(PYTHON) tools/plot.py results/bench_rtt_5.csv results/bench_rtt_20.csv results/bench_rtt_50.csv; fi
	@if [ -f results/bench_strategy_seeds.csv ]; then $(PYTHON) tools/plot.py results/bench_strategy_seeds.csv; fi

# AddressSanitizer + UndefinedBehaviorSanitizer: separate build tree, every finding is fatal and
# also written to build/asan/logs/ (background processes of the integration tests included).
# CFLAGS goes through the environment so the Makefile's "CFLAGS += -std=c11 ..." still applies.
SAN_FLAGS = -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all
# UBSan prints to stderr even with log_path, so the outputs the integration tests capture in
# results/tmp/ are searched as well (a finding in a background process must not go unnoticed).
SAN_PATTERN = runtime error:\|ERROR: AddressSanitizer\|ERROR: LeakSanitizer\|SUMMARY: UndefinedBehavior
sanitize:
	@rm -rf build/asan/logs && mkdir -p build/asan/logs results/tmp && touch build/asan/logs/.start
	ASAN_OPTIONS=log_path=$(CURDIR)/build/asan/logs/asan:detect_leaks=1 \
	UBSAN_OPTIONS=log_path=$(CURDIR)/build/asan/logs/ubsan:print_stacktrace=1 \
	CFLAGS="$(SAN_FLAGS)" $(MAKE) BUILD=build/asan BINDIR=build/asan/bin test
	@if ls build/asan/logs/[au]* >/dev/null 2>&1; then \
	    echo "SANITIZER FINDINGS:"; cat build/asan/logs/[au]*; exit 1; fi
	@if find results/tmp -type f -newer build/asan/logs/.start | xargs grep -l "$(SAN_PATTERN)" 2>/dev/null; then \
	    echo "SANITIZER FINDINGS in the files above"; exit 1; fi
	@echo "make sanitize: all tests passed under ASan + UBSan, no findings."

clean:
	rm -rf build bin/vehicle bin/channel bin/tollgate bin/bench results/tmp

-include $(LIB_OBJS:.o=.d) $(wildcard $(BUILD)/obj/apps/*.d) $(wildcard $(BUILD)/obj/tools/*.d)
