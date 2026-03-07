#ifndef REL_AST_REL_CONTEXT_BUILDER_H
#define REL_AST_REL_CONTEXT_BUILDER_H

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "rel_ast/projection.h"
#include "rel_ast/relation_info.h"
#include "rel_ast/rel_ast.h"
#include "rel_ast/rel_context.h"

namespace rel2sql {

// Mutable workspace during preprocessing. All visitors and rewriters receive RelContextBuilder*.
// Call Build() at the end to produce immutable RelContext, or Snapshot() for testing intermediate state.
class RelContextBuilder {
 public:
  RelContextBuilder();
  explicit RelContextBuilder(const RelationMap& edb_map);

  void SetRoot(std::shared_ptr<RelNode> root) { root_ = std::move(root); }

  // Process RelAST through the full pipeline (visitors + rewriters).
  // ANTLR → RelAST transformation is outside this class's scope.
  // Root may be RelProgram, RelFormula, or RelExpr.
  RelContext Process(std::shared_ptr<RelNode> root);

  // Mutating methods
  void MarkAsIDB(const std::string& id);
  void AddIDB(const std::string& id, int arity);
  void AddEDB(const std::string& edb, int arity);
  void AddEDB(const std::string& edb, const std::vector<std::string>& attribute_names);
  void AddVar(const std::string& var);
  void AddDependency(const std::string& id, const std::string& dep);
  void RegisterRecursiveBaseDisjunct(const std::string& id, std::shared_ptr<RelUnion> node);
  void RegisterRecursiveBranch(const std::string& id, const RecursiveBranchInfoTyped& info);
  void AddVariableDomain(const std::string& var, const std::unordered_set<Projection>& domain);
  void RemoveVarsFromDependencyGraph();
  void ComputeTopologicalSort();

  // Query methods (same interface as RelContext)
  std::shared_ptr<RelNode> Root() const { return root_; }
  int GetArity(const std::string& id) const;
  std::optional<RelationInfoTyped> GetRelationInfo(const std::string& edb) const;
  std::optional<RecursionInfoTyped> GetRecursionMetadata(const std::string& id) const;
  bool IsIDB(const std::string& id) const;
  bool IsEDB(const std::string& id) const;
  bool IsRelation(const std::string& id) const;
  bool IsVar(const std::string& var) const;
  bool IsID(const std::string& id) const;
  const std::vector<std::string>& SortedIDs() const;
  std::unordered_set<Projection> GetVariableDomain(const std::string& var) const;

  // Produce RelContext: Build() consumes, Snapshot() copies (for testing)
  RelContext Build();
  RelContext Snapshot() const;

 private:
  std::shared_ptr<RelNode> RunPipeline(std::shared_ptr<RelNode> root);

  friend class RelContext;
  std::shared_ptr<RelNode> root_;
  std::unordered_map<std::string, RelationInfoTyped> relation_info_;
  std::unordered_set<std::string> ids_;
  std::unordered_set<std::string> idb_;
  std::unordered_set<std::string> edb_;
  std::unordered_set<std::string> vars_;
  std::vector<std::string> sorted_ids_;
  std::unordered_map<std::string, std::unordered_set<Projection>> variable_domains_;
};

}  // namespace rel2sql

#endif  // REL_AST_REL_CONTEXT_BUILDER_H
