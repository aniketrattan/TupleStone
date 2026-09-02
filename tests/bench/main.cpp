#include "tuplestone/db.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

using Clock = std::chrono::steady_clock;

int Fail(const tuplestone::Status& status) {
  std::cerr << status.ToString() << '\n';
  return 1;
}

}  // namespace

int main(int argc, char** argv) {
  const std::string path = argc > 1 ? argv[1] : "tuplestone-benchmark.db";
  const int64_t row_count = argc > 2 ? std::atoll(argv[2]) : 20000;
  if (row_count <= 0) {
    std::cerr << "row count must be positive\n";
    return 2;
  }
  std::remove(path.c_str());
  std::remove((path + ".wal").c_str());

  auto database = tuplestone::Database::Open(path);
  if (!database.ok()) return Fail(database.status());
  auto connection = (*database)->Connect();
  if (!connection.ok()) return Fail(connection.status());
  if (const tuplestone::Status status =
          (*connection)
              ->Execute("CREATE TABLE bench (id INTEGER PRIMARY KEY, score INTEGER, label TEXT)");
      !status.ok()) {
    return Fail(status);
  }

  const auto insert_started = Clock::now();
  auto transaction = (*connection)->Begin();
  if (!transaction.ok()) return Fail(transaction.status());
  for (int64_t id = 0; id < row_count; ++id) {
    const std::string sql = "INSERT INTO bench VALUES (" + std::to_string(id) + ", " +
                            std::to_string(id % 1000) + ", 'row')";
    if (const tuplestone::Status status = (*connection)->Execute(sql); !status.ok()) {
      return Fail(status);
    }
  }
  if (const tuplestone::Status status = transaction->Commit(); !status.ok()) return Fail(status);
  const auto insert_elapsed =
      std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - insert_started);

  const auto query_started = Clock::now();
  auto result = (*connection)->Query("SELECT COUNT(*) FROM bench WHERE score >= 500");
  if (!result.ok()) return Fail(result.status());
  if (const auto next = result->Next(); !next.ok() || !*next) {
    return Fail(next.ok() ? tuplestone::Internal("benchmark query returned no row")
                          : next.status());
  }
  const int64_t matched = result->Get(0).AsInteger();
  const auto query_elapsed =
      std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - query_started);
  const double rows_per_second =
      static_cast<double>(row_count) * 1'000'000.0 / static_cast<double>(insert_elapsed.count());
  std::cout << "rows=" << row_count << " matched=" << matched
            << " insert_us=" << insert_elapsed.count() << " query_us=" << query_elapsed.count()
            << " insert_rows_per_sec=" << rows_per_second << '\n';
  (void)(*database)->Close();
  std::remove(path.c_str());
  std::remove((path + ".wal").c_str());
  return 0;
}
