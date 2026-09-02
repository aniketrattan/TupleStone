#include "disk/disk_manager.h"

#include <array>
#include <cstring>

#include "common/crc32c.h"
#include "common/endian.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace tuplestone {
namespace {
constexpr uint8_t kFormatVersion = 1;
constexpr char kMagic[] = "TSTONE01";

Status IoFailure(const char* operation, const std::string& path) {
  return IoError(std::string(operation) + " failed for " + path);
}

}  // namespace

DiskManager::~DiskManager() {
  (void)Close();
}

Status DiskManager::Open(std::string_view path, bool create_if_missing) {
  std::lock_guard lock(mutex_);
  if (file_.is_open()) return AlreadyExists("database is already open");
  path_ = std::string(path);
  file_.open(path_, std::ios::in | std::ios::out | std::ios::binary);
  if (!file_.is_open() && create_if_missing) {
    std::ofstream create(path_, std::ios::binary);
    if (!create) return IoFailure("create", path_);
    create.close();
    file_.open(path_, std::ios::in | std::ios::out | std::ios::binary);
  }
  if (!file_.is_open()) return IoFailure("open", path_);
  file_.seekg(0, std::ios::end);
  const std::streamoff size = file_.tellg();
  file_.clear();
  if (size == 0) {
    page_count_ = 1;
    free_list_head_ = kInvalidPageId;
    next_txn_id_ = 1;
    next_table_id_ = 1;
    next_index_id_ = 1;
    const Status status = WriteHeaderLocked();
    if (!status.ok()) {
      file_.close();
      return status;
    }
    return Status::Ok();
  }
  if (size < static_cast<std::streamoff>(kPageSize) ||
      size % static_cast<std::streamoff>(kPageSize) != 0) {
    file_.close();
    return Corruption("database file has an invalid size");
  }
  return ReadHeaderLocked();
}

Status DiskManager::Close() {
  std::lock_guard lock(mutex_);
  if (!file_.is_open()) return Status::Ok();
  const Status status = WriteHeaderLocked();
  file_.flush();
  file_.close();
  return status;
}

Status DiskManager::ReadHeaderLocked() {
  std::array<uint8_t, kPageSize> bytes{};
  file_.seekg(0);
  file_.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  if (!file_ || file_.gcount() != static_cast<std::streamsize>(bytes.size())) {
    file_.clear();
    return IoFailure("read header", path_);
  }
  if (std::memcmp(bytes.data(), kMagic, 8) != 0) return Incompatible("not a tuplestone database");
  if (bytes[8] != kFormatVersion || bytes[9] != kPageSizeLog2)
    return Incompatible("unsupported database format");
  if (LoadU32LE(bytes.data() + 4092) != Crc32c(bytes.data(), 4092))
    return Corruption("header checksum mismatch");
  page_count_ = LoadU32LE(bytes.data() + 12);
  free_list_head_ = LoadU32LE(bytes.data() + 16);
  next_txn_id_ = LoadU64LE(bytes.data() + 20);
  checkpoint_lsn_ = LoadU64LE(bytes.data() + 28);
  next_table_id_ = LoadU32LE(bytes.data() + 36);
  next_index_id_ = LoadU32LE(bytes.data() + 40);
  for (int i = 0; i < 3; ++i) catalog_roots_[i] = LoadU32LE(bytes.data() + 44 + i * 4);
  return Status::Ok();
}

Status DiskManager::WriteHeaderLocked() {
  if (!file_.is_open()) return IoError("database is not open");
  std::array<uint8_t, kPageSize> bytes{};
  std::memcpy(bytes.data(), kMagic, 8);
  bytes[8] = kFormatVersion;
  bytes[9] = kPageSizeLog2;
  StoreU32LE(bytes.data() + 12, page_count_);
  StoreU32LE(bytes.data() + 16, free_list_head_);
  StoreU64LE(bytes.data() + 20, next_txn_id_);
  StoreU64LE(bytes.data() + 28, checkpoint_lsn_);
  StoreU32LE(bytes.data() + 36, next_table_id_);
  StoreU32LE(bytes.data() + 40, next_index_id_);
  for (int i = 0; i < 3; ++i) StoreU32LE(bytes.data() + 44 + i * 4, catalog_roots_[i]);
  StoreU32LE(bytes.data() + 4092, Crc32c(bytes.data(), 4092));
  file_.seekp(0);
  file_.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
  if (!file_) {
    file_.clear();
    return IoFailure("write header", path_);
  }
  file_.flush();
  return Status::Ok();
}

