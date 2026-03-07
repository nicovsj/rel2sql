#include <gtest/gtest.h>

#include <cctype>
#include <memory>
#include <string>

#include "api/translate.h"
#include "rel_ast/rel_ast.h"
#include "rel_ast/rel_ast_builder.h"
#include "rel_ast/rel_context_builder.h"
#include "rel_ast/relation_info.h"
#include "rewriter/binding_rewriter.h"
#include "rewriter/term_rewriter.h"
#include "rewriter/wildcard_rewriter.h"

namespace rel2sql {

namespace {

std::string NormalizeWhitespace(std::string s) {
  std::string result;
  bool in_space = true;
  for (unsigned char c : s) {
    if (std::isspace(c)) {
      if (!in_space) {
        result += ' ';
        in_space = true;
      }
    } else {
      result += static_cast<char>(c);
      in_space = false;
    }
  }
  while (!result.empty() && result.back() == ' ') result.pop_back();
  return result;
}

std::shared_ptr<RelExpr> ParseExpr(std::string_view input) {
  auto parser = GetParser(input);
  auto tree = parser->expr();
  RelASTBuilder builder;
  return builder.BuildFromExpr(tree);
}

std::shared_ptr<RelProgram> ParseProgram(std::string_view input) {
  auto parser = GetParser(input);
  auto tree = parser->program();
  RelASTBuilder builder;
  return builder.Build(tree);
}

}  // namespace

std::string RewriteExprWithBindingDomain(std::string_view input) {
  auto expr = ParseExpr(input);
  if (!expr) return "";
  BindingRewriter r;
  expr = r.Visit(expr);
  return expr ? expr->ToString() : "";
}

std::string RewriteExprWithExpressionAsTerm(std::string_view input) {
  auto expr = ParseExpr(input);
  if (!expr) return "";
  TermRewriter r;
  expr = r.Visit(expr);
  return expr ? expr->ToString() : "";
}

std::string RewriteExprAll(std::string_view input) {
  auto expr = ParseExpr(input);
  if (!expr) return "";
  WildcardRewriter u;
  BindingRewriter b;
  TermRewriter e;
  expr = u.Visit(expr);
  expr = b.Visit(expr);
  expr = e.Visit(expr);
  return expr ? expr->ToString() : "";
}

std::string RewriteProgramWithBindingDomain(std::string_view input) {
  auto program = ParseProgram(input);
  if (!program) return "";
  BindingRewriter r;
  program = std::dynamic_pointer_cast<RelProgram>(r.Visit(program));
  return program ? program->ToString() : "";
}

std::string RewriteProgramWithExpressionAsTerm(std::string_view input) {
  auto program = ParseProgram(input);
  if (!program) return "";
  TermRewriter r;
  program = std::dynamic_pointer_cast<RelProgram>(r.Visit(program));
  return program ? program->ToString() : "";
}

std::string RewriteProgramAll(std::string_view input) {
  auto program = ParseProgram(input);
  if (!program) return "";
  WildcardRewriter u;
  BindingRewriter b;
  TermRewriter e;
  program = std::dynamic_pointer_cast<RelProgram>(u.Visit(program));
  program = std::dynamic_pointer_cast<RelProgram>(b.Visit(program));
  program = std::dynamic_pointer_cast<RelProgram>(e.Visit(program));
  return program ? program->ToString() : "";
}

std::string RewriteProgramWithUnderscore(std::string_view input,
                                         const RelationMap& edb_map = {}) {
  auto program = ParseProgram(input);
  if (!program) return "";

  RelContextBuilder context_builder(edb_map);
  WildcardRewriter u(&context_builder);

  program = std::dynamic_pointer_cast<RelProgram>(u.Visit(program));
  return program ? program->ToString() : "";
}

TEST(RewriterTest, UnderscoreFullApplication) {
  auto result = RewriteProgramWithUnderscore("def R { A(_, x) }");
  EXPECT_EQ(NormalizeWhitespace(RewriteProgramWithUnderscore("def R { A(_, x) }")),
            NormalizeWhitespace("def R {exists( (_z0) | A(_z0, x))}"));
}

TEST(RewriterTest, UnderscoreFullApplicationMultiple) {
  EXPECT_EQ(NormalizeWhitespace(RewriteProgramWithUnderscore("def R { B(_, _, x) }")),
            NormalizeWhitespace("def R {exists( (_z0, _z1) | B(_z0, _z1, x))}"));
}

TEST(RewriterTest, UnderscorePartialApplication) {
  RelationMap edb;
  edb["A"] = RelationInfo(4);  // A has 4 columns; A[x, _, y] leaves 1 for rest
  EXPECT_EQ(NormalizeWhitespace(RewriteProgramWithUnderscore("def R { A[x, _, y] }", edb)),
            NormalizeWhitespace("def R {(_z1) : exists( (_z0) | A(x, _z0, y, _z1))}"));
}

TEST(RewriterTest, BindingDomainExpr) {
  EXPECT_EQ(NormalizeWhitespace(RewriteExprWithBindingDomain("[x in A, y in B]: C[x, y]")),
            NormalizeWhitespace("[x, y] : C[x, y] where A(x) and B(y)"));
}

TEST(RewriterTest, ExpressionAsTermBindingsBody) {
  EXPECT_EQ(NormalizeWhitespace(RewriteExprWithExpressionAsTerm("[x, y]: x + y + 1")),
            NormalizeWhitespace("[x, y] : {(_x0) : _x0 = x + y + 1}"));
}

TEST(RewriterTest, ExpressionAsTermProduct) {
  EXPECT_EQ(NormalizeWhitespace(RewriteExprWithExpressionAsTerm("(x, y + 1)")),
            NormalizeWhitespace("(x, {(_x0) : _x0 = y + 1})"));
}

TEST(RewriterTest, ExpressionAsTermAbstraction) {
  EXPECT_EQ(NormalizeWhitespace(RewriteExprWithExpressionAsTerm("{x; y + 1}")),
            NormalizeWhitespace("{x; (_x0) : _x0 = y + 1}"));
}

TEST(RewriterTest, AllRewritersExpr) {
  EXPECT_EQ(NormalizeWhitespace(RewriteExprAll("[x in A, y in A]: x + y + 1")),
            NormalizeWhitespace("[x, y] : {(_x0) : _x0 = x + y + 1 and A(x) and A(y)}"));
}

TEST(RewriterTest, BindingDomainProgram) {
  EXPECT_EQ(NormalizeWhitespace(RewriteProgramWithBindingDomain("def R { [x in A]: x }")),
            NormalizeWhitespace("def R {[x] : x where A(x)}"));
}

}  // namespace rel2sql
