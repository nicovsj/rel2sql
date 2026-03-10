#include "rel_ast/rel_context_builder.h"

#include <algorithm>
#include <queue>
#include <stdexcept>

#include "preprocessing/arity_visitor.h"
#include "preprocessing/ids_visitor.h"
#include "preprocessing/lit_visitor.h"
#include "preprocessing/recursion_visitor.h"
#include "preprocessing/safety_inferrer.h"
#include "preprocessing/term_polynomial_visitor.h"
#include "preprocessing/vars_visitor.h"
#include "rel_ast/rel_context.h"
#include "rewriter/binding_rewriter.h"
#include "rewriter/term_rewriter.h"
#include "rewriter/wildcard_rewriter.h"

namespace rel2sql {

RelContextBuilder::RelContextBuilder() : root_(nullptr) {}

RelContextBuilder::RelContextBuilder(const RelationMap& edb_map) : root_(nullptr) {
  for (const auto& [id, rel_info] : edb_map) {
    if (rel_info.HasCustomNamedAttributes()) {
      AddEDB(id, rel_info.attribute_names);
    } else {
      AddEDB(id, rel_info.arity);
    }
  }
}

void RelContextBuilder::MarkAsIDB(const std::string& id) {
  ids_.insert(id);
  if (edb_.count(id)) throw std::runtime_error("IDB " + id + " already in the set of EDBs");
  vars_.erase(id);
  idb_.insert(id);
  if (relation_info_.find(id) == relation_info_.end()) {
    relation_info_.emplace(id, RelationInfoTyped(0));
  }
}

void RelContextBuilder::AddIDB(const std::string& id, int arity) {
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

void RelContextBuilder::AddEDB(const std::string& edb, int arity) {
  if (idb_.count(edb)) throw std::runtime_error("EDB " + edb + " already in the set of IDBs");
  if (vars_.count(edb)) throw std::runtime_error("EDB " + edb + " already in the set of variables");
  ids_.insert(edb);
  edb_.insert(edb);
  relation_info_.emplace(edb, RelationInfoTyped(arity));
}

void RelContextBuilder::AddEDB(const std::string& edb,
                                const std::vector<std::string>& attribute_names) {
  if (idb_.count(edb)) throw std::runtime_error("EDB " + edb + " already in the set of IDBs");
  if (vars_.count(edb)) throw std::runtime_error("EDB " + edb + " already in the set of variables");
  ids_.insert(edb);
  edb_.insert(edb);
  relation_info_.emplace(edb, RelationInfoTyped(attribute_names));
}

void RelContextBuilder::AddVar(const std::string& var) {
  if (idb_.count(var) || edb_.count(var)) return;
  ids_.insert(var);
  vars_.insert(var);
}

void RelContextBuilder::AddDependency(const std::string& id, const std::string& dep) {
  relation_info_[id].AddDependency(dep);
}

void RelContextBuilder::RegisterRecursiveBaseDisjunct(const std::string& id,
                                                       std::shared_ptr<RelUnion> node) {
  relation_info_[id].AddNonRecursiveDisjunct(std::move(node));
}

void RelContextBuilder::RegisterRecursiveBranch(const std::string& id,
                                                 const RecursiveBranchInfoTyped& info) {
  relation_info_[id].AddRecursiveDisjunct(info);
}

void RelContextBuilder::RemoveVarsFromDependencyGraph() {
  for (const auto& id : ids_) {
    if (IsVar(id)) {
      relation_info_.erase(id);
      continue;
    }
    auto& deps = relation_info_[id].dependencies;
    deps.erase(std::remove_if(deps.begin(), deps.end(), [this](const std::string& dep) {
                 return IsVar(dep);
               }),
               deps.end());
  }
}

void RelContextBuilder::ComputeTopologicalSort() {
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

int RelContextBuilder::GetArity(const std::string& id) const {
  auto rel_info = GetRelationInfo(id);
  if (!rel_info) return 1;
  return rel_info->arity;
}

std::optional<RelationInfoTyped> RelContextBuilder::GetRelationInfo(const std::string& edb) const {
  auto it = relation_info_.find(edb);
  if (it != relation_info_.end()) return it->second;
  return std::nullopt;
}

std::optional<RecursionInfoTyped> RelContextBuilder::GetRecursionMetadata(
    const std::string& id) const {
  auto rel_info = GetRelationInfo(id);
  if (!rel_info || rel_info->recursion_metadata.empty()) return std::nullopt;
  return rel_info->recursion_metadata;
}

bool RelContextBuilder::IsIDB(const std::string& id) const { return idb_.count(id) != 0; }

bool RelContextBuilder::IsEDB(const std::string& id) const { return edb_.count(id) != 0; }

bool RelContextBuilder::IsRelation(const std::string& id) const {
  return IsIDB(id) || IsEDB(id);
}

bool RelContextBuilder::IsVar(const std::string& var) const { return vars_.count(var) != 0; }

bool RelContextBuilder::IsID(const std::string& id) const { return ids_.count(id) != 0; }

const std::vector<std::string>& RelContextBuilder::SortedIDs() const { return sorted_ids_; }

RelContext RelContextBuilder::Process(std::shared_ptr<RelNode> root) {
  root = RunPipeline(std::move(root));
  SetRoot(root);
  return RelContext(std::move(*this));
}

std::shared_ptr<RelNode> RelContextBuilder::RunPipeline(std::shared_ptr<RelNode> root) {
  BindingRewriter binding_domain_rewriter;
  root = binding_domain_rewriter.Visit(root);

  TermRewriter expr_as_term_rewriter;
  root = expr_as_term_rewriter.Visit(root);

  IDsVisitor ids_visitor(this);
  ids_visitor.Visit(root);

  ArityVisitor arity_visitor(this);
  arity_visitor.Visit(root);

  WildcardRewriter underscore_rewriter(this);
  root = underscore_rewriter.Visit(root);

  IDsVisitor ids_visitor2(this);
  ids_visitor2.Visit(root);

  ArityVisitor arity_visitor2(this);
  arity_visitor2.Visit(root);

  VariablesVisitor vars_visitor(this);
  vars_visitor.Visit(root);

  LiteralVisitor lit_visitor;
  lit_visitor.Visit(root);

  TermPolynomialVisitor term_poly_visitor;
  term_poly_visitor.Visit(root);

  RecursionVisitor recursion_visitor(this);
  recursion_visitor.Visit(root);

  SafetyInferrer safety_inferrer(this);
  safety_inferrer.Run(root);

  return root;
}

RelContext RelContextBuilder::Build() {
  return RelContext(std::move(*this));
}

RelContext RelContextBuilder::Snapshot() const {
  return RelContext(*this);
}

}  // namespace rel2sql
