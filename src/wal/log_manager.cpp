#include "wal/log_manager.h"

#include <filesystem>
#include <fstream>

#include "common/crc32c.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace tuplestone {
namespace {

Status SyncFile(const std::string& path) {
#ifdef _WIN32
  HANDLE handle =
      CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (handle == INVALID_HANDLE_VALUE) return IoError("cannot open WAL for sync");
  const BOOL flushed = FlushFileBuffers(handle);
  CloseHandle(handle);
  return flushed == 0 ? IoError("cannot sync WAL") : Status::Ok();
#else
  const int descriptor = open(path.c_str(), O_RDONLY);
  if (descriptor < 0) return IoError("cannot open WAL for sync");
  const int result = fsync(descriptor);
  close(descriptor);
  return result != 0 ? IoError("cannot sync WAL") : Status::Ok();
#endif
}

StatusOr<std::vector<LogRecord>> ReadRecords(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) return IoError("cannot read WAL");

  std::vector<LogRecord> records;
  while (true) {
    uint8_t length_bytes[4]{};
    file.read(reinterpret_cast<char*>(length_bytes), 4);
    if (file.gcount() == 0) break;
    // A crash can leave a partial length at the end. The valid prefix remains
    // usable and recovery deliberately ignores this incomplete tail.
    if (file.gcount() != 4) break;

    uint32_t length = 0;
    for (int i = 0; i < 4; ++i) {
      length |= static_cast<uint32_t>(length_bytes[i]) << (i * 8);
    }
    if (length < 25 || length > (1u << 30)) break;

    std::string body(length + 4, '\0');
    file.read(body.data(), static_cast<std::streamsize>(body.size()));
    if (file.gcount() != static_cast<std::streamsize>(body.size())) break;

    uint32_t expected = 0;
    for (int i = 0; i < 4; ++i) {
      expected |= static_cast<uint32_t>(static_cast<uint8_t>(body[length + static_cast<size_t>(i)]))
                  << (i * 8);
    }
    std::string full;
    full.reserve(length + 4);
    full.append(reinterpret_cast<const char*>(length_bytes), 4);
    full.append(body.data(), length);
    // A torn final record is ignored; a later valid record cannot exist after
    // it because the log is append-only.
    if (Crc32c(reinterpret_cast<const uint8_t*>(full.data()), full.size()) != expected) break;

    auto read_u64 = [&body](size_t offset) {
      uint64_t value = 0;
      for (int i = 0; i < 8; ++i) {
        value |= static_cast<uint64_t>(static_cast<uint8_t>(body[offset + static_cast<size_t>(i)]))
                 << (i * 8);
      }
      return value;
    };
    LogRecord record;
    record.lsn = read_u64(0);
    record.txn_id = read_u64(8);
    record.prev_lsn = read_u64(16);
    record.type = static_cast<LogRecordType>(static_cast<uint8_t>(body[24]));
    record.payload.assign(body.data() + 25, length - 25);
    records.push_back(std::move(record));
  }
  return records;
}

}  // namespace

LogManager::~LogManager() {
  (void)Close();
}

Status LogManager::Open(std::string_view path) {
  {
    std::lock_guard lock(mutex_);
    if (file_.is_open()) return AlreadyExists("WAL is already open");
    path_ = std::string(path);
    file_.open(path_, std::ios::in | std::ios::out | std::ios::binary | std::ios::app);
    if (!file_) {
      std::ofstream create(path_, std::ios::binary);
      if (!create) return IoError("cannot create WAL");
      create.close();
      file_.open(path_, std::ios::in | std::ios::out | std::ios::binary | std::ios::app);
    }
    if (!file_) return IoError("cannot open WAL");
  }

  auto existing = ReadRecords(path_);
  if (!existing.ok()) return existing.status();
  std::lock_guard lock(mutex_);
  size_t valid_bytes = 0;
  for (const LogRecord& record : *existing) valid_bytes += 33u + record.payload.size();
  std::error_code resize_error;
  const uintmax_t file_bytes = std::filesystem::file_size(path_, resize_error);
  if (!resize_error && file_bytes > valid_bytes) {
    file_.close();
    std::filesystem::resize_file(path_, valid_bytes, resize_error);
    if (resize_error) return IoError("cannot discard torn WAL tail");
    file_.open(path_, std::ios::in | std::ios::out | std::ios::binary | std::ios::app);
    if (!file_) return IoError("cannot reopen WAL after tail recovery");
  }
  if (!existing->empty()) {
    next_lsn_ = existing->back().lsn + 1;
    durable_lsn_ = existing->back().lsn;
  }
  return Status::Ok();
}

Status LogManager::Close() {
  std::lock_guard lock(mutex_);
  if (!file_.is_open()) return Status::Ok();
  file_.flush();
  file_.close();
  return Status::Ok();
}

StatusOr<lsn_t> LogManager::Append(txn_id_t txn_id, lsn_t prev_lsn, LogRecordType type,
                                   std::string_view payload) {
  std::lock_guard lock(mutex_);
  if (!file_.is_open()) return IoError("WAL is not open");
  if (payload.size() > (1u << 30) - 25u) return OutOfRange("WAL payload is too large");

  const lsn_t lsn = next_lsn_++;
  const uint32_t length = static_cast<uint32_t>(25 + payload.size());
  std::string data;
  data.reserve(length + 8);
  auto append_u64 = [&data](uint64_t value) {
    for (int i = 0; i < 8; ++i) data.push_back(static_cast<char>(value >> (i * 8)));
  };
  for (int i = 0; i < 4; ++i) data.push_back(static_cast<char>(length >> (i * 8)));
  append_u64(lsn);
  append_u64(txn_id);
  append_u64(prev_lsn);
  data.push_back(static_cast<char>(type));
  data.append(payload);
  const uint32_t crc = Crc32c(reinterpret_cast<const uint8_t*>(data.data()), data.size());
  for (int i = 0; i < 4; ++i) data.push_back(static_cast<char>(crc >> (i * 8)));
  file_.write(data.data(), static_cast<std::streamsize>(data.size()));
  if (!file_) {
    file_.clear();
    return IoError("cannot append WAL record");
  }
  return lsn;
}

Status LogManager::Flush(lsn_t through) {
  std::string path;
  {
    std::lock_guard lock(mutex_);
    if (!file_.is_open()) return IoError("WAL is not open");
    file_.flush();
    if (!file_) {
      file_.clear();
      return IoError("cannot flush WAL");
    }
    durable_lsn_ = through == kInvalidLsn ? next_lsn_ - 1 : through;
    path = path_;
  }
  return SyncFile(path);
}

StatusOr<std::vector<LogRecord>> LogManager::ReadAll() const {
  std::lock_guard lock(mutex_);
  if (path_.empty()) return IoError("WAL has not been opened");
  return ReadRecords(path_);
}

Status LogManager::Truncate() {
  std::lock_guard lock(mutex_);
  if (!file_.is_open()) return IoError("WAL is not open");
  file_.flush();
  file_.close();
  std::ofstream truncate(path_, std::ios::binary | std::ios::trunc);
  if (!truncate) return IoError("cannot truncate WAL");
  truncate.close();
  file_.open(path_, std::ios::in | std::ios::out | std::ios::binary | std::ios::app);
  if (!file_) return IoError("cannot reopen WAL after truncate");
  next_lsn_ = 1;
  durable_lsn_ = 0;
  return SyncFile(path_);
}

}  // namespace tuplestone
