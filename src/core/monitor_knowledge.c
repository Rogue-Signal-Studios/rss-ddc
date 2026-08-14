#include "rss_ddc.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* This is intentionally a small bounded JSON reader, rather than a transport
 * dependency. The model is heap-owned so profile-sized records never become
 * large caller stack objects. */
enum {
  MK_MAX_DOCUMENT = 1024 * 1024,
  MK_MAX_ITEMS = 512,
  MK_MAX_METHODS = 32,
  MK_MAX_VALUES = 256,
  MK_MAX_EVIDENCE = 128,
  MK_MAX_SOURCES = 128
};
typedef struct {
  const char *p, *end;
} J;
typedef struct {
  RSSDDCMonitorKnowledgeValue view;
  RSSDDCRawValue *aliases;
  RSSDDCEvidence *evidence;
} Value;
typedef struct {
  RSSDDCMonitorKnowledgeMethod view;
  RSSDDCEvidence *evidence;
} Method;
typedef struct {
  RSSDDCMonitorKnowledgeCondition view;
  RSSDDCEvidence *evidence;
} Condition;
typedef struct {
  RSSDDCMonitorKnowledgeConditionGroup view;
  Condition *conditions;
  RSSDDCMonitorKnowledgeCondition *condition_views;
} ConditionGroup;
typedef struct {
  RSSDDCMonitorKnowledgeCapability view;
  ConditionGroup *condition_groups;
  RSSDDCMonitorKnowledgeConditionGroup *condition_group_views;
  Method *methods;
  Value *values;
  RSSDDCMonitorKnowledgeMethod *method_views;
  RSSDDCMonitorKnowledgeValue *value_views;
  RSSDDCEvidence *evidence;
} Capability;
typedef struct {
  RSSDDCInputRoute view;
  RSSDDCEvidence *evidence;
  bool read_value_present;
  bool switch_value_present;
} Route;
typedef struct {
  RSSDDCRelationship view;
  RSSDDCEvidence *evidence;
} Relationship;
struct RSSDDCMonitorKnowledge {
  char *schema;
  RSSDDCMonitorIdentity identity;
  RSSDDCEvidence *identity_evidence;
  RSSDDCMonitorKnowledgeSource *sources;
  size_t source_count;
  Capability *capabilities;
  size_t capability_count;
  Route *routes;
  size_t route_count;
  Relationship *relationships;
  size_t relationship_count;
};
static bool refresh_capability_views(Capability *c);

static void ws(J *j) {
  while (j->p < j->end && isspace((unsigned char)*j->p))
    ++j->p;
}
static bool take(J *j, char c) {
  ws(j);
  if (j->p == j->end || *j->p != c)
    return false;
  ++j->p;
  return true;
}
static char *dup_n(const char *p, size_t n) {
  char *s = malloc(n + 1);
  if (s != NULL) {
    memcpy(s, p, n);
    s[n] = 0;
  }
  return s;
}
static bool str(J *j, char **out) {
  ws(j);
  if (j->p == j->end || *j->p++ != '"')
    return false;
  const char *start = j->p;
  while (j->p < j->end && *j->p != '"') {
    if ((unsigned char)*j->p < 0x20 || *j->p == '\\')
      return false;
    ++j->p;
  }
  if (j->p == j->end)
    return false;
  *out = dup_n(start, (size_t)(j->p++ - start));
  return *out != NULL;
}
static bool number(J *j, int64_t *out) {
  ws(j);
  bool neg = j->p < j->end && *j->p == '-';
  if (neg)
    ++j->p;
  if (j->p == j->end || !isdigit((unsigned char)*j->p))
    return false;
  uint64_t v = 0;
  do {
    if (v > (UINT64_MAX - 9) / 10)
      return false;
    v = v * 10 + (uint64_t)(*j->p++ - '0');
  } while (j->p < j->end && isdigit((unsigned char)*j->p));
  if (j->p < j->end && (*j->p == '.' || *j->p == 'e' || *j->p == 'E'))
    return false;
  if ((!neg && v > INT64_MAX) || (neg && v > (uint64_t)INT64_MAX + 1))
    return false;
  *out = neg ? -(int64_t)v : (int64_t)v;
  return true;
}
static bool boolean(J *j, bool *out) {
  ws(j);
  if ((size_t)(j->end - j->p) >= 4 && memcmp(j->p, "true", 4) == 0) {
    j->p += 4;
    *out = true;
    return true;
  }
  if ((size_t)(j->end - j->p) >= 5 && memcmp(j->p, "false", 5) == 0) {
    j->p += 5;
    *out = false;
    return true;
  }
  return false;
}
static bool skip(J *j) {
  ws(j);
  if (j->p == j->end)
    return false;
  if (*j->p == '"') {
    char *s = NULL;
    bool ok = str(j, &s);
    free(s);
    return ok;
  }
  if (*j->p == '{') {
    ++j->p;
    ws(j);
    if (j->p < j->end && *j->p == '}') {
      ++j->p;
      return true;
    }
    for (;;) {
      char *k = NULL;
      bool ok = str(j, &k) && take(j, ':') && skip(j);
      free(k);
      if (!ok)
        return false;
      ws(j);
      if (j->p < j->end && *j->p == '}') {
        ++j->p;
        return true;
      }
      if (!take(j, ','))
        return false;
    }
  }
  if (*j->p == '[') {
    ++j->p;
    ws(j);
    if (j->p < j->end && *j->p == ']') {
      ++j->p;
      return true;
    }
    for (;;) {
      if (!skip(j))
        return false;
      ws(j);
      if (j->p < j->end && *j->p == ']') {
        ++j->p;
        return true;
      }
      if (!take(j, ','))
        return false;
    }
  }
  int64_t n;
  if (number(j, &n))
    return true;
  bool b;
  if (boolean(j, &b))
    return true;
  if ((size_t)(j->end - j->p) >= 4 && memcmp(j->p, "null", 4) == 0) {
    j->p += 4;
    return true;
  }
  return false;
}

