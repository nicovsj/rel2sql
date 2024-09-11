#include "opt_visitor.h"

#include "const_replacer.h"

namespace sql::ast {

void OptimizerVisitor::Visit(Expression& expr) { expr.Accept(*this); }

bool OptimizerVisitor::TryReplaceConstantInWhere(const std::shared_ptr<Source>& source, FromStatement& from_statement) {
  if (auto select = std::dynamic_pointer_cast<SelectStatement>(source->sourceable)) {
    if (select->columns.size() == 1) {
      if (auto term_selectable = std::dynamic_pointer_cast<TermSelectable>(select->columns[0])) {
        if (auto constant = std::dynamic_pointer_cast<Constant>(term_selectable->term)) {
          // Replace the constant in the WHERE condition
          if (from_statement.where) {
            ConstantReplacer replacer(source->Alias(), term_selectable->Alias(), constant);
            from_statement.where.value()->Accept(replacer);
            return true;
          }
        }
      }
    }
  }
  return false;
}

void OptimizerVisitor::Visit(FromStatement& from_statement) {
  std::vector<std::shared_ptr<Source>> new_sources;
  for (auto& source : from_statement.sources) {
    if (!TryReplaceConstantInWhere(source, from_statement)) {
      new_sources.push_back(source);
    }
  }
  from_statement.sources = new_sources;
}

}  // namespace sql::ast
