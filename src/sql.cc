#include "sql.h"

std::ostream& Subquery::Print(std::ostream& os) const { return os << "(" << *select << ") AS " << alias; }

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