#define NAMED(kind, type, ...)                                                 \
  static const char *kind##_names[] = {__VA_ARGS__};                           \
  const char *rss_ddc_##kind##_name(type v) {                                  \
    return (size_t)v < sizeof(kind##_names) / sizeof(*kind##_names)            \
               ? kind##_names[v]                                               \
               : "unknown";                                                    \
  }                                                                            \
  RSSDDCError rss_ddc_##kind##_parse(const char *s, type *v) {                 \
    if (!s || !v)                                                              \
      return RSS_DDC_ERROR_ARGUMENT;                                           \
    for (size_t i = 0; i < sizeof(kind##_names) / sizeof(*kind##_names); ++i)  \
      if (strcmp(s, kind##_names[i]) == 0) {                                   \
        *v = (type)i;                                                          \
        return RSS_DDC_OK;                                                     \
      }                                                                        \
    return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;                          \
  }
NAMED(confidence, RSSDDCConfidence, "unknown", "candidate", "observed",
      "correlated", "validated", "hardware_validated")
NAMED(validation, RSSDDCValidation, "not_validated", "read_validated",
      "correlation_validated", "set_confirmed", "hardware_validated")
NAMED(risk, RSSDDCRisk, "read_standard", "read_extended", "guided_read",
      "validate_safe_set", "vendor_experimental_set", "high_risk_denied")
NAMED(evidence_type, RSSDDCEvidenceType, "standard_defined", "mccs_advertised",
      "edid_derived", "profile_known", "rogue_validated_profile",
      "local_validated", "stable_get", "extended_discovery",
      "external_candidate", "manufacturer_family_hint", "model_family_hint",
      "osd_correlated", "set_confirmed")
static const char *availability_names[] = {"unknown", "supported",
                                           "unsupported", "conditional"};
static const char *method_names[] = {"mccs_vcp", "vendor_protocol",
                                     "provider_specific", "unknown"};
static const char *raw_names[] = {"unsigned", "signed", "bytes", "string"};
static const char *relationship_names[] = {"secondary_effect",
                                           "correlates_with", "depends_on",
                                           "conflicts_with", "enabled_by"};
static const char *condition_operator_names[] = {
    "equals",       "not_equals",      "enabled",   "disabled",
    "present",      "absent",          "less_than", "less_or_equal",
    "greater_than", "greater_or_equal"};
static const char *condition_group_names[] = {"all_of", "any_of"};
static bool enum_value(const char *const *names, size_t count, const char *s,
                       int *v) {
  for (size_t i = 0; i < count; ++i)
    if (strcmp(names[i], s) == 0) {
      *v = (int)i;
      return true;
    }
  return false;
}
static bool semantic(const char *s) {
  if (!s || !*s || strlen(s) >= RSS_DDC_MONITOR_KNOWLEDGE_ID_MAX)
    return false;
  bool dot = false;
  for (; *s; ++s) {
    if (*s == '.') {
      dot = true;
      continue;
    }
    if (!(islower((unsigned char)*s) || isdigit((unsigned char)*s) ||
          *s == '_' || *s == '-'))
      return false;
  }
  return dot || strncmp(s, "vendor.", 7) == 0;
}
static bool source_identifier(const char *s) {
  if (!s || !*s || strlen(s) >= RSS_DDC_MONITOR_KNOWLEDGE_ID_MAX)
    return false;
  for (; *s; ++s)
    if (!(islower((unsigned char)*s) || isdigit((unsigned char)*s) ||
          *s == '_' || *s == '-'))
      return false;
  return true;
}

static void free_evidence(RSSDDCEvidence *a, size_t n) {
  if (!a)
    return;
  for (size_t i = 0; i < n; ++i) {
    free((char *)a[i].source_id);
    free((char *)a[i].reference);
    free((char *)a[i].timestamp);
    free((char *)a[i].scope);
  }
  free(a);
}
static void free_sources(RSSDDCMonitorKnowledgeSource *sources, size_t count) {
  for (size_t i = 0; i < count; ++i) {
    free((char *)sources[i].id);
    free((char *)sources[i].type);
    free((char *)sources[i].reference);
  }
  free(sources);
}
static void discard_evidence(RSSDDCEvidence *e) {
  if (!e)
    return;
  free((char *)e->source_id);
  free((char *)e->reference);
  free((char *)e->timestamp);
  free((char *)e->scope);
}
static void free_raw(RSSDDCRawValue *r) {
  if (r && (r->type == RSS_DDC_RAW_BYTES || r->type == RSS_DDC_RAW_STRING))
    free((void *)r->data);
}
static void free_condition(Condition *condition) {
  if (!condition)
    return;
  free((char *)condition->view.semantic_id);
  free((char *)condition->view.value_id);
  free_raw(&condition->view.comparison);
  free_evidence(condition->evidence, condition->view.evidence_count);
  *condition = (Condition){};
}
static void free_condition_group(ConditionGroup *group) {
  if (!group)
    return;
  for (size_t i = 0; i < group->view.condition_count; ++i)
    free_condition(&group->conditions[i]);
  free(group->conditions);
  free(group->condition_views);
  *group = (ConditionGroup){};
}
static void free_knowledge(RSSDDCMonitorKnowledge *k) {
  if (!k)
    return;
  free(k->schema);
  free((char *)k->identity.manufacturer);
  free((char *)k->identity.model);
  free((char *)k->identity.edid_manufacturer);
  free((char *)k->identity.serial);
  free((char *)k->identity.provider);
  free((char *)k->identity.transport);
  free((char *)k->identity.branch);
  free((char *)k->identity.family_hint);
  free_evidence(k->identity_evidence, k->identity.evidence_count);
  free_sources(k->sources, k->source_count);
  for (size_t i = 0; i < k->capability_count; ++i) {
    Capability *c = &k->capabilities[i];
    free((char *)c->view.id);
    free((char *)c->view.label);
    free((char *)c->view.conditions);
    free((char *)c->view.advertised_range.units);
    free((char *)c->view.observed_range.units);
    free((char *)c->view.validated_range.units);
    free_raw(&c->view.reported_maximum);
    for (size_t g = 0; g < c->view.condition_group_count; ++g)
      free_condition_group(&c->condition_groups[g]);
    free(c->condition_groups);
    free(c->condition_group_views);
    free_evidence(c->evidence, c->view.evidence_count);
    for (size_t m = 0; m < c->view.method_count; ++m) {
      Method *x = &c->methods[m];
      free((char *)x->view.id);
      free((char *)x->view.protocol_id);
      free((char *)x->view.address);
      free((char *)x->view.parameters);
      free_evidence(x->evidence, x->view.evidence_count);
    }
    free(c->methods);
    free(c->method_views);
    for (size_t v = 0; v < c->view.value_count; ++v) {
      Value *x = &c->values[v];
      free((char *)x->view.id);
      free((char *)x->view.label);
      free_raw(&x->view.raw);
      for (size_t a = 0; a < x->view.raw_alias_count; ++a)
        free_raw(&x->aliases[a]);
      free(x->aliases);
      free_evidence(x->evidence, x->view.evidence_count);
    }
    free(c->values);
    free(c->value_views);
  }
  free(k->capabilities);
  for (size_t i = 0; i < k->route_count; ++i) {
    Route *r = &k->routes[i];
    free((char *)r->view.id);
    free((char *)r->view.connector);
    free((char *)r->view.port);
    free((char *)r->view.label);
    free_raw(&r->view.read_value);
    free_raw(&r->view.switch_value);
    free_evidence(r->evidence, r->view.evidence_count);
  }
  free(k->routes);
  for (size_t i = 0; i < k->relationship_count; ++i) {
    Relationship *r = &k->relationships[i];
    free((char *)r->view.source_id);
    free((char *)r->view.target_id);
    free((char *)r->view.source_value_id);
    free((char *)r->view.target_value_id);
    free_evidence(r->evidence, r->view.evidence_count);
  }
  free(k->relationships);
  free(k);
}
RSSDDCMonitorKnowledge *rss_ddc_monitor_knowledge_create(void) {
  RSSDDCMonitorKnowledge *k = calloc(1, sizeof(*k));
  if (k)
    k->schema = dup_n(RSS_DDC_MONITOR_KNOWLEDGE_SCHEMA,
                      strlen(RSS_DDC_MONITOR_KNOWLEDGE_SCHEMA));
  return k;
}
void rss_ddc_monitor_knowledge_destroy(RSSDDCMonitorKnowledge *k) {
  free_knowledge(k);
}
static char *copy_text(const char *s) { return s ? dup_n(s, strlen(s)) : NULL; }
static bool copy_raw(RSSDDCRawValue *to, const RSSDDCRawValue *from) {
  *to = (RSSDDCRawValue){};
  to->type = from->type;
  to->unsigned_value = from->unsigned_value;
  to->signed_value = from->signed_value;
  to->data_length = from->data_length;
  if (from->type == RSS_DDC_RAW_BYTES || from->type == RSS_DDC_RAW_STRING) {
    to->data =
        (const uint8_t *)dup_n((const char *)from->data, from->data_length);
    if (!to->data)
      return false;
  }
  return true;
}
static bool copy_evidence(RSSDDCEvidence **to, size_t *n,
                          const RSSDDCEvidence *from, size_t count) {
  *to = NULL;
  *n = 0;
  if (!count)
    return true;
  RSSDDCEvidence *a = calloc(count, sizeof(*a));
  if (!a)
    return false;
  for (size_t i = 0; i < count; ++i) {
    a[i] = from[i];
    a[i].source_id = copy_text(from[i].source_id);
    a[i].reference = copy_text(from[i].reference);
    a[i].timestamp = copy_text(from[i].timestamp);
    a[i].scope = copy_text(from[i].scope);
    if ((from[i].source_id && !a[i].source_id) ||
        (from[i].reference && !a[i].reference) ||
        (from[i].timestamp && !a[i].timestamp) ||
        (from[i].scope && !a[i].scope)) {
      free_evidence(a, count);
      return false;
    }
  }
  *to = a;
  *n = count;
  return true;
}
static bool copy_sources(RSSDDCMonitorKnowledgeSource **to, size_t *count,
                         const RSSDDCMonitorKnowledgeSource *from,
                         size_t from_count) {
  *to = NULL;
  *count = 0;
  if (!from_count)
    return true;
  RSSDDCMonitorKnowledgeSource *sources = calloc(from_count, sizeof(*sources));
  if (!sources)
    return false;
  for (size_t i = 0; i < from_count; ++i) {
    sources[i].id = copy_text(from[i].id);
    sources[i].type = copy_text(from[i].type);
    sources[i].reference = copy_text(from[i].reference);
    if (!sources[i].id || !sources[i].type || !sources[i].reference) {
      free_sources(sources, from_count);
      return false;
    }
  }
  *to = sources;
  *count = from_count;
  return true;
}
static bool copy_method(Method *to, const Method *from) {
  *to = (Method){};
  to->view = from->view;
  to->view.id = copy_text(from->view.id);
  to->view.protocol_id = copy_text(from->view.protocol_id);
  to->view.address = copy_text(from->view.address);
  to->view.parameters = copy_text(from->view.parameters);
  to->view.evidence = NULL;
  if (!to->view.id || !copy_evidence(&to->evidence, &to->view.evidence_count,
                                     from->evidence, from->view.evidence_count))
    return false;
  to->view.evidence = to->evidence;
  return true;
}
static bool copy_value(Value *to, const Value *from) {
  *to = (Value){};
  to->view = from->view;
  to->view.id = copy_text(from->view.id);
  to->view.label = copy_text(from->view.label);
  to->view.raw = (RSSDDCRawValue){};
  to->view.raw_alias_count = 0;
  to->view.raw_aliases = NULL;
  to->view.evidence = NULL;
  if (!to->view.id || !copy_raw(&to->view.raw, &from->view.raw) ||
      !copy_evidence(&to->evidence, &to->view.evidence_count, from->evidence,
                     from->view.evidence_count))
    return false;
  to->view.evidence = to->evidence;
  for (size_t i = 0; i < from->view.raw_alias_count; ++i) {
    RSSDDCRawValue *aliases =
        realloc(to->aliases, (i + 1) * sizeof(*to->aliases));
    if (!aliases)
      return false;
    to->aliases = aliases;
    if (!copy_raw(&to->aliases[i], &from->aliases[i]))
      return false;
    ++to->view.raw_alias_count;
  }
  to->view.raw_aliases = to->aliases;
  return true;
}
static bool copy_condition(Condition *to, const Condition *from) {
  *to = (Condition){};
  to->view = from->view;
  to->view.semantic_id = copy_text(from->view.semantic_id);
  to->view.value_id = copy_text(from->view.value_id);
  to->view.comparison = (RSSDDCRawValue){};
  to->view.evidence = NULL;
  if (!to->view.semantic_id ||
      (from->view.comparison_present &&
       !copy_raw(&to->view.comparison, &from->view.comparison)) ||
      !copy_evidence(&to->evidence, &to->view.evidence_count, from->evidence,
                     from->view.evidence_count))
    return false;
  to->view.evidence = to->evidence;
  return true;
}
static bool copy_capability(Capability *to, const Capability *from) {
  *to = (Capability){};
  to->view = from->view;
  to->view.id = copy_text(from->view.id);
  to->view.label = copy_text(from->view.label);
  to->view.conditions = copy_text(from->view.conditions);
  to->view.advertised_range.units =
      copy_text(from->view.advertised_range.units);
  to->view.observed_range.units = copy_text(from->view.observed_range.units);
  to->view.validated_range.units = copy_text(from->view.validated_range.units);
  to->view.reported_maximum = (RSSDDCRawValue){};
  to->view.condition_groups = NULL;
  to->view.methods = NULL;
  to->view.values = NULL;
  to->view.evidence = NULL;
  to->view.condition_group_count = 0;
  to->view.method_count = 0;
  to->view.value_count = 0;
  if (!to->view.id ||
      (from->view.reported_maximum_present &&
       !copy_raw(&to->view.reported_maximum,
                 &from->view.reported_maximum)) ||
      !copy_evidence(&to->evidence, &to->view.evidence_count,
                                     from->evidence, from->view.evidence_count))
    return false;
  to->view.evidence = to->evidence;
  for (size_t g = 0; g < from->view.condition_group_count; ++g) {
    ConditionGroup *groups =
        realloc(to->condition_groups, (g + 1) * sizeof(*to->condition_groups));
    if (!groups)
      return false;
    to->condition_groups = groups;
    ConditionGroup *group = &groups[g];
    *group = (ConditionGroup){};
    group->view.type = from->condition_groups[g].view.type;
    for (size_t q = 0; q < from->condition_groups[g].view.condition_count;
         ++q) {
      Condition *conditions =
          realloc(group->conditions, (q + 1) * sizeof(*group->conditions));
      if (!conditions)
        return false;
      group->conditions = conditions;
      if (!copy_condition(&conditions[q],
                          &from->condition_groups[g].conditions[q]))
        return false;
      ++group->view.condition_count;
    }
    group->condition_views =
        calloc(group->view.condition_count, sizeof(*group->condition_views));
    if (!group->condition_views)
      return false;
    for (size_t q = 0; q < group->view.condition_count; ++q)
      group->condition_views[q] = group->conditions[q].view;
    group->view.conditions = group->condition_views;
    ++to->view.condition_group_count;
  }
  for (size_t i = 0; i < from->view.method_count; ++i) {
    Method *methods = realloc(to->methods, (i + 1) * sizeof(*to->methods));
    if (!methods)
      return false;
    to->methods = methods;
    if (!copy_method(&methods[i], &from->methods[i]))
      return false;
    ++to->view.method_count;
  }
  for (size_t i = 0; i < from->view.value_count; ++i) {
    Value *values = realloc(to->values, (i + 1) * sizeof(*to->values));
    if (!values)
      return false;
    to->values = values;
    if (!copy_value(&values[i], &from->values[i]))
      return false;
    ++to->view.value_count;
  }
  return refresh_capability_views(to);
}
static bool copy_route(Route *to, const Route *from) {
  *to = (Route){};
  to->view = from->view;
  to->view.id = copy_text(from->view.id);
  to->view.connector = copy_text(from->view.connector);
  to->view.port = copy_text(from->view.port);
  to->view.label = copy_text(from->view.label);
  to->view.read_value = (RSSDDCRawValue){};
  to->view.switch_value = (RSSDDCRawValue){};
  to->view.evidence = NULL;
  if (!to->view.id || !copy_raw(&to->view.read_value, &from->view.read_value) ||
      !copy_raw(&to->view.switch_value, &from->view.switch_value) ||
      !copy_evidence(&to->evidence, &to->view.evidence_count, from->evidence,
                     from->view.evidence_count))
    return false;
  to->view.evidence = to->evidence;
  return true;
}
static bool copy_relationship(Relationship *to, const Relationship *from) {
  *to = (Relationship){};
  to->view = from->view;
  to->view.source_id = copy_text(from->view.source_id);
  to->view.target_id = copy_text(from->view.target_id);
  to->view.source_value_id = copy_text(from->view.source_value_id);
  to->view.target_value_id = copy_text(from->view.target_value_id);
  to->view.evidence = NULL;
  if (!to->view.source_id || !to->view.target_id ||
      !copy_evidence(&to->evidence, &to->view.evidence_count, from->evidence,
                     from->view.evidence_count))
    return false;
  to->view.evidence = to->evidence;
  return true;
}
static RSSDDCMonitorKnowledge *
copy_knowledge(const RSSDDCMonitorKnowledge *from) {
  RSSDDCMonitorKnowledge *to = rss_ddc_monitor_knowledge_create();
  if (!to)
    return NULL;
  free(to->schema);
  to->schema = copy_text(from->schema);
  to->identity = (RSSDDCMonitorIdentity){};
  to->identity.edid_product_code = from->identity.edid_product_code;
  to->identity.edid_product_code_present =
      from->identity.edid_product_code_present;
  to->identity.confidence = from->identity.confidence;
  to->identity.manufacturer = copy_text(from->identity.manufacturer);
  to->identity.model = copy_text(from->identity.model);
  to->identity.edid_manufacturer = copy_text(from->identity.edid_manufacturer);
  to->identity.serial = copy_text(from->identity.serial);
  to->identity.provider = copy_text(from->identity.provider);
  to->identity.transport = copy_text(from->identity.transport);
  to->identity.branch = copy_text(from->identity.branch);
  to->identity.family_hint = copy_text(from->identity.family_hint);
  to->identity.evidence = NULL;
  if (!to->schema ||
      !copy_evidence(&to->identity_evidence, &to->identity.evidence_count,
                     from->identity_evidence, from->identity.evidence_count)) {
    free_knowledge(to);
    return NULL;
  }
  to->identity.evidence = to->identity_evidence;
  if (!copy_sources(&to->sources, &to->source_count, from->sources,
                    from->source_count)) {
    free_knowledge(to);
    return NULL;
  }
  for (size_t i = 0; i < from->capability_count; ++i) {
    Capability *c =
        realloc(to->capabilities, (to->capability_count + 1) * sizeof(*c));
    if (!c) {
      free_knowledge(to);
      return NULL;
    }
    to->capabilities = c;
    memset(&to->capabilities[to->capability_count], 0,
           sizeof(*to->capabilities));
    to->capability_count++;
    if (!copy_capability(&to->capabilities[to->capability_count - 1],
                         &from->capabilities[i])) {
      free_knowledge(to);
      return NULL;
    }
  }
  for (size_t i = 0; i < from->route_count; ++i) {
    Route *routes =
        realloc(to->routes, (to->route_count + 1) * sizeof(*routes));
    if (!routes) {
      free_knowledge(to);
      return NULL;
    }
    to->routes = routes;
    memset(&routes[to->route_count], 0, sizeof(*routes));
    ++to->route_count;
    if (!copy_route(&routes[to->route_count - 1], &from->routes[i])) {
      free_knowledge(to);
      return NULL;
    }
  }
  for (size_t i = 0; i < from->relationship_count; ++i) {
    Relationship *relationships =
        realloc(to->relationships,
                (to->relationship_count + 1) * sizeof(*relationships));
    if (!relationships) {
      free_knowledge(to);
      return NULL;
    }
    to->relationships = relationships;
    memset(&relationships[to->relationship_count], 0, sizeof(*relationships));
    ++to->relationship_count;
    if (!copy_relationship(&relationships[to->relationship_count - 1],
                           &from->relationships[i])) {
      free_knowledge(to);
      return NULL;
    }
  }
  return to;
}

static RSSDDCError parse_evidence(J *j, RSSDDCEvidence **out, size_t *count) {
  if (!take(j, '['))
    return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
  RSSDDCEvidence *a = NULL;
  size_t n = 0;
  ws(j);
  if (j->p < j->end && *j->p == ']') {
    ++j->p;
    *out = a;
    *count = 0;
    return RSS_DDC_OK;
  }
  for (;;) {
    if (n == MK_MAX_EVIDENCE || !take(j, '{'))
      goto bad;
    RSSDDCEvidence e = {.contribution = RSS_DDC_CONFIDENCE_UNKNOWN};
    bool type = false;
    for (;;) {
      char *k = NULL;
      if (!str(j, &k) || !take(j, ':')) {
        free(k);
        goto bad;
      }
      if (strcmp(k, "type") == 0) {
        char *s = NULL;
        int x;
        if (type || !str(j, &s) ||
            !enum_value(evidence_type_names,
                        sizeof(evidence_type_names) /
                            sizeof(*evidence_type_names),
                        s, &x)) {
          free(k);
          free(s);
          goto bad;
        }
        e.type = (RSSDDCEvidenceType)x;
        type = true;
        free(s);
      } else if (strcmp(k, "sourceId") == 0) {
        if (e.source_id || !str(j, (char **)&e.source_id)) {
          free(k);
          goto bad;
        }
      } else if (strcmp(k, "reference") == 0) {
        if (e.reference || !str(j, (char **)&e.reference)) {
          free(k);
          goto bad;
        }
      } else if (strcmp(k, "timestamp") == 0) {
        if (e.timestamp || !str(j, (char **)&e.timestamp)) {
          free(k);
          goto bad;
        }
      } else if (strcmp(k, "scope") == 0) {
        if (e.scope || !str(j, (char **)&e.scope)) {
          free(k);
          goto bad;
        }
      } else if (strcmp(k, "contribution") == 0) {
        char *s = NULL;
        int x;
        if (!str(j, &s) ||
            !enum_value(confidence_names,
                        sizeof(confidence_names) / sizeof(*confidence_names), s,
                        &x)) {
          free(k);
          free(s);
          goto bad;
        }
        e.contribution = (RSSDDCConfidence)x;
        free(s);
      } else if (!skip(j)) {
        free(k);
        goto bad;
      }
      free(k);
      ws(j);
      if (j->p < j->end && *j->p == '}') {
        ++j->p;
        break;
      }
      if (!take(j, ','))
        goto bad;
    }
    if (!type) {
      discard_evidence(&e);
      goto bad;
    }
    RSSDDCEvidence *b = realloc(a, (n + 1) * sizeof(*a));
    if (!b) {
      discard_evidence(&e);
      goto bad;
    }
    a = b;
    a[n++] = e;
    ws(j);
    if (j->p < j->end && *j->p == ']') {
      ++j->p;
      break;
    }
    if (!take(j, ','))
      goto bad;
  }
  *out = a;
  *count = n;
  return RSS_DDC_OK;
bad:
  free_evidence(a, n);
  return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
}
static RSSDDCError parse_sources(J *j, RSSDDCMonitorKnowledge *knowledge) {
  if (knowledge->sources || !take(j, '['))
    return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
  ws(j);
  if (j->p < j->end && *j->p == ']') {
    ++j->p;
    return RSS_DDC_OK;
  }
  for (;;) {
    if (knowledge->source_count == MK_MAX_SOURCES || !take(j, '{'))
      goto bad;
    RSSDDCMonitorKnowledgeSource source = {};
    bool id = false, type = false, reference = false;
    for (;;) {
      char *key = NULL;
      if (!str(j, &key) || !take(j, ':')) {
        free(key);
        goto bad_source;
      }
      if (strcmp(key, "id") == 0) {
        if (id || !str(j, (char **)&source.id) ||
            !source_identifier(source.id)) {
          free(key);
          goto bad_source;
        }
        id = true;
      } else if (strcmp(key, "type") == 0) {
        if (type || !str(j, (char **)&source.type) ||
            !source_identifier(source.type)) {
          free(key);
          goto bad_source;
        }
        type = true;
      } else if (strcmp(key, "reference") == 0) {
        if (reference || !str(j, (char **)&source.reference)) {
          free(key);
          goto bad_source;
        }
        reference = true;
      } else if (!skip(j)) {
        free(key);
        goto bad_source;
      }
      free(key);
      ws(j);
      if (j->p < j->end && *j->p == '}') {
        ++j->p;
        break;
      }
      if (!take(j, ','))
        goto bad_source;
    }
    if (!id || !type || !reference)
      goto bad_source;
    for (size_t i = 0; i < knowledge->source_count; ++i)
      if (strcmp(knowledge->sources[i].id, source.id) == 0)
        goto bad_source;
    RSSDDCMonitorKnowledgeSource *sources =
        realloc(knowledge->sources,
                (knowledge->source_count + 1) * sizeof(*sources));
    if (!sources)
      goto bad_source;
    knowledge->sources = sources;
    knowledge->sources[knowledge->source_count++] = source;
    ws(j);
    if (j->p < j->end && *j->p == ']') {
      ++j->p;
      return RSS_DDC_OK;
    }
    if (!take(j, ','))
      goto bad;
    continue;
bad_source:
    free((char *)source.id);
    free((char *)source.type);
    free((char *)source.reference);
bad:
    return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
  }
}
static RSSDDCError parse_raw(J *j, RSSDDCRawValue *out) {
  if (!take(j, '{'))
    return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
  char *type = NULL;
  bool value = false;
  RSSDDCRawValue r = {};
  for (;;) {
    char *k = NULL;
    if (!str(j, &k) || !take(j, ':')) {
      free(k);
      goto bad;
    }
    if (strcmp(k, "type") == 0) {
      if (type || !str(j, &type)) {
        free(k);
        goto bad;
      }
    } else if (strcmp(k, "value") == 0) {
      if (value) {
        free(k);
        goto bad;
      }
      value = true;
      if (type && strcmp(type, "signed") == 0) {
        if (!number(j, &r.signed_value)) {
          free(k);
          goto bad;
        }
      } else if (type && strcmp(type, "unsigned") == 0) {
        int64_t x;
        if (!number(j, &x) || x < 0) {
          free(k);
          goto bad;
        }
        r.unsigned_value = (uint64_t)x;
      } else {
        char *s = NULL;
        if (!str(j, &s)) {
          free(k);
          goto bad;
        }
        r.data = (const uint8_t *)s;
        r.data_length = strlen(s);
      }
    } else if (!skip(j)) {
      free(k);
      goto bad;
    }
    free(k);
    ws(j);
    if (j->p < j->end && *j->p == '}') {
      ++j->p;
      break;
    }
    if (!take(j, ','))
      goto bad;
  }
  int x;
  if (!type || !value || !enum_value(raw_names, 4, type, &x))
    goto bad;
  r.type = (RSSDDCRawType)x;
  if ((r.type == RSS_DDC_RAW_UNSIGNED || r.type == RSS_DDC_RAW_SIGNED) &&
      r.data) {
    free((void *)r.data);
    goto bad;
  }
  if ((r.type == RSS_DDC_RAW_BYTES || r.type == RSS_DDC_RAW_STRING) && !r.data)
    goto bad;
  free(type);
  *out = r;
  return RSS_DDC_OK;
bad:
  free(type);
  free_raw(&r);
  return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
}
static RSSDDCError parse_raw_aliases(J *j, RSSDDCRawValue **out,
                                     size_t *count) {
  if (!take(j, '['))
    return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
  RSSDDCRawValue *a = NULL;
  size_t n = 0;
  ws(j);
  if (j->p < j->end && *j->p == ']') {
    ++j->p;
    *out = NULL;
    *count = 0;
    return RSS_DDC_OK;
  }
  for (;;) {
    if (n == MK_MAX_VALUES)
      goto bad;
    RSSDDCRawValue r = {};
    if (parse_raw(j, &r) != RSS_DDC_OK)
      goto bad;
    for (size_t i = 0; i < n; ++i) {
      if (a[i].type == r.type && a[i].unsigned_value == r.unsigned_value &&
          a[i].signed_value == r.signed_value &&
          a[i].data_length == r.data_length &&
          (!r.data_length || memcmp(a[i].data, r.data, r.data_length) == 0)) {
        free_raw(&r);
        goto bad;
      }
    }
    RSSDDCRawValue *b = realloc(a, (n + 1) * sizeof(*a));
    if (!b) {
      free_raw(&r);
      goto bad;
    }
    a = b;
    a[n++] = r;
    ws(j);
    if (j->p < j->end && *j->p == ']') {
      ++j->p;
      break;
    }
    if (!take(j, ','))
      goto bad;
  }
  *out = a;
  *count = n;
  return RSS_DDC_OK;
bad:
  for (size_t i = 0; i < n; ++i)
    free_raw(&a[i]);
  free(a);
  return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
}
static RSSDDCError parse_condition(J *j, Condition *condition) {
  bool semantic_id = false, op = false;
  if (!take(j, '{'))
    return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
  for (;;) {
    char *key = NULL;
    if (!str(j, &key) || !take(j, ':')) {
      free(key);
      goto bad;
    }
    if (strcmp(key, "semanticId") == 0) {
      if (semantic_id || !str(j, (char **)&condition->view.semantic_id) ||
          !semantic(condition->view.semantic_id)) {
        free(key);
        goto bad;
      }
      semantic_id = true;
    } else if (strcmp(key, "valueId") == 0) {
      if (condition->view.value_id ||
          !str(j, (char **)&condition->view.value_id)) {
        free(key);
        goto bad;
      }
    } else if (strcmp(key, "op") == 0) {
      char *name = NULL;
      int value = 0;
      if (op || !str(j, &name) ||
          !enum_value(condition_operator_names, 10, name, &value)) {
        free(key);
        free(name);
        goto bad;
      }
      condition->view.op = (RSSDDCConditionOperator)value;
      op = true;
      free(name);
    } else if (strcmp(key, "comparison") == 0) {
      if (condition->view.comparison_present ||
          parse_raw(j, &condition->view.comparison) != RSS_DDC_OK) {
        free(key);
        goto bad;
      }
      condition->view.comparison_present = true;
    } else if (strcmp(key, "confidence") == 0) {
      char *name = NULL;
      int value = 0;
      if (!str(j, &name) || !enum_value(confidence_names, 6, name, &value)) {
        free(key);
        free(name);
        goto bad;
      }
      condition->view.confidence = (RSSDDCConfidence)value;
      free(name);
    } else if (strcmp(key, "validation") == 0) {
      char *name = NULL;
      int value = 0;
      if (!str(j, &name) || !enum_value(validation_names, 5, name, &value)) {
        free(key);
        free(name);
        goto bad;
      }
      condition->view.validation = (RSSDDCValidation)value;
      free(name);
    } else if (strcmp(key, "evidence") == 0) {
      if (condition->evidence ||
          parse_evidence(j, &condition->evidence,
                         &condition->view.evidence_count) != RSS_DDC_OK) {
        free(key);
        goto bad;
      }
      condition->view.evidence = condition->evidence;
    } else if (!skip(j)) {
      free(key);
      goto bad;
    }
    free(key);
    ws(j);
    if (j->p < j->end && *j->p == '}') {
      ++j->p;
      break;
    }
    if (!take(j, ','))
      goto bad;
  }
  if (!semantic_id || !op ||
      ((condition->view.op == RSS_DDC_CONDITION_EQUALS ||
        condition->view.op == RSS_DDC_CONDITION_NOT_EQUALS ||
        condition->view.op >= RSS_DDC_CONDITION_LESS_THAN) !=
       condition->view.comparison_present))
    goto bad;
  return RSS_DDC_OK;
bad:
  free_condition(condition);
  return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
}
static RSSDDCError parse_condition_group(J *j, ConditionGroup *group) {
  bool type = false, conditions = false;
  if (!take(j, '{'))
    return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
  for (;;) {
    char *key = NULL;
    if (!str(j, &key) || !take(j, ':')) {
      free(key);
      goto bad;
    }
    if (strcmp(key, "type") == 0) {
      char *name = NULL;
      int value = 0;
      if (type || !str(j, &name) ||
          !enum_value(condition_group_names, 2, name, &value)) {
        free(key);
        free(name);
        goto bad;
      }
      group->view.type = (RSSDDCConditionGroupType)value;
      type = true;
      free(name);
    } else if (strcmp(key, "conditions") == 0) {
      if (conditions || !take(j, '[')) {
        free(key);
        goto bad;
      }
      conditions = true;
      ws(j);
      while (j->p < j->end && *j->p != ']') {
        if (group->view.condition_count == MK_MAX_VALUES)
          goto bad;
        Condition *items =
            realloc(group->conditions,
                    (group->view.condition_count + 1) * sizeof(*items));
        if (!items)
          goto bad;
        group->conditions = items;
        memset(&items[group->view.condition_count], 0, sizeof(*items));
        if (parse_condition(j, &items[group->view.condition_count]) !=
            RSS_DDC_OK)
          goto bad;
        ++group->view.condition_count;
        ws(j);
        if (j->p < j->end && *j->p == ']')
          break;
        if (!take(j, ','))
          goto bad;
      }
      if (!take(j, ']'))
        goto bad;
    } else if (!skip(j)) {
      free(key);
      goto bad;
    }
    free(key);
    ws(j);
    if (j->p < j->end && *j->p == '}') {
      ++j->p;
      break;
    }
    if (!take(j, ','))
      goto bad;
  }
  if (!type || !conditions || !group->view.condition_count)
    goto bad;
  group->condition_views =
      calloc(group->view.condition_count, sizeof(*group->condition_views));
  if (!group->condition_views)
    goto bad;
  for (size_t i = 0; i < group->view.condition_count; ++i)
    group->condition_views[i] = group->conditions[i].view;
  group->view.conditions = group->condition_views;
  return RSS_DDC_OK;
bad:
  free_condition_group(group);
  return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
}
static RSSDDCError parse_range(J *j, RSSDDCRange *r) {
  if (!take(j, '{'))
    return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
  bool min = false, max = false;
  for (;;) {
    char *k = NULL;
    if (!str(j, &k) || !take(j, ':')) {
      free(k);
      return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
    }
    if (strcmp(k, "min") == 0) {
      if (min || !number(j, &r->minimum)) {
        free(k);
        return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
      }
      min = true;
    } else if (strcmp(k, "max") == 0) {
      if (max || !number(j, &r->maximum)) {
        free(k);
        return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
      }
      max = true;
    } else if (strcmp(k, "step") == 0) {
      if (!number(j, &r->step)) {
        free(k);
        return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
      }
    } else if (strcmp(k, "units") == 0) {
      if (r->units || !str(j, (char **)&r->units)) {
        free(k);
        return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
      }
    } else if (!skip(j)) {
      free(k);
      return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
    }
    free(k);
    ws(j);
    if (j->p < j->end && *j->p == '}') {
      ++j->p;
      break;
    }
    if (!take(j, ','))
      return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
  }
  r->present = min && max;
  return r->present && r->minimum <= r->maximum
             ? RSS_DDC_OK
             : RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
}
static RSSDDCError parse_method(J *j, Method *m) {
  if (!take(j, '{'))
    return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
  bool id = false, type = false, read = false, write = false, risk = false;
  for (;;) {
    char *k = NULL;
    if (!str(j, &k) || !take(j, ':')) {
      free(k);
      goto bad;
    }
    if (strcmp(k, "id") == 0) {
      if (id || !str(j, (char **)&m->view.id)) {
        free(k);
        goto bad;
      }
      id = true;
    } else if (strcmp(k, "type") == 0) {
      char *s = NULL;
      int x;
      if (type || !str(j, &s) || !enum_value(method_names, 4, s, &x)) {
        free(k);
        free(s);
        goto bad;
      }
      m->view.type = (RSSDDCMethodType)x;
      type = true;
      free(s);
    } else if (strcmp(k, "vcpCode") == 0) {
      int64_t x;
      if (!number(j, &x) || x < 0 || x > 255) {
        free(k);
        goto bad;
      }
      m->view.vcp_code = (uint32_t)x;
    } else if (strcmp(k, "protocolId") == 0) {
      if (m->view.protocol_id || !str(j, (char **)&m->view.protocol_id)) {
        free(k);
        goto bad;
      }
    } else if (strcmp(k, "address") == 0) {
      if (m->view.address || !str(j, (char **)&m->view.address)) {
        free(k);
        goto bad;
      }
    } else if (strcmp(k, "parameters") == 0) {
      if (m->view.parameters || !str(j, (char **)&m->view.parameters)) {
        free(k);
        goto bad;
      }
    } else if (strcmp(k, "readable") == 0) {
      if (read || !boolean(j, &m->view.readable)) {
        free(k);
        goto bad;
      }
      read = true;
    } else if (strcmp(k, "writable") == 0) {
      if (write || !boolean(j, &m->view.writable)) {
        free(k);
        goto bad;
      }
      write = true;
    } else if (strcmp(k, "risk") == 0) {
      char *s = NULL;
      int x;
      if (risk || !str(j, &s) || !enum_value(risk_names, 6, s, &x)) {
        free(k);
        free(s);
        goto bad;
      }
      m->view.risk = (RSSDDCRisk)x;
      risk = true;
      free(s);
    } else if (strcmp(k, "confidence") == 0) {
      char *s = NULL;
      int x;
      if (!str(j, &s) || !enum_value(confidence_names, 6, s, &x)) {
        free(k);
        free(s);
        goto bad;
      }
      m->view.confidence = (RSSDDCConfidence)x;
      free(s);
    } else if (strcmp(k, "evidence") == 0) {
      if (m->evidence ||
          parse_evidence(j, &m->evidence, &m->view.evidence_count) !=
              RSS_DDC_OK) {
        free(k);
        goto bad;
      }
      m->view.evidence = m->evidence;
    } else if (!skip(j)) {
      free(k);
      goto bad;
    }
    free(k);
    ws(j);
    if (j->p < j->end && *j->p == '}') {
      ++j->p;
      break;
    }
    if (!take(j, ','))
      goto bad;
  }
  if (!id || !type || !read || !write || !risk || !m->view.id ||
      !m->view.id[0] ||
      (m->view.type == RSS_DDC_METHOD_MCCS_VCP && !m->view.vcp_code) ||
      (m->view.writable && m->view.risk == RSS_DDC_RISK_HIGH_RISK_DENIED))
    goto bad;
  return RSS_DDC_OK;
bad:
  free((char *)m->view.id);
  free((char *)m->view.protocol_id);
  free((char *)m->view.address);
  free((char *)m->view.parameters);
  free_evidence(m->evidence, m->view.evidence_count);
  *m = (Method){};
  return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
}
static RSSDDCError parse_value(J *j, Value *v) {
  if (!take(j, '{'))
    return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
  bool id = false, raw = false;
  for (;;) {
    char *k = NULL;
    if (!str(j, &k) || !take(j, ':')) {
      free(k);
      goto bad;
    }
    if (strcmp(k, "id") == 0) {
      if (id || !str(j, (char **)&v->view.id)) {
        free(k);
        goto bad;
      }
      id = true;
    } else if (strcmp(k, "label") == 0) {
      if (v->view.label || !str(j, (char **)&v->view.label)) {
        free(k);
        goto bad;
      }
    } else if (strcmp(k, "raw") == 0) {
      if (raw || parse_raw(j, &v->view.raw) != RSS_DDC_OK) {
        free(k);
        goto bad;
      }
      raw = true;
    } else if (strcmp(k, "rawAliases") == 0) {
      if (v->aliases ||
          parse_raw_aliases(j, &v->aliases, &v->view.raw_alias_count) !=
              RSS_DDC_OK) {
        free(k);
        goto bad;
      }
      v->view.raw_aliases = v->aliases;
    } else if (strcmp(k, "readable") == 0) {
      if (!boolean(j, &v->view.readable)) {
        free(k);
        goto bad;
      }
    } else if (strcmp(k, "writable") == 0) {
      if (!boolean(j, &v->view.writable)) {
        free(k);
        goto bad;
      }
    } else if (strcmp(k, "confidence") == 0) {
      char *s = NULL;
      int x;
      if (!str(j, &s) || !enum_value(confidence_names, 6, s, &x)) {
        free(k);
        free(s);
        goto bad;
      }
      v->view.confidence = (RSSDDCConfidence)x;
      free(s);
    } else if (strcmp(k, "validation") == 0) {
      char *s = NULL;
      int x;
      if (!str(j, &s) || !enum_value(validation_names, 5, s, &x)) {
        free(k);
        free(s);
        goto bad;
      }
      v->view.validation = (RSSDDCValidation)x;
      free(s);
    } else if (strcmp(k, "availability") == 0) {
      char *s = NULL;
      int x;
      if (!str(j, &s) || !enum_value(availability_names, 4, s, &x)) {
        free(k);
        free(s);
        goto bad;
      }
      v->view.availability = (RSSDDCAvailability)x;
      free(s);
    } else if (strcmp(k, "evidence") == 0) {
      if (v->evidence ||
          parse_evidence(j, &v->evidence, &v->view.evidence_count) !=
              RSS_DDC_OK) {
        free(k);
        goto bad;
      }
      v->view.evidence = v->evidence;
    } else if (!skip(j)) {
      free(k);
      goto bad;
    }
    free(k);
    ws(j);
    if (j->p < j->end && *j->p == '}') {
      ++j->p;
      break;
    }
    if (!take(j, ','))
      goto bad;
  }
  if (!id || !raw || !v->view.id || !v->view.id[0])
    goto bad;
  return RSS_DDC_OK;
bad:
  free((char *)v->view.id);
  free((char *)v->view.label);
  free_raw(&v->view.raw);
  for (size_t i = 0; i < v->view.raw_alias_count; ++i)
    free_raw(&v->aliases[i]);
  free(v->aliases);
  free_evidence(v->evidence, v->view.evidence_count);
  *v = (Value){};
  return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
}
static bool refresh_capability_views(Capability *c) {
  free(c->method_views);
  free(c->value_views);
  free(c->condition_group_views);
  c->method_views = NULL;
  c->value_views = NULL;
  c->condition_group_views = NULL;
  if (c->view.condition_group_count) {
    c->condition_group_views = calloc(c->view.condition_group_count,
                                      sizeof(*c->condition_group_views));
    if (!c->condition_group_views)
      return false;
    for (size_t i = 0; i < c->view.condition_group_count; ++i)
      c->condition_group_views[i] = c->condition_groups[i].view;
  }
  if (c->view.method_count) {
    c->method_views = calloc(c->view.method_count, sizeof(*c->method_views));
    if (!c->method_views)
      return false;
    for (size_t i = 0; i < c->view.method_count; ++i)
      c->method_views[i] = c->methods[i].view;
  }
  if (c->view.value_count) {
    c->value_views = calloc(c->view.value_count, sizeof(*c->value_views));
    if (!c->value_views) {
      free(c->method_views);
      c->method_views = NULL;
      free(c->condition_group_views);
      c->condition_group_views = NULL;
      return false;
    }
    for (size_t i = 0; i < c->view.value_count; ++i)
      c->value_views[i] = c->values[i].view;
  }
  c->view.methods = c->method_views;
  c->view.values = c->value_views;
  c->view.condition_groups = c->condition_group_views;
  return true;
}
static RSSDDCError parse_capability(J *j, Capability *c) {
  if (!take(j, '{'))
    return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
  bool id = false;
  for (;;) {
    char *k = NULL;
    if (!str(j, &k) || !take(j, ':')) {
      free(k);
      goto bad;
    }
    if (strcmp(k, "id") == 0) {
      if (id || !str(j, (char **)&c->view.id) || !semantic(c->view.id)) {
        free(k);
        goto bad;
      }
      id = true;
    } else if (strcmp(k, "label") == 0) {
      if (c->view.label || !str(j, (char **)&c->view.label)) {
        free(k);
        goto bad;
      }
    } else if (strcmp(k, "availability") == 0) {
      char *s = NULL;
      int x;
      if (!str(j, &s) || !enum_value(availability_names, 4, s, &x)) {
        free(k);
        free(s);
        goto bad;
      }
      c->view.availability = (RSSDDCAvailability)x;
      free(s);
    } else if (strcmp(k, "conditions") == 0) {
      if (c->view.conditions || !str(j, (char **)&c->view.conditions)) {
        free(k);
        goto bad;
      }
    } else if (strcmp(k, "conditionGroups") == 0) {
      if (c->condition_groups || !take(j, '[')) {
        free(k);
        goto bad;
      }
      ws(j);
      while (j->p < j->end && *j->p != ']') {
        if (c->view.condition_group_count == MK_MAX_VALUES)
          goto bad;
        ConditionGroup *groups =
            realloc(c->condition_groups,
                    (c->view.condition_group_count + 1) * sizeof(*groups));
        if (!groups)
          goto bad;
        c->condition_groups = groups;
        memset(&groups[c->view.condition_group_count], 0, sizeof(*groups));
        if (parse_condition_group(j, &groups[c->view.condition_group_count]) !=
            RSS_DDC_OK)
          goto bad;
        ++c->view.condition_group_count;
        ws(j);
        if (j->p < j->end && *j->p == ']')
          break;
        if (!take(j, ','))
          goto bad;
      }
      if (!take(j, ']'))
        goto bad;
    } else if (strcmp(k, "confidence") == 0) {
      char *s = NULL;
      int x;
      if (!str(j, &s) || !enum_value(confidence_names, 6, s, &x)) {
        free(k);
        free(s);
        goto bad;
      }
      c->view.confidence = (RSSDDCConfidence)x;
      free(s);
    } else if (strcmp(k, "validation") == 0) {
      char *s = NULL;
      int x;
      if (!str(j, &s) || !enum_value(validation_names, 5, s, &x)) {
        free(k);
        free(s);
        goto bad;
      }
      c->view.validation = (RSSDDCValidation)x;
      free(s);
    } else if (strcmp(k, "advertisedRange") == 0) {
      if (c->view.advertised_range.present ||
          parse_range(j, &c->view.advertised_range) != RSS_DDC_OK) {
        free(k);
        goto bad;
      }
    } else if (strcmp(k, "observedRange") == 0) {
      if (c->view.observed_range.present ||
          parse_range(j, &c->view.observed_range) != RSS_DDC_OK) {
        free(k);
        goto bad;
      }
    } else if (strcmp(k, "validatedRange") == 0) {
      if (c->view.validated_range.present ||
          parse_range(j, &c->view.validated_range) != RSS_DDC_OK) {
        free(k);
        goto bad;
      }
    } else if (strcmp(k, "reportedMaximum") == 0) {
      if (c->view.reported_maximum_present ||
          parse_raw(j, &c->view.reported_maximum) != RSS_DDC_OK) {
        free(k);
        goto bad;
      }
      c->view.reported_maximum_present = true;
    } else if (strcmp(k, "methods") == 0) {
      if (c->methods || !take(j, '[')) {
        free(k);
        goto bad;
      }
      ws(j);
      if (j->p < j->end && *j->p == ']')
        ++j->p;
      else
        for (;;) {
          if (c->view.method_count == MK_MAX_METHODS)
            goto bad;
          Method *m =
              realloc(c->methods, (c->view.method_count + 1) * sizeof(*m));
          if (!m)
            goto bad;
          c->methods = m;
          memset(&m[c->view.method_count], 0, sizeof(*m));
          if (parse_method(j, &m[c->view.method_count++]) != RSS_DDC_OK)
            goto bad;
          ws(j);
          if (j->p < j->end && *j->p == ']') {
            ++j->p;
            break;
          }
          if (!take(j, ','))
            goto bad;
        }
    } else if (strcmp(k, "values") == 0) {
      if (c->values || !take(j, '[')) {
        free(k);
        goto bad;
      }
      ws(j);
      if (j->p < j->end && *j->p == ']')
        ++j->p;
      else
        for (;;) {
          if (c->view.value_count == MK_MAX_VALUES)
            goto bad;
          Value *v = realloc(c->values, (c->view.value_count + 1) * sizeof(*v));
          if (!v)
            goto bad;
          c->values = v;
          memset(&v[c->view.value_count], 0, sizeof(*v));
          if (parse_value(j, &v[c->view.value_count++]) != RSS_DDC_OK)
            goto bad;
          ws(j);
          if (j->p < j->end && *j->p == ']') {
            ++j->p;
            break;
          }
          if (!take(j, ','))
            goto bad;
        }
    } else if (strcmp(k, "evidence") == 0) {
      if (c->evidence ||
          parse_evidence(j, &c->evidence, &c->view.evidence_count) !=
              RSS_DDC_OK) {
        free(k);
        goto bad;
      }
      c->view.evidence = c->evidence;
    } else if (!skip(j)) {
      free(k);
      goto bad;
    }
    free(k);
    ws(j);
    if (j->p < j->end && *j->p == '}') {
      ++j->p;
      break;
    }
    if (!take(j, ','))
      goto bad;
  }
  if (!id)
    goto bad;
  for (size_t i = 0; i < c->view.value_count; ++i)
    for (size_t x = i + 1; x < c->view.value_count; ++x)
      if (strcmp(c->values[i].view.id, c->values[x].view.id) == 0)
        goto bad;
  return refresh_capability_views(c) ? RSS_DDC_OK : RSS_DDC_ERROR_SYSTEM;
bad:
  return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
}
static RSSDDCError parse_identity(J *j, RSSDDCMonitorKnowledge *k) {
  if (!take(j, '{'))
    return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
  for (;;) {
    char *x = NULL;
    if (!str(j, &x) || !take(j, ':')) {
      free(x);
      return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
    }
    char **field = NULL;
    if (strcmp(x, "manufacturer") == 0)
      field = (char **)&k->identity.manufacturer;
    else if (strcmp(x, "model") == 0)
      field = (char **)&k->identity.model;
    else if (strcmp(x, "edidManufacturer") == 0)
      field = (char **)&k->identity.edid_manufacturer;
    else if (strcmp(x, "serial") == 0)
      field = (char **)&k->identity.serial;
    else if (strcmp(x, "provider") == 0)
      field = (char **)&k->identity.provider;
    else if (strcmp(x, "transport") == 0)
      field = (char **)&k->identity.transport;
    else if (strcmp(x, "branch") == 0)
      field = (char **)&k->identity.branch;
    else if (strcmp(x, "familyHint") == 0)
      field = (char **)&k->identity.family_hint;
    if (field) {
      if (*field || !str(j, field)) {
        free(x);
        return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
      }
    } else if (strcmp(x, "edidProductCode") == 0) {
      int64_t n;
      if (k->identity.edid_product_code_present || !number(j, &n) || n < 0 ||
          n > UINT32_MAX) {
        free(x);
        return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
      }
      k->identity.edid_product_code = (uint32_t)n;
      k->identity.edid_product_code_present = true;
    } else if (strcmp(x, "confidence") == 0) {
      char *s = NULL;
      int v;
      if (!str(j, &s) || !enum_value(confidence_names, 6, s, &v)) {
        free(x);
        free(s);
        return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
      }
      k->identity.confidence = (RSSDDCConfidence)v;
      free(s);
    } else if (strcmp(x, "evidence") == 0) {
      if (k->identity_evidence ||
          parse_evidence(j, &k->identity_evidence,
                         &k->identity.evidence_count) != RSS_DDC_OK) {
        free(x);
        return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
      }
      k->identity.evidence = k->identity_evidence;
    } else if (!skip(j)) {
      free(x);
      return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
    }
    free(x);
    ws(j);
    if (j->p < j->end && *j->p == '}') {
      ++j->p;
      break;
    }
    if (!take(j, ','))
      return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
  }
  return RSS_DDC_OK;
}
static RSSDDCError parse_route(J *j, Route *r) {
  if (!take(j, '{'))
    return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
  bool id = false;
  for (;;) {
    char *k = NULL;
    if (!str(j, &k) || !take(j, ':')) {
      free(k);
      return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
    }
    char **f = NULL;
    if (strcmp(k, "id") == 0) {
      f = (char **)&r->view.id;
      id = true;
    } else if (strcmp(k, "connector") == 0)
      f = (char **)&r->view.connector;
    else if (strcmp(k, "port") == 0)
      f = (char **)&r->view.port;
    else if (strcmp(k, "label") == 0)
      f = (char **)&r->view.label;
    if (f) {
      if (*f || !str(j, f)) {
        free(k);
        return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
      }
    } else if (strcmp(k, "switchingSupported") == 0) {
      if (!boolean(j, &r->view.switching_supported)) {
        free(k);
        return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
      }
    } else if (strcmp(k, "currentReadable") == 0) {
      if (!boolean(j, &r->view.current_readable)) {
        free(k);
        return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
      }
    } else if (strcmp(k, "ddcPathMayChange") == 0) {
      if (!boolean(j, &r->view.ddc_path_may_change)) {
        free(k);
        return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
      }
    } else if (strcmp(k, "readValue") == 0) {
      if (r->read_value_present ||
          parse_raw(j, &r->view.read_value) != RSS_DDC_OK) {
        free(k);
        return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
      }
      r->read_value_present = true;
    } else if (strcmp(k, "switchValue") == 0) {
      if (r->switch_value_present ||
          parse_raw(j, &r->view.switch_value) != RSS_DDC_OK) {
        free(k);
        return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
      }
      r->switch_value_present = true;
    } else if (strcmp(k, "confidence") == 0) {
      char *s = NULL;
      int v;
      if (!str(j, &s) || !enum_value(confidence_names, 6, s, &v)) {
        free(k);
        free(s);
        return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
      }
      r->view.confidence = (RSSDDCConfidence)v;
      free(s);
    } else if (strcmp(k, "evidence") == 0) {
      if (r->evidence ||
          parse_evidence(j, &r->evidence, &r->view.evidence_count) !=
              RSS_DDC_OK) {
        free(k);
        return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
      }
      r->view.evidence = r->evidence;
    } else if (!skip(j)) {
      free(k);
      return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
    }
    free(k);
    ws(j);
    if (j->p < j->end && *j->p == '}') {
      ++j->p;
      break;
    }
    if (!take(j, ','))
      return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
  }
  return id && r->view.id && r->view.id[0]
             ? RSS_DDC_OK
             : RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
}
static RSSDDCError parse_relationship(J *j, Relationship *r) {
  if (!take(j, '{'))
    return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
  bool source = false, target = false, type = false;
  for (;;) {
    char *k = NULL;
    if (!str(j, &k) || !take(j, ':')) {
      free(k);
      return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
    }
    char **f = NULL;
    if (strcmp(k, "sourceId") == 0) {
      f = (char **)&r->view.source_id;
      source = true;
    } else if (strcmp(k, "targetId") == 0) {
      f = (char **)&r->view.target_id;
      target = true;
    } else if (strcmp(k, "sourceValueId") == 0)
      f = (char **)&r->view.source_value_id;
    else if (strcmp(k, "targetValueId") == 0)
      f = (char **)&r->view.target_value_id;
    if (f) {
      if (*f || !str(j, f)) {
        free(k);
        return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
      }
    } else if (strcmp(k, "type") == 0) {
      char *s = NULL;
      int v;
      if (type || !str(j, &s) || !enum_value(relationship_names, 5, s, &v)) {
        free(k);
        free(s);
        return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
      }
      r->view.type = (RSSDDCRelationshipType)v;
      type = true;
      free(s);
    } else if (strcmp(k, "confidence") == 0) {
      char *s = NULL;
      int v;
      if (!str(j, &s) || !enum_value(confidence_names, 6, s, &v)) {
        free(k);
        free(s);
        return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
      }
      r->view.confidence = (RSSDDCConfidence)v;
      free(s);
    } else if (strcmp(k, "evidence") == 0) {
      if (r->evidence ||
          parse_evidence(j, &r->evidence, &r->view.evidence_count) !=
              RSS_DDC_OK) {
        free(k);
        return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
      }
      r->view.evidence = r->evidence;
    } else if (!skip(j)) {
      free(k);
      return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
    }
    free(k);
    ws(j);
    if (j->p < j->end && *j->p == '}') {
      ++j->p;
      break;
    }
    if (!take(j, ','))
      return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
  }
  return source && target && type && semantic(r->view.source_id) &&
                 semantic(r->view.target_id)
             ? RSS_DDC_OK
             : RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
}
static RSSDDCError array_caps(J *j, RSSDDCMonitorKnowledge *k) {
  if (!take(j, '['))
    return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
  ws(j);
  if (j->p < j->end && *j->p == ']') {
    ++j->p;
    return RSS_DDC_OK;
  }
  for (;;) {
    if (k->capability_count == MK_MAX_ITEMS)
      return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_TOO_LARGE;
    Capability *c =
        realloc(k->capabilities, (k->capability_count + 1) * sizeof(*c));
    if (!c)
      return RSS_DDC_ERROR_SYSTEM;
    k->capabilities = c;
    memset(&c[k->capability_count], 0, sizeof(*c));
    if (parse_capability(j, &c[k->capability_count++]) != RSS_DDC_OK)
      return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
    ws(j);
    if (j->p < j->end && *j->p == ']') {
      ++j->p;
      return RSS_DDC_OK;
    }
    if (!take(j, ','))
      return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
  }
}
static RSSDDCError array_routes(J *j, RSSDDCMonitorKnowledge *k) {
  if (!take(j, '['))
    return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
  ws(j);
  if (j->p < j->end && *j->p == ']') {
    ++j->p;
    return RSS_DDC_OK;
  }
  for (;;) {
    if (k->route_count == MK_MAX_ITEMS)
      return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_TOO_LARGE;
    Route *r = realloc(k->routes, (k->route_count + 1) * sizeof(*r));
    if (!r)
      return RSS_DDC_ERROR_SYSTEM;
    k->routes = r;
    memset(&r[k->route_count], 0, sizeof(*r));
    if (parse_route(j, &r[k->route_count++]) != RSS_DDC_OK)
      return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
    ws(j);
    if (j->p < j->end && *j->p == ']') {
      ++j->p;
      return RSS_DDC_OK;
    }
    if (!take(j, ','))
      return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
  }
}
static RSSDDCError array_relationships(J *j, RSSDDCMonitorKnowledge *k) {
  if (!take(j, '['))
    return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
  ws(j);
  if (j->p < j->end && *j->p == ']') {
    ++j->p;
    return RSS_DDC_OK;
  }
  for (;;) {
    if (k->relationship_count == MK_MAX_ITEMS)
      return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_TOO_LARGE;
    Relationship *r =
        realloc(k->relationships, (k->relationship_count + 1) * sizeof(*r));
    if (!r)
      return RSS_DDC_ERROR_SYSTEM;
    k->relationships = r;
    memset(&r[k->relationship_count], 0, sizeof(*r));
    if (parse_relationship(j, &r[k->relationship_count++]) != RSS_DDC_OK)
      return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
    ws(j);
    if (j->p < j->end && *j->p == ']') {
      ++j->p;
      return RSS_DDC_OK;
    }
    if (!take(j, ','))
      return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
  }
}
RSSDDCError rss_ddc_monitor_knowledge_parse_json(const char *data,
                                                 size_t length,
                                                 RSSDDCMonitorKnowledge **out) {
  if (!data || !out || length == 0)
    return RSS_DDC_ERROR_ARGUMENT;
  if (length > MK_MAX_DOCUMENT)
    return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_TOO_LARGE;
  *out = NULL;
  RSSDDCMonitorKnowledge *k = rss_ddc_monitor_knowledge_create();
  if (!k)
    return RSS_DDC_ERROR_SYSTEM;
  J j = {data, data + length};
  bool schema = false, identity = false, sources = false, caps = false;
  if (!take(&j, '{'))
    goto bad;
  for (;;) {
    char *x = NULL;
    if (!str(&j, &x) || !take(&j, ':')) {
      free(x);
      goto bad;
    }
    RSSDDCError e = RSS_DDC_OK;
    if (strcmp(x, "schemaVersion") == 0) {
      char *s = NULL;
      if (schema || !str(&j, &s) ||
          strcmp(s, RSS_DDC_MONITOR_KNOWLEDGE_SCHEMA) != 0) {
        free(x);
        free(s);
        free_knowledge(k);
        return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_SCHEMA;
      }
      free(k->schema);
      k->schema = s;
      schema = true;
    } else if (strcmp(x, "identity") == 0) {
      if (identity) {
        free(x);
        goto bad;
      }
      e = parse_identity(&j, k);
      if (e != RSS_DDC_OK) {
        free(x);
        goto bad;
      }
      identity = true;
    } else if (strcmp(x, "sources") == 0) {
      if (sources || parse_sources(&j, k) != RSS_DDC_OK) {
        free(x);
        goto bad;
      }
      sources = true;
    } else if (strcmp(x, "capabilities") == 0) {
      if (caps || (e = array_caps(&j, k)) != RSS_DDC_OK) {
        free(x);
        if (e == RSS_DDC_ERROR_MONITOR_KNOWLEDGE_TOO_LARGE) {
          free_knowledge(k);
          return e;
        }
        goto bad;
      }
      caps = true;
    } else if (strcmp(x, "inputRoutes") == 0) {
      if (k->routes) {
        free(x);
        goto bad;
      }
      e = array_routes(&j, k);
      if (e != RSS_DDC_OK) {
        free(x);
        goto bad;
      }
    } else if (strcmp(x, "relationships") == 0) {
      if (k->relationships) {
        free(x);
        goto bad;
      }
      e = array_relationships(&j, k);
      if (e != RSS_DDC_OK) {
        free(x);
        goto bad;
      }
    } else if (!skip(&j)) {
      free(x);
      goto bad;
    }
    free(x);
    ws(&j);
    if (j.p < j.end && *j.p == '}') {
      ++j.p;
      break;
    }
    if (!take(&j, ','))
      goto bad;
  }
  ws(&j);
  if (j.p != j.end || !schema || !identity || !caps)
    goto bad;
  RSSDDCError e = rss_ddc_monitor_knowledge_validate(k);
  if (e != RSS_DDC_OK) {
    free_knowledge(k);
    return e;
  }
  *out = k;
  return RSS_DDC_OK;
bad:
  free_knowledge(k);
  return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
}
RSSDDCError
rss_ddc_monitor_knowledge_validate(const RSSDDCMonitorKnowledge *k) {
  if (!k || !k->schema ||
      strcmp(k->schema, RSS_DDC_MONITOR_KNOWLEDGE_SCHEMA) != 0)
    return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_SCHEMA;
  for (size_t i = 0; i < k->capability_count; ++i) {
    const Capability *c = &k->capabilities[i];
    if (!semantic(c->view.id))
      return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
    bool strong = false;
    for (size_t e = 0; e < c->view.evidence_count; ++e)
      if (c->evidence[e].type == RSS_DDC_EVIDENCE_SET_CONFIRMED ||
          c->evidence[e].type == RSS_DDC_EVIDENCE_LOCAL_VALIDATED ||
          c->evidence[e].type == RSS_DDC_EVIDENCE_ROGUE_VALIDATED_PROFILE)
        strong = true;
    if (c->view.confidence == RSS_DDC_CONFIDENCE_HARDWARE_VALIDATED && !strong)
      return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_UNSAFE;
    for (size_t m = 0; m < c->view.method_count; ++m) {
      const RSSDDCMonitorKnowledgeMethod *x = &c->methods[m].view;
      if (x->writable && x->risk == RSS_DDC_RISK_HIGH_RISK_DENIED)
        return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_UNSAFE;
    }
  }
  for (size_t i = 0; i < k->relationship_count; ++i)
    if (!semantic(k->relationships[i].view.source_id) ||
        !semantic(k->relationships[i].view.target_id))
      return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
  for (size_t i = 0; i < k->source_count; ++i) {
    const RSSDDCMonitorKnowledgeSource *source = &k->sources[i];
    if (!source_identifier(source->id) || !source_identifier(source->type) ||
        !source->reference)
      return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
    for (size_t x = i + 1; x < k->source_count; ++x)
      if (strcmp(source->id, k->sources[x].id) == 0)
        return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_CONFLICT;
  }
  return RSS_DDC_OK;
}
const char *
rss_ddc_monitor_knowledge_schema_version(const RSSDDCMonitorKnowledge *k) {
  return k ? k->schema : NULL;
}
RSSDDCError rss_ddc_monitor_knowledge_identity(const RSSDDCMonitorKnowledge *k,
                                               RSSDDCMonitorIdentity *out) {
  if (!k || !out)
    return RSS_DDC_ERROR_ARGUMENT;
  *out = k->identity;
  return RSS_DDC_OK;
}
size_t rss_ddc_monitor_knowledge_source_count(
    const RSSDDCMonitorKnowledge *k) {
  return k ? k->source_count : 0;
}
RSSDDCError rss_ddc_monitor_knowledge_source(
    const RSSDDCMonitorKnowledge *k, size_t index,
    RSSDDCMonitorKnowledgeSource *out) {
  if (!k || !out)
    return RSS_DDC_ERROR_ARGUMENT;
  if (index >= k->source_count)
    return RSS_DDC_ERROR_NOT_FOUND;
  *out = k->sources[index];
  return RSS_DDC_OK;
}
size_t
rss_ddc_monitor_knowledge_capability_count(const RSSDDCMonitorKnowledge *k) {
  return k ? k->capability_count : 0;
}
RSSDDCError
rss_ddc_monitor_knowledge_capability(const RSSDDCMonitorKnowledge *k, size_t i,
                                     RSSDDCMonitorKnowledgeCapability *out) {
  if (!k || !out)
    return RSS_DDC_ERROR_ARGUMENT;
  if (i >= k->capability_count)
    return RSS_DDC_ERROR_NOT_FOUND;
  *out = k->capabilities[i].view;
  return RSS_DDC_OK;
}
RSSDDCError rss_ddc_monitor_knowledge_find_capability(
    const RSSDDCMonitorKnowledge *k, const char *id,
    RSSDDCMonitorKnowledgeCapability *out) {
  if (!k || !id || !out)
    return RSS_DDC_ERROR_ARGUMENT;
  for (size_t i = 0; i < k->capability_count; ++i)
    if (strcmp(k->capabilities[i].view.id, id) == 0) {
      *out = k->capabilities[i].view;
      return RSS_DDC_OK;
    }
  return RSS_DDC_ERROR_NOT_FOUND;
}
size_t
rss_ddc_monitor_knowledge_input_route_count(const RSSDDCMonitorKnowledge *k) {
  return k ? k->route_count : 0;
}
RSSDDCError
rss_ddc_monitor_knowledge_input_route(const RSSDDCMonitorKnowledge *k, size_t i,
                                      RSSDDCInputRoute *out) {
  if (!k || !out)
    return RSS_DDC_ERROR_ARGUMENT;
  if (i >= k->route_count)
    return RSS_DDC_ERROR_NOT_FOUND;
  *out = k->routes[i].view;
  return RSS_DDC_OK;
}
size_t
rss_ddc_monitor_knowledge_relationship_count(const RSSDDCMonitorKnowledge *k) {
  return k ? k->relationship_count : 0;
}
RSSDDCError
rss_ddc_monitor_knowledge_relationship(const RSSDDCMonitorKnowledge *k,
                                       size_t i, RSSDDCRelationship *out) {
  if (!k || !out)
    return RSS_DDC_ERROR_ARGUMENT;
  if (i >= k->relationship_count)
    return RSS_DDC_ERROR_NOT_FOUND;
  *out = k->relationships[i].view;
  return RSS_DDC_OK;
}
typedef struct {
  char *p;
  size_t cap, n;
} W;
static bool put(W *w, const char *s) {
  size_t n = strlen(s);
  if (w->p && w->n + n < w->cap)
    memcpy(w->p + w->n, s, n);
  w->n += n;
  return true;
}
static bool putn(W *w, int64_t n) {
  char b[32];
  snprintf(b, sizeof(b), "%lld", (long long)n);
  return put(w, b);
}
static void raw_out(W *w, const RSSDDCRawValue *r) {
  put(w, "{\"type\":\"");
  put(w, raw_names[r->type]);
  put(w, "\",\"value\":");
  if (r->type == RSS_DDC_RAW_UNSIGNED)
    putn(w, (int64_t)r->unsigned_value);
  else if (r->type == RSS_DDC_RAW_SIGNED)
    putn(w, r->signed_value);
  else {
    put(w, "\"");
    put(w, (const char *)r->data);
    put(w, "\"");
  }
  put(w, "}");
}
static void evidence_out(W *w, const RSSDDCEvidence *evidence, size_t count) {
  put(w, "[");
  for (size_t i = 0; i < count; ++i) {
    const RSSDDCEvidence *e = &evidence[i];
    if (i)
      put(w, ",");
    put(w, "{\"type\":\"");
    put(w, rss_ddc_evidence_type_name(e->type));
    put(w, "\"");
    if (e->source_id) {
      put(w, ",\"sourceId\":\"");
      put(w, e->source_id);
      put(w, "\"");
    }
    if (e->reference) {
      put(w, ",\"reference\":\"");
      put(w, e->reference);
      put(w, "\"");
    }
    if (e->timestamp) {
      put(w, ",\"timestamp\":\"");
      put(w, e->timestamp);
      put(w, "\"");
    }
    if (e->scope) {
      put(w, ",\"scope\":\"");
      put(w, e->scope);
      put(w, "\"");
    }
    if (e->contribution != RSS_DDC_CONFIDENCE_UNKNOWN) {
      put(w, ",\"contribution\":\"");
      put(w, rss_ddc_confidence_name(e->contribution));
      put(w, "\"");
    }
    put(w, "}");
  }
  put(w, "]");
}
static void range_out(W *w, const RSSDDCRange *range) {
  put(w, "{\"min\":");
  putn(w, range->minimum);
  put(w, ",\"max\":");
  putn(w, range->maximum);
  if (range->step) {
    put(w, ",\"step\":");
    putn(w, range->step);
  }
  if (range->units) {
    put(w, ",\"units\":\"");
    put(w, range->units);
    put(w, "\"");
  }
  put(w, "}");
}
static void condition_out(W *w,
                          const RSSDDCMonitorKnowledgeCondition *condition) {
  put(w, "{\"semanticId\":\"");
  put(w, condition->semantic_id);
  put(w, "\"");
  if (condition->value_id) {
    put(w, ",\"valueId\":\"");
    put(w, condition->value_id);
    put(w, "\"");
  }
  put(w, ",\"op\":\"");
  put(w, condition_operator_names[condition->op]);
  put(w, "\"");
  if (condition->comparison_present) {
    put(w, ",\"comparison\":");
    raw_out(w, &condition->comparison);
  }
  if (condition->confidence != RSS_DDC_CONFIDENCE_UNKNOWN) {
    put(w, ",\"confidence\":\"");
    put(w, rss_ddc_confidence_name(condition->confidence));
    put(w, "\"");
  }
  if (condition->validation != RSS_DDC_VALIDATION_NOT_VALIDATED) {
    put(w, ",\"validation\":\"");
    put(w, rss_ddc_validation_name(condition->validation));
    put(w, "\"");
  }
  if (condition->evidence_count) {
    put(w, ",\"evidence\":");
    evidence_out(w, condition->evidence, condition->evidence_count);
  }
  put(w, "}");
}
static int compare_text(const char *left, const char *right) {
  if (!left)
    return right ? -1 : 0;
  return !right ? 1 : strcmp(left, right);
}
static int compare_capability_ptr(const void *left, const void *right) {
  const Capability *const *a = left;
  const Capability *const *b = right;
  return compare_text((*a)->view.id, (*b)->view.id);
}
static int compare_source_ptr(const void *left, const void *right) {
  const RSSDDCMonitorKnowledgeSource *const *a = left;
  const RSSDDCMonitorKnowledgeSource *const *b = right;
  return compare_text((*a)->id, (*b)->id);
}
static int compare_method_ptr(const void *left, const void *right) {
  const Method *const *a = left;
  const Method *const *b = right;
  return compare_text((*a)->view.id, (*b)->view.id);
}
static int compare_value_ptr(const void *left, const void *right) {
  const Value *const *a = left;
  const Value *const *b = right;
  return compare_text((*a)->view.id, (*b)->view.id);
}
static int compare_route_ptr(const void *left, const void *right) {
  const Route *const *a = left;
  const Route *const *b = right;
  return compare_text((*a)->view.id, (*b)->view.id);
}
static int compare_relationship_ptr(const void *left, const void *right) {
  const Relationship *const *a = left;
  const Relationship *const *b = right;
  int result = compare_text((*a)->view.source_id, (*b)->view.source_id);
  return result ? result
                : compare_text((*a)->view.target_id, (*b)->view.target_id);
}
RSSDDCError
rss_ddc_monitor_knowledge_serialize_json(const RSSDDCMonitorKnowledge *k,
                                         char *buffer, size_t capacity,
                                         size_t *required) {
  if (!k || !required)
    return RSS_DDC_ERROR_ARGUMENT;
  W w = {buffer, capacity, 0};
  put(&w, "{\"schemaVersion\":\"");
  put(&w, k->schema);
  put(&w, "\",\"identity\":{");
  bool comma = false;
  const char *keys[] = {"manufacturer", "model",     "edidManufacturer",
                        "serial",       "provider",  "transport",
                        "branch",       "familyHint"};
  const char *vals[] = {k->identity.manufacturer,
                        k->identity.model,
                        k->identity.edid_manufacturer,
                        k->identity.serial,
                        k->identity.provider,
                        k->identity.transport,
                        k->identity.branch,
                        k->identity.family_hint};
  for (size_t i = 0; i < 8; ++i)
    if (vals[i]) {
      if (comma)
        put(&w, ",");
      put(&w, "\"");
      put(&w, keys[i]);
      put(&w, "\":\"");
      put(&w, vals[i]);
      put(&w, "\"");
      comma = true;
    }
  if (k->identity.edid_product_code_present) {
    if (comma)
      put(&w, ",");
    put(&w, "\"edidProductCode\":");
    putn(&w, k->identity.edid_product_code);
    comma = true;
  }
  if (k->identity.confidence != RSS_DDC_CONFIDENCE_UNKNOWN) {
    if (comma)
      put(&w, ",");
    put(&w, "\"confidence\":\"");
    put(&w, rss_ddc_confidence_name(k->identity.confidence));
    put(&w, "\"");
    comma = true;
  }
  if (k->identity.evidence_count) {
    if (comma)
      put(&w, ",");
    put(&w, "\"evidence\":");
    evidence_out(&w, k->identity.evidence, k->identity.evidence_count);
  }
  put(&w, "},\"sources\":[");
  const RSSDDCMonitorKnowledgeSource *sources[MK_MAX_SOURCES];
  for (size_t i = 0; i < k->source_count; ++i)
    sources[i] = &k->sources[i];
  qsort(sources, k->source_count, sizeof(*sources), compare_source_ptr);
  for (size_t i = 0; i < k->source_count; ++i) {
    if (i)
      put(&w, ",");
    put(&w, "{\"id\":\"");
    put(&w, sources[i]->id);
    put(&w, "\",\"type\":\"");
    put(&w, sources[i]->type);
    put(&w, "\",\"reference\":\"");
    put(&w, sources[i]->reference);
    put(&w, "\"}");
  }
  put(&w, "],\"capabilities\":[");
  const Capability *caps[MK_MAX_ITEMS];
  for (size_t i = 0; i < k->capability_count; ++i)
    caps[i] = &k->capabilities[i];
  qsort(caps, k->capability_count, sizeof(*caps), compare_capability_ptr);
  for (size_t i = 0; i < k->capability_count; ++i) {
    const Capability *c = caps[i];
    if (i)
      put(&w, ",");
    put(&w, "{\"id\":\"");
    put(&w, c->view.id);
    put(&w, "\"");
    if (c->view.label) {
      put(&w, ",\"label\":\"");
      put(&w, c->view.label);
      put(&w, "\"");
    }
    if (c->view.availability != RSS_DDC_AVAILABILITY_UNKNOWN) {
      put(&w, ",\"availability\":\"");
      put(&w, availability_names[c->view.availability]);
      put(&w, "\"");
    }
    if (c->view.conditions) {
      put(&w, ",\"conditions\":\"");
      put(&w, c->view.conditions);
      put(&w, "\"");
    }
    if (c->view.confidence != RSS_DDC_CONFIDENCE_UNKNOWN) {
      put(&w, ",\"confidence\":\"");
      put(&w, rss_ddc_confidence_name(c->view.confidence));
      put(&w, "\"");
    }
    if (c->view.validation != RSS_DDC_VALIDATION_NOT_VALIDATED) {
      put(&w, ",\"validation\":\"");
      put(&w, rss_ddc_validation_name(c->view.validation));
      put(&w, "\"");
    }
    if (c->view.advertised_range.present) {
      put(&w, ",\"advertisedRange\":");
      range_out(&w, &c->view.advertised_range);
    }
    if (c->view.observed_range.present) {
      put(&w, ",\"observedRange\":");
      range_out(&w, &c->view.observed_range);
    }
    if (c->view.validated_range.present) {
      put(&w, ",\"validatedRange\":");
      range_out(&w, &c->view.validated_range);
    }
    if (c->view.reported_maximum_present) {
      put(&w, ",\"reportedMaximum\":");
      raw_out(&w, &c->view.reported_maximum);
    }
    if (c->view.condition_group_count) {
      put(&w, ",\"conditionGroups\":[");
      for (size_t g = 0; g < c->view.condition_group_count; ++g) {
        const RSSDDCMonitorKnowledgeConditionGroup *group =
            &c->view.condition_groups[g];
        if (g)
          put(&w, ",");
        put(&w, "{\"type\":\"");
        put(&w, condition_group_names[group->type]);
        put(&w, "\",\"conditions\":[");
        for (size_t q = 0; q < group->condition_count; ++q) {
          if (q)
            put(&w, ",");
          condition_out(&w, &group->conditions[q]);
        }
        put(&w, "]}");
      }
      put(&w, "]");
    }
    put(&w, ",\"methods\":[");
    const Method *methods[MK_MAX_METHODS];
    for (size_t m = 0; m < c->view.method_count; ++m)
      methods[m] = &c->methods[m];
    qsort(methods, c->view.method_count, sizeof(*methods), compare_method_ptr);
    for (size_t m = 0; m < c->view.method_count; ++m) {
      const RSSDDCMonitorKnowledgeMethod *x = &methods[m]->view;
      if (m)
        put(&w, ",");
      put(&w, "{\"id\":\"");
      put(&w, x->id);
      put(&w, "\",\"type\":\"");
      put(&w, method_names[x->type]);
      put(&w, "\",\"readable\":");
      put(&w, x->readable ? "true" : "false");
      put(&w, ",\"writable\":");
      put(&w, x->writable ? "true" : "false");
      put(&w, ",\"risk\":\"");
      put(&w, risk_names[x->risk]);
      put(&w, "\"");
      if (x->vcp_code) {
        put(&w, ",\"vcpCode\":");
        putn(&w, x->vcp_code);
      }
      if (x->protocol_id) {
        put(&w, ",\"protocolId\":\"");
        put(&w, x->protocol_id);
        put(&w, "\"");
      }
      if (x->address) {
        put(&w, ",\"address\":\"");
        put(&w, x->address);
        put(&w, "\"");
      }
      if (x->parameters) {
        put(&w, ",\"parameters\":\"");
        put(&w, x->parameters);
        put(&w, "\"");
      }
      if (x->confidence != RSS_DDC_CONFIDENCE_UNKNOWN) {
        put(&w, ",\"confidence\":\"");
        put(&w, rss_ddc_confidence_name(x->confidence));
        put(&w, "\"");
      }
      if (x->evidence_count) {
        put(&w, ",\"evidence\":");
        evidence_out(&w, x->evidence, x->evidence_count);
      }
      put(&w, "}");
    }
    put(&w, "],\"values\":[");
    const Value *values[MK_MAX_VALUES];
    for (size_t v = 0; v < c->view.value_count; ++v)
      values[v] = &c->values[v];
    qsort(values, c->view.value_count, sizeof(*values), compare_value_ptr);
    for (size_t v = 0; v < c->view.value_count; ++v) {
      const RSSDDCMonitorKnowledgeValue *x = &values[v]->view;
      if (v)
        put(&w, ",");
      put(&w, "{\"id\":\"");
      put(&w, x->id);
      put(&w, "\",\"raw\":");
      raw_out(&w, &x->raw);
      if (x->label) {
        put(&w, ",\"label\":\"");
        put(&w, x->label);
        put(&w, "\"");
      }
      if (x->raw_alias_count) {
        put(&w, ",\"rawAliases\":[");
        for (size_t a = 0; a < x->raw_alias_count; ++a) {
          if (a)
            put(&w, ",");
          raw_out(&w, &x->raw_aliases[a]);
        }
        put(&w, "]");
      }
      put(&w, ",\"readable\":");
      put(&w, x->readable ? "true" : "false");
      put(&w, ",\"writable\":");
      put(&w, x->writable ? "true" : "false");
      if (x->confidence != RSS_DDC_CONFIDENCE_UNKNOWN) {
        put(&w, ",\"confidence\":\"");
        put(&w, rss_ddc_confidence_name(x->confidence));
        put(&w, "\"");
      }
      if (x->validation != RSS_DDC_VALIDATION_NOT_VALIDATED) {
        put(&w, ",\"validation\":\"");
        put(&w, rss_ddc_validation_name(x->validation));
        put(&w, "\"");
      }
      if (x->availability != RSS_DDC_AVAILABILITY_UNKNOWN) {
        put(&w, ",\"availability\":\"");
        put(&w, availability_names[x->availability]);
        put(&w, "\"");
      }
      if (x->evidence_count) {
        put(&w, ",\"evidence\":");
        evidence_out(&w, x->evidence, x->evidence_count);
      }
      put(&w, "}");
    }
    put(&w, "]");
    if (c->view.evidence_count) {
      put(&w, ",\"evidence\":");
      evidence_out(&w, c->view.evidence, c->view.evidence_count);
    }
    put(&w, "}");
  }
  put(&w, "],\"inputRoutes\":[");
  const Route *routes[MK_MAX_ITEMS];
  for (size_t i = 0; i < k->route_count; ++i)
    routes[i] = &k->routes[i];
  qsort(routes, k->route_count, sizeof(*routes), compare_route_ptr);
  for (size_t i = 0; i < k->route_count; ++i) {
    const Route *route_record = routes[i];
    const RSSDDCInputRoute *route = &route_record->view;
    if (i)
      put(&w, ",");
    put(&w, "{\"id\":\"");
    put(&w, route->id);
    put(&w, "\"");
    if (route->connector) {
      put(&w, ",\"connector\":\"");
      put(&w, route->connector);
      put(&w, "\"");
    }
    if (route->port) {
      put(&w, ",\"port\":\"");
      put(&w, route->port);
      put(&w, "\"");
    }
    if (route->label) {
      put(&w, ",\"label\":\"");
      put(&w, route->label);
      put(&w, "\"");
    }
    put(&w, ",\"switchingSupported\":");
    put(&w, route->switching_supported ? "true" : "false");
    put(&w, ",\"currentReadable\":");
    put(&w, route->current_readable ? "true" : "false");
    put(&w, ",\"ddcPathMayChange\":");
    put(&w, route->ddc_path_may_change ? "true" : "false");
    if (route_record->read_value_present) {
      put(&w, ",\"readValue\":");
      raw_out(&w, &route->read_value);
    }
    if (route_record->switch_value_present) {
      put(&w, ",\"switchValue\":");
      raw_out(&w, &route->switch_value);
    }
    if (route->confidence != RSS_DDC_CONFIDENCE_UNKNOWN) {
      put(&w, ",\"confidence\":\"");
      put(&w, rss_ddc_confidence_name(route->confidence));
      put(&w, "\"");
    }
    if (route->evidence_count) {
      put(&w, ",\"evidence\":");
      evidence_out(&w, route->evidence, route->evidence_count);
    }
    put(&w, "}");
  }
  put(&w, "],\"relationships\":[");
  const Relationship *relationships[MK_MAX_ITEMS];
  for (size_t i = 0; i < k->relationship_count; ++i)
    relationships[i] = &k->relationships[i];
  qsort(relationships, k->relationship_count, sizeof(*relationships),
        compare_relationship_ptr);
  for (size_t i = 0; i < k->relationship_count; ++i) {
    const RSSDDCRelationship *relationship = &relationships[i]->view;
    if (i)
      put(&w, ",");
    put(&w, "{\"sourceId\":\"");
    put(&w, relationship->source_id);
    put(&w, "\",\"targetId\":\"");
    put(&w, relationship->target_id);
    put(&w, "\",\"type\":\"");
    put(&w, relationship_names[relationship->type]);
    put(&w, "\"");
    if (relationship->source_value_id) {
      put(&w, ",\"sourceValueId\":\"");
      put(&w, relationship->source_value_id);
      put(&w, "\"");
    }
    if (relationship->target_value_id) {
      put(&w, ",\"targetValueId\":\"");
      put(&w, relationship->target_value_id);
      put(&w, "\"");
    }
    if (relationship->confidence != RSS_DDC_CONFIDENCE_UNKNOWN) {
      put(&w, ",\"confidence\":\"");
      put(&w, rss_ddc_confidence_name(relationship->confidence));
      put(&w, "\"");
    }
    if (relationship->evidence_count) {
      put(&w, ",\"evidence\":");
      evidence_out(&w, relationship->evidence, relationship->evidence_count);
    }
    put(&w, "}");
  }
  put(&w, "]}");
  *required = w.n + 1;
  if (!buffer && capacity == 0)
    return RSS_DDC_OK;
  if (!buffer || capacity < w.n + 1)
    return RSS_DDC_ERROR_ARGUMENT;
  buffer[w.n] = 0;
  return RSS_DDC_OK;
}
static const RSSDDCSemanticRegistryEntry registry[] = {
    {"display.brightness", 0x10, "range", true, true, true},
    {"display.contrast", 0x12, "range", true, true, true},
    {"display.color_preset", 0x14, "enum", true, false, true},
    {"display.rgb.red_gain", 0x16, "range", true, true, true},
    {"display.rgb.green_gain", 0x18, "range", true, true, true},
    {"display.rgb.blue_gain", 0x1a, "range", true, true, true}};
const RSSDDCSemanticRegistryEntry *
rss_ddc_semantic_registry_lookup(const char *s) {
  if (!s)
    return NULL;
  for (size_t i = 0; i < sizeof(registry) / sizeof(*registry); ++i)
    if (strcmp(registry[i].semantic_id, s) == 0)
      return &registry[i];
  return NULL;
}
const RSSDDCSemanticRegistryEntry *
rss_ddc_semantic_registry_lookup_vcp(uint8_t v) {
  for (size_t i = 0; i < sizeof(registry) / sizeof(*registry); ++i)
    if (registry[i].vcp_code == v)
      return &registry[i];
  return NULL;
}
RSSDDCError
rss_ddc_monitor_knowledge_merge(const RSSDDCMonitorKnowledge *base,
                                const RSSDDCMonitorKnowledge *overlay,
                                RSSDDCMonitorKnowledge **merged) {
  if (!base || !overlay || !merged)
    return RSS_DDC_ERROR_ARGUMENT;
  *merged = NULL;
  if (strcmp(base->schema, overlay->schema) != 0)
    return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_SCHEMA;
  if (base->identity.manufacturer && overlay->identity.manufacturer &&
      strcmp(base->identity.manufacturer, overlay->identity.manufacturer) != 0)
    return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_CONFLICT;
  if (base->identity.model && overlay->identity.model &&
      strcmp(base->identity.model, overlay->identity.model) != 0)
    return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_CONFLICT;
  RSSDDCMonitorKnowledge *out = copy_knowledge(base);
  if (!out)
    return RSS_DDC_ERROR_SYSTEM;
  /* Re-merging the same immutable source is a no-op. Apart from avoiding
   * duplicate retained records, this keeps repeated profile ingestion stable.
   */
  if (base == overlay) {
    if (rss_ddc_monitor_knowledge_validate(out) != RSS_DDC_OK) {
      free_knowledge(out);
      return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
    }
    *merged = out;
    return RSS_DDC_OK;
  }
  for (size_t i = 0; i < overlay->source_count; ++i) {
    const RSSDDCMonitorKnowledgeSource *source = &overlay->sources[i];
    bool present = false;
    for (size_t x = 0; x < out->source_count; ++x) {
      RSSDDCMonitorKnowledgeSource *existing = &out->sources[x];
      if (strcmp(existing->id, source->id) != 0)
        continue;
      if (strcmp(existing->type, source->type) != 0 ||
          strcmp(existing->reference, source->reference) != 0) {
        free_knowledge(out);
        return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_CONFLICT;
      }
      present = true;
      break;
    }
    if (present)
      continue;
    if (out->source_count == MK_MAX_SOURCES) {
      free_knowledge(out);
      return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_TOO_LARGE;
    }
    RSSDDCMonitorKnowledgeSource *sources =
        realloc(out->sources, (out->source_count + 1) * sizeof(*sources));
    if (!sources) {
      free_knowledge(out);
      return RSS_DDC_ERROR_SYSTEM;
    }
    out->sources = sources;
    RSSDDCMonitorKnowledgeSource *copy = &sources[out->source_count];
    *copy = (RSSDDCMonitorKnowledgeSource){};
    copy->id = copy_text(source->id);
    copy->type = copy_text(source->type);
    copy->reference = copy_text(source->reference);
    if (!copy->id || !copy->type || !copy->reference) {
      free_knowledge(out);
      return RSS_DDC_ERROR_SYSTEM;
    }
    ++out->source_count;
  }
  for (size_t i = 0; i < overlay->capability_count; ++i) {
    Capability *c =
        realloc(out->capabilities, (out->capability_count + 1) * sizeof(*c));
    if (!c) {
      free_knowledge(out);
      return RSS_DDC_ERROR_SYSTEM;
    }
    out->capabilities = c;
    memset(&out->capabilities[out->capability_count], 0,
           sizeof(*out->capabilities));
    out->capability_count++;
    if (!copy_capability(&out->capabilities[out->capability_count - 1],
                         &overlay->capabilities[i])) {
      free_knowledge(out);
      return RSS_DDC_ERROR_SYSTEM;
    }
  }
  for (size_t i = 0; i < overlay->route_count; ++i) {
    Route *routes =
        realloc(out->routes, (out->route_count + 1) * sizeof(*routes));
    if (!routes) {
      free_knowledge(out);
      return RSS_DDC_ERROR_SYSTEM;
    }
    out->routes = routes;
    memset(&routes[out->route_count], 0, sizeof(*routes));
    ++out->route_count;
    if (!copy_route(&routes[out->route_count - 1], &overlay->routes[i])) {
      free_knowledge(out);
      return RSS_DDC_ERROR_SYSTEM;
    }
  }
  for (size_t i = 0; i < overlay->relationship_count; ++i) {
    Relationship *relationships =
        realloc(out->relationships,
                (out->relationship_count + 1) * sizeof(*relationships));
    if (!relationships) {
      free_knowledge(out);
      return RSS_DDC_ERROR_SYSTEM;
    }
    out->relationships = relationships;
    memset(&relationships[out->relationship_count], 0, sizeof(*relationships));
    ++out->relationship_count;
    if (!copy_relationship(&relationships[out->relationship_count - 1],
                           &overlay->relationships[i])) {
      free_knowledge(out);
      return RSS_DDC_ERROR_SYSTEM;
    }
  }
  if (rss_ddc_monitor_knowledge_validate(out) != RSS_DDC_OK) {
    free_knowledge(out);
    return RSS_DDC_ERROR_MONITOR_KNOWLEDGE_MALFORMED;
  }
  *merged = out;
  return RSS_DDC_OK;
}
