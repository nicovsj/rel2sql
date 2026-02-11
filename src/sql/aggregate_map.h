#ifndef SQL_AGGREGATE_MAP_H
#define SQL_AGGREGATE_MAP_H

#include <string>
#include <unordered_map>

#include "sql_ast/sql_ast.h"

namespace rel2sql {

inline const std::unordered_map<std::string, sql::ast::AggregateFunction>& GetAggregateMap() {
  static const std::unordered_map<std::string, sql::ast::AggregateFunction> kMap = {
      {"sum", sql::ast::AggregateFunction::SUM},
      {"average", sql::ast::AggregateFunction::AVG},
      {"avg", sql::ast::AggregateFunction::AVG},
      {"min", sql::ast::AggregateFunction::MIN},
      {"max", sql::ast::AggregateFunction::MAX},
  };
  return kMap;
}

}  // namespace rel2sql

#endif  // SQL_AGGREGATE_MAP_H
