#ifndef EXTENDED_DATA_H
#define EXTENDED_DATA_H

#include <antlr4-runtime.h>

#include <memory>
#include <string>
#include <vector>

#include "rel_ast/bound_set.h"
#include "rel_ast/relation_info.h"
#include "sql_ast/sql_ast.h"

namespace rel2sql {

struct RelASTNode {
  RelASTNode() = default;

  explicit RelASTNode(antlr4::ParserRuleContext* context) : ctx(context) {}

  antlr4::ParserRuleContext* ctx = nullptr;
  // Variables are the variables that are bound in the current context
  std::set<std::string> variables;
  std::set<std::string> free_variables;

  // SQL expression that corresponds to the current context
  std::shared_ptr<sql::ast::Expression> sql_expression;

  // Flag to disable translation of the current context
  bool disabled = false;

  // Constant value that unambiguously corresponds to the current context
  std::optional<sql::ast::constant_t> constant;

  // Special flag for defs of rel abs with only literal values
  bool has_only_literal_values = false;
  std::vector<antlr4::ParserRuleContext*> multiple_defs;

  // Flag to mark if the expression matches a recursion pattern
  bool is_recursive = false;

  // Name of the recursive source
  std::string recursive_definition_name;

  // Arity of the current context
  int arity = 0;

  // Output of the safety analysis
  BoundSet safety;

  // AND term partitioning variables
  std::vector<antlr4::ParserRuleContext*> comparator_conjuncts;
  std::vector<antlr4::ParserRuleContext*> non_comparator_conjuncts;

  std::vector<antlr4::ParserRuleContext*> negated_conjuncts;
  std::vector<antlr4::ParserRuleContext*> non_negated_conjuncts;

  // Children nodes in the ExtendedAST tree structure
  std::vector<std::shared_ptr<RelASTNode>> children;

  const std::vector<std::shared_ptr<RelASTNode>>& GetChildren() const { return children; }

  void VariablesInplaceUnion(const RelASTNode& other) {
    variables.insert(other.variables.begin(), other.variables.end());
    free_variables.insert(other.free_variables.begin(), other.free_variables.end());
  }

  void VariablesInplaceDifference(const RelASTNode& other) {
    RelASTNode result;
    variables.insert(other.variables.begin(), other.variables.end());
    for (const auto& var : other.free_variables) {
      free_variables.erase(var);
    }
  }

  bool IsConjunctionWithTerms() const { return !comparator_conjuncts.empty(); }

  bool IsConjunctionWithNegations() const { return !negated_conjuncts.empty(); }
};

// Forward declaration for operator==
bool operator==(const RelASTNode& lhs, const RelASTNode& rhs);
bool operator!=(const RelASTNode& lhs, const RelASTNode& rhs);

// Represents the extended AST of the ANTLR parse tree.
class RelAST {
 public:
  RelAST();

  explicit RelAST(antlr4::ParserRuleContext* root);

  RelAST(antlr4::ParserRuleContext* root, const rel2sql::RelationMap& edb_map);

  // Get the root node
  std::shared_ptr<RelASTNode> Root() const;

  // Get the node for a context
  std::shared_ptr<RelASTNode> GetNode(antlr4::ParserRuleContext* ctx);

  // Get the arity of a relation
  int GetArity(const std::string& id) const;

  // Get the parse tree
  antlr4::ParserRuleContext* ParseTree() const;

  // Set the parse tree
  void SetParseTree(antlr4::ParserRuleContext* root);

  // Mark an ID as an IDB (without arity, arity will be set later)
  void MarkAsIDB(const std::string& id);

  // Add an IDB with arity (or update arity if already marked as IDB)
  void AddIDB(const std::string& id, int arity);

  // Add an EDB with an arity
  void AddEDB(const std::string& edb, int arity);

  // Add an EDB with named attributes
  void AddEDB(const std::string& edb, const std::vector<std::string>& attribute_names);

  // Add a variable
  void AddVar(const std::string& var);

  // Add a dependency between two relations
  void AddDependency(const std::string& id, const std::string& dep);

  void RegisterRecursiveBaseDisjunct(const std::string& id, antlr4::ParserRuleContext* ctx);

  void RegisterRecursiveBranch(const std::string& id, const RecursiveBranchInfo& info);

  std::optional<RecursionInfo> GetRecursionMetadata(const std::string& id) const;

  // Get RelationInfo for a relation (if it exists)
  std::optional<RelationInfo> GetRelationInfo(const std::string& edb) const;

  // Check if a relation is an IDB
  bool IsIDB(const std::string& id) const;

  // Check if a relation is an EDB
  bool IsEDB(const std::string& id) const;

  // Check if a variable is a variable
  bool IsVar(const std::string& var) const;

  // Check if a relation is an ID
  bool IsID(const std::string& id) const;

  // Get all IDs sorted by their definition order (topological sort of the dependency graph)
  const std::vector<std::string>& SortedIDs() const;

  // Remove variables from the dependency graph
  void RemoveVarsFromDependencyGraph();

  // Compute and store the topological sort of the dependency graph
  void ComputeTopologicalSort();

 private:
  // The root context of the parse tree
  antlr4::ParserRuleContext* root_;

  // Index of the nodes by context
  std::unordered_map<antlr4::ParserRuleContext*, std::shared_ptr<RelASTNode>> index_;

  // Relation information by ID
  std::unordered_map<std::string, RelationInfo> relation_info_;

  // All IDs
  std::unordered_set<std::string> ids_;

  // Intensional databases
  std::unordered_set<std::string> idb_;

  // Extensional databases
  std::unordered_set<std::string> edb_;

  // Variables
  std::unordered_set<std::string> vars_;

  // Topological sort of the dependency graph of IDs
  std::vector<std::string> sorted_ids_;
};

const std::map<std::string, sql::ast::AggregateFunction> AGGREGATE_MAP = {
    {"max", sql::ast::AggregateFunction::MAX},
    {"min", sql::ast::AggregateFunction::MIN},
    {"sum", sql::ast::AggregateFunction::SUM},
    {"average", sql::ast::AggregateFunction::AVG},
    {"count", sql::ast::AggregateFunction::COUNT}};

}  // namespace rel2sql

#endif  // EXTENDED_DATA_H
