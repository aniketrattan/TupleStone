#include "txn/transaction_manager.h"
namespace tuplestone {
TxnHandle TransactionManager::Begin() {
  std::lock_guard lock(mutex_);
  TxnHandle txn;
  txn.id = next_id_++;
  txn.snapshot.xmin = active_.empty() ? next_id_ : *active_.begin();
  txn.snapshot.xmax = next_id_;
  txn.snapshot.active = active_;
  active_.insert(txn.id);
  return txn;
}
Status TransactionManager::Commit(TxnHandle* txn) {
  if (txn == nullptr || txn->state != TxnState::kActive)
    return InvalidArgument("transaction is not active");
  std::lock_guard lock(mutex_);
  active_.erase(txn->id);
  committed_.insert(txn->id);
  txn->state = TxnState::kCommitted;
  return Status::Ok();
}
Status TransactionManager::Abort(TxnHandle* txn) {
  if (txn == nullptr || txn->state != TxnState::kActive)
    return InvalidArgument("transaction is not active");
  std::lock_guard lock(mutex_);
  active_.erase(txn->id);
  txn->state = TxnState::kAborted;
  return Status::Ok();
}
bool TransactionManager::IsCommitted(txn_id_t id) const {
  std::lock_guard lock(mutex_);
  return committed_.contains(id);
}
Snapshot TransactionManager::CurrentSnapshot(txn_id_t self) const {
  Snapshot snapshot;
  snapshot.xmin = active_.empty() ? next_id_ : *active_.begin();
  snapshot.xmax = next_id_;
  snapshot.active = active_;
  snapshot.active.erase(self);
  return snapshot;
}
}  // namespace tuplestone
