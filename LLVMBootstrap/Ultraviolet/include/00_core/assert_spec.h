#pragma once

#include "00_core/spec_trace.h"

// Conformance traceability hooks. These are no-ops unless tracing is enabled.
#define SPEC_RULE(id)                                                         \
  do {                                                                        \
    (void)sizeof(id);                                                         \
    if (ultraviolet::core::Conformance::Enabled()) {                             \
      ultraviolet::core::Conformance::Record(id);                                 \
    }                                                                         \
  } while (0)
#define SPEC_RULE_AT(id, span_expr)                                           \
  do {                                                                        \
    (void)sizeof(id);                                                         \
    if (ultraviolet::core::Conformance::Enabled()) {                             \
      ultraviolet::core::Conformance::Record(id, (span_expr), "");               \
    }                                                                         \
  } while (0)
#define SPEC_DEF(name, section) do { (void)sizeof(name); (void)sizeof(section); } while (0)
#define SPEC_COV(id) do { (void)sizeof(id); } while (0)
