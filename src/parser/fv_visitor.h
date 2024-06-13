#ifndef FV_VISITOR_H
#define FV_VISITOR_H

#include <antlr4-runtime.h>

#include "parser/generated/PrunedCoreRelParserBaseVisitor.h"

struct ExtendedData {
  std::set<std::string> variables;
  std::set<std::string> free_variables;

  void InplaceUnion(const ExtendedData &other) {
    variables.insert(other.variables.begin(), other.variables.end());
    free_variables.insert(other.free_variables.begin(), other.free_variables.end());
  }

  void InplaceDifference(const ExtendedData &other) {
    ExtendedData result;
    variables.insert(other.variables.begin(), other.variables.end());
    for (const auto &var : other.free_variables) {
      free_variables.erase(var);
    }
  }
};

struct ExtendedAST {
  antlr4::ParserRuleContext *root;
  std::unordered_map<antlr4::ParserRuleContext *, ExtendedData> extended_data;
};

class ExtendedASTVisitor : public rel_parser::PrunedCoreRelParserBaseVisitor {
 public:
  std::any visitProgram(rel_parser::PrunedCoreRelParser::ProgramContext *ctx) override;

  std::any visitRelDef(rel_parser::PrunedCoreRelParser::RelDefContext *ctx) override;

  std::any visitRelAbs(rel_parser::PrunedCoreRelParser::RelAbsContext *ctx) override;

  // Expression branches

  std::any visitLitExpr(rel_parser::PrunedCoreRelParser::LitExprContext *ctx) override;

  std::any visitIDExpr(rel_parser::PrunedCoreRelParser::IDExprContext *ctx) override;

  std::any visitProductExpr(rel_parser::PrunedCoreRelParser::ProductExprContext *ctx) override;

  std::any visitConditionExpr(rel_parser::PrunedCoreRelParser::ConditionExprContext *ctx) override;

  std::any visitRelAbsExpr(rel_parser::PrunedCoreRelParser::RelAbsExprContext *ctx) override;

  std::any visitFormulaExpr(rel_parser::PrunedCoreRelParser::FormulaExprContext *ctx) override;

  std::any visitBindingsExpr(rel_parser::PrunedCoreRelParser::BindingsExprContext *ctx) override;

  std::any visitBindingsFormula(rel_parser::PrunedCoreRelParser::BindingsFormulaContext *ctx) override;

  std::any visitPartialAppl(rel_parser::PrunedCoreRelParser::PartialApplContext *ctx) override;

  //  Formula branches

  std::any visitFullAppl(rel_parser::PrunedCoreRelParser::FullApplContext *ctx) override;

  std::any visitBinOp(rel_parser::PrunedCoreRelParser::BinOpContext *ctx) override;

  std::any visitUnOp(rel_parser::PrunedCoreRelParser::UnOpContext *ctx) override;

  std::any visitQuantification(rel_parser::PrunedCoreRelParser::QuantificationContext *ctx) override;

  std::any visitParen(rel_parser::PrunedCoreRelParser::ParenContext *ctx) override;

  //  Binding branches

  std::any visitBindingInner(rel_parser::PrunedCoreRelParser::BindingInnerContext *ctx) override;

  std::any visitBinding(rel_parser::PrunedCoreRelParser::BindingContext *ctx) override;

  std::any visitApplBase(rel_parser::PrunedCoreRelParser::ApplBaseContext *ctx) override;

  std::any visitApplParams(rel_parser::PrunedCoreRelParser::ApplParamsContext *ctx) override;

  std::any visitApplParam(rel_parser::PrunedCoreRelParser::ApplParamContext *ctx) override;

 private:
  std::unordered_map<antlr4::ParserRuleContext *, ExtendedData> data_;
};

#endif  // FV_VISITOR_H
