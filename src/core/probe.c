#include "rss_ddc.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

enum {
  RSS_DDC_PROBE_STANDARD_CONTROL_COUNT = 6,
  RSS_DDC_PROBE_ADDRESS_COUNT = 256,
  RSS_DDC_PROBE_EXTENDED_DELAY_MS = 25,
  RSS_DDC_PROBE_TRANSPORT_FAILURE_LIMIT = 8
};

typedef struct {
  char *data;
  size_t length;
  size_t capacity;
} JSONWriter;

struct RSSDDCProbe {
  RSSDDCProbeTarget target;
  RSSDDCProbeReadTransport transport;
  RSSDDCMonitorKnowledge *knowledge;
  RSSDDCProbeControlDiagnostic *controls;
  char semantic_ids[RSS_DDC_PROBE_ADDRESS_COUNT][32];
  RSSDDCProbeDiagnostics diagnostics;
  uint32_t system_list_index;
};

static bool writer_reserve(JSONWriter *writer, size_t extra) {
  if (extra > SIZE_MAX - writer->length - 1)
    return false;
  size_t required = writer->length + extra + 1;
  if (required <= writer->capacity)
    return true;
  size_t capacity = writer->capacity ? writer->capacity : 1024;
  while (capacity < required) {
    if (capacity > SIZE_MAX / 2)
      return false;
    capacity *= 2;
  }
  char *data = realloc(writer->data, capacity);
  if (!data)
    return false;
  writer->data = data;
  writer->capacity = capacity;
  return true;
}

static bool writer_put(JSONWriter *writer, const char *text) {
  size_t length = strlen(text);
  if (!writer_reserve(writer, length))
    return false;
  memcpy(writer->data + writer->length, text, length);
  writer->length += length;
  writer->data[writer->length] = '\0';
  return true;
}

static bool writer_format(JSONWriter *writer, const char *format, ...)
    __attribute__((format(printf, 2, 3)));
static bool writer_format(JSONWriter *writer, const char *format, ...) {
  va_list arguments;
  va_start(arguments, format);
  int length = vsnprintf(NULL, 0, format, arguments);
  va_end(arguments);
  if (length < 0 || !writer_reserve(writer, (size_t)length))
    return false;
  va_start(arguments, format);
  vsnprintf(writer->data + writer->length, writer->capacity - writer->length,
            format, arguments);
  va_end(arguments);
  writer->length += (size_t)length;
  return true;
}

/* The v0.1 parser intentionally accepts only simple JSON strings. Hardware
 * identity text is therefore normalized rather than being allowed to produce
 * malformed output. */
static bool writer_string(JSONWriter *writer, const char *text) {
  if (!writer_put(writer, "\""))
    return false;
  for (const unsigned char *cursor = (const unsigned char *)(text ? text : "");
       *cursor; ++cursor) {
    char value = (*cursor < 0x20 || *cursor == '"' || *cursor == '\\')
                     ? '_'
                     : (char)*cursor;
    if (!writer_reserve(writer, 1))
      return false;
    writer->data[writer->length++] = value;
    writer->data[writer->length] = '\0';
  }
  return writer_put(writer, "\"");
}

static bool append_evidence(JSONWriter *writer, const char *type,
                            const char *source_id, const char *reference) {
  if (!writer_put(writer, "{\"type\":"))
    return false;
  if (!writer_string(writer, type))
    return false;
  if (source_id) {
    if (!writer_put(writer, ",\"sourceId\":"))
      return false;
    if (!writer_string(writer, source_id))
      return false;
  }
  if (reference) {
    if (!writer_put(writer, ",\"reference\":"))
      return false;
    if (!writer_string(writer, reference))
      return false;
  }
  return writer_put(writer, "}");
}

static bool mccs_advertises(const RSSDDCMCCSCapabilities *mccs, bool available,
                            uint8_t vcp_code) {
  return available && rss_ddc_mccs_capabilities_has_vcp(mccs, vcp_code);
}

