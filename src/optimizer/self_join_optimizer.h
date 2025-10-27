#ifndef SELF_JOIN_OPTIMIZER_H
#define SELF_JOIN_OPTIMIZER_H

#include <unordered_map>

#include "base_optimizer.h"

namespace rel2sql {
namespace sql::ast {

class SelfJoinOptimizer : public BaseOptimizer {
 public:
  using BaseOptimizer::Visit;

  void Visit(SelectStatement& select_statement) override;

 private:
  struct SelfJoin {
    std::shared_ptr<Source> primary_source;
    std::shared_ptr<Source> redundant_source;

    std::vector<std::shared_ptr<ComparisonCondition>> join_conditions;
    std::unordered_map<std::string, std::shared_ptr<Column>> column_map;

    SelfJoin() = default;

    SelfJoin(std::shared_ptr<Source> primary_source, std::shared_ptr<Source> redundant_source)
        : primary_source(primary_source), redundant_source(redundant_source) {}

    bool IsComplete() const {
      auto table = std::dynamic_pointer_cast<Table>(primary_source->sourceable);
      return column_map.size() == table->arity;
    }

    void AddJoinCondition(std::shared_ptr<ComparisonCondition> join_condition) {
      join_conditions.push_back(join_condition);
    }

    void AddColumn(std::shared_ptr<Column> column) { column_map[column->name] = column; }
  };
  // Type alias for self-join matcher map: maps source pairs to column mappings
  using SelfJoinMatcherMap = std::map<std::pair<std::string, std::string>, SelfJoin>;

  // Type alias for sources grouped by table name
  using SourcesByTable = std::unordered_map<std::string, std::vector<std::shared_ptr<Source>>>;

  bool EliminateRedundantSelfJoins(SelectStatement& select_statement);

  SourcesByTable GroupSourcesByTableName(const std::vector<std::shared_ptr<Source>>& sources);

  /**
   * Recursively extracts all ComparisonCondition objects from a Condition tree.
   * Returns an empty vector if non-AND logical operations (OR, NOT) are encountered,
   * as self-join elimination is not safe with these operations.
   */
  std::vector<std::shared_ptr<ComparisonCondition>> CollectComparisonConditions(
      const std::shared_ptr<Condition>& condition);

  /**
   * Filters comparison conditions to only include those between different candidate sources.
   *
   * @param comparisons All comparison conditions from the WHERE clause
   * @param candidate_sources Sources that are candidates for self-join elimination
   * @return Filtered comparisons where both columns belong to different candidate sources
   */
  std::vector<std::shared_ptr<ComparisonCondition>> FilterComparisonsForSources(
      const std::vector<std::shared_ptr<ComparisonCondition>>& comparisons,
      const std::vector<std::shared_ptr<Source>>& candidate_sources);

  SelfJoinMatcherMap BuildSelfJoinMatcherMap(const std::vector<std::shared_ptr<ComparisonCondition>>& comparisons);

  /**
   * Removes join conditions from the WHERE clause.
   *
   * @param conditions_to_remove The comparison conditions to remove
   * @param where_condition The WHERE condition to modify
   */
  void RemoveConditionsFromWhere(const std::vector<std::shared_ptr<ComparisonCondition>>& conditions_to_remove,
                                 std::shared_ptr<Condition>& where_condition);

  /**
   * Removes a redundant source and its associated join conditions from the query.
   *
   * @param self_join_candidate The self-join candidate containing source and conditions to remove
   * @param from_statement The FROM statement to modify
   */
  void RemoveRedundantSource(const SelfJoin& self_join_candidate, FromStatement& from_statement);

  /**
   * Eliminates redundant self-joins from the given matcher map.
   *
   * @param matcher_map Map of source pairs to self-join candidates
   * @param from_statement The FROM statement to modify (includes WHERE clause)
   * @return true if any self-joins were eliminated, false otherwise
   */
  bool EliminateSelfJoins(const SelfJoinMatcherMap& matcher_map, FromStatement& from_statement);
};  // class SelfJoinOptimizer

}  // namespace sql::ast
}  // namespace rel2sql

#endif  // SELF_JOIN_OPTIMIZER_H
