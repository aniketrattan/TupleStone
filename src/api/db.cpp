#include "tuplestone/db.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

#include "wal/log_manager.h"

namespace tuplestone {
namespace {

using Row = std::vector<Value>;
struct Table {
  Schema schema;
  std::vector<Row> rows;
};
using Tables = std::map<std::string, Table>;

std::string Lower(std::string_view text) {
  std::string result(text);
  std::transform(result.begin(), result.end(), result.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return result;
}
std::string TrimLower(std::string_view text) {
  size_t begin = 0, end = text.size();
  while (begin < end && std::isspace(static_cast<unsigned char>(text[begin])) != 0) ++begin;
  while (end > begin &&
         (std::isspace(static_cast<unsigned char>(text[end - 1])) != 0 || text[end - 1] == ';'))
    --end;
  return Lower(text.substr(begin, end - begin));
}

class Writer {
 public:
  void U8(uint8_t value) { data_.push_back(static_cast<char>(value)); }
  void U32(uint32_t value) {
    for (int i = 0; i < 4; ++i) U8(static_cast<uint8_t>(value >> (i * 8)));
  }
  void U64(uint64_t value) {
    for (int i = 0; i < 8; ++i) U8(static_cast<uint8_t>(value >> (i * 8)));
  }
  void String(std::string_view value) {
    U32(static_cast<uint32_t>(value.size()));
    data_.append(value);
  }
  void ValueData(const Value& value) {
    U8(static_cast<uint8_t>(value.type()));
    switch (value.type()) {
      case TypeId::kNull:
        break;
      case TypeId::kBoolean:
        U8(value.AsBoolean() ? 1 : 0);
        break;
      case TypeId::kInteger:
        U64(static_cast<uint64_t>(value.AsInteger()));
        break;
      case TypeId::kReal: {
        uint64_t bits = 0;
        const double real = value.AsReal();
        std::memcpy(&bits, &real, sizeof(bits));
        U64(bits);
        break;
      }
      case TypeId::kText:
        String(value.AsText());
        break;
      case TypeId::kBlob:
        U32(static_cast<uint32_t>(value.AsBlob().size()));
        if (!value.AsBlob().empty())
          data_.append(reinterpret_cast<const char*>(value.AsBlob().data()), value.AsBlob().size());
        break;
    }
  }
  const std::string& data() const { return data_; }

 private:
  std::string data_;
};

class Reader {
 public:
  explicit Reader(std::string_view data) : data_(data) {}
  bool U8(uint8_t* out) {
    if (pos_ >= data_.size()) return false;
    *out = static_cast<uint8_t>(static_cast<unsigned char>(data_[pos_++]));
    return true;
  }
  bool U32(uint32_t* out) {
    uint32_t value = 0;
    for (int i = 0; i < 4; ++i) {
      uint8_t byte = 0;
      if (!U8(&byte)) return false;
      value |= static_cast<uint32_t>(byte) << (i * 8);
    }
    *out = value;
    return true;
  }
  bool U64(uint64_t* out) {
    uint64_t value = 0;
    for (int i = 0; i < 8; ++i) {
      uint8_t byte = 0;
      if (!U8(&byte)) return false;
      value |= static_cast<uint64_t>(byte) << (i * 8);
    }
    *out = value;
    return true;
  }
  bool String(std::string* out) {
    uint32_t size = 0;
    if (!U32(&size) || size > data_.size() - pos_) return false;
    out->assign(data_.substr(pos_, size));
    pos_ += size;
    return true;
  }
  bool ValueData(Value* out) {
    uint8_t tag = 0;
    if (!U8(&tag)) return false;
    switch (static_cast<TypeId>(tag)) {
      case TypeId::kNull:
        *out = Value();
        return true;
      case TypeId::kBoolean: {
        uint8_t b = 0;
        if (!U8(&b) || b > 1) return false;
        *out = Value(b != 0);
        return true;
      }
      case TypeId::kInteger: {
        uint64_t n = 0;
        if (!U64(&n)) return false;
        *out = Value(static_cast<int64_t>(n));
        return true;
      }
      case TypeId::kReal: {
        uint64_t bits = 0;
        if (!U64(&bits)) return false;
        double d = 0;
        std::memcpy(&d, &bits, sizeof(d));
        *out = Value(d);
        return true;
      }
      case TypeId::kText: {
        std::string s;
        if (!String(&s)) return false;
        *out = Value(std::move(s));
        return true;
      }
      case TypeId::kBlob: {
        uint32_t size = 0;
        if (!U32(&size) || size > data_.size() - pos_) return false;
        Value::Blob b(size);
        if (size != 0) std::copy_n(data_.data() + pos_, size, reinterpret_cast<char*>(b.data()));
        pos_ += size;
        *out = Value(std::move(b));
        return true;
      }
    }
    return false;
  }
  bool Done() const { return pos_ == data_.size(); }

 private:
  std::string_view data_;
  size_t pos_ = 0;
};

std::string SerializeTables(const Tables& tables) {
  Writer writer;
  writer.String("TUPLESTONE-DATA-2");
  writer.U32(static_cast<uint32_t>(tables.size()));
  for (const auto& [name, table] : tables) {
    writer.String(name);
    writer.U32(static_cast<uint32_t>(table.schema.size()));
    for (const Column& column : table.schema.columns()) {
      writer.String(column.name);
      writer.U8(static_cast<uint8_t>(column.type));
      writer.U8(column.nullable ? 1 : 0);
      writer.U8(column.primary_key ? 1 : 0);
      writer.U8(column.unique ? 1 : 0);
    }
    writer.U32(static_cast<uint32_t>(table.rows.size()));
    for (const Row& row : table.rows) {
      writer.U32(static_cast<uint32_t>(row.size()));
      for (const Value& value : row) writer.ValueData(value);
    }
  }
  return writer.data();
}

Status SaveTables(const std::string& path, const Tables& tables) {
  const std::string bytes = SerializeTables(tables);
  const std::string temp = path + ".tmp";
  {
    std::ofstream file(temp, std::ios::binary | std::ios::trunc);
    if (!file) return IoError("cannot create database file");
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!file) return IoError("cannot write database file");
    file.flush();
  }
  std::error_code ec;
  std::filesystem::rename(temp, path, ec);
  if (ec) {
    std::filesystem::remove(path, ec);
    ec.clear();
    std::filesystem::rename(temp, path, ec);
  }
  if (ec) return IoError("cannot replace database file: " + ec.message());
  return Status::Ok();
}

Status LoadTablesBytes(std::string_view bytes, Tables* tables) {
  if (tables == nullptr) return InvalidArgument("table output is null");
  Reader reader(bytes);
  std::string magic;
  if (!reader.String(&magic) || magic != "TUPLESTONE-DATA-2")
    return Incompatible("unsupported database format");
  uint32_t table_count = 0;
  if (!reader.U32(&table_count) || table_count > 100000) return Corruption("invalid table count");
  Tables result;
  for (uint32_t ti = 0; ti < table_count; ++ti) {
    std::string name;
    uint32_t column_count = 0;
    if (!reader.String(&name) || !reader.U32(&column_count) || column_count > 10000)
      return Corruption("invalid schema");
    std::vector<Column> columns;
    columns.reserve(column_count);
    for (uint32_t ci = 0; ci < column_count; ++ci) {
      Column column;
      uint8_t tag = 0, nullable = 0, primary = 0, unique = 0;
      if (!reader.String(&column.name) || !reader.U8(&tag) || !reader.U8(&nullable) ||
          !reader.U8(&primary) || !reader.U8(&unique))
        return Corruption("truncated schema");
      column.type = static_cast<TypeId>(tag);
      column.nullable = nullable != 0;
      column.primary_key = primary != 0;
      column.unique = unique != 0;
      if (column.type < TypeId::kBoolean || column.type > TypeId::kBlob)
        return Corruption("invalid column type");
      columns.push_back(std::move(column));
    }
    uint32_t row_count = 0;
    if (!reader.U32(&row_count) || row_count > 100000000) return Corruption("invalid row count");
    Table table{Schema(std::move(columns)), {}};
    table.rows.reserve(row_count);
    for (uint32_t ri = 0; ri < row_count; ++ri) {
      uint32_t width = 0;
      if (!reader.U32(&width) || width != table.schema.size())
        return Corruption("invalid row width");
      Row row;
      row.reserve(width);
      for (uint32_t ci = 0; ci < width; ++ci) {
        Value value;
        if (!reader.ValueData(&value)) return Corruption("truncated row");
        row.push_back(std::move(value));
      }
      table.rows.push_back(std::move(row));
    }
    result.emplace(Lower(name), std::move(table));
  }
  if (!reader.Done()) return Corruption("trailing database bytes");
  *tables = std::move(result);
  return Status::Ok();
}

Status LoadTables(const std::string& path, Tables* tables) {
  std::ifstream file(path, std::ios::binary);
  if (!file) return NotFound("database file does not exist");
  std::ostringstream stream;
  stream << file.rdbuf();
  return LoadTablesBytes(stream.str(), tables);
}

struct Token {
  enum class Kind {
    kWord,
    kNumber,
    kString,
    kOperator,
    kPunctuation,
    kParameter,
    kEnd
  } kind = Kind::kEnd;
  std::string text;
  size_t position = 0;
};

std::vector<Token> Tokenize(std::string_view sql, Status* status) {
  std::vector<Token> tokens;
  size_t i = 0;
  while (i < sql.size()) {
    if (std::isspace(static_cast<unsigned char>(sql[i])) != 0) {
      ++i;
      continue;
    }
    if (sql[i] == '-' && i + 1 < sql.size() && sql[i + 1] == '-') {
      i += 2;
      while (i < sql.size() && sql[i] != '\n') ++i;
      continue;
    }
    const size_t start = i;
    if (std::isalpha(static_cast<unsigned char>(sql[i])) != 0 || sql[i] == '_') {
      ++i;
      while (i < sql.size() &&
             (std::isalnum(static_cast<unsigned char>(sql[i])) != 0 || sql[i] == '_'))
        ++i;
      tokens.push_back({Token::Kind::kWord, Lower(sql.substr(start, i - start)), start});
      continue;
    }
    if (std::isdigit(static_cast<unsigned char>(sql[i])) != 0 ||
        (sql[i] == '.' && i + 1 < sql.size() &&
         std::isdigit(static_cast<unsigned char>(sql[i + 1])) != 0)) {
      ++i;
      while (i < sql.size() &&
             (std::isdigit(static_cast<unsigned char>(sql[i])) != 0 || sql[i] == '.'))
        ++i;
      tokens.push_back({Token::Kind::kNumber, std::string(sql.substr(start, i - start)), start});
      continue;
    }
    if (sql[i] == '\'' || sql[i] == '"') {
      const char quote = sql[i++];
      std::string value;
      bool closed = false;
      while (i < sql.size()) {
        if (sql[i] == quote) {
          if (i + 1 < sql.size() && sql[i + 1] == quote) {
            value.push_back(quote);
            i += 2;
          } else {
            ++i;
            closed = true;
            break;
          }
        } else
          value.push_back(sql[i++]);
      }
      if (!closed) {
        *status = SyntaxError("unterminated quoted literal");
        return {};
      }
      tokens.push_back({quote == '\'' ? Token::Kind::kString : Token::Kind::kWord,
                        quote == '\'' ? value : Lower(value), start});
      continue;
    }
    if (sql[i] == '?') {
      ++i;
      tokens.push_back({Token::Kind::kParameter, "?", start});
      continue;
    }
    if (i + 1 < sql.size() && (sql.substr(i, 2) == "<=" || sql.substr(i, 2) == ">=" ||
                               sql.substr(i, 2) == "!=" || sql.substr(i, 2) == "<>")) {
      tokens.push_back({Token::Kind::kOperator, std::string(sql.substr(i, 2)), start});
      i += 2;
      continue;
    }
    if (std::string_view("=<>+-*/%!").find(sql[i]) != std::string_view::npos) {
      tokens.push_back({Token::Kind::kOperator, std::string(1, sql[i++]), start});
      continue;
    }
    if (std::string_view("(),.;").find(sql[i]) != std::string_view::npos) {
      tokens.push_back({Token::Kind::kPunctuation, std::string(1, sql[i++]), start});
      continue;
    }
    *status = SyntaxError("unexpected character in SQL");
    return {};
  }
  tokens.push_back({Token::Kind::kEnd, {}, sql.size()});
  return tokens;
}

struct Expr {
  enum class Kind { kLiteral, kColumn, kUnary, kBinary, kIsNull, kFunction } kind = Kind::kLiteral;
  Value literal;
  std::string name;
  std::string op;
  size_t parameter_index = 0;
  std::vector<std::shared_ptr<Expr>> args;
};
using ExprPtr = std::shared_ptr<Expr>;

class Parser {
 public:
  explicit Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}
  const Token& Peek() const { return tokens_[index_]; }
  const Token& Peek(size_t offset) const {
    const size_t position = std::min(index_ + offset, tokens_.size() - 1);
    return tokens_[position];
  }
  bool Accept(std::string_view text) {
    if (text != ")") closed_paren_ = false;
    if (text == ")" && closed_paren_ && Peek().text != ")") {
      closed_paren_ = false;
      return true;
    }
    if (Peek().text == text) {
      ++index_;
      return true;
    }
    return false;
  }
  bool Expect(std::string_view text, Status* status) {
    if (Accept(text)) {
      if (text == ")") closed_paren_ = true;
      return true;
    }
    *status = SyntaxError("expected '" + std::string(text) + "'");
    return false;
  }
  std::string Identifier(Status* status) {
    if (Peek().kind != Token::Kind::kWord) {
      *status = SyntaxError("expected identifier");
      return {};
    }
    return tokens_[index_++].text;
  }
  ExprPtr Expression(Status* status) { return ParseOr(status); }
  ExprPtr ParseOr(Status* status) {
    ExprPtr left = ParseAnd(status);
    while (left && Accept("or")) left = Binary("or", left, ParseAnd(status));
    return left;
  }
  ExprPtr ParseAnd(Status* status) {
    ExprPtr left = ParseCompare(status);
    while (left && Accept("and")) left = Binary("and", left, ParseCompare(status));
    return left;
  }
  ExprPtr ParseCompare(Status* status) {
    ExprPtr left = ParseLike(status);
    if (!left) return nullptr;
    if (Peek().kind == Token::Kind::kOperator || Peek().text == "is" || Peek().text == "in") {
      std::string op = Peek().text;
      ++index_;
      if (op == "is") {
        const bool neg = Accept("not");
        if (!Expect("null", status)) return nullptr;
        auto out = std::make_shared<Expr>();
        out->kind = Expr::Kind::kIsNull;
        out->op = neg ? "is not null" : "is null";
        out->args = {left};
        return out;
      }
      if (op == "in") {
        if (!Expect("(", status)) return nullptr;
        auto out = std::make_shared<Expr>();
        out->kind = Expr::Kind::kFunction;
        out->name = "in";
        out->args.push_back(left);
        while (!Accept(")")) {
          out->args.push_back(Expression(status));
          if (!Accept(",") && !Expect(")", status)) return nullptr;
        }
        return out;
      }
      return Binary(op, left, ParseAdd(status));
    }
    return left;
  }
  ExprPtr ParseLike(Status* status) {
    ExprPtr left = ParseAdd(status);
    if (left && Accept("like")) return Binary("like", left, ParseAdd(status));
    return left;
  }
  ExprPtr ParseAdd(Status* status) {
    ExprPtr left = ParseMul(status);
    while (Peek().text == "+" || Peek().text == "-") {
      std::string op = Peek().text;
      ++index_;
      left = Binary(op, left, ParseMul(status));
    }
    return left;
  }
  ExprPtr ParseMul(Status* status) {
    ExprPtr left = ParseUnary(status);
    while (Peek().text == "*" || Peek().text == "/" || Peek().text == "%") {
      std::string op = Peek().text;
      ++index_;
      left = Binary(op, left, ParseUnary(status));
    }
    return left;
  }
  ExprPtr ParseUnary(Status* status) {
    if (Accept("not") || Accept("-") || Accept("+")) {
      const std::string op = tokens_[index_ - 1].text;
      auto out = std::make_shared<Expr>();
      out->kind = Expr::Kind::kUnary;
      out->op = op;
      out->args = {ParseUnary(status)};
      return out;
    }
    return ParsePrimary(status);
  }
  ExprPtr ParsePrimary(Status* status) {
    if (Accept("(")) {
      ExprPtr value = Expression(status);
      if (!Expect(")", status)) return nullptr;
      return value;
    }
    if (Peek().kind == Token::Kind::kNumber) {
      const std::string text = Peek().text;
      ++index_;
      auto out = std::make_shared<Expr>();
      out->kind = Expr::Kind::kLiteral;
      try {
        out->literal = text.find('.') == std::string::npos
                           ? Value(static_cast<int64_t>(std::stoll(text)))
                           : Value(std::stod(text));
      } catch (...) {
        *status = OutOfRange("invalid numeric literal");
        return nullptr;
      }
      return out;
    }
    if (Peek().kind == Token::Kind::kString) {
      auto out = std::make_shared<Expr>();
      out->kind = Expr::Kind::kLiteral;
      out->literal = Value(Peek().text);
      ++index_;
      return out;
    }
    if (Peek().kind == Token::Kind::kParameter) {
      ++index_;
      auto out = std::make_shared<Expr>();
      out->kind = Expr::Kind::kColumn;
      out->name = "?";
      out->parameter_index = parameter_count_++;
      return out;
    }
    if (Peek().kind == Token::Kind::kWord) {
      std::string name = Identifier(status);
      if (name == "null" || name == "true" || name == "false") {
        auto out = std::make_shared<Expr>();
        out->kind = Expr::Kind::kLiteral;
        out->literal = name == "null" ? Value() : Value(name == "true");
        return out;
      }
      if (Accept(".")) name += "." + Identifier(status);
      if (Accept("(")) {
        auto out = std::make_shared<Expr>();
        out->kind = Expr::Kind::kFunction;
        out->name = name;
        if (!Accept(")")) {
          while (true) {
            if (Accept("*")) {
              auto star = std::make_shared<Expr>();
              star->kind = Expr::Kind::kColumn;
              star->name = "*";
              out->args.push_back(star);
            } else
              out->args.push_back(Expression(status));
            if (Accept(")")) break;
            if (!Expect(",", status)) return nullptr;
          }
        }
        return out;
      }
      auto out = std::make_shared<Expr>();
      out->kind = Expr::Kind::kColumn;
      out->name = name;
      return out;
    }
    *status = SyntaxError("expected expression");
    return nullptr;
  }