StatusOr<page_id_t> DiskManager::AllocatePage() {
  std::lock_guard lock(mutex_);
  if (!file_.is_open()) return IoError("database is not open");
  page_id_t result = free_list_head_;
  if (result != kInvalidPageId) {
    std::array<uint8_t, kPageSize> bytes{};
    file_.seekg(static_cast<std::streamoff>(result) * kPageSize);
    file_.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!file_) {
      file_.clear();
      return IoFailure("read free page", path_);
    }
    free_list_head_ = LoadU32LE(bytes.data() + 16);
  } else {
    result = page_count_++;
  }
  Page page(result);
  page.data()[4] = static_cast<uint8_t>(PageType::kFree);
  StoreU32LE(page.data() + 16, kInvalidPageId);
  const Status status = WritePageLocked(result, page);
  if (!status.ok()) return status;
  TUPLESTONE_RETURN_IF_ERROR(WriteHeaderLocked());
  return result;
}

Status DiskManager::EnsurePageLocked(page_id_t page_id) {
  if (page_id == kInvalidPageId || page_id >= page_count_)
    return OutOfRange("page id is out of range");
  return Status::Ok();
}

Status DiskManager::ReadPage(page_id_t page_id, Page* page) {
  if (page == nullptr) return InvalidArgument("page output is null");
  std::lock_guard lock(mutex_);
  TUPLESTONE_RETURN_IF_ERROR(EnsurePageLocked(page_id));
  std::array<uint8_t, kPageSize> bytes{};
  file_.seekg(static_cast<std::streamoff>(page_id) * kPageSize);
  file_.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  if (!file_) {
    file_.clear();
    return IoFailure("read page", path_);
  }
  if (page_id != kHeaderPageId && LoadU32LE(bytes.data()) != Crc32c(bytes.data() + 4, 4092)) {
    return Corruption("page checksum mismatch");
  }
  page->set_id(page_id);
  std::memcpy(page->data(), bytes.data(), bytes.size());
  return Status::Ok();
}

Status DiskManager::WritePage(page_id_t page_id, const Page& page) {
  std::lock_guard lock(mutex_);
  return WritePageLocked(page_id, page);
}

Status DiskManager::WritePageLocked(page_id_t page_id, const Page& page) {
  TUPLESTONE_RETURN_IF_ERROR(EnsurePageLocked(page_id));
  std::array<uint8_t, kPageSize> bytes{};
  std::memcpy(bytes.data(), page.data(), bytes.size());
  if (page_id != kHeaderPageId) StoreU32LE(bytes.data(), Crc32c(bytes.data() + 4, 4092));
  file_.seekp(static_cast<std::streamoff>(page_id) * kPageSize);
  file_.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
  if (!file_) {
    file_.clear();
    return IoFailure("write page", path_);
  }
  return Status::Ok();
}

Status DiskManager::FreePage(page_id_t page_id) {
  std::lock_guard lock(mutex_);
  TUPLESTONE_RETURN_IF_ERROR(EnsurePageLocked(page_id));
  if (page_id == kHeaderPageId) return InvalidArgument("cannot free header page");
  Page page(page_id);
  page.data()[4] = static_cast<uint8_t>(PageType::kFree);
  StoreU32LE(page.data() + 16, free_list_head_);
  const Status status = WritePageLocked(page_id, page);
  if (!status.ok()) return status;
  free_list_head_ = page_id;
  return WriteHeaderLocked();
}

Status DiskManager::Sync() {
  std::lock_guard lock(mutex_);
  if (!file_.is_open()) return IoError("database is not open");
  file_.flush();
#ifdef _WIN32
  HANDLE handle =
      CreateFileA(path_.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (handle == INVALID_HANDLE_VALUE) return IoFailure("sync", path_);
  const BOOL flushed = FlushFileBuffers(handle);
  CloseHandle(handle);
  if (flushed == 0) return IoFailure("sync", path_);
#else
  const int fd = open(path_.c_str(), O_RDONLY);
  if (fd >= 0) {
    const int result = fsync(fd);
    close(fd);
    if (result != 0) return IoFailure("sync", path_);
  }
#endif
  return Status::Ok();
}

Status FaultDiskManager::WritePage(page_id_t page_id, const Page& page) {
  ++write_count_;
  if (fail_write_at_ != 0 && write_count_ == fail_write_at_)
    return IoError("injected write failure");
  if (tear_write_at_ != 0 && write_count_ == tear_write_at_) {
    Page partial = page;
    std::memset(partial.data() + kPageSize / 2, 0, kPageSize / 2);
    return DiskManager::WritePage(page_id, partial);
  }
  return DiskManager::WritePage(page_id, page);
}

Status FaultDiskManager::Sync() {
  ++write_count_;
  if (fail_write_at_ != 0 && write_count_ == fail_write_at_)
    return IoError("injected sync failure");
  return DiskManager::Sync();
}

}  // namespace tuplestone