static bool append_capability(JSONWriter *writer,
                              const RSSDDCProbeControlDiagnostic *control,
                              const RSSDDCMCCSCapabilities *mccs) {
  bool advertised = control->mccs_advertised;
  if (!control->readable && !advertised)
    return true;
  if (!writer_put(writer, "{\"id\":"))
    return false;
  if (!writer_string(writer, control->semantic_id))
    return false;
  if (!writer_put(
          writer,
          ",\"availability\":\"supported\",\"confidence\":\"observed\""))
    return false;
  if (control->readable &&
      !writer_format(writer,
                     ",\"reportedMaximum\":{\"type\":\"unsigned\",\"value\":%u}",
                     control->maximum_value))
    return false;
  if (!writer_put(writer, ",\"methods\":[{\"id\":"))
    return false;
  char method_id[32] = {};
  snprintf(method_id, sizeof(method_id), "mccs-vcp-%02x", control->vcp_code);
  if (!writer_string(writer, method_id) ||
      !writer_format(writer,
                     ",\"type\":\"mccs_vcp\",\"vcpCode\":%u,\"readable\":%s,"
                     "\"writable\":false,\"risk\":\"%s\","
                     "\"confidence\":\"observed\",\"evidence\":[",
                     control->vcp_code, control->readable ? "true" : "false",
                     control->known_semantic ? "read_standard" : "read_extended"))
    return false;
  bool evidence_comma = false;
  if (advertised) {
    if (!append_evidence(writer, "mccs_advertised", "mccs-capabilities-1",
                         NULL))
      return false;
    evidence_comma = true;
  }
  if (control->readable) {
    if (evidence_comma && !writer_put(writer, ","))
      return false;
    if (!append_evidence(writer,
                         control->stable ? "stable_get" : "extended_discovery",
                         NULL, NULL))
      return false;
  }
  if (!writer_put(writer, "]}],\"values\":["))
    return false;
  bool value_comma = false;
  if (control->readable) {
    if (!writer_put(
            writer,
            "{\"id\":\"observed\",\"raw\":{\"type\":\"unsigned\",\"value\":"))
      return false;
    if (!writer_format(writer, "%u", control->current_value) ||
        !writer_put(
            writer,
            "},\"readable\":true,\"writable\":false,\"confidence\":"
            "\"observed\",\"validation\":\"read_validated\",\"evidence\":["))
      return false;
    if (!append_evidence(writer,
                         control->stable ? "stable_get" : "extended_discovery",
                         NULL, NULL) ||
        !writer_put(writer, "]}"))
      return false;
    value_comma = true;
    if (!control->stable && control->repeat_error == RSS_DDC_OK) {
      if (!writer_format(writer,
                         ",{\"id\":\"observed_repeat\",\"raw\":{\"type\":\"unsigned\",\"value\":%u},\"readable\":true,\"writable\":false,\"confidence\":\"observed\",\"evidence\":[",
                         control->repeat_current_value) ||
          !append_evidence(writer, "extended_discovery", NULL, NULL) ||
          !writer_put(writer, "]}"))
        return false;
    }
  }
  if (advertised) {
    const uint8_t *values = NULL;
    size_t value_count = 0;
    if (rss_ddc_mccs_capabilities_enum_values(mccs, control->vcp_code, &values,
                                              &value_count) == RSS_DDC_OK) {
      for (size_t i = 0; i < value_count; ++i) {
        if (value_comma && !writer_put(writer, ","))
          return false;
        if (!writer_format(
                writer,
                "{\"id\":\"advertised_%02x\",\"raw\":{\"type\":\"unsigned\","
                "\"value\":%u},\"readable\":true,\"writable\":false,"
                "\"availability\":\"supported\",\"evidence\":[",
                values[i], values[i]) ||
            !append_evidence(writer, "mccs_advertised", "mccs-capabilities-1",
                             NULL) ||
            !writer_put(writer, "]}"))
          return false;
        value_comma = true;
      }
    }
  }
  if (!writer_put(writer, "],\"evidence\":["))
    return false;
  evidence_comma = false;
  if (advertised) {
    if (!append_evidence(writer, "mccs_advertised", "mccs-capabilities-1",
                         NULL))
      return false;
    evidence_comma = true;
  }
  if (control->readable) {
    if (evidence_comma && !writer_put(writer, ","))
      return false;
    if (!append_evidence(writer,
                         control->stable ? "stable_get" : "extended_discovery",
                         NULL, NULL))
      return false;
  }
  return writer_put(writer, "]}");
}

