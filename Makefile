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
#   make clean         remove build outputs

CC      ?= cc
PYTHON  ?= python3
CFLAGS  ?= -O2 -g
CFLAGS  += -std=c11 -Wall -Wextra -Wpedantic -Iinclude -MMD -MP \
           -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE -D_DARWIN_C_SOURCE
LDLIBS  += -lm

# Library: every src/*.c is a module of the FEC/link library.
LIB_SRCS  := $(wildcard src/*.c)
LIB_OBJS  := $(patsubst src/%.c,build/obj/%.o,$(LIB_SRCS))
LIB       := build/libfeclink.a

# Programs: src/apps/<name>.c -> bin/<name>, tools/bench.c -> bin/bench
APP_SRCS  := $(wildcard src/apps/*.c)
APPS      := $(patsubst src/apps/%.c,bin/%,$(APP_SRCS))
ifneq ($(wildcard tools/bench.c),)
APPS      += bin/bench
endif

# Unit tests: tests/test_<name>.c -> build/tests/test_<name>
TEST_SRCS := $(wildcard tests/test_*.c)
TEST_BINS := $(patsubst tests/%.c,build/tests/%,$(TEST_SRCS))

# Demo / benchmark knobs (override on the command line, e.g. make demo COUNT=50)
COUNT   ?= 20
DELAY   ?= 250
SEED    ?= 42
CODE    ?= hamming74
CCOUNT  ?= 1000
FRAMES  ?= 5000
SCHEDULE ?= toll:2000,hospital:2000,toll:2000
ASCHED  ?= toll:60,hospital:80,toll:100

.PHONY: all test unit-test integration-test demo demo-hospital demo-arq demo-harq demo-adaptive dashboard compare bench plots clean

all: $(APPS)

build/obj/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

build/obj/apps/%.o: src/apps/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

build/obj/tools/%.o: tools/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(LIB): $(LIB_OBJS)
	@mkdir -p $(dir $@)
	rm -f $@
	ar rcs $@ $^

bin/%: build/obj/apps/%.o $(LIB)
	@mkdir -p bin
	$(CC) $(CFLAGS) $< $(LIB) $(LDLIBS) -o $@

bin/bench: build/obj/tools/bench.o $(LIB)
	@mkdir -p bin
	$(CC) $(CFLAGS) $< $(LIB) $(LDLIBS) -o $@

build/tests/%: tests/%.c tests/testlib.h $(LIB)
	@mkdir -p build/tests
	$(CC) $(CFLAGS) $< $(LIB) $(LDLIBS) -o $@

test: unit-test integration-test

unit-test: $(TEST_BINS)
	@set -e; for t in $(TEST_BINS); do echo "== $$t"; ./$$t; done
	@echo "All unit tests passed."

integration-test: all
	@if [ -f tests/test_integration.sh ]; then sh tests/test_integration.sh; fi
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

plots:
	$(PYTHON) tools/plot.py results/bench.csv
	@if [ -f results/bench_hospital.csv ]; then $(PYTHON) tools/plot.py results/bench_hospital.csv --suffix _hospital; fi
	@if [ -f results/bench_strategy.csv ]; then $(PYTHON) tools/plot.py results/bench_strategy.csv; fi
	@if [ -f results/adaptive_trace.csv ]; then $(PYTHON) tools/plot.py results/adaptive_trace.csv; fi
	@if [ -f results/bench_rtt_5.csv ]; then $(PYTHON) tools/plot.py results/bench_rtt_5.csv results/bench_rtt_20.csv results/bench_rtt_50.csv; fi

clean:
	rm -rf build bin/vehicle bin/channel bin/tollgate bin/bench results/tmp

-include $(LIB_OBJS:.o=.d) $(wildcard build/obj/apps/*.d) $(wildcard build/obj/tools/*.d)