 private:
  ExprPtr Binary(std::string op, ExprPtr left, ExprPtr right) {
    auto out = std::make_shared<Expr>();
    out->kind = Expr::Kind::kBinary;
    out->op = std::move(op);
    out->args = {std::move(left), std::move(right)};
    return out;
  }
  std::vector<Token> tokens_;
  size_t index_ = 0;
  size_t parameter_count_ = 0;
  bool closed_paren_ = false;
};

struct EvalContext {
  const Schema* schema = nullptr;
  const Row* row = nullptr;
  const std::vector<Value>* params = nullptr;
  size_t next_param = 0;
};

std::optional<Value> Eval(const ExprPtr& expr, EvalContext* context, Status* status) {
  if (!expr) return std::nullopt;
  if (expr->kind == Expr::Kind::kLiteral) return expr->literal;
  if (expr->kind == Expr::Kind::kColumn && expr->name == "?") {
    if (context->params == nullptr || expr->parameter_index >= context->params->size()) {
      *status = InvalidArgument("unbound parameter");
      return std::nullopt;
    }
    return (*context->params)[expr->parameter_index];
  }
  if (expr->kind == Expr::Kind::kColumn) {
    if (expr->name == "?") {
      if (context->params == nullptr || context->next_param >= context->params->size()) {
        *status = InvalidArgument("unbound parameter");
        return std::nullopt;
      }
      return (*context->params)[context->next_param++];
    }
    const int index = context->schema == nullptr ? -1 : context->schema->Find(Lower(expr->name));
    if (index < 0 || context->row == nullptr ||
        static_cast<size_t>(index) >= context->row->size()) {
      *status = NotFound("unknown column '" + expr->name + "'");
      return std::nullopt;
    }
    return (*context->row)[static_cast<size_t>(index)];
  }
  if (expr->kind == Expr::Kind::kIsNull) {
    auto value = Eval(expr->args[0], context, status);
    if (!value.has_value()) return std::nullopt;
    return Value(expr->op == "is null" ? value->IsNull() : !value->IsNull());
  }
  if (expr->kind == Expr::Kind::kUnary) {
    auto value = Eval(expr->args[0], context, status);
    if (!value.has_value()) return std::nullopt;
    if (expr->op == "not") {
      if (value->IsNull()) return Value();
      if (value->type() != TypeId::kBoolean) {
        *status = TypeError("NOT requires BOOLEAN");
        return std::nullopt;
      }
      return Value(!value->AsBoolean());
    }
    if (value->type() != TypeId::kInteger && value->type() != TypeId::kReal) {
      *status = TypeError("unary operator requires a number");
      return std::nullopt;
    }
    return expr->op == "+" ? *value
                           : (value->type() == TypeId::kInteger ? Value(-value->AsInteger())
                                                                : Value(-value->AsReal()));
  }
  if (expr->kind == Expr::Kind::kFunction) {
    if (expr->name == "in") {
      auto needle = Eval(expr->args[0], context, status);
      if (!needle.has_value() || needle->IsNull()) return Value();
      for (size_t i = 1; i < expr->args.size(); ++i) {
        auto candidate = Eval(expr->args[i], context, status);
        if (!candidate.has_value()) return std::nullopt;
        if (needle->Equals(*candidate).value_or(false)) return Value(true);
      }
      return Value(false);
    }
    if (expr->name == "coalesce") {
      for (const ExprPtr& arg : expr->args) {
        auto value = Eval(arg, context, status);
        if (!value.has_value()) return std::nullopt;
        if (!value->IsNull()) return value;
      }
      return Value();
    }
    if (expr->args.empty()) {
      *status = InvalidArgument("function requires an argument");
      return std::nullopt;
    }
    auto value = Eval(expr->args[0], context, status);
    if (!value.has_value() || value->IsNull()) return Value();
    if (expr->name == "abs") {
      if (value->type() == TypeId::kInteger)
        return Value(value->AsInteger() < 0 ? -value->AsInteger() : value->AsInteger());
      if (value->type() == TypeId::kReal) return Value(std::fabs(value->AsReal()));
    }
    if (expr->name == "length")
      return Value(static_cast<int64_t>(value->type() == TypeId::kText ? value->AsText().size()
                                                                       : value->ToString().size()));
    if (expr->name == "upper" || expr->name == "lower") {
      if (value->type() != TypeId::kText) {
        *status = TypeError("text function requires TEXT");
        return std::nullopt;
      }
      std::string text = value->AsText();
      std::transform(text.begin(), text.end(), text.begin(),
                     [upper = expr->name == "upper"](unsigned char c) {
                       return static_cast<char>(upper ? std::toupper(c) : std::tolower(c));
                     });
      return Value(std::move(text));
    }
    *status = NotSupported("function '" + expr->name + "' is not implemented");
    return std::nullopt;
  }
  auto left = Eval(expr->args[0], context, status);
  auto right = Eval(expr->args[1], context, status);
  if (!left.has_value() || !right.has_value()) return std::nullopt;
  if (expr->op == "and" || expr->op == "or") {
    if (left->type() != TypeId::kBoolean && !left->IsNull()) {
      *status = TypeError("logical operator requires BOOLEAN");
      return std::nullopt;
    }
    if (right->type() != TypeId::kBoolean && !right->IsNull()) {
      *status = TypeError("logical operator requires BOOLEAN");
      return std::nullopt;
    }
    const bool lv = !left->IsNull() && left->AsBoolean(),
               rv = !right->IsNull() && right->AsBoolean();
    if (expr->op == "and") {
      if ((!left->IsNull() && !lv) || (!right->IsNull() && !rv)) return Value(false);
      return Value(left->IsNull() || right->IsNull() ? Value() : Value(true));
    }
    if ((!left->IsNull() && lv) || (!right->IsNull() && rv)) return Value(true);
    return Value(left->IsNull() || right->IsNull() ? Value() : Value(false));
  }
  if (expr->op == "=" || expr->op == "==" || expr->op == "!=" || expr->op == "<>") {
    auto equal = left->Equals(*right);
    if (!equal.has_value()) return Value();
    return Value(expr->op == "=" || expr->op == "==" ? *equal : !*equal);
  }
  if (expr->op == "like") {
    if (left->IsNull() || right->IsNull()) return Value();
    if (left->type() != TypeId::kText || right->type() != TypeId::kText) {
      *status = TypeError("LIKE requires TEXT values");
      return std::nullopt;
    }
    const std::string& value = left->AsText();
    const std::string& pattern = right->AsText();
    size_t value_index = 0;
    size_t pattern_index = 0;
    size_t wildcard = std::string::npos;
    size_t wildcard_value = 0;
    while (value_index < value.size()) {
      if (pattern_index < pattern.size() &&
          (pattern[pattern_index] == '_' || pattern[pattern_index] == value[value_index])) {
        ++pattern_index;
        ++value_index;
      } else if (pattern_index < pattern.size() && pattern[pattern_index] == '%') {
        wildcard = pattern_index++;
        wildcard_value = value_index;
      } else if (wildcard != std::string::npos) {
        pattern_index = wildcard + 1;
        value_index = ++wildcard_value;
      } else
        return Value(false);
    }
    while (pattern_index < pattern.size() && pattern[pattern_index] == '%') ++pattern_index;
    return Value(pattern_index == pattern.size());
  }
  if (expr->op == "<" || expr->op == "<=" || expr->op == ">" || expr->op == ">=") {
    auto cmp = left->Compare(*right);
    if (!cmp.has_value()) {
      *status = TypeError("values are not comparable");
      return std::nullopt;
    }
    if (expr->op == "<") return Value(*cmp < 0);
    if (expr->op == "<=") return Value(*cmp <= 0);
    if (expr->op == ">") return Value(*cmp > 0);
    return Value(*cmp >= 0);
  }
  if (left->IsNull() || right->IsNull() ||
      (left->type() != TypeId::kInteger && left->type() != TypeId::kReal) ||
      (right->type() != TypeId::kInteger && right->type() != TypeId::kReal)) {
    *status = TypeError("arithmetic requires non-NULL numbers");
    return std::nullopt;
  }
  const bool real = left->type() == TypeId::kReal || right->type() == TypeId::kReal;
  const double a =
      left->type() == TypeId::kReal ? left->AsReal() : static_cast<double>(left->AsInteger());
  const double b =
      right->type() == TypeId::kReal ? right->AsReal() : static_cast<double>(right->AsInteger());
  if ((expr->op == "/" || expr->op == "%") && b == 0) {
    *status = OutOfRange("division by zero");
    return std::nullopt;
  }
  if (real || expr->op == "/") {
    if (expr->op == "+") return Value(a + b);
    if (expr->op == "-") return Value(a - b);
    if (expr->op == "*") return Value(a * b);
    return Value(a / b);
  }
  const int64_t ia = left->AsInteger(), ib = right->AsInteger();
  if (expr->op == "+") return Value(ia + ib);
  if (expr->op == "-") return Value(ia - ib);
  if (expr->op == "*") return Value(ia * ib);
  return Value(ia % ib);
}

bool Truthy(const Value& value) {
  return !value.IsNull() && value.type() == TypeId::kBoolean && value.AsBoolean();
}
Status ValidateRow(const Table& table, const Row& row, const Row* ignored = nullptr) {
  if (row.size() != table.schema.size()) return InvalidArgument("wrong number of values");
  for (size_t i = 0; i < row.size(); ++i) {
    const Column& column = table.schema[i];
    if (row[i].IsNull() && !column.nullable)
      return InvalidArgument("column '" + column.name + "' may not be NULL");
    if (!row[i].IsNull() && row[i].type() != column.type &&
        !((row[i].type() == TypeId::kInteger || row[i].type() == TypeId::kReal) &&
          (column.type == TypeId::kInteger || column.type == TypeId::kReal)))
      return TypeError("value for column '" + column.name + "' has the wrong type");
  }
  for (size_t i = 0; i < row.size(); ++i)
    if (table.schema[i].primary_key || table.schema[i].unique)
      for (const Row& existing : table.rows) {
        if (&existing == ignored) continue;
        if (existing[i] == row[i] && !row[i].IsNull())
          return AlreadyExists("duplicate value for column '" + table.schema[i].name + "'");
      }
  return Status::Ok();
}

void RefreshCatalog(Tables* tables) {
  const std::vector<Column> table_columns = {
      {"table_id", TypeId::kInteger, false, true, true},
      {"name", TypeId::kText, false, false, true},
      {"first_page", TypeId::kInteger, false, false, false},
      {"tuple_count", TypeId::kInteger, false, false, false}};
  const std::vector<Column> column_columns = {
      {"table_id", TypeId::kInteger, false, false, false},
      {"ordinal", TypeId::kInteger, false, false, false},
      {"name", TypeId::kText, false, false, false},
      {"type", TypeId::kInteger, false, false, false},
      {"nullable", TypeId::kBoolean, false, false, false},
      {"is_primary", TypeId::kBoolean, false, false, false}};
  const std::vector<Column> index_columns = {
      {"index_id", TypeId::kInteger, false, true, true},
      {"table_id", TypeId::kInteger, false, false, false},
      {"name", TypeId::kText, false, false, false},
      {"root_page", TypeId::kInteger, false, false, false},
      {"is_unique", TypeId::kBoolean, false, false, false},
      {"column_ordinals", TypeId::kText, false, false, false}};
  (*tables)["tuplestone_tables"] = Table{Schema(table_columns), {}};
  (*tables)["tuplestone_columns"] = Table{Schema(column_columns), {}};
  (*tables)["tuplestone_indexes"] = Table{Schema(index_columns), {}};
  int64_t table_id = 1;
  for (const auto& [name, table] : *tables) {
    if (name.rfind("tuplestone_", 0) == 0) continue;
    (*tables)["tuplestone_tables"].rows.push_back({Value(table_id), Value(name), Value(int64_t{0}),
                                                   Value(static_cast<int64_t>(table.rows.size()))});
    for (size_t ordinal = 0; ordinal < table.schema.size(); ++ordinal) {
      const Column& column = table.schema[ordinal];
      (*tables)["tuplestone_columns"].rows.push_back(
          {Value(table_id), Value(static_cast<int64_t>(ordinal)), Value(column.name),
           Value(static_cast<int64_t>(static_cast<uint8_t>(column.type))), Value(column.nullable),
           Value(column.primary_key)});
    }
    ++table_id;
  }
}

}  // namespace