static bool append_identity_text(JSONWriter *writer, bool *comma,
                                 const char *key, const char *value) {
  if (!value || !value[0])
    return true;
  if (*comma && !writer_put(writer, ","))
    return false;
  return writer_string(writer, key) && writer_put(writer, ":") &&
         writer_string(writer, value) && (*comma = true);
}

static RSSDDCError build_knowledge(RSSDDCProbe *probe,
                                   const RSSDDCMCCSCapabilities *mccs,
                                   bool mccs_available) {
  JSONWriter writer = {};
  bool identity_comma = false;
  bool ok = writer_put(&writer, "{\"schemaVersion\":\"monitor-knowledge/"
                                "v0.1\",\"identity\":{") &&
            append_identity_text(&writer, &identity_comma, "manufacturer",
                                 probe->target.display.manufacturer) &&
            append_identity_text(&writer, &identity_comma, "model",
                                 probe->target.display.product_name) &&
            append_identity_text(&writer, &identity_comma, "edidManufacturer",
                                 probe->target.display.edid_manufacturer);
  if (ok && probe->target.display.edid_product_code_present) {
    if (identity_comma)
      ok = writer_put(&writer, ",");
    ok = ok && writer_format(&writer, "\"edidProductCode\":%u",
                              probe->target.display.edid_product_code);
    identity_comma = true;
  }
  ok = ok && append_identity_text(&writer, &identity_comma, "serial",
                                  probe->target.display.serial) &&
       append_identity_text(&writer, &identity_comma, "provider",
                            rss_ddc_provider_string(
                                probe->target.display.provider)) &&
       append_identity_text(&writer, &identity_comma, "transport",
                            probe->target.display.transport) &&
       append_identity_text(&writer, &identity_comma, "branch",
                            probe->target.display.branch_device_id) &&
       (!identity_comma || writer_put(&writer, ",")) &&
       writer_put(&writer, "\"confidence\":\"observed\"},\"sources\":[");
  if (ok && mccs_available)
    ok = writer_put(&writer,
                    "{\"id\":\"mccs-capabilities-1\",\"type\":"
                    "\"mccs_capabilities\",\"reference\":") &&
         writer_string(&writer, mccs->raw) && writer_put(&writer, "}");
  ok = ok && writer_put(&writer, "],\"capabilities\":[");
  bool comma = false;
  for (size_t i = 0; ok && i < probe->diagnostics.control_count; ++i) {
    bool retained =
        probe->controls[i].readable ||
        probe->controls[i].mccs_advertised;
    if (retained && comma)
      ok = writer_put(&writer, ",");
    if (ok && retained)
      ok =
          append_capability(&writer, &probe->controls[i], mccs);
    comma = comma || retained;
  }
  ok = ok && writer_put(&writer, "],\"inputRoutes\":[],\"relationships\":[]}");
  RSSDDCError result = RSS_DDC_ERROR_SYSTEM;
  if (ok)
    result = rss_ddc_monitor_knowledge_parse_json(writer.data, writer.length,
                                                  &probe->knowledge);
  free(writer.data);
  return result;
}

