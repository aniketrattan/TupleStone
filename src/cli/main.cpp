#include "tuplestone/db.h"

#include <cctype>
#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {

std::string TrimLower(std::string text) {
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) {
    text.erase(text.begin());
  }
  while (!text.empty() &&
         (std::isspace(static_cast<unsigned char>(text.back())) != 0 || text.back() == ';')) {
    text.pop_back();
  }
  for (char& character : text) {
    character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
  }
  return text;
}

bool IsQuery(const std::string& sql) {
  const std::string command = TrimLower(sql);
  return command.rfind("select", 0) == 0 || command.rfind("explain", 0) == 0;
}

void PrintResult(tuplestone::ResultSet* result) {
  for (size_t i = 0; i < result->schema().size(); ++i) {
    if (i != 0) std::cout << " | ";
    std::cout << result->schema()[i].name;
  }
  std::cout << '\n';
  while (true) {
    auto next = result->Next();
    if (!next.ok()) {
      std::cerr << next.status().ToString() << '\n';
      return;
    }
    if (!*next) break;
    for (size_t i = 0; i < result->schema().size(); ++i) {
      if (i != 0) std::cout << " | ";
      std::cout << result->Get(static_cast<int>(i)).ToString();
    }
    std::cout << '\n';
  }
}

std::string TypeName(tuplestone::Value value) {
  if (value.type() != tuplestone::TypeId::kInteger) return "?";
  switch (static_cast<tuplestone::TypeId>(value.AsInteger())) {
    case tuplestone::TypeId::kBoolean:
      return "BOOLEAN";
    case tuplestone::TypeId::kInteger:
      return "INTEGER";
    case tuplestone::TypeId::kReal:
      return "REAL";
    case tuplestone::TypeId::kText:
      return "TEXT";
    case tuplestone::TypeId::kBlob:
      return "BLOB";
    case tuplestone::TypeId::kNull:
      return "NULL";
  }
  return "?";
}

void PrintSchema(tuplestone::Connection* connection, std::string_view requested) {
  auto tables = connection->Query("SELECT table_id, name FROM tuplestone_tables");
  if (!tables.ok()) {
    std::cerr << tables.status().ToString() << '\n';
    return;
  }
  int64_t table_id = -1;
  while (true) {
    auto next = tables->Next();
    if (!next.ok() || !*next) break;
    if (requested.empty() || tables->Get(1).AsText() == requested) {
      table_id = tables->Get(0).AsInteger();
      if (!requested.empty()) break;
      std::cout << tables->Get(1).AsText() << '\n';
    }
  }
  if (requested.empty()) return;
  if (table_id < 0) {
    std::cerr << "table not found: " << requested << '\n';
    return;
  }
  auto detailed = connection->Query(
      "SELECT table_id, ordinal, name, type, nullable, is_primary FROM tuplestone_columns");
  if (!detailed.ok()) {
    std::cerr << detailed.status().ToString() << '\n';
    return;
  }
  while (true) {
    auto next = detailed->Next();
    if (!next.ok() || !*next) break;
    if (detailed->Get(0).AsInteger() != table_id) continue;
    std::cout << "  " << detailed->Get(1).AsInteger() << ": " << detailed->Get(2).AsText() << " "
              << TypeName(detailed->Get(3)) << (detailed->Get(4).AsBoolean() ? "" : " NOT NULL")
              << (detailed->Get(5).AsBoolean() ? " PRIMARY KEY" : "") << '\n';
  }
}

}  // namespace

int main(int argc, char** argv) {
  const std::string path = argc > 1 ? argv[1] : "tuplestone.db";
  auto database = tuplestone::Database::Open(path);
  if (!database.ok()) {
    std::cerr << database.status().ToString() << '\n';
    return 1;
  }
  auto connection = (*database)->Connect();
  if (!connection.ok()) {
    std::cerr << connection.status().ToString() << '\n';
    return 1;
  }
  bool timer = false;
  std::string line;
  while (std::cout << "tuplestone> " && std::getline(std::cin, line)) {
    const std::string command = TrimLower(line);
    if (command == ".quit" || command == ".exit") break;
    if (command == ".help") {
      std::cout << ".tables  .schema [TABLE]  .timer on|off  .read FILE  .quit\n";
      continue;
    }
    if (command == ".timer on" || command == ".timer off") {
      timer = command == ".timer on";
      continue;
    }
    if (command == ".tables") {
      PrintSchema((*connection).get(), {});
      continue;
    }
    if (command.rfind(".schema", 0) == 0) {
      std::string requested = command.size() > 7 ? command.substr(7) : std::string();
      while (!requested.empty() &&
             std::isspace(static_cast<unsigned char>(requested.front())) != 0) {
        requested.erase(requested.begin());
      }
      PrintSchema((*connection).get(), requested);
      continue;
    }
    if (command.rfind(".read ", 0) == 0) {
      std::ifstream file(line.substr(6));
      if (!file) {
        std::cerr << "cannot read script file\n";
        continue;
      }
      std::string script((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
      std::istringstream statements(script);
      std::string statement;
      while (std::getline(statements, statement)) {
        if (!statement.empty()) {
          const tuplestone::Status status = (*connection)->Execute(statement);
          if (!status.ok()) std::cerr << status.ToString() << '\n';
        }
      }
      continue;
    }
    if (line.empty()) continue;
    const auto started = std::chrono::steady_clock::now();
    if (IsQuery(line)) {
      auto query = (*connection)->Query(line);
      if (!query.ok()) {
        std::cerr << query.status().ToString() << '\n';
      } else {
        PrintResult(&*query);
      }
    } else {
      const tuplestone::Status status = (*connection)->Execute(line);
      if (!status.ok()) std::cerr << status.ToString() << '\n';
    }
    if (timer) {
      const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                               std::chrono::steady_clock::now() - started)
                               .count();
      std::cout << "time: " << elapsed << " us\n";
    }
  }
  (void)(*database)->Close();
  return 0;
}
