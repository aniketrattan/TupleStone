// The SQL value model. The full Value type — three-valued logic, coercion rules,
// and the memcomparable key encoding — is built in M4 (PLAN.md). This header
// exists from M0 because db.h's public signatures name Value and Schema, and
// ARCHITECTURE.md §12 freezes that surface before anything implements it.
#ifndef NANOSQL_VALUE_H_
#define NANOSQL_VALUE_H_

#include <cstdint>

namespace nanosql {

// ARCHITECTURE.md §9. The on-disk encoding of these discriminants is fixed by
// the catalog's `type` column, so the numeric values may not be reordered.
enum class TypeId : uint8_t {
  kNull = 0,
  kBoolean = 1,
  kInteger = 2,  // int64_t
  kReal = 3,     // double
  kText = 4,
  kBlob = 5,
};

const char* TypeIdName(TypeId type);

// Defined in M4 (src/common/value.h). Declared here so the frozen public API
// signatures in db.h compile against a name that already exists.
class Value;

// Defined in M8 (src/catalog/). Same reason.
class Schema;

}  // namespace nanosql

#endif  // NANOSQL_VALUE_H_
