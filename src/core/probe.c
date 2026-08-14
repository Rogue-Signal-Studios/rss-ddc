#include "rss_ddc.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { RSS_DDC_PROBE_STANDARD_CONTROL_COUNT = 6 };

typedef struct {
  char *data;
  size_t length;
  size_t capacity;
} JSONWriter;

struct RSSDDCProbe {
  RSSDDCProbeTarget target;
  RSSDDCProbeReadTransport transport;
  RSSDDCMonitorKnowledge *knowledge;
  RSSDDCProbeControlDiagnostic controls[RSS_DDC_PROBE_STANDARD_CONTROL_COUNT];
  RSSDDCProbeDiagnostics diagnostics;
  uint32_t system_list_index;
  bool system_transport;
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
                              const RSSDDCMCCSCapabilities *mccs,
                              bool mccs_available) {
  bool advertised = mccs_advertises(mccs, mccs_available, control->vcp_code);
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
                     "\"writable\":false,\"risk\":\"read_standard\","
                     "\"confidence\":\"observed\",\"evidence\":[",
                     control->vcp_code, control->readable ? "true" : "false"))
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
        mccs_advertises(mccs, mccs_available, probe->controls[i].vcp_code);
    if (retained && comma)
      ok = writer_put(&writer, ",");
    if (ok && retained)
      ok =
          append_capability(&writer, &probe->controls[i], mccs, mccs_available);
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
  free(probe);
}

RSSDDCError rss_ddc_probe_quick(RSSDDCProbe *probe) {
  if (!probe || !probe->transport.get_vcp)
    return RSS_DDC_ERROR_ARGUMENT;
  rss_ddc_monitor_knowledge_destroy(probe->knowledge);
  probe->knowledge = NULL;
  memset(probe->controls, 0, sizeof(probe->controls));
  probe->diagnostics.controls_attempted = 0;
  probe->diagnostics.controls_readable = 0;
  probe->diagnostics.controls_stable = 0;
  probe->diagnostics.controls_variable = 0;
  probe->diagnostics.controls_failed = 0;
  probe->diagnostics.control_count = RSS_DDC_PROBE_STANDARD_CONTROL_COUNT;
  RSSDDCMCCSCapabilities mccs = {};
  bool mccs_available = false;
  if ((probe->target.display.capabilities & RSS_DDC_CAP_MCCS_CAPABILITIES) &&
      probe->transport.get_mccs_capabilities) {
    probe->diagnostics.mccs_error =
        probe->transport.get_mccs_capabilities(probe->transport.context, &mccs);
    mccs_available = probe->diagnostics.mccs_error == RSS_DDC_OK;
  }
  static const uint8_t standard_vcps[] = {0x10, 0x12, 0x14, 0x16, 0x18, 0x1a};
  for (size_t i = 0; i < sizeof(standard_vcps); ++i) {
    RSSDDCProbeControlDiagnostic *diagnostic = &probe->controls[i];
    const RSSDDCSemanticRegistryEntry *semantic =
        rss_ddc_semantic_registry_lookup_vcp(standard_vcps[i]);
    diagnostic->vcp_code = standard_vcps[i];
    diagnostic->semantic_id = semantic ? semantic->semantic_id : NULL;
    ++probe->diagnostics.controls_attempted;
    RSSDDCVCPResult first = {};
    diagnostic->first_error = probe->transport.get_vcp(
        probe->transport.context, diagnostic->vcp_code, &first);
    if (diagnostic->first_error != RSS_DDC_OK ||
        first.vcp_code != diagnostic->vcp_code) {
      if (diagnostic->first_error == RSS_DDC_OK)
        diagnostic->first_error = RSS_DDC_ERROR_REPLY_VCP;
      ++probe->diagnostics.controls_failed;
      continue;
    }
    diagnostic->readable = true;
    diagnostic->current_value = first.current_value;
    diagnostic->maximum_value = first.maximum_value;
    ++probe->diagnostics.controls_readable;
    RSSDDCVCPResult repeat = {};
    diagnostic->repeat_error = probe->transport.get_vcp(
        probe->transport.context, diagnostic->vcp_code, &repeat);
    diagnostic->stable = diagnostic->repeat_error == RSS_DDC_OK &&
                         repeat.vcp_code == diagnostic->vcp_code &&
                         repeat.current_value == first.current_value &&
                         repeat.maximum_value == first.maximum_value;
    if (diagnostic->stable)
      ++probe->diagnostics.controls_stable;
    else
      ++probe->diagnostics.controls_variable;
  }
  return build_knowledge(probe, &mccs, mccs_available);
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
      .get_vcp = system_get_vcp, .get_mccs_capabilities = system_get_mccs};
  RSSDDCProbe *probe = NULL;
  error = rss_ddc_probe_create(&target, &transport, &probe);
  if (error != RSS_DDC_OK)
    return error;
  probe->system_list_index = list_index;
  probe->system_transport = true;
  probe->transport.context = probe;
  error = rss_ddc_probe_quick(probe);
  if (error != RSS_DDC_OK) {
    rss_ddc_probe_destroy(probe);
    return error;
  }
  *out = probe;
  return RSS_DDC_OK;
}
