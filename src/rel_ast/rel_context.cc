#include "rel_ast/rel_context.h"

#include "rel_ast/rel_context_builder.h"

namespace rel2sql {

RelContext::RelContext(RelContextBuilder&& builder)
    : root_(std::move(builder.root_)),
      relation_info_(std::move(builder.relation_info_)),
      ids_(std::move(builder.ids_)),
      idb_(std::move(builder.idb_)),
      edb_(std::move(builder.edb_)),
      vars_(std::move(builder.vars_)),
      sorted_ids_(std::move(builder.sorted_ids_)) {}

RelContext::RelContext(const RelContextBuilder& builder)
    : root_(builder.Root()),
      relation_info_(builder.relation_info_),
      ids_(builder.ids_),
      idb_(builder.idb_),
      edb_(builder.edb_),
      vars_(builder.vars_),
      sorted_ids_(builder.sorted_ids_) {}

int RelContext::GetArity(const std::string& id) const {
  auto rel_info = GetRelationInfo(id);
  if (!rel_info) return 1;
  return rel_info->arity;
}

std::optional<RelationInfoTyped> RelContext::GetRelationInfo(const std::string& edb) const {
  auto it = relation_info_.find(edb);
  if (it != relation_info_.end()) return it->second;
  return std::nullopt;
}

std::optional<RecursionInfoTyped> RelContext::GetRecursionMetadata(const std::string& id) const {
  auto rel_info = GetRelationInfo(id);
  if (!rel_info || rel_info->recursion_metadata.empty()) return std::nullopt;
  return rel_info->recursion_metadata;
}

bool RelContext::IsIDB(const std::string& id) const { return idb_.count(id) != 0; }

bool RelContext::IsEDB(const std::string& id) const { return edb_.count(id) != 0; }

bool RelContext::IsRelation(const std::string& id) const { return IsIDB(id) || IsEDB(id); }

bool RelContext::IsVar(const std::string& var) const { return vars_.count(var) != 0; }

bool RelContext::IsID(const std::string& id) const { return ids_.count(id) != 0; }

const std::vector<std::string>& RelContext::SortedIDs() const { return sorted_ids_; }

}  // namespace rel2sql
