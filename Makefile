NAME = rss-ddc
CC = clang
CFLAGS = -std=c11 -Wall -Wextra -Werror -fmodules -Iinclude -Isrc/ddc -Isrc/platform/macos
LDLIBS = -framework CoreDisplay -framework CoreGraphics -framework IOKit -framework Foundation
BUILD = build

.DEFAULT_GOAL := $(NAME)

CORE_SOURCES = src/core/provider.c src/core/rss_ddc.c src/ddc/protocol.c src/platform/macos/providers/dispatch.c \
	src/platform/macos/providers/dp/get_vcp.c src/platform/macos/providers/mcdp/get_vcp.c
MACOS_SOURCES = src/platform/macos/discovery.m src/platform/macos/providers/ps190.m
CLI_SOURCE = cli/main.m
TESTS = $(BUILD)/test_protocol $(BUILD)/test_provider

$(BUILD):
	mkdir -p $(BUILD)

$(NAME): $(BUILD) $(CORE_SOURCES) $(MACOS_SOURCES) $(CLI_SOURCE)
	$(CC) $(CFLAGS) $(CORE_SOURCES) $(MACOS_SOURCES) $(CLI_SOURCE) -o $@ $(LDLIBS)

$(BUILD)/test_protocol: $(BUILD) tests/test_protocol.c src/ddc/protocol.c src/core/provider.c
	$(CC) $(CFLAGS) tests/test_protocol.c src/ddc/protocol.c src/core/provider.c -o $@

$(BUILD)/test_provider: $(BUILD) tests/test_provider.c src/core/provider.c
	$(CC) $(CFLAGS) tests/test_provider.c src/core/provider.c -o $@

test: $(TESTS)
	$(BUILD)/test_protocol
	$(BUILD)/test_provider

clean:
	rm -rf $(BUILD) $(NAME)

.PHONY: test clean
