#include "self_join_optimizer.h"

#include <sstream>
#include <vector>

#include "canonical_form.h"
#include "replacers.h"

namespace rel2sql {
namespace sql::ast {

namespace {

// Extracts a single Column from a term tree if and only if the term
// references exactly one column (through optional ParenthesisTerm and
// Operation nodes). Otherwise returns nullptr.
std::shared_ptr<Column> ExtractSingleColumnFromTerm(const std::shared_ptr<Term>& term) {
  if (!term) return nullptr;

  if (auto col = std::dynamic_pointer_cast<Column>(term)) {
    return col;
  }

  if (auto paren = std::dynamic_pointer_cast<ParenthesisTerm>(term)) {
    return ExtractSingleColumnFromTerm(paren->term);
  }

  if (auto op = std::dynamic_pointer_cast<Operation>(term)) {
    auto left_col = ExtractSingleColumnFromTerm(op->lhs);
    auto right_col = ExtractSingleColumnFromTerm(op->rhs);
    if (left_col && !right_col) return left_col;
    if (right_col && !left_col) return right_col;
    // Either no columns or multiple columns – not a simple single-column term.
    return nullptr;
  }

  // Other term types (Function, CaseWhen, Constant, etc.) are not treated
  // as simple single-column affine terms here.
  return nullptr;
}

}  // namespace

void SelfJoinOptimizer::Visit(Select& select) {
  // First navigate depth-first through possible subqueries in FROM statements
  if (select.from.has_value()) {
    for (auto& source : select.from.value()->sources) {
      Visit(*source);
    }
  }
  // Then try to eliminate redundant self-joins
  EliminateRedundantSelfJoins(select);
}

bool SelfJoinOptimizer::EliminateRedundantSelfJoins(Select& select) {
  if (!select.from.has_value()) {
    return false;
  }

  auto& from_statement = *select.from.value();
  bool simplified = false;

  auto grouped_sources = GroupSourcesByIdentifier(from_statement.sources, select);

  // Check for complete self-join in WHERE clause
  if (!from_statement.where.has_value()) return false;

  auto where_condition = from_statement.where.value();

  auto comparisons = CollectEquivalenceConditions(where_condition);

  // If no comparisons are found, there's no possible self-join.
  if (comparisons.empty()) return false;

  // Compute transitive closure to find equivalence classes
  auto equivalence_classes = ComputeColumnEquivalenceClasses(comparisons);

  for (const auto& [_, candidate_sources] : grouped_sources) {
    // No possible self-join if there's only one source.
    if (candidate_sources.size() <= 1) continue;

    // Generate all possible pairs from candidate sources
    auto source_pairs = GenerateSourcePairs(candidate_sources);

    // Process each source pair to check for complete equivalence classes
    for (auto source_pair : source_pairs) {
      // Check if this source pair forms a self join in the query
      // IsSelfJoin checks if source2 can be eliminated, so we try both directions
      if (IsSelfJoin(source_pair, equivalence_classes, select)) {
        SourceReplacer replacer(source_pair.second->Alias(), source_pair.first);
        base_expr_->Accept(replacer);
        simplified = true;
      } else {
        // Try the reverse: swap sources and check if source1 can be eliminated
        std::swap(source_pair.first, source_pair.second);
        if (IsSelfJoin(source_pair, equivalence_classes, select)) {
          // Now source_pair.second is the original source_pair.first, which can be eliminated
          SourceReplacer replacer(source_pair.second->Alias(), source_pair.first);
          base_expr_->Accept(replacer);
          simplified = true;
        }
      }
    }
  }

  RedundancyReplacer redundancy_replacer;
  base_expr_->Accept(redundancy_replacer);

  return simplified;
}

SelfJoinOptimizer::SourcesByIdentifier SelfJoinOptimizer::GroupSourcesByIdentifier(
    const std::vector<std::shared_ptr<Source>>& sources, const Select& select) {
  SourcesByIdentifier grouped_sources;

  for (const auto& source : sources) {
    std::string identifier;

    if (auto table = std::dynamic_pointer_cast<Table>(source->sourceable)) {
      // For tables, use table name as identifier
      identifier = table->name;
    } else {
      // Check if this source references a CTE by looking up the alias in the CTE list
      std::string source_alias = source->Alias();
      std::shared_ptr<Source> matching_cte = nullptr;

      for (const auto& cte : select.ctes) {
        if (cte->Alias() == source_alias && cte->IsCTE()) {
          matching_cte = cte;
          break;
        }
      }

      if (matching_cte) {
        // This source references a CTE - use the CTE's sourceable pointer as identifier
        // Convert pointer to string for use as map key
        std::ostringstream oss;
        oss << matching_cte->sourceable.get();
        identifier = "CTE:" + oss.str();
      } else {
        // Skip other source types (subqueries, etc.) for now
        continue;
      }
    }

    grouped_sources[identifier].push_back(source);
  }

  return grouped_sources;
}

SelfJoinOptimizer::SourcePairs SelfJoinOptimizer::GenerateSourcePairs(
    const std::vector<std::shared_ptr<Source>>& candidate_sources) {
  SourcePairs source_pairs;

  // Generate all possible pairs (i, j) where i < j to avoid duplicates
  for (size_t i = 0; i < candidate_sources.size(); ++i) {
    for (size_t j = i + 1; j < candidate_sources.size(); ++j) {
      source_pairs.emplace_back(candidate_sources[i], candidate_sources[j]);
    }
  }

  return source_pairs;
}

SelfJoinOptimizer::EquivalenceClassesMap SelfJoinOptimizer::ComputeColumnEquivalenceClasses(
    const std::vector<std::shared_ptr<ComparisonCondition>>& equivalences) {
  EquivalenceClassesMap result;
  size_t next_class_id = 0;

  // Process each equality condition
  for (const auto& equivalence : equivalences) {
    auto left_col = std::dynamic_pointer_cast<Column>(equivalence->lhs);
    auto right_col = std::dynamic_pointer_cast<Column>(equivalence->rhs);

    // If the sides are not plain columns, check algebraic equality via canonical form
    // (e.g. 22*(T2.A1+3)/22 + -3 vs T2.A1) and extract single underlying column.
    if (!left_col || !right_col) {
      if (!AreEqualityExpressionsEqual(equivalence)) continue;
      left_col = ExtractSingleColumnFromTerm(equivalence->lhs);
      right_col = ExtractSingleColumnFromTerm(equivalence->rhs);
    }

    if (!left_col || !right_col) continue;
    if (!left_col->source.has_value() || !right_col->source.has_value()) continue;

    std::string left_alias = left_col->source.value()->Alias();
    std::string right_alias = right_col->source.value()->Alias();

    ColumnId left_id = {left_alias, left_col->name};
    ColumnId right_id = {right_alias, right_col->name};

    // Find existing classes for both columns
    auto left_class_it = result.column_to_class.find(left_id);
    auto right_class_it = result.column_to_class.find(right_id);

    if (left_class_it == result.column_to_class.end() && right_class_it == result.column_to_class.end()) {
      // Neither column is in any class, create a new one
      size_t new_class_id = next_class_id++;
      result.column_to_class[left_id] = new_class_id;
      result.column_to_class[right_id] = new_class_id;
      result.class_to_columns[new_class_id].insert(left_id);
      result.class_to_columns[new_class_id].insert(right_id);
    } else if (left_class_it == result.column_to_class.end()) {
      // Left column is new, add it to right column's class
      size_t class_id = right_class_it->second;
      result.column_to_class[left_id] = class_id;
      result.class_to_columns[class_id].insert(left_id);
    } else if (right_class_it == result.column_to_class.end()) {
      // Right column is new, add it to left column's class
      size_t class_id = left_class_it->second;
      result.column_to_class[right_id] = class_id;
      result.class_to_columns[class_id].insert(right_id);
    } else if (left_class_it->second != right_class_it->second) {
      // Both columns are in different classes, merge them
      size_t left_class_id = left_class_it->second;
      size_t right_class_id = right_class_it->second;

      // Merge right class into left class
      for (const auto& col_id : result.class_to_columns[right_class_id]) {
        result.column_to_class[col_id] = left_class_id;
        result.class_to_columns[left_class_id].insert(col_id);
      }

      // Remove the right class
      result.class_to_columns.erase(right_class_id);
    }
    // If both columns are already in the same class, do nothing
  }

  return result;
}

std::unordered_set<std::string> SelfJoinOptimizer::CollectReferencedColumns(const Select& select,
                                                                             const std::string& source_alias) {
  std::unordered_set<std::string> referenced;

  // Collect from SELECT clause
  for (const auto& selectable : select.columns) {
    if (auto term_selectable = std::dynamic_pointer_cast<TermSelectable>(selectable)) {
      CollectColumnsFromTerm(term_selectable->term, source_alias, referenced);
    }
    // Note: Wildcard selectables are not handled here as they would reference all columns,
    // which would make the optimization more complex
  }

  // Collect from WHERE clause
  if (select.from.has_value() && select.from.value()->where.has_value()) {
    CollectColumnsFromConditionRecursive(select.from.value()->where.value(), source_alias, referenced);
  }

  // Collect from GROUP BY clause if present
  if (select.group_by.has_value()) {
    for (const auto& group_item : select.group_by.value()->columns) {
      if (auto term_selectable = std::dynamic_pointer_cast<TermSelectable>(group_item)) {
        CollectColumnsFromTerm(term_selectable->term, source_alias, referenced);
      }
    }
  }

  return referenced;
}

void SelfJoinOptimizer::CollectColumnsFromTerm(const std::shared_ptr<Term>& term, const std::string& source_alias,
                                                std::unordered_set<std::string>& referenced) {
  if (!term) return;

  // Check if it's a Column
  if (auto column = std::dynamic_pointer_cast<Column>(term)) {
    if (column->source && column->source.value()->Alias() == source_alias) {
      referenced.insert(column->name);
    }
    return;
  }

  // Check if it's an Operation (has lhs and rhs Terms)
  if (auto operation = std::dynamic_pointer_cast<Operation>(term)) {
    CollectColumnsFromTerm(operation->lhs, source_alias, referenced);
    CollectColumnsFromTerm(operation->rhs, source_alias, referenced);
    return;
  }

  // Check if it's a Function (has an arg Term)
  if (auto function = std::dynamic_pointer_cast<Function>(term)) {
    CollectColumnsFromTerm(function->arg, source_alias, referenced);
    return;
  }

  // Check if it's a CaseWhen (has conditions and terms)
  if (auto case_when = std::dynamic_pointer_cast<CaseWhen>(term)) {
    for (const auto& [condition, case_term] : case_when->cases) {
      CollectColumnsFromConditionRecursive(condition, source_alias, referenced);
      CollectColumnsFromTerm(case_term, source_alias, referenced);
    }
    return;
  }

  // Constant and other types don't reference columns, so we're done
}

void SelfJoinOptimizer::CollectColumnsFromConditionRecursive(const std::shared_ptr<Condition>& condition,
                                                              const std::string& source_alias,
                                                              std::unordered_set<std::string>& referenced) {
  if (!condition) return;

  // Check if it's a ComparisonCondition
  if (auto comp_condition = std::dynamic_pointer_cast<ComparisonCondition>(condition)) {
    // Check left side
    CollectColumnsFromTerm(comp_condition->lhs, source_alias, referenced);
    // Check right side
    CollectColumnsFromTerm(comp_condition->rhs, source_alias, referenced);
    return;
  }

  // Check if it's a LogicalCondition
  if (auto logical_condition = std::dynamic_pointer_cast<LogicalCondition>(condition)) {
    // Recursively visit all sub-conditions
    for (const auto& sub_condition : logical_condition->conditions) {
      CollectColumnsFromConditionRecursive(sub_condition, source_alias, referenced);
    }
    return;
  }

  // Check if it's an Inclusion condition
  if (auto inclusion = std::dynamic_pointer_cast<Inclusion>(condition)) {
    for (const auto& column : inclusion->columns) {
      if (column->source && column->source.value()->Alias() == source_alias) {
        referenced.insert(column->name);
      }
    }
    // Note: We don't recurse into the subquery as it's in a different scope
    return;
  }

  // Check if it's an Exists condition
  // Note: We don't recurse into the subquery as it's in a different scope
  // Exists conditions don't reference columns from the outer query in a way that affects self-join elimination
}

bool SelfJoinOptimizer::IsSelfJoin(const SourcePair& source_pair, const EquivalenceClassesMap& eq_class_map,
                                    const Select& select) {
  // Check if both sources are tables
  auto table1 = std::dynamic_pointer_cast<Table>(source_pair.first->sourceable);
  auto table2 = std::dynamic_pointer_cast<Table>(source_pair.second->sourceable);

  bool both_tables = (table1 != nullptr && table2 != nullptr);

  // Check if both sources reference the same CTE
  std::shared_ptr<Source> cte1 = nullptr;
  std::shared_ptr<Source> cte2 = nullptr;
  if (!both_tables) {
    std::string alias1 = source_pair.first->Alias();
    std::string alias2 = source_pair.second->Alias();

    for (const auto& cte : select.ctes) {
      if (cte->Alias() == alias1 && cte->IsCTE()) {
        cte1 = cte;
      }
      if (cte->Alias() == alias2 && cte->IsCTE()) {
        cte2 = cte;
      }
    }
  }

  bool both_ctes = (cte1 != nullptr && cte2 != nullptr && cte1 == cte2);

  if (!both_tables && !both_ctes) return false;

  // For tables: check arity
  if (both_tables) {
    if (table1->arity != table2->arity) return false;
  }

  // For CTEs: check if they reference the same CTE definition and have same arity
  if (both_ctes) {
    // Check arity - get number of columns from the CTE definition
    auto cte_select = std::dynamic_pointer_cast<Select>(cte1->sourceable);
    if (!cte_select) return false;

    // For CTE references, arity is determined by the CTE definition's columns
    // Both sources reference the same CTE, so they have the same arity
    // No need to check arity separately since they're the same CTE
  }

  std::string source1_alias = source_pair.first->Alias();
  std::string source2_alias = source_pair.second->Alias();

  // Collect columns referenced from each source
  auto source1_refs = CollectReferencedColumns(select, source1_alias);
  auto source2_refs = CollectReferencedColumns(select, source2_alias);

  // The caller always eliminates source2 (source_pair.second), so we need to check
  // if source2 can be safely eliminated by checking if all its referenced columns
  // are equivalent to source1's corresponding columns

  // If source2 has no referenced columns, we can safely eliminate it
  if (source2_refs.empty()) {
    // However, we should be conservative: if source1 has referenced columns but source2 doesn't,
    // it might mean source2 is not used at all, which is a different optimization (unused source removal)
    // For self-join elimination, we want both sources to be used, so we require source2 to have references
    // But if source1 also has no references, we can't determine equivalence, so be conservative
    return false;
  }

  // Check if all referenced columns from source2 are equivalent to source1
  for (const auto& col_name : source2_refs) {
    // Check if source2.col_name is equivalent to source1.col_name
    ColumnId source2_id = {source2_alias, col_name};
    ColumnId source1_id = {source1_alias, col_name};

    auto source2_eq = eq_class_map.column_to_class.find(source2_id);
    auto source1_eq = eq_class_map.column_to_class.find(source1_id);

    // If either column is not found in any equivalence class, or they're not in the same class
    if (source2_eq == eq_class_map.column_to_class.end() ||
        source1_eq == eq_class_map.column_to_class.end() ||
        source2_eq->second != source1_eq->second) {
      return false;
    }
  }

  // All referenced columns from source2 are equivalent to source1, so we can eliminate source2
  return true;
}

bool SelfJoinOptimizer::IsEquivalenceCandidate(const std::shared_ptr<ComparisonCondition>& comp_condition) {
  if (!comp_condition) return false;
  if (comp_condition->op != CompOp::EQ) return false;

  auto left_col = std::dynamic_pointer_cast<Column>(comp_condition->lhs);
  auto right_col = std::dynamic_pointer_cast<Column>(comp_condition->rhs);

  // Accept either plain column = column, or algebraically equal expressions
  // (e.g. 22*(T2.A1+3)/22 + -3 = T2.A1) where each references exactly one column.
  if (!left_col || !right_col) {
    if (!AreEqualityExpressionsEqual(comp_condition)) return false;
    left_col = ExtractSingleColumnFromTerm(comp_condition->lhs);
    right_col = ExtractSingleColumnFromTerm(comp_condition->rhs);
  }

  if (!left_col || !right_col) return false;
  if (!left_col->source.has_value() || !right_col->source.has_value()) return false;

  return true;
}

std::vector<std::shared_ptr<ComparisonCondition>> SelfJoinOptimizer::CollectEquivalenceConditions(
    const std::shared_ptr<Condition>& where_condition) {
  // We can assume that the LogicalConditions are already flattened. This is guaranteed by the flattener optimizer.
  // So we need to check if the WHERE condition is a LogicalCondition with AND operator and
  // then collect all the equivalences in it.
  // If the WHERE condition is a single ComparisonCondition, we should check it as if it was
  // contained inside an AND LogicalCondition.

  std::vector<std::shared_ptr<ComparisonCondition>> equivalences;

  // First check if it's a single ComparisonCondition
  auto comp_condition = std::dynamic_pointer_cast<ComparisonCondition>(where_condition);
  if (comp_condition) {
    if (IsEquivalenceCandidate(comp_condition)) {
      equivalences.push_back(comp_condition);
    }
    return equivalences;
  }

  // Otherwise, check if it's a LogicalCondition with AND operator
  auto logical_condition = std::dynamic_pointer_cast<LogicalCondition>(where_condition);
  if (!logical_condition) return {};
  if (logical_condition->op != LogicalOp::AND) return {};

  for (const auto& sub_condition : logical_condition->conditions) {
    auto sub_comp_condition = std::dynamic_pointer_cast<ComparisonCondition>(sub_condition);
    if (IsEquivalenceCandidate(sub_comp_condition)) {
      equivalences.push_back(sub_comp_condition);
    }
  }
  return equivalences;
}

}  // namespace sql::ast
}  // namespace rel2sql
