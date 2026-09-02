#ifndef TUPLESTONE_TXN_TRANSACTION_MANAGER_H_
#define TUPLESTONE_TXN_TRANSACTION_MANAGER_H_
#include <atomic>
#include <mutex>
#include <set>
#include "common/types.h"
#include "tuplestone/status.h"
namespace tuplestone {
struct Snapshot {
  txn_id_t xmin = kInvalidTxnId;
  txn_id_t xmax = kInvalidTxnId;
  std::set<txn_id_t> active;
};
enum class TxnState : uint8_t { kActive, kCommitted, kAborted };
struct TxnHandle {
  txn_id_t id = kInvalidTxnId;
  Snapshot snapshot;
  TxnState state = TxnState::kActive;
};
class TransactionManager {
 public:
  TxnHandle Begin();
  Status Commit(TxnHandle* txn);
  Status Abort(TxnHandle* txn);
  bool IsCommitted(txn_id_t id) const;
  Snapshot CurrentSnapshot(txn_id_t self = kInvalidTxnId) const;

 private:
  mutable std::mutex mutex_;
  txn_id_t next_id_ = 1;
  std::set<txn_id_t> active_;
  std::set<txn_id_t> committed_;
};
}  // namespace tuplestone
#endif
