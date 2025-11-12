#ifndef PREPROCESSOR_H
#define PREPROCESSOR_H

#include <antlr4-runtime.h>

#include "preproc/arity_visitor.h"
#include "preproc/balancing_visitor.h"
#include "preproc/ids_visitor.h"
#include "preproc/lit_visitor.h"
#include "preproc/recursion_visitor.h"
#include "preproc/safe_visitor.h"
#include "preproc/vars_visitor.h"
#include "structs/extended_ast.h"

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
  Preprocessor() : ast_data_(std::make_shared<ExtendedASTData>()),
                   ids_visitor_(ast_data_),
                   arity_visitor_(ast_data_),
                   variables_visitor_(ast_data_),
                   recursion_visitor_(ast_data_),
                   literal_visitor_(ast_data_),
                   balancing_visitor_(ast_data_),
                   safeness_visitor_(ast_data_) {}

  explicit Preprocessor(const rel2sql::EDBMap& edb_map)
      : ast_data_(std::make_shared<ExtendedASTData>(edb_map)),
        ids_visitor_(ast_data_),
        arity_visitor_(ast_data_),
        variables_visitor_(ast_data_),
        recursion_visitor_(ast_data_),
        literal_visitor_(ast_data_),
        balancing_visitor_(ast_data_),
        safeness_visitor_(ast_data_) {}

  /**
   * Process the parsing tree with all preprocessing visitors in the correct order.
   * @param tree The parsing tree to process
   * @return ExtendedAST containing the processed tree and extended data
   */
  ExtendedAST Process(antlr4::ParserRuleContext* tree) {
    ids_visitor_.visit(tree);
    arity_visitor_.visit(tree);
    variables_visitor_.visit(tree);
    recursion_visitor_.visit(tree);
    literal_visitor_.visit(tree);
    balancing_visitor_.visit(tree);
    safeness_visitor_.visit(tree);

    return ExtendedAST{tree, ast_data_};
  }

 private:
  std::shared_ptr<ExtendedASTData> ast_data_;
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
