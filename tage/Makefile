CC        = gcc
CFLAGS    = -std=c99 -Wall -Wextra -g -I. -I..

UNITY_DIR = tests/unity
UNITY_URL = https://raw.githubusercontent.com/ThrowTheSwitch/Unity/master/src


# Auto-discover every tests/test_*.c file.
# To add a new test suite, just create tests/test_<name>.c.
# A matching binary tests/run_<name> is built and run automatically.
TEST_SRCS := $(wildcard tests/test_*.c)
TEST_BINS := $(patsubst tests/test_%.c, tests/run_%, $(TEST_SRCS))


.PHONY: all
all:
	@echo "Available targets:"
	@echo "  make fetch-unity  - download Unity (run once before 'make test')"
	@echo "  make test         - build and run all test_*.c suites"
	@echo "  make sample       - build and run the standalone sample"
	@echo "  make tage.o       - compile object for linking into SimpleScalar"
	@echo "  make clean        - remove build artifacts"


# Fetch Unity (3 files from GitHub)
.PHONY: fetch-unity
fetch-unity: $(UNITY_DIR)/unity.h $(UNITY_DIR)/unity.c $(UNITY_DIR)/unity_internals.h

$(UNITY_DIR):
	mkdir -p $(UNITY_DIR)

$(UNITY_DIR)/unity.h: | $(UNITY_DIR)
	curl -fsSL "$(UNITY_URL)/unity.h" -o $(UNITY_DIR)/unity.h

$(UNITY_DIR)/unity.c: | $(UNITY_DIR)
	curl -fsSL "$(UNITY_URL)/unity.c" -o $(UNITY_DIR)/unity.c

$(UNITY_DIR)/unity_internals.h: | $(UNITY_DIR)
	curl -fsSL "$(UNITY_URL)/unity_internals.h" -o $(UNITY_DIR)/unity_internals.h

.PHONY: check-unity
check-unity:
	@if [ ! -f "$(UNITY_DIR)/unity.h" ]; then \
		echo "Unity not found. Run 'make fetch-unity' first."; \
		exit 1; \
	fi

# Unit tests  (one binary per test_*.c file)

.PHONY: test
test: $(TEST_BINS) | check-unity
	@for bin in $(TEST_BINS); do \
		echo ""; \
		echo "=== $$bin ==="; \
		./$$bin; \
	done

# Pattern rule: tests/run_foo  <-  tests/test_foo.c  +  tage.c
tests/run_%: tests/test_%.c $(SRCS) | check-unity
	$(CC) $(CFLAGS) -o $@ $< $(UNITY_DIR)/unity.c $(SRCS) -lm

# ─────────────────────────────────────────────
# Standalone sample (no tage.c dependency)
# ─────────────────────────────────────────────
.PHONY: sample
sample: tests/sample/run_sample | check-unity
	./tests/sample/run_sample

tests/sample/run_sample: tests/sample/main.c | check-unity
	$(CC) $(CFLAGS) -I$(UNITY_DIR) -o $@ $< $(UNITY_DIR)/unity.c

# ─────────────────────────────────────────────
# Object for linking into SimpleScalar
# ─────────────────────────────────────────────
tage.o: $(SRCS)
	$(CC) $(CFLAGS) -c -o $@ $(SRCS)

.PHONY: clean
clean:
	rm -rf tage.o $(TEST_BINS) tests/sample/run_sample \
	       $(TEST_BINS:%=%.dSYM) tests/sample/run_sample.dSYM
