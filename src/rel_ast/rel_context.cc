#include "rel_ast/rel_context.h"

#include <algorithm>
#include <queue>
#include <stdexcept>

namespace rel2sql {

RelContext::RelContext() : root_(nullptr) {}

RelContext::RelContext(const RelationMap& edb_map) : root_(nullptr) {
  for (const auto& [id, rel_info] : edb_map) {
    if (rel_info.HasCustomNamedAttributes()) {
      AddEDB(id, rel_info.attribute_names);
    } else {
      AddEDB(id, rel_info.arity);
    }
  }
}

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

void RelContext::MarkAsIDB(const std::string& id) {
  ids_.insert(id);
  if (edb_.count(id)) throw std::runtime_error("IDB " + id + " already in the set of EDBs");
  vars_.erase(id);
  idb_.insert(id);
  if (relation_info_.find(id) == relation_info_.end()) {
    relation_info_.emplace(id, RelationInfoTyped(0));
  }
}

void RelContext::AddIDB(const std::string& id, int arity) {
  ids_.insert(id);
  if (edb_.count(id)) throw std::runtime_error("IDB " + id + " already in the set of EDBs");
  vars_.erase(id);
  idb_.insert(id);
  auto it = relation_info_.find(id);
  if (it != relation_info_.end()) {
    it->second.arity = arity;
    if (it->second.attribute_names.empty() ||
        it->second.attribute_names.size() != static_cast<size_t>(arity)) {
      it->second.attribute_names.clear();
      it->second.attribute_names.reserve(arity);
      for (int i = 0; i < arity; ++i) {
        it->second.attribute_names.push_back("A" + std::to_string(i + 1));
      }
    }
  } else {
    relation_info_.emplace(id, RelationInfoTyped(arity));
  }
}

void RelContext::AddEDB(const std::string& edb, int arity) {
  if (idb_.count(edb)) throw std::runtime_error("EDB " + edb + " already in the set of IDBs");
  if (vars_.count(edb)) throw std::runtime_error("EDB " + edb + " already in the set of variables");
  ids_.insert(edb);
  edb_.insert(edb);
  relation_info_.emplace(edb, RelationInfoTyped(arity));
}

void RelContext::AddEDB(const std::string& edb, const std::vector<std::string>& attribute_names) {
  if (idb_.count(edb)) throw std::runtime_error("EDB " + edb + " already in the set of IDBs");
  if (vars_.count(edb)) throw std::runtime_error("EDB " + edb + " already in the set of variables");
  ids_.insert(edb);
  edb_.insert(edb);
  relation_info_.emplace(edb, RelationInfoTyped(attribute_names));
}

void RelContext::AddVar(const std::string& var) {
  if (idb_.count(var) || edb_.count(var)) return;
  ids_.insert(var);
  vars_.insert(var);
}

void RelContext::AddDependency(const std::string& id, const std::string& dep) {
  relation_info_[id].AddDependency(dep);
}

void RelContext::RegisterRecursiveBaseDisjunct(const std::string& id, std::shared_ptr<RelAbstraction> node) {
  relation_info_[id].AddNonRecursiveDisjunct(std::move(node));
}

void RelContext::RegisterRecursiveBranch(const std::string& id, const RecursiveBranchInfoTyped& info) {
  relation_info_[id].AddRecursiveDisjunct(info);
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

void RelContext::RemoveVarsFromDependencyGraph() {
  for (const auto& id : ids_) {
    if (IsVar(id)) {
      relation_info_.erase(id);
      continue;
    }
    auto& deps = relation_info_[id].dependencies;
    deps.erase(std::remove_if(deps.begin(), deps.end(), [this](const std::string& dep) { return IsVar(dep); }),
               deps.end());
  }
}

void RelContext::ComputeTopologicalSort() {
  std::unordered_map<std::string, std::vector<std::string>> graph;
  for (const auto& [id, info] : relation_info_) {
    if (!IsVar(id)) graph[id] = info.dependencies;
  }
  std::unordered_map<std::string, int> in_degree;
  for (const auto& [id, _] : graph) in_degree[id] = 0;
  for (const auto& [id, deps] : graph) {
    for (const auto& dep : deps) {
      if (graph.count(dep)) in_degree[dep]++;
    }
  }
  std::queue<std::string> q;
  for (const auto& [id, degree] : in_degree) {
    if (degree == 0) q.push(id);
  }
  std::vector<std::string> order;
  while (!q.empty()) {
    std::string id = q.front();
    q.pop();
    order.push_back(id);
    for (const auto& dep : graph[id]) {
      if (graph.count(dep)) {
        in_degree[dep]--;
        if (in_degree[dep] == 0) q.push(dep);
      }
    }
  }
  std::reverse(order.begin(), order.end());
  sorted_ids_ = std::move(order);
}

std::unordered_set<Projection> RelContext::GetVariableDomain(const std::string& var) const {
  auto it = variable_domains_.find(var);
  if (it != variable_domains_.end()) return it->second;
  return {};
}

void RelContext::AddVariableDomain(const std::string& var, const std::unordered_set<Projection>& domain) {
  auto& existing = variable_domains_[var];
  existing.insert(domain.begin(), domain.end());
}

}  // namespace rel2sql
