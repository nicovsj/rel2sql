#include "sql.h"

namespace sql::ast {

std::ostream& Subquery::Print(std::ostream& os) const { return os << "(" << *select << ") AS " << alias; }

std::ostream& Exists::Print(std::ostream& os) const { return os << "EXISTS (" << *select << ")"; }

std::ostream& Inclusion::Print(std::ostream& os) const {
  if (columns.size() == 1) {
    os << *columns.at(0);
  } else {
    os << "(" << *columns.at(0);
    for (size_t i = 1; i < columns.size(); i++) {
      os << ", " << *columns.at(i);
    }
    os << ")";
  }

  if (is_not) {
    os << " NOT";
  }

  return os << " IN (" << *select << ")";
}

std::shared_ptr<Condition> EqualitySS(std::unordered_map<ParserRuleContext*, std::shared_ptr<Source>> input_map,
                                      std::unordered_map<ParserRuleContext*, ExtendedData> extended_data_map) {
  std::unordered_map<std::string, std::vector<ParserRuleContext*>> repetition_map;
  for (auto const& [ctx, _] : input_map) {
    for (auto const& var : extended_data_map[ctx].variables) {
      repetition_map[var].push_back(ctx);
    }
  }

  std::vector<std::shared_ptr<Condition>> conditions;

  for (auto const& [var, ctxs] : repetition_map) {
    if (ctxs.size() < 2) continue;

    for (size_t i = 0; i < ctxs.size(); i++) {
      for (size_t j = i + 1; j < ctxs.size(); j++) {
        auto lhs = std::make_shared<Column>(var, input_map[ctxs[i]]);
        auto rhs = std::make_shared<Column>(var, input_map[ctxs[j]]);
        conditions.push_back(std::make_shared<ColumnComparisonCondition>(lhs, CompOp::EQ, rhs));
      }
    }
  }

  return std::make_shared<LogicalCondition>(conditions, LogicalOp::AND);
}

std::vector<std::shared_ptr<Selectable>> VarListSS(
    std::unordered_map<ParserRuleContext*, std::shared_ptr<Source>> input_map,
    std::unordered_map<ParserRuleContext*, ExtendedData> extended_data_map) {
  std::set<std::string> seen_vars;

  std::vector<std::shared_ptr<Selectable>> columns;

  for (auto const& [ctx, data] : input_map) {
    for (auto const& var : extended_data_map[ctx].variables) {
      if (seen_vars.find(var) != seen_vars.end()) continue;

      columns.push_back(std::static_pointer_cast<Selectable>(std::make_shared<Column>(var, data)));
      seen_vars.insert(var);
    }
  }

  return columns;
}

}  // namespace sql::ast
