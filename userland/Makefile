CC ?= cc
CFLAGS ?= -std=gnu11 -Wall -Wextra -O2

BUILD_DIR := build/bin

COMMON_SRCS := common/log.c

.PHONY: all clean test

all: $(BUILD_DIR)/walud $(BUILD_DIR)/authd $(BUILD_DIR)/storaged

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/walud: walud/walud.c $(COMMON_SRCS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $^

$(BUILD_DIR)/authd: authd/authd.c $(COMMON_SRCS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $^ -lcrypt

$(BUILD_DIR)/storaged: storaged/storaged.c $(COMMON_SRCS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -rf $(BUILD_DIR)

test: all
	chmod +x tests/test_userland.sh
	tests/test_userland.sh
