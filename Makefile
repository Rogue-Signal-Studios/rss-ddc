NAME = rss-ddc
LIBRARY = build/librss-ddc.a
CC = clang
CXX = clang++
AR ?= ar
BUILD = build
PREFIX ?= /usr/local
DESTDIR ?=
CONSUMER_TEST_PREFIX = $(abspath $(BUILD)/consumer-prefix)
CONSUMER_TEST_BINARY = $(BUILD)/consumer
CONSUMER_TEST_CPP_BINARY = $(BUILD)/consumer-cpp

CLI_PRESENTATION_SOURCES = \
	cli/presentation/tri_state.c cli/presentation/config.c cli/presentation/terminal.c \
	cli/presentation/output_settings.c cli/presentation/color.c cli/presentation/table.c \
	cli/presentation/plain.c cli/presentation/render.c cli/presentation/args.c cli/presentation/visible_width.c

CFLAGS = -std=c11 -Wall -Wextra -Werror -Wformat=2 -fmodules -Iinclude -Isrc/core -Isrc/ddc -Isrc/dpcd -Isrc/platform/macos
CLI_CFLAGS = $(CFLAGS) -Icli/presentation
# On the current supported macOS SDK, CoreDisplay re-exports the CoreGraphics,
# ColorSync, IOKit, CoreFoundation, and Objective-C dependencies used by the
# private backend. Keep the external consumer contract to this proven minimum.
LDLIBS = -framework CoreDisplay

PORTABLE_CORE_SOURCES = \
	src/core/characterize.c src/core/characterize_prepare.c src/core/correlation.c src/core/enumeration.c src/core/input_switch.c src/core/mccs_capabilities.c src/core/mccs_retrieval.c src/core/monitor_knowledge.c src/core/picture_mode.c src/core/probe.c src/core/profile_store.c src/core/provider.c src/core/rss_ddc.c src/core/verify.c \
	src/ddc/input_switch.c src/ddc/protocol.c src/ddc/edid.c src/dpcd/dpcd.c src/dpcd/reader.c \
	src/platform/macos/providers/dispatch.c src/platform/macos/providers/mcdp/get_vcp.c
MACOS_BACKEND_SOURCES = \
	src/platform/macos/discovery.m src/platform/macos/providers/ps190.m \
	src/platform/macos/providers/dp/get_vcp.m src/platform/macos/providers/dp/input_switch.m src/platform/macos/providers/dp/mccs_capabilities.m src/platform/macos/providers/dp/set_vcp.m
LIBRARY_SOURCES = $(PORTABLE_CORE_SOURCES) $(MACOS_BACKEND_SOURCES)
LIBRARY_OBJECTS = $(patsubst %.c,$(BUILD)/%.o,$(filter %.c,$(LIBRARY_SOURCES))) \
	$(patsubst %.m,$(BUILD)/%.o,$(filter %.m,$(LIBRARY_SOURCES)))
CLI_SOURCES = cli/main.m

# Historical validation runners are intentionally test-only: runtime paths use provider backends directly.
TEST_SUPPORT_SOURCES = src/ddc/get_validation.c src/ddc/set_validation.c
TESTS = \
	$(BUILD)/test_protocol $(BUILD)/test_provider $(BUILD)/test_correlation $(BUILD)/test_enumeration \
	$(BUILD)/test_dispatch $(BUILD)/test_verify $(BUILD)/test_edid $(BUILD)/test_dpcd \
	$(BUILD)/test_dcpdpservice $(BUILD)/test_dcpdpservice_get $(BUILD)/test_dcpdpservice_set \
	$(BUILD)/test_display_resolution $(BUILD)/test_mccs_capabilities $(BUILD)/test_mccs_retrieval \
	$(BUILD)/test_input_switch $(BUILD)/test_input_switch_api $(BUILD)/test_picture_mode $(BUILD)/test_profile_store \
	$(BUILD)/test_monitor_knowledge $(BUILD)/test_characterize $(BUILD)/test_probe $(BUILD)/test_probe_extended $(BUILD)/test_cli_presentation \
	$(BUILD)/test_library_strings

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

$(NAME): $(LIBRARY) $(CLI_SOURCES) $(CLI_PRESENTATION_SOURCES)
	$(CC) $(CLI_CFLAGS) $(CLI_SOURCES) $(CLI_PRESENTATION_SOURCES) $(LIBRARY) -o $@ $(LDLIBS)

# Private bindings embed public display snapshots. Rebuild every library
# object and the CLI together whenever either layout-defining header changes.
$(LIBRARY_OBJECTS) $(NAME): include/rss_ddc.h src/platform/macos/macos_internal.h

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

