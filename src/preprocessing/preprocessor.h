#ifndef PREPROCESSOR_H
#define PREPROCESSOR_H

#include <antlr4-runtime.h>

#include "preprocessing/arity_visitor.h"
#include "preprocessing/balancing_visitor.h"
#include "preprocessing/ids_visitor.h"
#include "preprocessing/lit_visitor.h"
#include "preprocessing/recursion_visitor.h"
#include "preprocessing/safe_visitor.h"
#include "preprocessing/tree_structure_visitor.h"
#include "preprocessing/vars_visitor.h"
#include "rel_ast/extended_ast.h"

namespace rel2sql {

/**
 * Preprocessor coordinates all visitors needed to preprocess a parsing tree
 * and build an ExtendedAST. This class centralizes the preprocessing step and makes
 * it clear which visitors are applied and in what order.
 *
 * To add a new preprocessing visitor:
 * 1. Create your visitor class inheriting from BaseVisitor
 * 2. Add it as a member field to Preprocessor
 * 3. Initialize it in the constructor
 * 4. Call visitor.visit(tree) in the Process method
 */
class Preprocessor {
 public:
  Preprocessor()
      : ast_(std::make_shared<RelAST>()),
        tree_structure_visitor_(ast_),
        ids_visitor_(ast_),
        arity_visitor_(ast_),
        variables_visitor_(ast_),
        recursion_visitor_(ast_),
        literal_visitor_(ast_),
        balancing_visitor_(ast_),
        safeness_visitor_(ast_) {}

  explicit Preprocessor(const rel2sql::EDBMap& edb_map)
      : ast_(std::make_shared<RelAST>(nullptr, edb_map)),
        tree_structure_visitor_(ast_),
        ids_visitor_(ast_),
        arity_visitor_(ast_),
        variables_visitor_(ast_),
        recursion_visitor_(ast_),
        literal_visitor_(ast_),
        balancing_visitor_(ast_),
        safeness_visitor_(ast_) {}

  /**
   * Process the parsing tree with all preprocessing visitors in the correct order.
   * @param tree The parsing tree to process
   * @return ExtendedAST containing the processed tree and extended data
   */
  RelAST Process(antlr4::ParserRuleContext* tree) {
    ast_->SetParseTree(tree);

    tree_structure_visitor_.visit(tree);
    ids_visitor_.visit(tree);
    arity_visitor_.visit(tree);
    variables_visitor_.visit(tree);
    recursion_visitor_.visit(tree);
    literal_visitor_.visit(tree);
    balancing_visitor_.visit(tree);
    safeness_visitor_.visit(tree);

    return *ast_;
  }

 private:
  std::shared_ptr<RelAST> ast_;
  TreeStructureVisitor tree_structure_visitor_;
  IDsVisitor ids_visitor_;
  ArityVisitor arity_visitor_;
  VariablesVisitor variables_visitor_;
  RecursionVisitor recursion_visitor_;
  LiteralVisitor literal_visitor_;
  BalancingVisitor balancing_visitor_;
  SafeVisitor safeness_visitor_;
};

}  // namespace rel2sql

#endif  // PREPROCESSOR_H
