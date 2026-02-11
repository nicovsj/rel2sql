#include <gtest/gtest.h>

#include <cctype>
#include <string>

#include "api/translate.h"
#include "rel_ast/rel_ast_builder.h"
#include "rewriter/binding_domain_rewriter.h"
#include "rewriter/expression_as_term_rewriter.h"
#include "rewriter/rewriter.h"

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
  Rewriter r;
  r.Clear();
  r.Add(std::make_unique<BindingDomainRewriter>());
  expr = r.Run(expr);
  return expr ? expr->ToString() : "";
}

std::string RewriteExprWithExpressionAsTerm(std::string_view input) {
  auto expr = ParseExpr(input);
  if (!expr) return "";
  Rewriter r;
  r.Clear();
  r.Add(std::make_unique<ExpressionAsTermRewriter>());
  expr = r.Run(expr);
  return expr ? expr->ToString() : "";
}

std::string RewriteExprAll(std::string_view input) {
  auto expr = ParseExpr(input);
  if (!expr) return "";
  Rewriter r;
  expr = r.Run(expr);
  return expr ? expr->ToString() : "";
}

std::string RewriteProgramWithBindingDomain(std::string_view input) {
  auto program = ParseProgram(input);
  if (!program) return "";
  Rewriter r;
  r.Clear();
  r.Add(std::make_unique<BindingDomainRewriter>());
  r.Run(program);
  return program ? program->ToString() : "";
}

std::string RewriteProgramWithExpressionAsTerm(std::string_view input) {
  auto program = ParseProgram(input);
  if (!program) return "";
  Rewriter r;
  r.Clear();
  r.Add(std::make_unique<ExpressionAsTermRewriter>());
  r.Run(program);
  return program ? program->ToString() : "";
}

std::string RewriteProgramAll(std::string_view input) {
  auto program = ParseProgram(input);
  if (!program) return "";
  Rewriter r;
  r.Run(program);
  return program ? program->ToString() : "";
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