$(BUILD)/test_display_resolution: tests/test_display_resolution.c src/core/rss_ddc.c src/core/provider.c | $(BUILD)
	$(CC) $(CFLAGS) -pthread $^ -o $@

$(BUILD)/test_mccs_capabilities: tests/test_mccs_capabilities.c src/core/mccs_capabilities.c | $(BUILD)
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/test_mccs_retrieval: tests/test_mccs_retrieval.c src/core/mccs_capabilities.c src/core/mccs_retrieval.c src/ddc/protocol.c | $(BUILD)
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/test_input_switch: tests/test_input_switch.c src/ddc/input_switch.c src/ddc/protocol.c | $(BUILD)
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/test_input_switch_api: tests/test_input_switch_api.c src/core/input_switch.c src/ddc/input_switch.c src/ddc/protocol.c | $(BUILD)
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/test_picture_mode: tests/test_picture_mode.c src/core/picture_mode.c | $(BUILD)
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/test_profile_store: tests/test_profile_store.c src/core/profile_store.c src/core/provider.c | $(BUILD)
	$(CC) $(CFLAGS) -pthread $^ -o $@

$(BUILD)/test_monitor_knowledge: tests/test_monitor_knowledge.c src/core/monitor_knowledge.c src/core/profile_store.c src/core/provider.c | $(BUILD)
	$(CC) $(CFLAGS) -pthread $^ -o $@

$(BUILD)/test_characterize: tests/test_characterize.c src/core/characterize.c src/core/monitor_knowledge.c src/core/profile_store.c src/core/provider.c src/core/mccs_capabilities.c src/core/probe.c | $(BUILD)
	$(CC) $(CFLAGS) -pthread $^ -o $@

$(BUILD)/test_probe: tests/test_probe.c src/core/probe.c src/core/monitor_knowledge.c src/core/profile_store.c src/core/provider.c src/core/mccs_capabilities.c | $(BUILD)
	$(CC) $(CFLAGS) -DRSS_DDC_TESTING -pthread $^ -o $@

$(BUILD)/test_probe_extended: tests/test_probe_extended.c src/core/probe.c src/core/monitor_knowledge.c src/core/profile_store.c src/core/provider.c src/core/mccs_capabilities.c | $(BUILD)
	$(CC) $(CFLAGS) -DRSS_DDC_TESTING -pthread $^ -o $@

$(BUILD)/test_cli_presentation: tests/test_cli_presentation.c tests/cli_presentation_support.c $(CLI_PRESENTATION_SOURCES) src/core/provider.c | $(BUILD)
	$(CC) $(CLI_CFLAGS) $^ -o $@

$(BUILD)/test_library_strings: tests/test_library_strings.c $(LIBRARY) | $(BUILD)
	$(CC) $(CFLAGS) tests/test_library_strings.c $(LIBRARY) -o $@ $(LDLIBS)

check-library-sources: $(LIBRARY) $(TEST_SUPPORT_SOURCES)
	@! $(AR) t $(LIBRARY) | grep -E '(get_validation|set_validation|(^|/)tests/|(^|/)cli/|(^|/)research/)'

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
	$(BUILD)/test_display_resolution
	$(BUILD)/test_mccs_capabilities
	$(BUILD)/test_mccs_retrieval
	$(BUILD)/test_input_switch
	$(BUILD)/test_input_switch_api
	$(BUILD)/test_picture_mode
	$(BUILD)/test_profile_store
	$(BUILD)/test_monitor_knowledge
	$(BUILD)/test_characterize
	$(BUILD)/test_probe
	$(BUILD)/test_probe_extended
	$(BUILD)/test_cli_presentation
	$(BUILD)/test_library_strings

install-library: $(LIBRARY)
	install -d $(DESTDIR)$(PREFIX)/include $(DESTDIR)$(PREFIX)/lib
	install -m 644 include/rss_ddc.h $(DESTDIR)$(PREFIX)/include/rss_ddc.h
	install -m 644 $(LIBRARY) $(DESTDIR)$(PREFIX)/lib/librss-ddc.a

install-cli: $(NAME)
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 $(NAME) $(DESTDIR)$(PREFIX)/bin/$(NAME)

install: install-library install-cli

uninstall-library:
	rm -f $(DESTDIR)$(PREFIX)/include/rss_ddc.h $(DESTDIR)$(PREFIX)/lib/librss-ddc.a

uninstall-cli:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(NAME)

uninstall: uninstall-library uninstall-cli

