#ifndef REL_AST_REL_AST_CONTAINER_H
#define REL_AST_REL_AST_CONTAINER_H

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "rel_ast/projection.h"
#include "rel_ast/relation_info.h"
#include "rel_ast/rel_ast.h"

namespace rel2sql {

class RelASTContainer {
 public:
  RelASTContainer();
  explicit RelASTContainer(const RelationMap& edb_map);

  void SetRoot(std::shared_ptr<RelProgram> root) { root_ = std::move(root); }
  std::shared_ptr<RelProgram> Root() const { return root_; }

  int GetArity(const std::string& id) const;
  std::optional<RelationInfoTyped> GetRelationInfo(const std::string& edb) const;

  void MarkAsIDB(const std::string& id);
  void AddIDB(const std::string& id, int arity);
  void AddEDB(const std::string& edb, int arity);
  void AddEDB(const std::string& edb, const std::vector<std::string>& attribute_names);
  void AddVar(const std::string& var);
  void AddDependency(const std::string& id, const std::string& dep);

  void RegisterRecursiveBaseDisjunct(const std::string& id, std::shared_ptr<RelAbstraction> node);
  void RegisterRecursiveBranch(const std::string& id, const RecursiveBranchInfoTyped& info);

  std::optional<RecursionInfoTyped> GetRecursionMetadata(const std::string& id) const;

  bool IsIDB(const std::string& id) const;
  bool IsEDB(const std::string& id) const;
  bool IsRelation(const std::string& id) const;
  bool IsVar(const std::string& var) const;
  bool IsID(const std::string& id) const;

  const std::vector<std::string>& SortedIDs() const;
  void RemoveVarsFromDependencyGraph();
  void ComputeTopologicalSort();

  std::unordered_set<Projection> GetVariableDomain(const std::string& var) const;
  void AddVariableDomain(const std::string& var, const std::unordered_set<Projection>& domain);

 private:
  std::shared_ptr<RelProgram> root_;
  std::unordered_map<std::string, RelationInfoTyped> relation_info_;
  std::unordered_set<std::string> ids_;
  std::unordered_set<std::string> idb_;
  std::unordered_set<std::string> edb_;
  std::unordered_set<std::string> vars_;
  std::vector<std::string> sorted_ids_;
  std::unordered_map<std::string, std::unordered_set<Projection>> variable_domains_;
};

}  // namespace rel2sql

#endif  // REL_AST_REL_AST_CONTAINER_H
