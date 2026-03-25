#ifndef REL_AST_REL_CONTEXT_H
#define REL_AST_REL_CONTEXT_H

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "rel_ast/rel_ast.h"
#include "rel_ast/relation_info.h"

namespace rel2sql {

class RelContextBuilder;

// Immutable result of preprocessing. Query-only. Produced via RelContextBuilder::Build() or Snapshot().
class RelContext {
 public:
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

 private:
  RelContext() = default;
  explicit RelContext(RelContextBuilder&& builder);
  explicit RelContext(const RelContextBuilder& builder);
  friend class RelContextBuilder;
  std::shared_ptr<RelNode> root_;
  std::unordered_map<std::string, RelationInfoTyped> relation_info_;
  std::unordered_set<std::string> ids_;
  std::unordered_set<std::string> idb_;
  std::unordered_set<std::string> edb_;
  std::unordered_set<std::string> vars_;
  std::vector<std::string> sorted_ids_;
};

}  // namespace rel2sql

#endif  // REL_AST_REL_CONTEXT_H