# Builds against staged, installed artifacts only. Fixtures are compiled and
# linked but never run, so they cannot enumerate displays or open user clients.
consumer-test: $(LIBRARY) examples/consumer.c examples/consumer.cpp | $(BUILD)
	rm -rf $(CONSUMER_TEST_PREFIX) $(CONSUMER_TEST_BINARY) $(CONSUMER_TEST_CPP_BINARY)
	$(MAKE) install-library PREFIX=$(CONSUMER_TEST_PREFIX)
	$(CC) -std=c11 -Wall -Wextra -Werror -Wformat=2 -I$(CONSUMER_TEST_PREFIX)/include examples/consumer.c $(CONSUMER_TEST_PREFIX)/lib/librss-ddc.a -o $(CONSUMER_TEST_BINARY) $(LDLIBS)
	$(CXX) -std=c++17 -Wall -Wextra -Werror -Wformat=2 -I$(CONSUMER_TEST_PREFIX)/include examples/consumer.cpp $(CONSUMER_TEST_PREFIX)/lib/librss-ddc.a -o $(CONSUMER_TEST_CPP_BINARY) $(LDLIBS)
	test -x $(CONSUMER_TEST_BINARY)
	test -x $(CONSUMER_TEST_CPP_BINARY)
	$(MAKE) uninstall-library PREFIX=$(CONSUMER_TEST_PREFIX)
	test ! -e $(CONSUMER_TEST_PREFIX)/include/rss_ddc.h
	test ! -e $(CONSUMER_TEST_PREFIX)/lib/librss-ddc.a
	rmdir $(CONSUMER_TEST_PREFIX)/include $(CONSUMER_TEST_PREFIX)/lib $(CONSUMER_TEST_PREFIX)
	rm -f $(CONSUMER_TEST_BINARY) $(CONSUMER_TEST_CPP_BINARY)

clean:
	rm -rf $(BUILD) $(NAME)

# Isolated predecessor DDC labs. These are never part of `make`, `make test`,
# `make consumer-test`, or librss-ddc.a.
RESEARCH_CFLAGS = $(CFLAGS) -Iresearch/common -Isrc/platform/macos/private
RESEARCH_LDLIBS = -framework CoreDisplay -framework CoreGraphics -framework IOKit -framework Foundation
RESEARCH_COMMON_PARSER = research/common/ddc_parser.c
RESEARCH_COMMON_SUPPORT = research/common/ioav_lab_support.c
RESEARCH_BINS = \
	$(BUILD)/research/ioav-ddc-lab \
	$(BUILD)/research/ioav-device-lab \
	$(BUILD)/research/iodp-ddc-lab
RESEARCH_TESTS = $(BUILD)/test_research_lab_support

$(BUILD)/research:
	mkdir -p $@

$(BUILD)/research/ioav-ddc-lab: research/ioav-ddc-lab/ioav-ddc-lab.m \
	$(RESEARCH_COMMON_PARSER) $(RESEARCH_COMMON_SUPPORT) $(LIBRARY) | $(BUILD)/research
	$(CC) $(RESEARCH_CFLAGS) research/ioav-ddc-lab/ioav-ddc-lab.m \
		$(RESEARCH_COMMON_PARSER) $(RESEARCH_COMMON_SUPPORT) $(LIBRARY) \
		-o $@ $(RESEARCH_LDLIBS)

$(BUILD)/research/ioav-device-lab: research/ioav-device-lab/ioav-device-lab.m \
	$(RESEARCH_COMMON_PARSER) $(RESEARCH_COMMON_SUPPORT) $(LIBRARY) | $(BUILD)/research
	$(CC) $(RESEARCH_CFLAGS) research/ioav-device-lab/ioav-device-lab.m \
		$(RESEARCH_COMMON_PARSER) $(RESEARCH_COMMON_SUPPORT) $(LIBRARY) \
		-o $@ $(RESEARCH_LDLIBS)

$(BUILD)/research/iodp-ddc-lab: research/iodp-ddc-lab/iodp-ddc-lab.m $(LIBRARY) | $(BUILD)/research
	$(CC) $(RESEARCH_CFLAGS) research/iodp-ddc-lab/iodp-ddc-lab.m $(LIBRARY) -o $@ $(RESEARCH_LDLIBS)

$(BUILD)/test_research_lab_support: research/tests/test_research_lab_support.c \
	$(RESEARCH_COMMON_SUPPORT) $(RESEARCH_COMMON_PARSER) src/ddc/protocol.c | $(BUILD)
	$(CC) $(RESEARCH_CFLAGS) research/tests/test_research_lab_support.c \
		$(RESEARCH_COMMON_SUPPORT) $(RESEARCH_COMMON_PARSER) src/ddc/protocol.c -o $@

research: $(RESEARCH_BINS)

research-test: $(RESEARCH_TESTS) research
	$(BUILD)/test_research_lab_support

.PHONY: all library check-library-sources test install-library install-cli install \
	uninstall-library uninstall-cli uninstall consumer-test clean research research-test
