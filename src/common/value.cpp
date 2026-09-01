#include "nanosql/value.h"

namespace nanosql {

const char* TypeIdName(TypeId type) {
  switch (type) {
    case TypeId::kNull: return "NULL";
    case TypeId::kBoolean: return "BOOLEAN";
    case TypeId::kInteger: return "INTEGER";
    case TypeId::kReal: return "REAL";
    case TypeId::kText: return "TEXT";
    case TypeId::kBlob: return "BLOB";
  }
  return "UNKNOWN";
}

}  // namespace nanosql
