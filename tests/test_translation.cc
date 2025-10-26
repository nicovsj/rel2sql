// cspell:ignore GTEST
#include <gtest/gtest.h>

#include "structs/edb_info.h"
#include "structs/sql_ast.h"
#include "translate.h"

namespace rel2sql {

// Template function to eliminate code duplication
template <typename ContextType, typename DataType>
std::string TranslateRel(const std::string& input, ContextType* (*parse_method)(rel_parser::PrunedCoreRelParser*),
                         DataType&& ast_data) {
  auto parser = GetParser(input);
  auto tree = dynamic_cast<ContextType*>(parse_method(parser.get()));
  auto ast = GetExtendedASTFromTree(tree, ast_data);
  auto result = GetSQLFromTree(tree, ast);
  std::ostringstream os;
  os << *result;
  return os.str();
}

std::string TranslateRelProgram(const std::string& input,
                                std::unordered_map<std::string, int> external_arity_map = {}) {
  /*
   * This function takes a string CoreRel program input and returns the SQL translation.
   */
  auto ast_data = std::make_shared<ExtendedASTData>(rel2sql::edb_utils::FromArityMap(external_arity_map));
  return TranslateRel<rel_parser::PrunedCoreRelParser::ProgramContext>(
      input, [](rel_parser::PrunedCoreRelParser* p) { return p->program(); }, ast_data);
}

std::string TranslateRelDef(const std::string& input, std::unordered_map<std::string, int> external_arity_map = {}) {
  /*
   * This function takes a string CoreRel program input and returns the SQL translation.
   */
  auto ast_data = std::make_shared<ExtendedASTData>(rel2sql::edb_utils::FromArityMap(external_arity_map));
  return TranslateRel<rel_parser::PrunedCoreRelParser::RelDefContext>(
      input, [](rel_parser::PrunedCoreRelParser* p) { return p->relDef(); }, ast_data);
}

std::string TranslateRelFormula(const std::string& input,
                                std::unordered_map<std::string, int> external_arity_map = {}) {
  /*
   * This function takes a string CoreRel formula input and returns the SQL translation.
   */
  auto ast_data = std::make_shared<ExtendedASTData>(rel2sql::edb_utils::FromArityMap(external_arity_map));
  return TranslateRel<rel_parser::PrunedCoreRelParser::FormulaContext>(
      input, [](rel_parser::PrunedCoreRelParser* p) { return p->formula(); }, ast_data);
}

std::string TranslateRelExpression(const std::string& input,
                                   std::unordered_map<std::string, int> external_arity_map = {}) {
  /*
   * This function takes a string CoreRel expression input and returns the SQL translation.
   */
  auto ast_data = std::make_shared<ExtendedASTData>(rel2sql::edb_utils::FromArityMap(external_arity_map));
  return TranslateRel<rel_parser::PrunedCoreRelParser::ExprContext>(
      input, [](rel_parser::PrunedCoreRelParser* p) { return p->expr(); }, ast_data);
}

std::string TranslateRelProgramWithEDB(const std::string& input, const rel2sql::EDBMap& edb_map) {
  /*
   * This function takes a string CoreRel program input and returns the SQL translation using EDB info.
   */
  auto ast_data = std::make_shared<ExtendedASTData>(edb_map);
  return TranslateRel<rel_parser::PrunedCoreRelParser::ProgramContext>(
      input, [](rel_parser::PrunedCoreRelParser* p) { return p->program(); }, ast_data);
}

std::string TranslateRelDefWithEDB(const std::string& input, const rel2sql::EDBMap& edb_map) {
  /*
   * This function takes a string CoreRel definition input and returns the SQL translation using EDB info.
   */
  auto ast_data = std::make_shared<ExtendedASTData>(edb_map);
  return TranslateRel<rel_parser::PrunedCoreRelParser::RelDefContext>(
      input, [](rel_parser::PrunedCoreRelParser* p) { return p->relDef(); }, ast_data);
}

std::string TranslateRelFormulaWithEDB(const std::string& input, const rel2sql::EDBMap& edb_map) {
  /*
   * This function takes a string CoreRel formula input and returns the SQL translation using EDB info.
   */
  auto ast_data = std::make_shared<ExtendedASTData>(edb_map);
  return TranslateRel<rel_parser::PrunedCoreRelParser::FormulaContext>(
      input, [](rel_parser::PrunedCoreRelParser* p) { return p->formula(); }, ast_data);
}

std::string TranslateRelExpressionWithEDB(const std::string& input, const rel2sql::EDBMap& edb_map) {
  /*
   * This function takes a string CoreRel expression input and returns the SQL translation using EDB info.
   */
  auto ast_data = std::make_shared<ExtendedASTData>(edb_map);
  return TranslateRel<rel_parser::PrunedCoreRelParser::ExprContext>(
      input, [](rel_parser::PrunedCoreRelParser* p) { return p->expr(); }, ast_data);
}

TEST(SQLVisitorTest, EqualitySpecialCondition) {
  std::string input = "F(x) and G(x)";

  auto parser = GetParser(input);

  auto tree = dynamic_cast<rel_parser::PrunedCoreRelParser::BinOpContext*>(parser->formula());

  auto ast = GetExtendedASTFromTree(tree);

  auto table_F = std::make_shared<sql::ast::Source>(std::make_shared<sql::ast::Table>("F", 1));
  auto table_G = std::make_shared<sql::ast::Source>(std::make_shared<sql::ast::Table>("G", 1));

  ast.Get(tree->lhs).sql_expression = table_F;
  ast.Get(tree->rhs).sql_expression = table_G;

  auto visitor = SQLVisitor(ast.Data());

  auto condition = visitor.EqualityShorthand(std::vector<antlr4::ParserRuleContext*>{tree->lhs, tree->rhs});

  std::ostringstream os;

  os << *condition;

  EXPECT_EQ(os.str(), "F.x = G.x");
}

TEST(SQLVisitorTest, SpecialVarList) {
  std::string input = "F(x) and G(x, y)";

  auto parser = GetParser(input);

  auto tree = dynamic_cast<rel_parser::PrunedCoreRelParser::BinOpContext*>(parser->formula());

  auto ast = GetExtendedASTFromTree(tree);

  auto table_F = std::make_shared<sql::ast::Source>(std::make_shared<sql::ast::Table>("F", 1));
  auto table_G = std::make_shared<sql::ast::Source>(std::make_shared<sql::ast::Table>("G", 2));

  ast.Get(tree->lhs).sql_expression = table_F;
  ast.Get(tree->rhs).sql_expression = table_G;

  auto visitor = SQLVisitor(ast.Data());

  auto var_list = visitor.VarListShorthand(std::vector<antlr4::ParserRuleContext*>{tree->lhs, tree->rhs});

  std::ostringstream os;

  for (auto& col : var_list) {
    os << *col << " ";
  }

  EXPECT_EQ(os.str(), "F.x G.y ");
}

// Lets convene that we have 3 relations:
//   def F { 1; 2; 3}
//   def G { (1, 2); (2, 3); (3, 4)}
//   def H { (1, 2, 3); (2, 3, 4); (3, 4, 5)}

TEST(TranslationTest, FullApplicationFormula) {
  EXPECT_EQ(TranslateRelFormula("F(x)"), "SELECT T0.A1 AS x FROM F AS T0");
}

TEST(TranslationTest, FullApplicationFormulaMultipleParams1) {
  EXPECT_EQ(TranslateRelFormula("G(x, y)"), "SELECT T0.A1 AS x, T0.A2 AS y FROM G AS T0");
}

TEST(TranslationTest, FullApplicationFormulaMultipleParams2) {
  EXPECT_EQ(TranslateRelFormula("H(x, y, z)"), "SELECT T0.A1 AS x, T0.A2 AS y, T0.A3 AS z FROM H AS T0");
}

TEST(TranslationTest, RepeatedVariableFormula1) {
  EXPECT_EQ(TranslateRelFormula("G(x, x)"), "SELECT T0.A1 AS x FROM G AS T0 WHERE T0.A1 = T0.A2");
}

TEST(TranslationTest, RepeatedVariableFormula2) {
  EXPECT_EQ(TranslateRelFormula("H(x, x, x)"), "SELECT T0.A1 AS x FROM H AS T0 WHERE T0.A1 = T0.A2 AND T0.A2 = T0.A3");
}

TEST(TranslationTest, RepeatedVariableFormula3) {
  EXPECT_EQ(TranslateRelFormula("H(x, y, x)"), "SELECT T0.A1 AS x, T0.A2 AS y FROM H AS T0 WHERE T0.A1 = T0.A3");
}

TEST(TranslationTest, OperatorFormula) {
  EXPECT_EQ(TranslateRelFormula("F(x) and x*x > 5"),
            "SELECT T1.x FROM (SELECT T0.A1 AS x FROM F AS T0) AS T1 WHERE T1.x * T1.x > 5");
}

TEST(TranslationTest, ConjunctionFormula) {
  EXPECT_EQ(TranslateRelFormula("F(x) and G(x)"),
            "SELECT T1.x FROM (SELECT T0.A1 AS x FROM F AS T0) AS T1, (SELECT T2.A1 AS x FROM G AS T2) AS T3 WHERE "
            "T1.x = T3.x");
}

TEST(TranslationTest, DisjunctionFormula) {
  EXPECT_EQ(TranslateRelFormula("F(x) or G(x)"),
            "SELECT T2.x FROM (SELECT T0.A1 AS x FROM F AS T0) AS T2 UNION SELECT T3.x FROM (SELECT T1.A1 AS x FROM G "
            "AS T1) AS T3");
}

TEST(TranslationTest, ExistentialFormula1) {
  EXPECT_EQ(TranslateRelFormula("exists (y | F(x, y))", {{"F", 2}}),
            "SELECT T1.x FROM (SELECT T0.A1 AS x, T0.A2 AS y FROM F AS T0) AS T1");
}

TEST(TranslationTest, ExistentialFormula2) {
  EXPECT_EQ(TranslateRelFormula("exists (y, z | F(x, y, z))"),
            "SELECT T1.x FROM (SELECT T0.A1 AS x, T0.A2 AS y, T0.A3 AS z FROM F AS T0) AS T1");
}

TEST(TranslationTest, ExistentialFormula3) {
  EXPECT_EQ(TranslateRelFormula("exists (y in G | F(x, y))"),
            "SELECT T1.x FROM (SELECT T0.A1 AS x, T0.A2 AS y FROM F AS T0) AS T1, G WHERE T1.y = G.A1");
}

TEST(TranslationTest, ExistentialFormula4) {
  EXPECT_EQ(TranslateRelFormula("exists (y in G, z in H | F(x, y, z))"),
            "SELECT T1.x FROM (SELECT T0.A1 AS x, T0.A2 AS y, T0.A3 AS z FROM F AS T0) AS T1, G, H WHERE T1.y = G.A1 "
            "AND T1.z = H.A1");
}

TEST(TranslationTest, ExistentialFormula5) {
  EXPECT_EQ(TranslateRelFormula("exists (y in G, z | F(x, y, z))"),
            "SELECT T1.x FROM (SELECT T0.A1 AS x, T0.A2 AS y, T0.A3 AS z FROM F AS T0) AS T1, G WHERE T1.y = G.A1");
}

TEST(TranslationTest, UniversalFormula1) {
  // TODO: Must remove inner-most FROM subquery alias (final "AS T1")
  EXPECT_EQ(TranslateRelFormula("forall (y in G | F(x, y))"),
            "SELECT T1.x FROM (SELECT T0.A1 AS x, T0.A2 AS y FROM F AS T0) AS T1 WHERE EXISTS (SELECT * FROM G WHERE "
            "(T1.x, G.A1) NOT IN (SELECT * FROM (SELECT T0.A1 AS x, T0.A2 AS y FROM F AS T0) AS T1))");
}

TEST(TranslationTest, UniversalFormula2) {
  EXPECT_EQ(TranslateRelFormula("forall (y in G, z in H | F(x, y, z))"),
            "SELECT T1.x FROM (SELECT T0.A1 AS x, T0.A2 AS y, T0.A3 AS z FROM F AS T0) AS T1 WHERE EXISTS (SELECT * "
            "FROM G, H WHERE (T1.x, G.A1, H.A1) NOT IN (SELECT * FROM (SELECT T0.A1 AS x, T0.A2 AS y, T0.A3 AS z FROM "
            "F AS T0) AS T1))");
}

TEST(TranslationTest, ProductExpression) { EXPECT_EQ(TranslateRelExpression("(1, 2)"), "SELECT 1, 2"); }

TEST(TranslationTest, ConditionExpression) {
  EXPECT_EQ(TranslateRelExpression("F[x] where G(x)", {{"F", 2}, {"G", 1}}),
            "SELECT T2.x, T2.A1 FROM (SELECT T0.A1 AS x, T0.A2 AS A1 FROM F AS T0) AS T2, (SELECT T1.A1 AS x FROM G AS "
            "T1) AS T3 WHERE T2.x = T3.x");
}

TEST(TranslationTest, PartialApplication1) {
  EXPECT_EQ(TranslateRelExpression("F[x]", {{"F", 2}}), "SELECT T0.A1 AS x, T0.A2 AS A1 FROM F AS T0");
}

TEST(TranslationTest, PartialApplication2) {
  EXPECT_EQ(TranslateRelExpression("F[1]", {{"F", 2}}),
            "SELECT T0.A2 AS A1 FROM F AS T0, (SELECT 1 AS A1) AS T1 WHERE T0.A1 = T1.A1");
}

TEST(TranslationTest, NestedPartialApplication1) {
  EXPECT_EQ(
      TranslateRelExpression("F[G[x]]", {{"F", 2}, {"G", 2}}),
      "SELECT T2.x, T0.A2 AS A1 FROM F AS T0, (SELECT T1.A1 AS x, T1.A2 AS A1 FROM G AS T1) AS T2 WHERE T0.A1 = T2.A1");
}

TEST(TranslationTest, NestedPartialApplication2) {
  EXPECT_EQ(TranslateRelExpression("F[G[H[x]]]", {{"F", 2}, {"G", 2}, {"H", 2}}),
            "SELECT T4.x, T0.A2 AS A1 FROM F AS T0, (SELECT T3.x, T1.A2 AS A1 FROM G AS T1, (SELECT T2.A1 AS x, T2.A2 "
            "AS A1 FROM H AS T2) AS T3 WHERE T1.A1 = T3.A1) AS T4 WHERE T0.A1 = T4.A1");
}

TEST(TranslationTest, PartialApplicationMixedParams1) {
  EXPECT_EQ(TranslateRelExpression("F[G[x], y]", {{"F", 3}, {"G", 2}}),
            "SELECT T2.x, T0.A2 AS y, T0.A3 AS A1 FROM F AS T0, (SELECT T1.A1 AS x, T1.A2 AS A1 FROM G AS T1) AS T2 "
            "WHERE T0.A1 = T2.A1");
}

TEST(TranslationTest, PartialApplicationMixedParams2) {
  EXPECT_EQ(TranslateRelExpression("F[x, 1]", {{"F", 3}}),
            "SELECT T0.A1 AS x, T0.A3 AS A1 FROM F AS T0, (SELECT 1 AS A1) AS T1 WHERE T0.A2 = T1.A1");
}

TEST(TranslationTest, PartialApplicationMixedParams3) {
  EXPECT_EQ(TranslateRelExpression("F[G[x], H[y]]", {{"F", 3}, {"G", 2}, {"H", 2}}),
            "SELECT T2.x, T4.y, T0.A3 AS A1 FROM F AS T0, (SELECT T1.A1 AS x, T1.A2 AS A1 FROM G AS T1) AS T2, (SELECT "
            "T3.A1 AS y, T3.A2 AS A1 FROM H AS T3) AS T4 WHERE T0.A1 = T2.A1 AND T0.A2 = T4.A1");
}

TEST(TranslationTest, PartialApplicationSharingVariables1) {
  EXPECT_EQ(TranslateRelExpression("F[G[x], H[x]]", {{"F", 3}, {"G", 2}, {"H", 2}}),
            "SELECT T2.x, T0.A3 AS A1 FROM F AS T0, (SELECT T1.A1 AS x, T1.A2 AS A1 FROM G AS T1) AS T2, (SELECT T3.A1 "
            "AS x, T3.A2 AS A1 FROM H AS T3) AS T4 WHERE T0.A1 = T2.A1 AND T0.A2 = T4.A1 AND T2.x = T4.x");
}

TEST(TranslationTest, PartialApplicationSharingVariables2) {
  EXPECT_EQ(TranslateRelExpression("F[G[x, y], H[y, z]]", {{"F", 3}, {"G", 3}, {"H", 3}}),
            "SELECT T2.x, T2.y, T4.z, T0.A3 AS A1 FROM F AS T0, (SELECT T1.A1 AS x, T1.A2 AS y, T1.A3 AS A1 FROM G AS "
            "T1) AS T2, (SELECT T3.A1 AS y, T3.A2 AS z, T3.A3 AS A1 FROM H AS T3) AS T4 WHERE T0.A1 = T2.A1 AND T0.A2 "
            "= T4.A1 AND T2.y = T4.y");
}

TEST(TranslationTest, PartialApplicationSharingVariables3) {
  EXPECT_EQ(TranslateRelExpression("F[G[x], x]", {{"F", 3}, {"G", 2}}),
            "SELECT T2.x, T0.A3 AS A1 FROM F AS T0, (SELECT T1.A1 AS x, T1.A2 AS A1 FROM G AS T1) AS T2 WHERE T2.x = "
            "T0.A2 AND T0.A1 = T2.A1");
}

TEST(TranslationTest, PartialApplicationSharingVariables4) {
  EXPECT_EQ(TranslateRelExpression("F[G[x], x, y]", {{"F", 3}, {"G", 2}}),
            "SELECT T2.x, T0.A3 AS y FROM F AS T0, (SELECT T1.A1 AS x, T1.A2 AS A1 FROM G AS T1) AS T2 WHERE T2.x = "
            "T0.A2 AND T0.A1 = T2.A1");
}

TEST(TranslationTest, AggregateExpression1) {
  EXPECT_EQ(TranslateRelExpression("sum[F]", {{"F", 2}}), "SELECT SUM(T0.A2) AS A1 FROM F AS T0");
}

TEST(TranslationTest, AggregateExpression2) {
  EXPECT_EQ(TranslateRelExpression("average[F]", {{"F", 2}}), "SELECT AVG(T0.A2) AS A1 FROM F AS T0");
}

TEST(TranslationTest, AggregateExpression3) {
  EXPECT_EQ(TranslateRelExpression("min[F]", {{"F", 2}}), "SELECT MIN(T0.A2) AS A1 FROM F AS T0");
}

TEST(TranslationTest, AggregateExpression4) {
  EXPECT_EQ(TranslateRelExpression("max[F]", {{"F", 2}}), "SELECT MAX(T0.A2) AS A1 FROM F AS T0");
}

TEST(TranslationTest, AggregateExpression5) {
  EXPECT_EQ(TranslateRelExpression("max[F[x]]", {{"F", 2}}),
            "SELECT T1.x, MAX(T1.A1) AS A1 FROM (SELECT T0.A1 AS x, T0.A2 AS A1 FROM F AS T0) AS T1 GROUP BY T1.x");
}

TEST(TranslationTest, RelationalAbstraction) {
  EXPECT_EQ(TranslateRelExpression("{(1,2); (3,4)}"),
            "SELECT CASE WHEN Ind0.I = 1 THEN T0.A1 WHEN Ind0.I = 2 THEN T1.A1 END AS A1, CASE WHEN Ind0.I = 1 THEN "
            "T0.A2 WHEN Ind0.I = 2 THEN T1.A2 END AS A2 FROM (SELECT 1, 2) AS T0, (SELECT 3, 4) AS T1, (VALUES (1), "
            "(2)) AS Ind0(I)");
}

TEST(TranslationTest, BindingExpression) {
  EXPECT_EQ(
      TranslateRelExpression("[x in T, y in R]: F[x, y]", {{"T", 1}, {"R", 1}, {"F", 3}}),
      "WITH S1(x) AS (SELECT * FROM T), S0(y) AS (SELECT * FROM R) SELECT S1.x AS A1, S0.y AS A2, T1.A1 AS A3 FROM "
      "(SELECT T0.A1 AS x, T0.A2 AS y, T0.A3 AS A1 FROM F AS T0) AS T1, S1, S0 WHERE S1.x = T1.x AND S0.y = T1.y");
}

TEST(TranslationTest, BindingExpressionBounded) {
  EXPECT_EQ(
      TranslateRelExpression("[x in T, y]: F[x, y] where R(y)", {{"T", 1}, {"R", 1}, {"F", 3}}),
      "WITH S1(x) AS (SELECT * FROM T), S0(y) AS (SELECT * FROM R) SELECT S1.x AS A1, S0.y AS A2, T4.A1 AS A3 FROM "
      "(SELECT T2.x, T2.y, T2.A1 FROM (SELECT T0.A1 AS x, T0.A2 AS y, T0.A3 AS A1 FROM F AS T0) AS T2, (SELECT T1.A1 "
      "AS y FROM R AS T1) AS T3 WHERE T2.y = T3.y) AS T4, S1, S0 WHERE S1.x = T4.x AND S0.y = T4.y");
}

TEST(TranslationTest, BindingFormula) {
  EXPECT_EQ(TranslateRelExpression("[x in T, y in R]: F(x, y)", {{"T", 1}, {"R", 1}, {"F", 2}}),
            "WITH S1(x) AS (SELECT * FROM T), S0(y) AS (SELECT * FROM R) SELECT S1.x AS A1, S0.y AS A2 FROM (SELECT "
            "T0.A1 AS x, T0.A2 AS y FROM F AS T0) AS T1, S1, S0 WHERE S1.x = T1.x AND S0.y = T1.y");
}

TEST(TranslationTest, Program) {
  EXPECT_EQ(TranslateRelDef("def F {[x in H]: G[x]}", {{"H", 1}, {"G", 2}}),
            "CREATE VIEW F AS (WITH S0(x) AS (SELECT * FROM H) SELECT S0.x AS A1, T1.A1 AS A2 FROM (SELECT T0.A1 AS x, "
            "T0.A2 AS A1 FROM G AS T0) AS T1, S0 WHERE S0.x = T1.x)");
}

TEST(TranslationTest, MultipleDefs1) {
  EXPECT_EQ(TranslateRelProgram("def F {(1, 2); (3, 4)} \n def F {(1, 4); (3, 4)}"),
            "CREATE VIEW F AS (SELECT DISTINCT * FROM (VALUES (1, 2), (3, 4), (1, 4), (3, 4)) AS T0(A1, A2));");
}

TEST(TranslationTest, MultipleDefs2) {
  EXPECT_EQ(TranslateRelProgram("def G {(1, 2); (3, 4)} \n def F {G[1]} \n def F {G[3]}"),
            "CREATE VIEW G AS (SELECT DISTINCT * FROM (VALUES (1, 2), (3, 4)) AS T0(A1, A2));\n\nCREATE VIEW F AS "
            "SELECT T1.A2 AS A1 FROM G AS T1, (SELECT 1 AS A1) AS T2 WHERE T1.A1 = T2.A1 UNION SELECT T3.A2 AS A1 FROM "
            "G AS T3, (SELECT 3 AS A1) AS T4 WHERE T3.A1 = T4.A1;");
}

TEST(TranslationTest, TableDefinition) {
  EXPECT_EQ(TranslateRelDef("def F {(1, 2); (3, 4)}"),
            "CREATE VIEW F AS (SELECT DISTINCT * FROM (VALUES (1, 2), (3, 4)) AS T0(A1, A2))");
}

// Tests for EDB with named attributes
TEST(EDBTranslationTest, NamedAttributesFormula) {
  rel2sql::EDBMap edb_map;
  edb_map["F"] = rel2sql::EDBInfo({"id", "name"});

  EXPECT_EQ(TranslateRelFormulaWithEDB("F(x, y)", edb_map), "SELECT T0.id AS x, T0.name AS y FROM F AS T0");
}

TEST(EDBTranslationTest, NamedAttributesConjunction) {
  rel2sql::EDBMap edb_map;
  edb_map["F"] = rel2sql::EDBInfo({"id", "name"});
  edb_map["G"] = rel2sql::EDBInfo({"student_id", "grade"});

  EXPECT_EQ(TranslateRelFormulaWithEDB("F(x, y) and G(x, z)", edb_map),
            "SELECT T1.x, T1.y, T3.z FROM (SELECT T0.id AS x, T0.name AS y FROM F AS T0) AS T1, (SELECT T2.student_id "
            "AS x, T2.grade AS z FROM G AS T2) AS T3 WHERE T1.x = T3.x");
}

TEST(EDBTranslationTest, NamedAttributesExistential) {
  rel2sql::EDBMap edb_map;
  edb_map["F"] = rel2sql::EDBInfo({"student_id", "course_id"});
  edb_map["G"] = rel2sql::EDBInfo({"course_id"});

  EXPECT_EQ(TranslateRelFormulaWithEDB("exists (y in G | F(x, y))", edb_map),
            "SELECT T2.x FROM (SELECT T1.student_id AS x, T1.course_id AS y FROM F AS T1) AS T2, G AS T0(course_id) "
            "WHERE T2.y = T0.course_id");
}

TEST(EDBTranslationTest, NamedAttributesPartialApplication) {
  rel2sql::EDBMap edb_map;
  edb_map["F"] = rel2sql::EDBInfo({"student_id", "course_id", "grade"});

  EXPECT_EQ(TranslateRelDefWithEDB("def G {F[x]}", edb_map),
            "CREATE VIEW G AS (SELECT T0.student_id AS x, T0.course_id AS A1, T0.grade AS A2 FROM F AS T0)");
}

TEST(EDBTranslationTest, NamedAttributesAggregate) {
  rel2sql::EDBMap edb_map;
  edb_map["F"] = rel2sql::EDBInfo({"student_id", "grade"});

  EXPECT_EQ(TranslateRelDefWithEDB("def G {max[F[x]]}", edb_map),
            "CREATE VIEW G AS (SELECT T1.x, MAX(T1.A1) AS A1 FROM (SELECT T0.student_id AS x, T0.grade AS A1 FROM F AS "
            "T0) AS T1 GROUP BY T1.x)");
}

// Tests for mixed EDB (some with named attributes, some without)
TEST(EDBTranslationTest, MixedEDBAttributes) {
  rel2sql::EDBMap edb_map;
  edb_map["F"] = rel2sql::EDBInfo({"id", "name"});  // Named attributes
  edb_map["G"] = rel2sql::EDBInfo();                // Unnamed attributes (empty vector)

  EXPECT_EQ(TranslateRelFormulaWithEDB("F(x, y) and G(x, z)", edb_map),
            "SELECT T1.x, T1.y, T3.z FROM (SELECT T0.id AS x, T0.name AS y FROM F AS T0) AS T1, (SELECT T2.A1 AS x, "
            "T2.A2 AS z FROM G AS T2) AS T3 WHERE T1.x = T3.x");
}

// Tests for EDB with single attribute
TEST(EDBTranslationTest, SingleNamedAttribute) {
  rel2sql::EDBMap edb_map;
  edb_map["F"] = rel2sql::EDBInfo({"id"});

  EXPECT_EQ(TranslateRelFormulaWithEDB("F(x)", edb_map), "SELECT T0.id AS x FROM F AS T0");
}

// Tests for EDB with three attributes
TEST(EDBTranslationTest, ThreeNamedAttributes) {
  rel2sql::EDBMap edb_map;
  edb_map["F"] = rel2sql::EDBInfo({"student_id", "course_id", "grade"});

  EXPECT_EQ(TranslateRelFormulaWithEDB("F(x, y, z)", edb_map),
            "SELECT T0.student_id AS x, T0.course_id AS y, T0.grade AS z FROM F AS T0");
}

// Test for backward compatibility - empty EDB map should work like before
TEST(EDBTranslationTest, EmptyEDBMap) {
  rel2sql::EDBMap edb_map;

  EXPECT_EQ(TranslateRelFormulaWithEDB("F(x)", edb_map), "SELECT T0.A1 AS x FROM F AS T0");
}

// Test for EDB with repeated variables using named attributes
TEST(EDBTranslationTest, NamedAttributesRepeatedVariables) {
  rel2sql::EDBMap edb_map;
  edb_map["F"] = rel2sql::EDBInfo({"id", "parent_id"});

  EXPECT_EQ(TranslateRelFormulaWithEDB("F(x, x)", edb_map),
            "SELECT T0.id AS x FROM F AS T0 WHERE T0.id = T0.parent_id");
}

TEST(EDBTranslationTest, BindingFormula) {
  rel2sql::EDBMap edb_map;
  edb_map["F"] = rel2sql::EDBInfo({"name"});

  EXPECT_EQ(TranslateRelExpressionWithEDB("(x): F(x)", edb_map),
            "WITH S0(x) AS (SELECT * FROM F) SELECT S0.x AS A1 FROM (SELECT T0.name AS x FROM F AS T0) AS T1, S0 WHERE "
            "S0.x = T1.x");
}

}  // namespace rel2sql
