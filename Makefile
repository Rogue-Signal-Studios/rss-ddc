NAME = rss-ddc
LIBRARY = build/librss-ddc.a
CC = clang
AR ?= ar
BUILD = build
PREFIX ?= /usr/local
DESTDIR ?=

CFLAGS = -std=c11 -Wall -Wextra -Werror -Wformat=2 -fmodules -Iinclude -Isrc/core -Isrc/ddc -Isrc/dpcd -Isrc/platform/macos
LDLIBS = -framework CoreDisplay -framework CoreGraphics -framework ColorSync -framework IOKit -framework Foundation

PORTABLE_CORE_SOURCES = \
	src/core/correlation.c src/core/enumeration.c src/core/provider.c src/core/rss_ddc.c src/core/verify.c \
	src/ddc/protocol.c src/ddc/edid.c src/dpcd/dpcd.c src/dpcd/reader.c \
	src/platform/macos/providers/dispatch.c src/platform/macos/providers/mcdp/get_vcp.c
MACOS_BACKEND_SOURCES = \
	src/platform/macos/discovery.m src/platform/macos/providers/ps190.m \
	src/platform/macos/providers/dp/get_vcp.m src/platform/macos/providers/dp/set_vcp.m
LIBRARY_SOURCES = $(PORTABLE_CORE_SOURCES) $(MACOS_BACKEND_SOURCES)
LIBRARY_OBJECTS = $(patsubst %.c,$(BUILD)/%.o,$(filter %.c,$(LIBRARY_SOURCES))) \
	$(patsubst %.m,$(BUILD)/%.o,$(filter %.m,$(LIBRARY_SOURCES)))
CLI_SOURCES = cli/main.m

# Historical validation runners are intentionally test-only: runtime paths use provider backends directly.
TEST_SUPPORT_SOURCES = src/ddc/get_validation.c src/ddc/set_validation.c
TESTS = \
	$(BUILD)/test_protocol $(BUILD)/test_provider $(BUILD)/test_correlation $(BUILD)/test_enumeration \
	$(BUILD)/test_dispatch $(BUILD)/test_verify $(BUILD)/test_edid $(BUILD)/test_dpcd \
	$(BUILD)/test_dcpdpservice $(BUILD)/test_dcpdpservice_get $(BUILD)/test_dcpdpservice_set

.DEFAULT_GOAL := all

all: $(NAME) $(LIBRARY)

$(BUILD):
	mkdir -p $@

$(BUILD)/%.o: %.c | $(BUILD)
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: %.m | $(BUILD)
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(LIBRARY): $(LIBRARY_OBJECTS)
	$(AR) rcs $@ $^

$(NAME): $(LIBRARY) $(CLI_SOURCES)
	$(CC) $(CFLAGS) $(CLI_SOURCES) $(LIBRARY) -o $@ $(LDLIBS)

library: $(LIBRARY)

$(BUILD)/test_protocol: tests/test_protocol.c src/ddc/protocol.c src/core/provider.c | $(BUILD)
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/test_provider: tests/test_provider.c src/core/provider.c | $(BUILD)
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/test_correlation: tests/test_correlation.c src/core/correlation.c | $(BUILD)
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/test_enumeration: tests/test_enumeration.c src/core/enumeration.c | $(BUILD)
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/test_dispatch: tests/test_dispatch.c src/core/provider.c src/platform/macos/providers/dispatch.c | $(BUILD)
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/test_verify: tests/test_verify.c src/core/verify.c src/core/provider.c | $(BUILD)
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/test_edid: tests/test_edid.c src/ddc/edid.c | $(BUILD)
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/test_dpcd: tests/test_dpcd.c src/dpcd/dpcd.c src/dpcd/reader.c | $(BUILD)
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/test_dcpdpservice: tests/test_dcpdpservice.c src/core/correlation.c src/dpcd/reader.c src/dpcd/dpcd.c | $(BUILD)
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/test_dcpdpservice_get: tests/test_dcpdpservice_get.c src/core/correlation.c src/core/provider.c \
	src/ddc/get_validation.c src/ddc/protocol.c | $(BUILD)
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/test_dcpdpservice_set: tests/test_dcpdpservice_set.c src/core/correlation.c src/core/provider.c \
	src/ddc/set_validation.c src/ddc/protocol.c | $(BUILD)
	$(CC) $(CFLAGS) $^ -o $@

check-library-sources: $(LIBRARY) $(TEST_SUPPORT_SOURCES)
	@! $(AR) t $(LIBRARY) | grep -E '(get_validation|set_validation)'

test: $(TESTS) check-library-sources
	$(BUILD)/test_protocol
	$(BUILD)/test_provider
	$(BUILD)/test_correlation
	$(BUILD)/test_enumeration
	$(BUILD)/test_dispatch
	$(BUILD)/test_verify
	$(BUILD)/test_edid
	$(BUILD)/test_dpcd
	$(BUILD)/test_dcpdpservice
	$(BUILD)/test_dcpdpservice_get
	$(BUILD)/test_dcpdpservice_set

install: $(NAME) $(LIBRARY)
	install -d $(DESTDIR)$(PREFIX)/bin $(DESTDIR)$(PREFIX)/include $(DESTDIR)$(PREFIX)/lib
	install -m 755 $(NAME) $(DESTDIR)$(PREFIX)/bin/$(NAME)
	install -m 644 include/rss_ddc.h $(DESTDIR)$(PREFIX)/include/rss_ddc.h
	install -m 644 $(LIBRARY) $(DESTDIR)$(PREFIX)/lib/librss-ddc.a

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(NAME) $(DESTDIR)$(PREFIX)/include/rss_ddc.h $(DESTDIR)$(PREFIX)/lib/librss-ddc.a

clean:
	rm -rf $(BUILD) $(NAME)

.PHONY: all library check-library-sources test install uninstall clean
