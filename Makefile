# cachecliff -- hosted build and the checks CI runs.
#
# For a bare-metal target, compile include/, src/ and bank/ with your own port
# (see port/posix/ for what a port provides).
#
#   make              build ./cachecliff
#   make run          build and run the sweeps
#   make test         unit tests (fake timer), tests with the real timer, generator tests
#   make asan         the unit tests under AddressSanitizer + UBSan
#   make coverage     the unit tests with a line-coverage gate on src/
#   make cross        compile-check the core for Cortex-M0/M3/M4/M7 (arm-none-eabi-gcc)
#   make format       clang-format and black, in place
#   make format-check formatting and lint, as CI does
#   make doc          HTML documentation; any Doxygen warning is an error
#   make check        everything but the cross build and the coverage gate
#
# Capacity (see include/cachecliff.h):
#   make CC_NFUNCS=256 CC_DATA_BYTES=262144

CC       ?= cc
OPT      ?= -O2
CC_NFUNCS     ?= 1024
CC_DATA_BYTES ?= 16777216
WERROR   ?=
WARN     := -Wall -Wextra -Wpedantic $(WERROR)
CFLAGS   ?= $(OPT) -std=c99 $(WARN)
CFLAGS   += -Iinclude -DCC_NFUNCS=$(CC_NFUNCS) -DCC_DATA_BYTES=$(CC_DATA_BYTES)

CORE  := src/cc_engine.c src/cc_main.c
BANK  := $(wildcard bank/cc_bank_*.c) bank/cc_table.c
POSIX := port/posix/cc_port.c port/posix/main.c
BUILD := build

# The unit tests run with the fake timer and a small data region, so they are fast.
TEST_FLAGS := -Iinclude -Itests -DCC_NFUNCS=1024 -DCC_DATA_BYTES=262144 -DCC_PILOT_MAX_CALLS=4096 \
    -std=c99 $(WARN) -g
TEST_SRC   := $(CORE) $(BANK) tests/port_fake.c tests/test_main.c tests/test_engine.c tests/test_report.c

# the minimum line coverage of src/, in percent
COV_MIN ?= 95

.PHONY: all run test test-hosted asan coverage cross format format-check doc check clean
all: cachecliff

cachecliff: $(patsubst %.c,$(BUILD)/%.o,$(CORE) $(BANK) $(POSIX))
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD)/%.o: %.c include/cachecliff.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

run: cachecliff
	./cachecliff

$(BUILD)/test_cc: $(TEST_SRC) include/cachecliff.h tests/cc_test.h tests/port_fake.h
	@mkdir -p $(BUILD)
	$(CC) $(TEST_FLAGS) $(OPT) -o $@ $(TEST_SRC)

# The same checks with the real timer: they show that what is timed is executed.
HOSTED_SRC := $(CORE) $(BANK) port/posix/cc_port.c tests/test_main.c tests/test_hosted.c
$(BUILD)/test_hosted: $(HOSTED_SRC) include/cachecliff.h tests/cc_test.h
	@mkdir -p $(BUILD)
	$(CC) -Iinclude -Itests -DCC_NFUNCS=1024 -DCC_DATA_BYTES=16777216 -std=gnu99 $(WARN) -O2 \
	    -o $@ $(HOSTED_SRC)

test-hosted: $(BUILD)/test_hosted
	./$(BUILD)/test_hosted

test: $(BUILD)/test_cc cachecliff test-hosted
	./$(BUILD)/test_cc
	python3 tests/test_ccgen.py
	./cachecliff --validate
	./cachecliff --quick --csv > /dev/null

asan:
	@mkdir -p $(BUILD)
	$(CC) $(TEST_FLAGS) -O1 -fsanitize=address,undefined -fno-omit-frame-pointer \
	    -fno-sanitize-recover=all -o $(BUILD)/test_cc_asan $(TEST_SRC)
	ASAN_OPTIONS=halt_on_error=1:detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	    ./$(BUILD)/test_cc_asan

# The bank is generated code: it is exercised by the tests but not measured.
coverage:
	@mkdir -p $(BUILD)/cov
	$(CC) $(TEST_FLAGS) -O0 --coverage -o $(BUILD)/cov/test_cc $(TEST_SRC)
	cd $(BUILD)/cov && ./test_cc
	gcovr --root . --filter 'src/' --object-directory $(BUILD)/cov \
	    --print-summary --fail-under-line $(COV_MIN) \
	    --html-details $(BUILD)/cov/index.html

# Compile only, for the cores cachecliff is meant for: the core has to build
# for a target without a hosted libc.
ARM_CC ?= arm-none-eabi-gcc
CROSS_CPUS := cortex-m0 cortex-m3 cortex-m4 cortex-m7
cross:
	@for cpu in $(CROSS_CPUS); do \
	    echo "cross: $$cpu"; \
	    for f in $(CORE) $(BANK); do \
	        $(ARM_CC) -mcpu=$$cpu -mthumb -Os -std=c99 $(WARN) -Wno-unused-function \
	            -Iinclude -DCC_NFUNCS=256 -DCC_DATA_BYTES=16384 -c -o /dev/null $$f || exit 1; \
	    done; \
	done

format:
	bash scripts/format.sh
	bash scripts/format-py.sh

format-check:
	bash scripts/format.sh --check
	bash scripts/format-py.sh --check

doc:
	bash scripts/check-doc.sh

check: format-check test asan doc

clean:
	rm -rf $(BUILD) cachecliff doc/html