class Database::Impl {
 public:
  std::string path;
  Options options;
  mutable std::mutex mutex;
  Tables tables;
  LogManager wal;
  txn_id_t next_wal_txn = 1;
  uint64_t generation = 0;
  bool closed = false;
};
class Connection::Impl {
 public:
  explicit Impl(std::shared_ptr<Database::Impl> state) : db(std::move(state)) {}
  std::shared_ptr<Database::Impl> db;
  std::shared_ptr<Tables> transaction_tables;
  uint64_t transaction_generation = 0;
  StatusOr<ResultSet> ExecuteQuery(std::string_view sql, const std::vector<Value>& params);
};
class ResultSet::Impl {
 public:
  Schema schema;
  std::vector<Row> rows;
  size_t index = 0;
  bool closed = false;
  Impl* impl_ = this;
  operator StatusOr<ResultSet>() const;
};
class PreparedStatement::Impl {
 public:
  Connection::Impl* connection = nullptr;
  std::string sql;
  std::vector<Value> bindings;
};
class Transaction::Impl {
 public:
  Connection::Impl* connection = nullptr;
  bool finished = false;
};

namespace {

constexpr std::string_view kSnapshotPrefix = "TUPLESTONE-SNAPSHOT-1\n";

Status PersistCommittedTables(Database::Impl* database, const Tables& tables) {
  if (database == nullptr) return InvalidArgument("database is null");
  const txn_id_t txn_id = database->next_wal_txn++;
  const std::string serialized = SerializeTables(tables);
  const std::string payload = std::string(kSnapshotPrefix) + serialized;
  auto begin = database->wal.Append(txn_id, kInvalidLsn, LogRecordType::kBegin);
  if (!begin.ok()) return begin.status();
  auto update = database->wal.Append(txn_id, *begin, LogRecordType::kUpdate, payload);
  if (!update.ok()) return update.status();
  auto commit = database->wal.Append(txn_id, *update, LogRecordType::kCommit);
  if (!commit.ok()) return commit.status();
  if (database->options.sync_on_commit) {
    const Status status = database->wal.Flush(*commit);
    if (!status.ok()) return status;
  }
  return SaveTables(database->path, tables);
}

Status RecoverCommittedTables(Database::Impl* database) {
  if (database == nullptr) return InvalidArgument("database is null");
  auto records = database->wal.ReadAll();
  if (!records.ok()) return records.status();
  std::map<txn_id_t, std::string> snapshots;
  txn_id_t largest_txn = 0;
  bool recovered = false;
  for (const LogRecord& record : *records) {
    largest_txn = std::max(largest_txn, record.txn_id);
    if (record.type == LogRecordType::kUpdate && record.payload.starts_with(kSnapshotPrefix)) {
      snapshots[record.txn_id] = record.payload.substr(kSnapshotPrefix.size());
    } else if (record.type == LogRecordType::kCommit) {
      const auto snapshot = snapshots.find(record.txn_id);
      if (snapshot == snapshots.end()) continue;
      Tables committed;
      const Status status = LoadTablesBytes(snapshot->second, &committed);
      if (!status.ok())
        return Corruption(std::string("committed WAL snapshot is invalid: ") +
                          std::string(status.message()));
      database->tables = std::move(committed);
      recovered = true;
      snapshots.erase(snapshot);
    }
  }
  database->next_wal_txn = std::max<txn_id_t>(database->next_wal_txn, largest_txn + 1);
  if (recovered) {
    const Status status = SaveTables(database->path, database->tables);
    if (!status.ok()) return status;
    ++database->generation;
  }
  return Status::Ok();
}

}  // namespace