RSSDDCError rss_ddc_probe_create(const RSSDDCProbeTarget *target,
                                 const RSSDDCProbeReadTransport *transport,
                                 RSSDDCProbe **out) {
  if (!target || !transport || !transport->get_vcp || !out)
    return RSS_DDC_ERROR_ARGUMENT;
  *out = NULL;
  if (target->correlation != RSS_DDC_PROBE_CORRELATION_EXACT)
    return RSS_DDC_ERROR_DISCOVERY;
  RSSDDCProbe *probe = calloc(1, sizeof(*probe));
  if (!probe)
    return RSS_DDC_ERROR_SYSTEM;
  probe->target = *target;
  probe->transport = *transport;
  probe->controls = calloc(RSS_DDC_PROBE_ADDRESS_COUNT, sizeof(*probe->controls));
  if (!probe->controls) {
    free(probe);
    return RSS_DDC_ERROR_SYSTEM;
  }
  probe->diagnostics.display = target->display;
  probe->diagnostics.mccs_error = RSS_DDC_ERROR_UNSUPPORTED_CAPABILITY;
  probe->diagnostics.controls = probe->controls;
  *out = probe;
  return RSS_DDC_OK;
}

void rss_ddc_probe_destroy(RSSDDCProbe *probe) {
  if (!probe)
    return;
  rss_ddc_monitor_knowledge_destroy(probe->knowledge);
  free(probe->controls);
  free(probe);
}

