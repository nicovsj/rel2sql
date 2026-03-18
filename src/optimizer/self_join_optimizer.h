#ifndef SELF_JOIN_OPTIMIZER_H
#define SELF_JOIN_OPTIMIZER_H

#include <unordered_map>
#include <unordered_set>

#include "base_optimizer.h"

namespace rel2sql {
namespace sql::ast {

class SelfJoinOptimizer : public BaseOptimizer {
 public:
  using BaseOptimizer::Visit;

  void Visit(Select& select) override;

  /** Checks if a condition is an equivalence candidate (for testing canonical form). */
  static bool IsEquivalenceCandidate(const std::shared_ptr<ComparisonCondition>& comp_condition);

 private:
  // Type alias for sources grouped by identifier (table name for tables, sourceable pointer for CTEs)
  using SourcesByIdentifier = std::unordered_map<std::string, std::vector<std::shared_ptr<Source>>>;

  // Type alias for source pairs
  using SourcePair = std::pair<std::shared_ptr<Source>, std::shared_ptr<Source>>;
  using SourcePairs = std::vector<SourcePair>;

  // Type alias for column identifier (source alias + column name)
  using ColumnId = std::pair<std::string, std::string>;  // (source_alias, column_name)

  // Custom hash function for ColumnId
  struct ColumnIdHash {
    std::size_t operator()(const ColumnId& id) const {
      std::size_t h1 = std::hash<std::string>{}(id.first);
      std::size_t h2 = std::hash<std::string>{}(id.second);
      // Simple hash combination (not perfect but sufficient)
      return h1 ^ (h2 << 1);
    }
  };

  struct EquivalenceClassesMap {
    std::unordered_map<ColumnId, size_t, ColumnIdHash> column_to_class;
    std::unordered_map<size_t, std::unordered_set<ColumnId, ColumnIdHash>> class_to_columns;
  };

  bool EliminateRedundantSelfJoins(Select& select);

  SourcesByIdentifier GroupSourcesByIdentifier(const std::vector<std::shared_ptr<Source>>& sources,
                                                const Select& select);

  /**
   * Generates all possible pairs from a vector of candidate sources.
   *
   * @param candidate_sources The sources to generate pairs from
   * @return Vector of all possible source pairs (order doesn't matter)
   */
  SourcePairs GenerateSourcePairs(const std::vector<std::shared_ptr<Source>>& candidate_sources);

  /**
   * Computes the transitive closure of equality conditions to find equivalence classes.
   *
   * @param equivalences The equality conditions to analyze
   * @return EquivalenceClassesMap containing both column_to_class and class_to_columns mappings
   */
  EquivalenceClassesMap ComputeColumnEquivalenceClasses(
      const std::vector<std::shared_ptr<ComparisonCondition>>& equivalences);

  /**
   * Checks if a source pair forms a complete equivalence class based on referenced columns.
   *
   * @param source_pair The source pair to check
   * @param eq_class_map The computed equivalence classes map
   * @param select The SELECT statement to analyze for column references
   * @return true if the source pair forms a self join in the query, false otherwise
   */
  bool IsSelfJoin(const SourcePair& source_pair, const EquivalenceClassesMap& eq_class_map,
                  const Select& select);

  /**
   * Collects all columns from a specific source that are referenced in the SELECT statement.
   * This includes columns from SELECT clause, WHERE clause, and GROUP BY clause.
   *
   * @param select The SELECT statement to analyze
   * @param source_alias The alias of the source to collect columns for
   * @return Set of column names referenced from the source
   */
  std::unordered_set<std::string> CollectReferencedColumns(const Select& select,
                                                            const std::string& source_alias);

  /**
   * Recursively collects columns from a Term that reference a specific source.
   *
   * @param term The term to analyze
   * @param source_alias The alias of the source to collect columns for
   * @param referenced Set to add referenced column names to
   */
  void CollectColumnsFromTerm(const std::shared_ptr<Term>& term, const std::string& source_alias,
                               std::unordered_set<std::string>& referenced);

  /**
   * Recursively collects columns from a Condition that reference a specific source.
   *
   * @param condition The condition to analyze
   * @param source_alias The alias of the source to collect columns for
   * @param referenced Set to add referenced column names to
   */
  void CollectColumnsFromConditionRecursive(const std::shared_ptr<Condition>& condition,
                                             const std::string& source_alias,
                                             std::unordered_set<std::string>& referenced);

  /**
   * Recursively extracts all ComparisonCondition objects from a Condition tree.
   * Returns an empty vector if non-AND logical operations (OR, NOT) are encountered,
   * as self-join elimination is not safe with these operations.
   */
  std::vector<std::shared_ptr<ComparisonCondition>> CollectEquivalenceConditions(
      const std::shared_ptr<Condition>& condition);
};  // class SelfJoinOptimizer

}  // namespace sql::ast
}  // namespace rel2sql

#endif  // SELF_JOIN_OPTIMIZER_H