ResultSet::ResultSet() = default;
ResultSet::ResultSet(const Impl& source) : impl_(std::make_unique<Impl>(source)) {
  impl_->impl_ = impl_.get();
}
ResultSet::Impl::operator StatusOr<ResultSet>() const {
  return ResultSet(*this);
}
ResultSet::ResultSet(ResultSet&&) noexcept = default;
ResultSet& ResultSet::operator=(ResultSet&&) noexcept = default;
ResultSet::~ResultSet() = default;
const Schema& ResultSet::schema() const {
  static const Schema empty;
  return impl_ == nullptr ? empty : impl_->schema;
}
StatusOr<bool> ResultSet::Next() {
  if (impl_ == nullptr || impl_->closed) return InvalidArgument("result set is closed");
  if (impl_->index >= impl_->rows.size()) {
    impl_->closed = true;
    return false;
  }
  ++impl_->index;
  return true;
}
const Value& ResultSet::Get(int column) const {
  static const Value null_value;
  if (impl_ == nullptr || impl_->index == 0 || impl_->index > impl_->rows.size() || column < 0 ||
      static_cast<size_t>(column) >= impl_->rows[impl_->index - 1].size())
    return null_value;
  return impl_->rows[impl_->index - 1][static_cast<size_t>(column)];
}
const Value& ResultSet::Get(std::string_view column_name) const {
  return Get(schema().Find(Lower(column_name)));
}
Status ResultSet::Close() {
  if (impl_ != nullptr) impl_->closed = true;
  return Status::Ok();
}
PreparedStatement::PreparedStatement() = default;
PreparedStatement::PreparedStatement(PreparedStatement&&) noexcept = default;
PreparedStatement& PreparedStatement::operator=(PreparedStatement&&) noexcept = default;
PreparedStatement::~PreparedStatement() = default;
Status PreparedStatement::Bind(int index, const Value& value) {
  if (impl_ == nullptr || index <= 0) return InvalidArgument("parameter index starts at 1");
  if (impl_->bindings.size() < static_cast<size_t>(index))
    impl_->bindings.resize(static_cast<size_t>(index));
  impl_->bindings[static_cast<size_t>(index - 1)] = value;
  return Status::Ok();
}
StatusOr<ResultSet> PreparedStatement::Execute() {
  if (impl_ == nullptr || impl_->connection == nullptr)
    return InvalidArgument("invalid prepared statement");
  return impl_->connection->ExecuteQuery(impl_->sql, impl_->bindings);
}
Status PreparedStatement::Reset() {
  if (impl_ == nullptr) return InvalidArgument("invalid prepared statement");
  impl_->bindings.clear();
  return Status::Ok();
}
Transaction::Transaction() = default;
Transaction::Transaction(Transaction&&) noexcept = default;
Transaction& Transaction::operator=(Transaction&&) noexcept = default;
Transaction::~Transaction() {
  if (impl_ != nullptr && !impl_->finished) (void)Rollback();
}
Status Transaction::Commit() {
  if (impl_ == nullptr || impl_->connection == nullptr || impl_->finished)
    return InvalidArgument("transaction is finished");
  auto& connection = *impl_->connection;
  auto& db = *connection.db;
  std::lock_guard lock(db.mutex);
  if (db.generation != connection.transaction_generation) {
    impl_->finished = true;
    connection.transaction_tables.reset();
    return SerializationFailure("transaction conflict");
  }
  RefreshCatalog(connection.transaction_tables.get());
  db.tables = *connection.transaction_tables;
  ++db.generation;
  const Status status = PersistCommittedTables(&db, db.tables);
  connection.transaction_tables.reset();
  impl_->finished = true;
  return status;
}
Status Transaction::Rollback() {
  if (impl_ == nullptr || impl_->finished) return Status::Ok();
  impl_->connection->transaction_tables.reset();
  impl_->finished = true;
  return Status::Ok();
}
Connection::Connection() = default;
Connection::~Connection() = default;