static bool transport_failure(RSSDDCError error) {
  return error == RSS_DDC_ERROR_READ || error == RSS_DDC_ERROR_SYSTEM ||
         error == RSS_DDC_ERROR_SERVICE_CONSTRUCTION ||
         error == RSS_DDC_ERROR_DISCOVERY || error == RSS_DDC_ERROR_SAFETY_GATE;
}
static uint64_t monotonicish_milliseconds(void) {
  struct timeval now = {};
  gettimeofday(&now, NULL);
  return (uint64_t)now.tv_sec * 1000u + (uint64_t)now.tv_usec / 1000u;
}
static bool unsupported_response(RSSDDCError error) {
  return error == RSS_DDC_ERROR_UNSUPPORTED_CAPABILITY ||
         error == RSS_DDC_ERROR_REPLY_STATUS;
}
static void reset_observation(RSSDDCProbe *probe, bool extended) {
  rss_ddc_monitor_knowledge_destroy(probe->knowledge);
  probe->knowledge = NULL;
  memset(probe->controls, 0,
         RSS_DDC_PROBE_ADDRESS_COUNT * sizeof(*probe->controls));
  probe->diagnostics = (RSSDDCProbeDiagnostics){
      .display = probe->target.display,
      .mccs_error = RSS_DDC_ERROR_UNSUPPORTED_CAPABILITY,
      .extended = extended,
      .inter_request_delay_ms = extended ? RSS_DDC_PROBE_EXTENDED_DELAY_MS : 0,
      .stability_read_count = 2,
      .requested_addresses = extended ? RSS_DDC_PROBE_ADDRESS_COUNT :
                                       RSS_DDC_PROBE_STANDARD_CONTROL_COUNT,
      .controls = probe->controls};
}
static void set_semantic_id(RSSDDCProbe *probe, size_t index, uint8_t code) {
  const RSSDDCSemanticRegistryEntry *semantic =
      rss_ddc_semantic_registry_lookup_vcp(code);
  RSSDDCProbeControlDiagnostic *diagnostic = &probe->controls[index];
  diagnostic->known_semantic = semantic != NULL;
  if (semantic)
    diagnostic->semantic_id = semantic->semantic_id;
  else {
    snprintf(probe->semantic_ids[index], sizeof(probe->semantic_ids[index]),
             "vendor.unknown.vcp.%02x", code);
    diagnostic->semantic_id = probe->semantic_ids[index];
  }
}
static void record_failed_read(RSSDDCProbe *probe,
                               RSSDDCProbeControlDiagnostic *diagnostic,
                               RSSDDCError error) {
  ++probe->diagnostics.controls_failed;
  if (unsupported_response(error)) {
    diagnostic->classification = RSS_DDC_PROBE_CONTROL_UNSUPPORTED;
    ++probe->diagnostics.controls_unsupported;
  } else if (transport_failure(error)) {
    diagnostic->classification = RSS_DDC_PROBE_CONTROL_TRANSPORT_ERROR;
    ++probe->diagnostics.controls_transport_errors;
  } else {
    diagnostic->classification = RSS_DDC_PROBE_CONTROL_MALFORMED;
    ++probe->diagnostics.controls_malformed;
  }
}
static bool attempt_control(RSSDDCProbe *probe, size_t index, uint8_t code,
                            const RSSDDCMCCSCapabilities *mccs,
                            bool mccs_available, bool delay_before_repeat,
                            size_t *consecutive_transport_failures) {
  RSSDDCProbeControlDiagnostic *diagnostic = &probe->controls[index];
  diagnostic->vcp_code = code;
  diagnostic->mccs_advertised = mccs_advertises(mccs, mccs_available, code);
  set_semantic_id(probe, index, code);
  ++probe->diagnostics.controls_attempted;
  ++probe->diagnostics.control_count;
  if (probe->transport.progress)
    probe->transport.progress(probe->transport.context,
                              probe->diagnostics.controls_attempted,
                              probe->diagnostics.requested_addresses);
  RSSDDCVCPResult first = {};
  diagnostic->first_error =
      probe->transport.get_vcp(probe->transport.context, code, &first);
  if (diagnostic->first_error != RSS_DDC_OK || first.vcp_code != code) {
    if (diagnostic->first_error == RSS_DDC_OK)
      diagnostic->first_error = RSS_DDC_ERROR_REPLY_VCP;
    record_failed_read(probe, diagnostic, diagnostic->first_error);
    if (transport_failure(diagnostic->first_error))
      ++*consecutive_transport_failures;
    else
      *consecutive_transport_failures = 0;
    return *consecutive_transport_failures < RSS_DDC_PROBE_TRANSPORT_FAILURE_LIMIT;
  }
  *consecutive_transport_failures = 0;
  diagnostic->readable = true;
  diagnostic->current_value = first.current_value;
  diagnostic->maximum_value = first.maximum_value;
  ++probe->diagnostics.controls_readable;
  if (delay_before_repeat && probe->transport.delay)
    probe->transport.delay(probe->transport.context,
                           RSS_DDC_PROBE_EXTENDED_DELAY_MS);
  RSSDDCVCPResult repeat = {};
  diagnostic->repeat_error =
      probe->transport.get_vcp(probe->transport.context, code, &repeat);
  diagnostic->repeat_current_value = repeat.current_value;
  diagnostic->repeat_maximum_value = repeat.maximum_value;
  if (diagnostic->repeat_error == RSS_DDC_OK && repeat.vcp_code != code)
    diagnostic->repeat_error = RSS_DDC_ERROR_REPLY_VCP;
  diagnostic->stable = diagnostic->repeat_error == RSS_DDC_OK &&
                       repeat.current_value == first.current_value &&
                       repeat.maximum_value == first.maximum_value;
  if (diagnostic->stable) {
    diagnostic->classification = RSS_DDC_PROBE_CONTROL_STABLE;
    ++probe->diagnostics.controls_stable;
  } else {
    diagnostic->classification = RSS_DDC_PROBE_CONTROL_VARIABLE;
    ++probe->diagnostics.controls_variable;
  }
  return true;
}
static RSSDDCError collect_mccs(RSSDDCProbe *probe, RSSDDCMCCSCapabilities *mccs,
                                 bool *available) {
  *available = false;
  if ((probe->target.display.capabilities & RSS_DDC_CAP_MCCS_CAPABILITIES) &&
      probe->transport.get_mccs_capabilities) {
    probe->diagnostics.mccs_error =
        probe->transport.get_mccs_capabilities(probe->transport.context, mccs);
    *available = probe->diagnostics.mccs_error == RSS_DDC_OK;
  }
  return RSS_DDC_OK;
}
static bool extended_provider_supported(RSSDDCProvider provider) {
  /* These are the only providers whose dispatch path already reaches a
   * validated GET implementation. MCDP intentionally has no GET path, so an
   * exhaustive scan must fail closed rather than exercising speculative IO. */
  return provider == RSS_DDC_PROVIDER_DCPDP13 ||
         provider == RSS_DDC_PROVIDER_DCPDP_SERVICE ||
         provider == RSS_DDC_PROVIDER_PS190;
}
RSSDDCError rss_ddc_probe_quick(RSSDDCProbe *probe) {
  if (!probe || !probe->transport.get_vcp)
    return RSS_DDC_ERROR_ARGUMENT;
  uint64_t started = monotonicish_milliseconds();
  reset_observation(probe, false);
  RSSDDCMCCSCapabilities mccs = {};
  bool mccs_available = false;
  collect_mccs(probe, &mccs, &mccs_available);
  static const uint8_t standard_vcps[] = {0x10, 0x12, 0x14, 0x16, 0x18, 0x1a};
  size_t failures = 0;
  for (size_t i = 0; i < sizeof(standard_vcps); ++i)
    (void)attempt_control(probe, i, standard_vcps[i], &mccs, mccs_available,
                          false, &failures);
  RSSDDCError error = build_knowledge(probe, &mccs, mccs_available);
  probe->diagnostics.duration_ms = monotonicish_milliseconds() - started;
  return error;
}
RSSDDCError rss_ddc_probe_extended(RSSDDCProbe *probe) {
  if (!probe || !probe->transport.get_vcp)
    return RSS_DDC_ERROR_ARGUMENT;
  if (!extended_provider_supported(probe->target.display.provider))
    return RSS_DDC_ERROR_UNSUPPORTED_PROVIDER;
  uint64_t started = monotonicish_milliseconds();
  reset_observation(probe, true);
  RSSDDCMCCSCapabilities mccs = {};
  bool mccs_available = false;
  collect_mccs(probe, &mccs, &mccs_available);
  bool planned[RSS_DDC_PROBE_ADDRESS_COUNT] = {};
  size_t failures = 0;
  size_t index = 0;
#define EXTENDED_ATTEMPT(code)                                                   \
  do {                                                                           \
    uint8_t planned_code = (uint8_t)(code);                                      \
    if (!planned[planned_code]) {                                                \
      if (index != 0 && probe->transport.delay)                                 \
        probe->transport.delay(probe->transport.context,                        \
                               RSS_DDC_PROBE_EXTENDED_DELAY_MS);                \
      planned[planned_code] = true;                                              \
      if (!attempt_control(probe, index++, planned_code, &mccs, mccs_available, \
                           true, &failures)) {                                  \
        probe->diagnostics.aborted = true;                                       \
        probe->diagnostics.abort_reason =                                       \
            RSS_DDC_PROBE_ABORT_TRANSPORT_FAILURE_STORM;                        \
        goto complete;                                                           \
      }                                                                          \
    }                                                                            \
  } while (0)
  if (mccs_available)
    for (size_t i = 0; i < mccs.feature_count; ++i)
      EXTENDED_ATTEMPT(mccs.features[i].vcp_code);
  static const uint8_t standard_vcps[] = {0x10, 0x12, 0x14, 0x16, 0x18, 0x1a};
  for (size_t i = 0; i < sizeof(standard_vcps); ++i)
    EXTENDED_ATTEMPT(standard_vcps[i]);
  for (unsigned int code = 0; code <= UINT8_MAX; ++code)
    EXTENDED_ATTEMPT(code);
complete:
  ;
#undef EXTENDED_ATTEMPT
  RSSDDCError error = build_knowledge(probe, &mccs, mccs_available);
  probe->diagnostics.duration_ms = monotonicish_milliseconds() - started;
  return error;
}

