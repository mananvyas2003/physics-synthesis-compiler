CC ?= gcc

CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -I./src
LDFLAGS ?=

BUILD := build

CORE_SRC := $(wildcard src/*.c)
CORE_OBJ := $(patsubst src/%.c,$(BUILD)/%.o,$(CORE_SRC))

TEST_SRC := $(wildcard tests/*_test.c)
TEST_BIN := $(patsubst tests/%.c,$(BUILD)/%,$(TEST_SRC))

.PHONY: all test clean

all: $(TEST_BIN)

test: $(TEST_BIN)
	@for t in $(TEST_BIN); do \
		echo "=== $$t ==="; \
		./$$t || exit 1; \
	done

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/%.o: src/%.c | $(BUILD)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/%: tests/%.c $(CORE_OBJ) | $(BUILD)
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@

clean:
	rm -rf $(BUILD)

-include $(CORE_OBJ:.o=.d)