namespace {
Tables& WorkingTables(Connection::Impl* connection) {
  Tables& tables =
      connection->transaction_tables ? *connection->transaction_tables : connection->db->tables;
  RefreshCatalog(&tables);
  return tables;
}

StatusOr<ResultSet> ExecuteSql(Connection::Impl* connection, std::string_view sql,
                               const std::vector<Value>& params) {
  Status tokenize_status;
  std::vector<Token> tokens = Tokenize(sql, &tokenize_status);
  if (!tokenize_status.ok()) return tokenize_status;
  Parser parser(std::move(tokens));
  const std::string command = parser.Peek().text;
  Tables& tables = WorkingTables(connection);
  auto result = std::make_shared<ResultSet::Impl>();
  auto persist = [&]() -> Status {
    if (connection->transaction_tables) return Status::Ok();
    std::lock_guard lock(connection->db->mutex);
    ++connection->db->generation;
    return PersistCommittedTables(connection->db.get(), connection->db->tables);
  };
  if (command == "explain") {
    result->schema = Schema({Column{"plan", TypeId::kText, false, false, false}});
    result->rows.push_back({Value("SeqScan (rule-based planner)")});
    return *result;
  }
  if (command == "select" && Lower(sql).find("count(*)") != std::string::npos) {
    const std::string lowered_sql = Lower(sql);
    const size_t count_pos = lowered_sql.find("count(*)");
    std::string expanded(sql);
    expanded.replace(count_pos, 8, "*");
    auto filtered = ExecuteSql(connection, expanded, params);
    if (filtered.ok()) {
      result->schema = Schema({Column{"count", TypeId::kInteger, false, false, false}});
      result->rows.push_back({Value(static_cast<int64_t>(filtered->impl_->rows.size()))});
      return *result;
    }
    const std::string lowered = Lower(sql);
    const size_t from = lowered.find(" from ");
    if (from == std::string::npos) return SyntaxError("COUNT requires FROM");
    size_t begin = from + 6;
    while (begin < lowered.size() && std::isspace(static_cast<unsigned char>(lowered[begin])) != 0)
      ++begin;
    size_t end = begin;
    while (end < lowered.size() && std::isalnum(static_cast<unsigned char>(lowered[end])) != 0)
      ++end;
    const auto table_it = tables.find(lowered.substr(begin, end - begin));
    if (table_it == tables.end()) return NotFound("table does not exist");
    result->schema = Schema({Column{"count", TypeId::kInteger, false, false, false}});
    result->rows.push_back({Value(static_cast<int64_t>(table_it->second.rows.size()))});
    return *result;
  }
  if (command == "create" && (parser.Peek(1).text == "index" ||
                              (parser.Peek(1).text == "unique" && parser.Peek(2).text == "index")))
    return NotSupported("index DDL requires the page-backed index milestone");
  if (command == "drop" && parser.Peek(1).text == "index")
    return NotSupported("index DDL requires the page-backed index milestone");
  if (command == "create" && parser.Accept("create")) {
    if (!parser.Expect("table", &tokenize_status)) return tokenize_status;
    const bool if_not_exists =
        parser.Accept("if") && parser.Accept("not") && parser.Accept("exists");
    Status status;
    const std::string name = Lower(parser.Identifier(&status));
    if (!status.ok()) return status;
    if (tables.find(name) != tables.end())
      return if_not_exists ? StatusOr<ResultSet>(*result)
                           : StatusOr<ResultSet>(AlreadyExists("table already exists"));
    if (!parser.Expect("(", &status)) return status;
    std::vector<Column> columns;
    while (!parser.Accept(")")) {
      Column column;
      column.name = parser.Identifier(&status);
      if (!status.ok()) return status;
      const std::string type = parser.Identifier(&status);
      if (!status.ok()) return status;
      if (type == "integer")
        column.type = TypeId::kInteger;
      else if (type == "real")
        column.type = TypeId::kReal;
      else if (type == "text")
        column.type = TypeId::kText;
      else if (type == "boolean")
        column.type = TypeId::kBoolean;
      else if (type == "blob")
        column.type = TypeId::kBlob;
      else
        return TypeError("unknown column type");
      if (parser.Accept("not")) {
        if (!parser.Expect("null", &status)) return status;
        column.nullable = false;
      }
      if (parser.Accept("primary")) {
        if (!parser.Expect("key", &status)) return status;
        column.primary_key = true;
        column.nullable = false;
      }
      if (parser.Accept("unique")) column.unique = true;
      columns.push_back(std::move(column));
      if (!parser.Accept(",") && !parser.Expect(")", &status)) return status;
    }
    tables.emplace(name, Table{Schema(std::move(columns)), {}});
    return persist();
  }
  if (command == "drop" && parser.Accept("drop")) {
    Status status;
    if (!parser.Expect("table", &status)) return status;
    const std::string name = Lower(parser.Identifier(&status));
    if (!status.ok()) return status;
    auto it = tables.find(name);
    if (it == tables.end()) return NotFound("table does not exist");
    tables.erase(it);
    return persist();
  }
  if (command == "insert" && parser.Accept("insert")) {
    Status status;
    if (!parser.Expect("into", &status)) return status;
    const std::string name = Lower(parser.Identifier(&status));
    if (!status.ok()) return status;
    auto table_it = tables.find(name);
    if (table_it == tables.end()) return NotFound("table does not exist");
    Table& table = table_it->second;
    std::vector<int> target_columns;
    if (parser.Accept("(")) {
      while (!parser.Accept(")")) {
        const int index = table.schema.Find(parser.Identifier(&status));
        if (index < 0) return NotFound("unknown column");
        target_columns.push_back(index);
        if (!parser.Accept(",") && !parser.Expect(")", &status)) return status;
      }
    } else {
      for (size_t i = 0; i < table.schema.size(); ++i)
        target_columns.push_back(static_cast<int>(i));
    }
    if (!parser.Expect("values", &status)) return status;
    size_t param_index = 0;
    while (true) {
      if (!parser.Expect("(", &status)) return status;
      Row row(table.schema.size());
      for (size_t i = 0; i < target_columns.size(); ++i) {
        ExprPtr expr = parser.Expression(&status);
        if (!status.ok()) return status;
        EvalContext context{&table.schema, nullptr, &params, param_index};
        auto value = Eval(expr, &context, &status);
        param_index = context.next_param;
        if (!status.ok() || !value.has_value()) return status;
        row[static_cast<size_t>(target_columns[i])] = *value;
        if (i + 1 < target_columns.size() && !parser.Expect(",", &status)) return status;
      }
      if (!parser.Expect(")", &status)) return status;
      TUPLESTONE_RETURN_IF_ERROR(ValidateRow(table, row));
      table.rows.push_back(std::move(row));
      if (!parser.Accept(",")) break;
    }
    return persist();
  }
  if (command == "update" && parser.Accept("update")) {
    Status status;
    const std::string name = Lower(parser.Identifier(&status));
    if (!status.ok()) return status;
    auto it = tables.find(name);
    if (it == tables.end()) return NotFound("table does not exist");
    Table& table = it->second;
    if (!parser.Expect("set", &status)) return status;
    std::vector<std::pair<int, ExprPtr>> assignments;
    while (true) {
      const int col = table.schema.Find(parser.Identifier(&status));
      if (!status.ok() || col < 0) return NotFound("unknown column");
      if (!parser.Expect("=", &status)) return status;
      assignments.emplace_back(col, parser.Expression(&status));
      if (!status.ok()) return status;
      if (!parser.Accept(",")) break;
    }
    ExprPtr predicate;
    if (parser.Accept("where")) predicate = parser.Expression(&status);
    for (Row& row : table.rows) {
      EvalContext context{&table.schema, &row, &params, 0};
      auto selected =
          predicate ? Eval(predicate, &context, &status) : std::optional<Value>(Value(true));
      if (!status.ok()) return status;
      if (!selected.has_value() || !Truthy(*selected)) continue;
      Row next = row;
      context.next_param = 0;
      for (const auto& [col, expr] : assignments) {
        context.row = &row;
        auto value = Eval(expr, &context, &status);
        if (!status.ok() || !value.has_value()) return status;
        next[static_cast<size_t>(col)] = *value;
      }
      TUPLESTONE_RETURN_IF_ERROR(ValidateRow(table, next, &row));
      row = std::move(next);
    }
    return persist();
  }
  if (command == "delete" && parser.Accept("delete")) {
    Status status;
    if (!parser.Expect("from", &status)) return status;
    const std::string name = Lower(parser.Identifier(&status));
    if (!status.ok()) return status;
    auto it = tables.find(name);
    if (it == tables.end()) return NotFound("table does not exist");
    Table& table = it->second;
    ExprPtr predicate;
    if (parser.Accept("where")) predicate = parser.Expression(&status);
    std::vector<Row> kept;
    for (const Row& row : table.rows) {
      EvalContext context{&table.schema, &row, &params, 0};
      auto selected =
          predicate ? Eval(predicate, &context, &status) : std::optional<Value>(Value(true));
      if (!status.ok()) return status;
      if (!selected.has_value() || !Truthy(*selected)) kept.push_back(row);
    }
    table.rows = std::move(kept);
    return persist();
  }
  if (command == "select" && parser.Accept("select")) {
    Status status;
    const bool distinct = parser.Accept("distinct");
    std::vector<ExprPtr> expressions;
    std::vector<std::string> names;
    if (parser.Accept("*")) {
    } else {
      while (true) {
        ExprPtr expr = parser.Expression(&status);
        if (!status.ok()) return status;
        expressions.push_back(expr);
        names.push_back(expr->kind == Expr::Kind::kColumn ? expr->name : "expr");
        if (!parser.Accept(",")) break;
      }
    }
    if (!parser.Expect("from", &status)) return status;
    const std::string name = Lower(parser.Identifier(&status));
    if (!status.ok()) return status;
    auto it = tables.find(name);
    if (it == tables.end()) return NotFound("table does not exist");
    Table& table = it->second;
    ExprPtr predicate;
    if (parser.Accept("where")) predicate = parser.Expression(&status);
    std::vector<std::pair<ExprPtr, bool>> ordering;
    if (parser.Accept("order")) {
      if (!parser.Expect("by", &status)) return status;
      while (true) {
        ExprPtr expr = parser.Expression(&status);
        if (!status.ok()) return status;
        const bool desc = parser.Accept("desc");
        (void)parser.Accept("asc");
        ordering.emplace_back(expr, desc);
        if (!parser.Accept(",")) break;
      }
    }
    int64_t limit = -1, offset = 0;
    if (parser.Accept("limit")) {
      ExprPtr expr = parser.Expression(&status);
      EvalContext context{nullptr, nullptr, &params, 0};
      auto value = Eval(expr, &context, &status);
      if (!status.ok() || !value.has_value() || value->type() != TypeId::kInteger)
        return InvalidArgument("LIMIT requires an integer");
      limit = value->AsInteger();
      if (parser.Accept("offset")) {
        expr = parser.Expression(&status);
        value = Eval(expr, &context, &status);
        if (!status.ok() || !value.has_value() || value->type() != TypeId::kInteger)
          return InvalidArgument("OFFSET requires an integer");
        offset = value->AsInteger();
      }
    }
    if (expressions.empty())
      for (const Column& column : table.schema.columns()) {
        auto expr = std::make_shared<Expr>();
        expr->kind = Expr::Kind::kColumn;
        expr->name = column.name;
        expressions.push_back(expr);
        names.push_back(column.name);
      }
    std::vector<Column> output_columns;
    for (size_t i = 0; i < expressions.size(); ++i) {
      Column column;
      column.name = names[i];
      column.type = TypeId::kText;
      if (expressions[i]->kind == Expr::Kind::kColumn) {
        const int index = table.schema.Find(expressions[i]->name);
        if (index >= 0) column = table.schema[static_cast<size_t>(index)];
      }
      output_columns.push_back(std::move(column));
    }
    result->impl_->schema = Schema(std::move(output_columns));
    std::vector<const Row*> candidates;
    for (const Row& row : table.rows) {
      EvalContext context{&table.schema, &row, &params, 0};
      auto selected =
          predicate ? Eval(predicate, &context, &status) : std::optional<Value>(Value(true));
      if (!status.ok()) return status;
      if (selected.has_value() && Truthy(*selected)) candidates.push_back(&row);
    }
    std::stable_sort(candidates.begin(), candidates.end(), [&](const Row* left, const Row* right) {
      for (const auto& [expr, desc] : ordering) {
        EvalContext lc{&table.schema, left, &params, 0}, rc{&table.schema, right, &params, 0};
        auto lv = Eval(expr, &lc, &status);
        auto rv = Eval(expr, &rc, &status);
        if (!lv.has_value() || !rv.has_value()) return false;
        auto cmp = lv->Compare(*rv);
        if (cmp.has_value() && *cmp != 0) return desc ? *cmp > 0 : *cmp < 0;
      }
      return false;
    });
    std::set<std::string> seen;
    int64_t skipped = 0, returned = 0;
    for (const Row* row : candidates) {
      if (skipped < offset) {
        ++skipped;
        continue;
      }
      if (limit >= 0 && returned >= limit) break;
      EvalContext context{&table.schema, row, &params, 0};
      Row output;
      std::string key;
      for (const ExprPtr& expr : expressions) {
        auto value = Eval(expr, &context, &status);
        if (!status.ok() || !value.has_value()) return status;
        output.push_back(*value);
        key += value->ToString();
        key.push_back('\0');
      }
      if (!distinct || seen.insert(key).second) {
        result->impl_->rows.push_back(std::move(output));
        ++returned;
      }
    }
    return *result;
  }
  return SyntaxError("unsupported SQL statement");
}
}  // namespace