RSSDDCError rss_ddc_probe_knowledge(const RSSDDCProbe *probe,
                                    const RSSDDCMonitorKnowledge **knowledge) {
  if (!probe || !knowledge)
    return RSS_DDC_ERROR_ARGUMENT;
  if (!probe->knowledge)
    return RSS_DDC_ERROR_NOT_FOUND;
  *knowledge = probe->knowledge;
  return RSS_DDC_OK;
}

RSSDDCError rss_ddc_probe_diagnostics(const RSSDDCProbe *probe,
                                      RSSDDCProbeDiagnostics *diagnostics) {
  if (!probe || !diagnostics)
    return RSS_DDC_ERROR_ARGUMENT;
  *diagnostics = probe->diagnostics;
  return RSS_DDC_OK;
}

static RSSDDCError system_get_vcp(void *context, uint8_t vcp_code,
                                  RSSDDCVCPResult *result) {
  const RSSDDCProbe *probe = context;
  return rss_ddc_get_vcp(probe->system_list_index, vcp_code, result);
}

static RSSDDCError system_get_mccs(void *context,
                                   RSSDDCMCCSCapabilities *capabilities) {
  const RSSDDCProbe *probe = context;
  return rss_ddc_get_mccs_capabilities(probe->system_list_index, capabilities);
}
static void system_delay(void *context, uint32_t milliseconds) {
  (void)context;
  usleep((useconds_t)milliseconds * 1000u);
}
static void system_progress(void *context, size_t attempted, size_t requested) {
  (void)context;
  fprintf(stderr, "\rrss-ddc: Extended Probe progress: %zu/%zu", attempted,
          requested);
  if (attempted == requested)
    fputc('\n', stderr);
}

