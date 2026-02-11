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

void IDsVisitor::Visit(RelProgram& node) {
  for (auto& def : node.defs) {
    if (def) def->Accept(*this);
  }
  container_->RemoveVarsFromDependencyGraph();
  container_->ComputeTopologicalSort();
}

void IDsVisitor::Visit(RelDef& node) {
  std::string id = node.name;
  if (IsSQLKeyword(id)) {
    throw SemanticException("Relation name '" + id + "' is a reserved SQL keyword", ErrorCode::RESERVED_RELATION_NAME,
                            GetSourceLocationFromNode(&node));
  }
  container_->MarkAsIDB(id);
  current_def_id_ = id;
  deps_.clear();
  if (node.body) node.body->Accept(*this);
  for (const auto& dep : deps_) {
    container_->AddDependency(id, dep);
  }
}

void IDsVisitor::Visit(RelAbstraction& node) {
  for (auto& expr : node.exprs) {
    if (expr) expr->Accept(*this);
  }
}

void IDsVisitor::Visit(RelIDTerm& node) {
  container_->AddVar(node.id);
  deps_.insert(node.id);  // Collect for dependency graph; vars removed in RemoveVarsFromDependencyGraph
}

void IDsVisitor::Visit(RelNumTerm& node) { RelASTVisitor::Visit(node); }

void IDsVisitor::Visit(RelOpTerm& node) { RelASTVisitor::Visit(node); }

void IDsVisitor::Visit(RelParenthesisTerm& node) { RelASTVisitor::Visit(node); }

void IDsVisitor::Visit(RelLitExpr& node) { RelASTVisitor::Visit(node); }

void IDsVisitor::Visit(RelTermExpr& node) { RelASTVisitor::Visit(node); }

void IDsVisitor::Visit(RelProductExpr& node) { RelASTVisitor::Visit(node); }

void IDsVisitor::Visit(RelConditionExpr& node) { RelASTVisitor::Visit(node); }

void IDsVisitor::Visit(RelAbstractionExpr& node) { RelASTVisitor::Visit(node); }

void IDsVisitor::Visit(RelFormulaExpr& node) { RelASTVisitor::Visit(node); }

void IDsVisitor::Visit(RelBindingsExpr& node) {
  AddDepsFromBindings(node.bindings);
  RelASTVisitor::Visit(node);
}

void IDsVisitor::Visit(RelBindingsFormula& node) {
  AddDepsFromBindings(node.bindings);
  RelASTVisitor::Visit(node);
}

void IDsVisitor::Visit(RelPartialAppl& node) {
  AddDepsFromBase(node.base);
  AddDepsFromParams(node.params);
  RelASTVisitor::Visit(node);
}

void IDsVisitor::Visit(RelFullAppl& node) {
  AddDepsFromBase(node.base);
  AddDepsFromParams(node.params);
  RelASTVisitor::Visit(node);
}

void IDsVisitor::Visit(RelBinOp& node) { RelASTVisitor::Visit(node); }

void IDsVisitor::Visit(RelUnOp& node) { RelASTVisitor::Visit(node); }

void IDsVisitor::Visit(RelQuantification& node) { RelASTVisitor::Visit(node); }

void IDsVisitor::Visit(RelParen& node) { RelASTVisitor::Visit(node); }

void IDsVisitor::Visit(RelComparison& node) { RelASTVisitor::Visit(node); }

void IDsVisitor::AddDepsFromBase(const std::shared_ptr<RelApplBase>& base) {
  if (auto* id_base = dynamic_cast<RelIDApplBase*>(base.get())) {
    deps_.insert(id_base->id);
  } else if (auto* abs_base = dynamic_cast<RelAbstractionApplBase*>(base.get())) {
    if (abs_base->rel_abs) abs_base->rel_abs->Accept(*this);
  }
}

void IDsVisitor::AddDepsFromParams(const std::vector<std::shared_ptr<RelApplParam>>& params) {
  for (const auto& param : params) {
    if (param && param->GetExpr()) param->GetExpr()->Accept(*this);
  }
}

void IDsVisitor::AddDepsFromBindings(const std::vector<std::shared_ptr<RelBinding>>& bindings) {
  for (const auto& binding : bindings) {
    if (auto* vb = dynamic_cast<RelVarBinding*>(binding.get())) {
      if (vb->domain) deps_.insert(*vb->domain);
      container_->AddVar(vb->id);
    }
  }
}

}  // namespace rel2sql