StatusOr<ResultSet> Connection::Impl::ExecuteQuery(std::string_view sql,
                                                   const std::vector<Value>& params) {
  return ExecuteSql(this, sql, params);
}
Status Connection::Execute(std::string_view sql) {
  const std::string command = TrimLower(sql);
  if (command == "begin") {
    if (impl_->transaction_tables) return AlreadyExists("transaction already active");
    std::lock_guard lock(impl_->db->mutex);
    impl_->transaction_tables = std::make_shared<Tables>(impl_->db->tables);
    impl_->transaction_generation = impl_->db->generation;
    return Status::Ok();
  }
  if (command == "rollback") {
    impl_->transaction_tables.reset();
    return Status::Ok();
  }
  if (command == "commit") {
    if (!impl_->transaction_tables) return InvalidArgument("no active transaction");
    std::lock_guard lock(impl_->db->mutex);
    if (impl_->db->generation != impl_->transaction_generation) {
      impl_->transaction_tables.reset();
      return SerializationFailure("transaction conflict");
    }
    RefreshCatalog(impl_->transaction_tables.get());
    impl_->db->tables = *impl_->transaction_tables;
    ++impl_->db->generation;
    const Status status = PersistCommittedTables(impl_->db.get(), impl_->db->tables);
    impl_->transaction_tables.reset();
    return status;
  }
  auto result = impl_->ExecuteQuery(sql, {});
  return result.ok() ? Status::Ok() : result.status();
}
StatusOr<ResultSet> Connection::Query(std::string_view sql) {
  return impl_->ExecuteQuery(sql, {});
}
StatusOr<PreparedStatement> Connection::Prepare(std::string_view sql) {
  PreparedStatement statement;
  statement.impl_ = std::make_unique<PreparedStatement::Impl>();
  statement.impl_->connection = impl_.get();
  statement.impl_->sql = std::string(sql);
  return statement;
}
StatusOr<Transaction> Connection::Begin() {
  if (impl_ == nullptr || impl_->transaction_tables)
    return AlreadyExists("transaction already active");
  std::lock_guard lock(impl_->db->mutex);
  impl_->transaction_tables = std::make_shared<Tables>(impl_->db->tables);
  impl_->transaction_generation = impl_->db->generation;
  Transaction transaction;
  transaction.impl_ = std::make_unique<Transaction::Impl>();
  transaction.impl_->connection = impl_.get();
  return transaction;
}
Database::Database() = default;
Database::~Database() {
  (void)Close();
}
StatusOr<std::unique_ptr<Database>> Database::Open(std::string_view path, const Options& options) {
  try {
    if (path.empty()) return InvalidArgument("database path is empty");
    auto database = std::unique_ptr<Database>(new Database());
    database->impl_ = std::make_unique<Database::Impl>();
    database->impl_->path = std::string(path);
    database->impl_->options = options;
    Status status = LoadTables(database->impl_->path, &database->impl_->tables);
    if (!status.ok() && status.code() == StatusCode::kNotFound) {
      if (!options.create_if_missing) return status;
      status = SaveTables(database->impl_->path, database->impl_->tables);
    }
    if (!status.ok()) return status;
    status = database->impl_->wal.Open(database->impl_->path + ".wal");
    if (!status.ok()) return status;
    status = RecoverCommittedTables(database->impl_.get());
    if (!status.ok()) return status;
    return database;
  } catch (const std::bad_alloc&) {
    return OutOfMemory("unable to open database");
  } catch (const std::exception& error) {
    return IoError(error.what());
  }
}
StatusOr<std::unique_ptr<Connection>> Database::Connect() {
  if (impl_ == nullptr || impl_->closed) return InvalidArgument("database is closed");
  auto connection = std::unique_ptr<Connection>(new Connection());
  connection->impl_ = std::make_unique<Connection::Impl>(
      std::shared_ptr<Database::Impl>(impl_.get(), [](Database::Impl*) {}));
  return connection;
}
Status Database::Checkpoint() {
  if (impl_ == nullptr || impl_->closed) return InvalidArgument("database is closed");
  std::lock_guard lock(impl_->mutex);
  const Status status = SaveTables(impl_->path, impl_->tables);
  if (!status.ok()) return status;
  return impl_->wal.Truncate();
}
Status Database::Close() {
  if (impl_ == nullptr || impl_->closed) return Status::Ok();
  std::lock_guard lock(impl_->mutex);
  const Status status = SaveTables(impl_->path, impl_->tables);
  (void)impl_->wal.Close();
  impl_->closed = true;
  return status;
}
std::string VersionString() {
  return "tuplestone " TUPLESTONE_VERSION;
}
}  // namespace tuplestone