RSSDDCError rss_ddc_probe_quick_for_display(uint32_t list_index,
                                            RSSDDCProbe **out) {
  if (!out)
    return RSS_DDC_ERROR_ARGUMENT;
  RSSDDCDisplay display = {};
  RSSDDCError error = rss_ddc_get_display(list_index, &display);
  if (error != RSS_DDC_OK)
    return error;
  RSSDDCProbeTarget target = {.display = display,
                              .correlation = RSS_DDC_PROBE_CORRELATION_EXACT};
  RSSDDCProbeReadTransport transport = {
      .get_vcp = system_get_vcp,
      .get_mccs_capabilities = system_get_mccs,
      .delay = system_delay};
  RSSDDCProbe *probe = NULL;
  error = rss_ddc_probe_create(&target, &transport, &probe);
  if (error != RSS_DDC_OK)
    return error;
  probe->system_list_index = list_index;
  probe->transport.context = probe;
  error = rss_ddc_probe_quick(probe);
  if (error != RSS_DDC_OK) {
    rss_ddc_probe_destroy(probe);
    return error;
  }
  *out = probe;
  return RSS_DDC_OK;
}
RSSDDCError rss_ddc_probe_extended_for_display(uint32_t list_index,
                                               RSSDDCProbe **out) {
  if (!out)
    return RSS_DDC_ERROR_ARGUMENT;
  RSSDDCDisplay display = {};
  RSSDDCError error = rss_ddc_get_display(list_index, &display);
  if (error != RSS_DDC_OK)
    return error;
  RSSDDCProbeTarget target = {.display = display,
                              .correlation = RSS_DDC_PROBE_CORRELATION_EXACT};
  RSSDDCProbeReadTransport transport = {
      .get_vcp = system_get_vcp,
      .get_mccs_capabilities = system_get_mccs,
      .delay = system_delay,
      .progress = system_progress};
  RSSDDCProbe *probe = NULL;
  error = rss_ddc_probe_create(&target, &transport, &probe);
  if (error != RSS_DDC_OK)
    return error;
  probe->system_list_index = list_index;
  probe->transport.context = probe;
  error = rss_ddc_probe_extended(probe);
  if (error != RSS_DDC_OK) {
    rss_ddc_probe_destroy(probe);
    return error;
  }
  *out = probe;
  return RSS_DDC_OK;
}
