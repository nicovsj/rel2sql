#include "preprocessing/ids_visitor.h"

#include <antlr4-runtime.h>

#include <cctype>

#include "support/exceptions.h"

namespace rel2sql {

namespace {

const std::unordered_set<std::string> kSQLKeywords = {
    "SELECT", "DISTINCT", "FROM",      "WHERE", "GROUP",  "BY",    "WITH", "AS",  "UNION", "ALL",  "VALUES",
    "CASE",   "WHEN",     "THEN",      "END",   "EXISTS", "IN",    "NOT",  "AND", "OR",    "TRUE", "FALSE",
    "CREATE", "REPLACE",  "RECURSIVE", "VIEW",  "TABLE",  "COUNT", "SUM",  "AVG", "MIN",   "MAX",  "JOIN"};

bool IsSQLKeyword(const std::string& identifier) {
  std::string normalized;
  normalized.reserve(identifier.size());
  for (unsigned char ch : identifier) {
    normalized.push_back(static_cast<char>(std::toupper(ch)));
  }
  return kSQLKeywords.find(normalized) != kSQLKeywords.end();
}

SourceLocation GetSourceLocationFromNode(RelNode* node) {
  if (!node || !node->ctx) return SourceLocation(0, 0);
  auto* ctx = node->ctx;
  int line = ctx->getStart() ? ctx->getStart()->getLine() : 0;
  int column = ctx->getStart() ? ctx->getStart()->getCharPositionInLine() : 0;
  std::string text_snippet = ctx->getText();
  if (text_snippet.length() > 100) text_snippet = text_snippet.substr(0, 97) + "...";
  return SourceLocation(line, column, text_snippet);
}

}  // namespace

std::shared_ptr<RelProgram> IDsVisitor::Visit(const std::shared_ptr<RelProgram>& node) {
  for (auto& def : node->defs) {
    if (def) Visit(def);
  }
  builder_->RemoveVarsFromDependencyGraph();
  builder_->ComputeTopologicalSort();
  return node;
}

std::shared_ptr<RelDef> IDsVisitor::Visit(const std::shared_ptr<RelDef>& node) {
  std::string id = node->name;
  if (IsSQLKeyword(id)) {
    throw SemanticException("Relation name '" + id + "' is a reserved SQL keyword", ErrorCode::RESERVED_RELATION_NAME,
                            GetSourceLocationFromNode(node.get()));
  }
  builder_->MarkAsIDB(id);
  current_def_id_ = id;
  deps_.clear();
  if (node->body) Visit(node->body);
  for (const auto& dep : deps_) {
    builder_->AddDependency(id, dep);
  }
  return node;
}

std::shared_ptr<RelAbstraction> IDsVisitor::Visit(const std::shared_ptr<RelAbstraction>& node) {
  for (auto& expr : node->exprs) {
    if (expr) Visit(expr);
  }
  return node;
}

std::shared_ptr<RelTerm> IDsVisitor::Visit(const std::shared_ptr<RelIDTerm>& node) {
  builder_->AddVar(node->id);
  deps_.insert(node->id);  // Collect for dependency graph; vars removed in RemoveVarsFromDependencyGraph
  return node;
}

std::shared_ptr<RelExpr> IDsVisitor::Visit(const std::shared_ptr<RelBindingsExpr>& node) {
  AddDepsFromBindings(node->bindings);
  return BaseRelVisitor::Visit(node);
}

std::shared_ptr<RelExpr> IDsVisitor::Visit(const std::shared_ptr<RelBindingsFormula>& node) {
  AddDepsFromBindings(node->bindings);
  return BaseRelVisitor::Visit(node);
}

std::shared_ptr<RelExpr> IDsVisitor::Visit(const std::shared_ptr<RelPartialAppl>& node) {
  AddDepsFromBase(node->base);
  AddDepsFromParams(node->params);
  return node;
}

std::shared_ptr<RelFormula> IDsVisitor::Visit(const std::shared_ptr<RelFullAppl>& node) {
  AddDepsFromBase(node->base);
  AddDepsFromParams(node->params);
  return BaseRelVisitor::Visit(node);
}

void IDsVisitor::AddDepsFromBase(const std::shared_ptr<RelApplBase>& base) {
  if (auto* id_base = dynamic_cast<RelIDApplBase*>(base.get())) {
    deps_.insert(id_base->id);
  } else if (auto* abs_base = dynamic_cast<RelAbstractionApplBase*>(base.get())) {
    if (abs_base->rel_abs) Visit(abs_base->rel_abs);
  }
}

void IDsVisitor::AddDepsFromParams(const std::vector<std::shared_ptr<RelApplParam>>& params) {
  for (const auto& param : params) {
    if (param && param->GetExpr()) Visit(param->GetExpr());
  }
}

void IDsVisitor::AddDepsFromBindings(const std::vector<std::shared_ptr<RelBinding>>& bindings) {
  for (const auto& binding : bindings) {
    if (auto* vb = dynamic_cast<RelVarBinding*>(binding.get())) {
      if (vb->domain) deps_.insert(*vb->domain);
      builder_->AddVar(vb->id);
    }
  }
}

}  // namespace rel2sql
