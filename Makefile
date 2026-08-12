NAME = rss-ddc
CC = clang
CFLAGS = -std=c11 -Wall -Wextra -Werror -fmodules -Iinclude -Isrc/core -Isrc/ddc -Isrc/dpcd -Isrc/platform/macos
LDLIBS = -framework CoreDisplay -framework CoreGraphics -framework ColorSync -framework IOKit -framework Foundation
BUILD = build

.DEFAULT_GOAL := $(NAME)

CORE_SOURCES = src/core/correlation.c src/core/provider.c src/core/rss_ddc.c src/core/verify.c src/ddc/protocol.c src/ddc/edid.c src/dpcd/dpcd.c src/dpcd/validation.c src/platform/macos/providers/dispatch.c \
	src/platform/macos/providers/mcdp/get_vcp.c
MACOS_SOURCES = src/platform/macos/discovery.m src/platform/macos/providers/ps190.m \
	src/platform/macos/providers/dp/get_vcp.m src/platform/macos/providers/dp/set_vcp.m
CLI_SOURCE = cli/main.m
TESTS = $(BUILD)/test_protocol $(BUILD)/test_provider $(BUILD)/test_correlation $(BUILD)/test_dispatch $(BUILD)/test_verify $(BUILD)/test_edid $(BUILD)/test_dpcd $(BUILD)/test_dcpdpservice

$(BUILD):
	mkdir -p $(BUILD)

$(NAME): $(BUILD) $(CORE_SOURCES) $(MACOS_SOURCES) $(CLI_SOURCE)
	$(CC) $(CFLAGS) $(CORE_SOURCES) $(MACOS_SOURCES) $(CLI_SOURCE) -o $@ $(LDLIBS)

$(BUILD)/test_protocol: $(BUILD) tests/test_protocol.c src/ddc/protocol.c src/core/provider.c
	$(CC) $(CFLAGS) tests/test_protocol.c src/ddc/protocol.c src/core/provider.c -o $@

$(BUILD)/test_provider: $(BUILD) tests/test_provider.c src/core/provider.c
	$(CC) $(CFLAGS) tests/test_provider.c src/core/provider.c -o $@

$(BUILD)/test_correlation: $(BUILD) tests/test_correlation.c src/core/correlation.c
	$(CC) $(CFLAGS) tests/test_correlation.c src/core/correlation.c -o $@

$(BUILD)/test_dispatch: $(BUILD) tests/test_dispatch.c src/core/provider.c src/platform/macos/providers/dispatch.c
	$(CC) $(CFLAGS) tests/test_dispatch.c src/core/provider.c src/platform/macos/providers/dispatch.c -o $@

$(BUILD)/test_verify: $(BUILD) tests/test_verify.c src/core/verify.c src/core/provider.c
	$(CC) $(CFLAGS) tests/test_verify.c src/core/verify.c src/core/provider.c -o $@

test: $(TESTS)
	$(BUILD)/test_protocol
	$(BUILD)/test_provider
	$(BUILD)/test_correlation
	$(BUILD)/test_dispatch
	$(BUILD)/test_verify
	$(BUILD)/test_edid
	$(BUILD)/test_dpcd
	$(BUILD)/test_dcpdpservice

clean:
	rm -rf $(BUILD) $(NAME)

.PHONY: test clean

$(BUILD)/test_edid: $(BUILD) tests/test_edid.c src/ddc/edid.c
	$(CC) $(CFLAGS) tests/test_edid.c src/ddc/edid.c -o $@

$(BUILD)/test_dpcd: $(BUILD) tests/test_dpcd.c src/dpcd/dpcd.c src/dpcd/validation.c
	$(CC) $(CFLAGS) tests/test_dpcd.c src/dpcd/dpcd.c src/dpcd/validation.c -o $@

$(BUILD)/test_dcpdpservice: $(BUILD) tests/test_dcpdpservice.c src/core/correlation.c src/dpcd/validation.c src/dpcd/dpcd.c
	$(CC) $(CFLAGS) -Isrc/core tests/test_dcpdpservice.c src/core/correlation.c src/dpcd/validation.c src/dpcd/dpcd.c -o $@
