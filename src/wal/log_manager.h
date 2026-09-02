#ifndef TUPLESTONE_WAL_LOG_MANAGER_H_
#define TUPLESTONE_WAL_LOG_MANAGER_H_
#include <cstdint>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>
#include "common/types.h"
#include "tuplestone/status.h"
namespace tuplestone {
enum class LogRecordType : uint8_t {
  kBegin = 1,
  kCommit = 2,
  kAbort = 3,
  kInsert = 4,
  kUpdate = 5,
  kDelete = 6,
  kNewPage = 7,
  kClr = 8,
  kCheckpointBegin = 9,
  kCheckpointEnd = 10
};
struct LogRecord {
  lsn_t lsn = 0;
  txn_id_t txn_id = kInvalidTxnId;
  lsn_t prev_lsn = kInvalidLsn;
  LogRecordType type = LogRecordType::kBegin;
  std::string payload;
};
class LogManager {
 public:
  LogManager() = default;
  ~LogManager();
  Status Open(std::string_view path);
  Status Close();
  StatusOr<lsn_t> Append(txn_id_t txn_id, lsn_t prev_lsn, LogRecordType type,
                         std::string_view payload = {});
  Status Flush(lsn_t through = kInvalidLsn);
  StatusOr<std::vector<LogRecord>> ReadAll() const;
  // Discard records after their effects have been checkpointed into the data file.
  // The operation is atomic from the point of view of a future Open(): a crash
  // before the truncate simply replays the already-checkpointed snapshot.
  Status Truncate();
  lsn_t durable_lsn() const { return durable_lsn_; }

 private:
  mutable std::mutex mutex_;
  std::string path_;
  std::fstream file_;
  lsn_t next_lsn_ = 1;
  lsn_t durable_lsn_ = 0;
};
}  // namespace tuplestone
#endif
