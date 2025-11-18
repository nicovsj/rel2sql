// my_test.cc

#include <gtest/gtest.h>

#include "sql_ast/sql_ast.h"

namespace rel2sql {

namespace sql::ast {

TEST(SQLPrintingTest, TablePrint) {
  auto t1 = std::make_shared<Source>(std::make_shared<Table>("T1", 1));

  std::ostringstream os;

  os << *t1;

  EXPECT_EQ(os.str(), "T1");
}

TEST(SQLPrintingTest, ColumnPrint) {
  auto t1 = std::make_shared<Source>(std::make_shared<Table>("T1", 2));
  auto a1 = std::make_shared<Column>("A1");
  auto a2 = std::make_shared<Column>("A2", t1);

  std::ostringstream os;

  os << *a1 << " " << *a2;

  EXPECT_EQ(os.str(), "A1 T1.A2");
}

TEST(SQLPrintingTest, ColumnTablePrint) {
  auto t1 = std::make_shared<Source>(std::make_shared<Table>("T1", 2), "T1_alias");
  auto a1 = std::make_shared<Column>("A1");
  auto a2 = std::make_shared<Column>("A2", t1);

  std::ostringstream os;

  os << *a1 << " " << *a2;

  EXPECT_EQ(os.str(), "A1 T1_alias.A2");
}

TEST(SQLPrintingTest, ValueConditionPrint) {
  auto t1 = std::make_shared<Source>(std::make_shared<Table>("T1", 2));
  auto a1 = std::make_shared<Column>("A1");
  auto a2 = std::make_shared<Column>("A2");

  auto vc1 = std::make_shared<ComparisonCondition>(a1, CompOp::EQ, 1);
  auto vc2 = std::make_shared<ComparisonCondition>(a2, CompOp::EQ, "Smith");

  std::ostringstream os;

  os << *vc1 << " " << *vc2;

  EXPECT_EQ(os.str(), "A1 = 1 A2 = 'Smith'");
}

TEST(SQLPrintingTest, ColumnComparisonConditionPrint) {
  auto t1 = std::make_shared<Source>(std::make_shared<Table>("T1", 2));
  auto a1 = std::make_shared<Column>("A1");
  auto a2 = std::make_shared<Column>("A2");

  auto cc1 = std::make_shared<ComparisonCondition>(a1, CompOp::EQ, a2);

  std::ostringstream os;

  os << *cc1;

  EXPECT_EQ(os.str(), "A1 = A2");
}

TEST(SQLPrintingTest, LogicalConditionPrint) {
  auto t1 = std::make_shared<Source>(std::make_shared<Table>("T1", 2));
  auto a1 = std::make_shared<Column>("A1");
  auto a2 = std::make_shared<Column>("A2");

  auto vc1 = std::make_shared<ComparisonCondition>(a1, CompOp::EQ, 1);
  auto vc2 = std::make_shared<ComparisonCondition>(a2, CompOp::EQ, "Smith");

  auto cc1 = std::make_shared<ComparisonCondition>(a1, CompOp::EQ, a2);

  auto lc1 = LogicalCondition({vc1, vc2, cc1}, LogicalOp::AND);

  std::ostringstream os;

  os << lc1;

  EXPECT_EQ(os.str(), "A1 = 1 AND A2 = 'Smith' AND A1 = A2");
}

TEST(SQLPrintingTest, SelectStatementPrint) {
  auto t1 = std::make_shared<Source>(std::make_shared<Table>("T1", 2));

  auto c1 = std::make_shared<Column>("A1", t1);
  auto c2 = std::make_shared<Column>("A2", t1);

  auto a1 = std::make_shared<TermSelectable>(c1);
  auto a2 = std::make_shared<TermSelectable>(c2);

  auto t2 = std::make_shared<Source>(std::make_shared<Table>("T2", 2));

  auto c3 = std::make_shared<Column>("A1", t2);

  auto a3 = std::make_shared<TermSelectable>(c3);

  auto vc1 = std::make_shared<ComparisonCondition>(c1, CompOp::EQ, 1);
  auto vc2 = std::make_shared<ComparisonCondition>(c2, CompOp::EQ, "Smith");

  auto cc1 = std::make_shared<ComparisonCondition>(c1, CompOp::EQ, c3);

  auto lc1 = std::make_shared<LogicalCondition>(std::vector<std::shared_ptr<Condition>>{vc1, vc2, cc1}, LogicalOp::AND);

  auto f1 = std::make_shared<FromStatement>(std::vector<std::shared_ptr<Source>>{t1, t2}, lc1);

  auto ss1 = std::make_shared<SelectStatement>(std::vector<std::shared_ptr<Selectable>>{a1, a2, a3}, f1);

  std::ostringstream os;

  os << *ss1;

  EXPECT_EQ(os.str(), "SELECT T1.A1, T1.A2, T2.A1 FROM T1, T2 WHERE T1.A1 = 1 AND T1.A2 = 'Smith' AND T1.A1 = T2.A1");
}

TEST(SQLPrintingTest, SelectSubqueryPrint) {
  auto t1 = std::make_shared<Source>(std::make_shared<Table>("T1", 1));
  auto c1 = std::make_shared<Column>("A1", t1);

  auto a1 = std::make_shared<TermSelectable>(c1);

  auto vc1 = std::make_shared<ComparisonCondition>(c1, CompOp::EQ, 1);

  auto f1 = std::make_shared<FromStatement>(std::vector<std::shared_ptr<Source>>{t1}, vc1);

  auto ss1 = std::make_shared<SelectStatement>(std::vector<std::shared_ptr<Selectable>>{a1}, f1);

  auto sq = std::make_shared<Source>(ss1, "subquery1");
  auto sc1 = std::make_shared<Column>("A1", sq);

  auto sa1 = std::make_shared<TermSelectable>(sc1);

  auto vc2 = std::make_shared<ComparisonCondition>(sc1, CompOp::GTE, 1);

  auto f2 = std::make_shared<FromStatement>(std::vector<std::shared_ptr<Source>>{sq}, vc2);

  auto ss2 = std::make_shared<SelectStatement>(std::vector<std::shared_ptr<Selectable>>{sa1}, f2);

  std::ostringstream os;

  os << *ss2;

  EXPECT_EQ(os.str(),
            "SELECT subquery1.A1 FROM (SELECT T1.A1 FROM T1 WHERE T1.A1 = 1) AS subquery1 WHERE subquery1.A1 >= 1");
}

TEST(SQLPrintingTest, SelectStatementWithoutConditionPrint) {
  auto t1 = std::make_shared<Source>(std::make_shared<Table>("T1", 2));
  auto a1 = std::make_shared<TermSelectable>(std::make_shared<Column>("A1", t1));
  auto a2 = std::make_shared<TermSelectable>(std::make_shared<Column>("A2", t1));

  auto f1 = std::make_shared<FromStatement>(std::vector<std::shared_ptr<Source>>{t1});

  auto ss1 = std::make_shared<SelectStatement>(std::vector<std::shared_ptr<Selectable>>{a1, a2}, f1);

  std::ostringstream os;

  os << *ss1;

  EXPECT_EQ(os.str(), "SELECT T1.A1, T1.A2 FROM T1");
}

TEST(SQLPrintingTest, SelectStatementWithoutFromStatement) {
  auto a1 = std::make_shared<TermSelectable>(std::make_shared<Column>("A1"));
  auto a2 = std::make_shared<TermSelectable>(std::make_shared<Column>("A2"));

  auto ss1 = std::make_shared<SelectStatement>(std::vector<std::shared_ptr<Selectable>>{a1, a2});

  std::ostringstream os;

  os << *ss1;

  EXPECT_EQ(os.str(), "SELECT A1, A2");
}

TEST(SQLPrintingTest, SelectStatementForConstant) {
  auto c1 = std::make_shared<TermSelectable>(std::make_shared<Constant>(1));
  auto c2 = std::make_shared<TermSelectable>(std::make_shared<Constant>("Smith"));

  auto ss1 = std::make_shared<SelectStatement>(std::vector<std::shared_ptr<Selectable>>{c1, c2});

  std::ostringstream os;

  os << *ss1;

  EXPECT_EQ(os.str(), "SELECT 1, 'Smith'");
}

TEST(SQLPrintingTest, SelectStatementForAlias) {
  auto t1 = std::make_shared<Source>(std::make_shared<Table>("T1", 1));
  auto c1 = std::make_shared<Column>("A1", t1);
  auto a1 = std::make_shared<TermSelectable>(c1, "X");

  auto v1 = std::make_shared<Constant>(1);
  auto a2 = std::make_shared<TermSelectable>(v1, "Y");

  auto f1 = std::make_shared<FromStatement>(std::vector<std::shared_ptr<Source>>{t1});

  auto ss1 = std::make_shared<SelectStatement>(std::vector<std::shared_ptr<Selectable>>{a1, a2}, f1);

  std::ostringstream os;

  os << *ss1;

  EXPECT_EQ(os.str(), "SELECT T1.A1 AS X, 1 AS Y FROM T1");
}

TEST(SQLPrintingTest, ExistsPrint) {
  auto t1 = std::make_shared<Source>(std::make_shared<Table>("T1", 1));
  auto c1 = std::make_shared<Column>("A1", t1);
  auto a1 = std::make_shared<TermSelectable>(c1);

  auto vc1 = std::make_shared<ComparisonCondition>(c1, CompOp::EQ, 1);

  auto f1 = std::make_shared<FromStatement>(std::vector<std::shared_ptr<Source>>{t1}, vc1);

  auto ss1 = std::make_shared<SelectStatement>(std::vector<std::shared_ptr<Selectable>>{a1}, f1);

  auto e1 = std::make_shared<Exists>(ss1);

  std::ostringstream os;

  os << *e1;

  EXPECT_EQ(os.str(), "EXISTS (SELECT T1.A1 FROM T1 WHERE T1.A1 = 1)");
}

TEST(SQLPrintingTest, InclusionPrint) {
  auto t1 = std::make_shared<Source>(std::make_shared<Table>("T1", 1));
  auto c1 = std::make_shared<Column>("A1", t1);

  auto a1 = std::make_shared<TermSelectable>(c1);

  auto vc1 = std::make_shared<ComparisonCondition>(c1, CompOp::EQ, 1);

  auto f1 = std::make_shared<FromStatement>(std::vector<std::shared_ptr<Source>>{t1}, vc1);

  auto ss1 = std::make_shared<SelectStatement>(std::vector<std::shared_ptr<Selectable>>{a1}, f1);

  auto i1 = std::make_shared<Inclusion>(std::vector<std::shared_ptr<Column>>{c1}, ss1, false);

  std::ostringstream os;

  os << *i1;

  EXPECT_EQ(os.str(), "T1.A1 IN (SELECT T1.A1 FROM T1 WHERE T1.A1 = 1)");
}

TEST(SQLPrintingTest, NotInclusionPrint) {
  auto t1 = std::make_shared<Source>(std::make_shared<Table>("T1", 1));
  auto c1 = std::make_shared<Column>("A1", t1);

  auto a1 = std::make_shared<TermSelectable>(c1);

  auto vc1 = std::make_shared<ComparisonCondition>(c1, CompOp::EQ, 1);

  auto f1 = std::make_shared<FromStatement>(std::vector<std::shared_ptr<Source>>{t1}, vc1);

  auto ss1 = std::make_shared<SelectStatement>(std::vector<std::shared_ptr<Selectable>>{a1}, f1);

  auto i1 = std::make_shared<Inclusion>(std::vector<std::shared_ptr<Column>>{c1}, ss1, true);

  std::ostringstream os;

  os << *i1;

  EXPECT_EQ(os.str(), "T1.A1 NOT IN (SELECT T1.A1 FROM T1 WHERE T1.A1 = 1)");
}

TEST(SQLPrintingTest, TupleInclusionPrint) {
  auto t1 = std::make_shared<Source>(std::make_shared<Table>("T1", 2));

  auto c1 = std::make_shared<Column>("A1", t1);
  auto c2 = std::make_shared<Column>("A2", t1);

  auto a1 = std::make_shared<TermSelectable>(c1);
  auto a2 = std::make_shared<TermSelectable>(c2);

  auto vc1 = std::make_shared<ComparisonCondition>(c1, CompOp::EQ, 1);
  auto vc2 = std::make_shared<ComparisonCondition>(c2, CompOp::EQ, "Smith");

  auto f1 = std::make_shared<FromStatement>(std::vector<std::shared_ptr<Source>>{t1}, vc1);

  auto ss1 = std::make_shared<SelectStatement>(std::vector<std::shared_ptr<Selectable>>{a1, a2}, f1);

  auto i1 = std::make_shared<Inclusion>(std::vector<std::shared_ptr<Column>>{c1, c2}, ss1, false);

  std::ostringstream os;

  os << *i1;

  EXPECT_EQ(os.str(), "(T1.A1, T1.A2) IN (SELECT T1.A1, T1.A2 FROM T1 WHERE T1.A1 = 1)");
}

TEST(SQLPrintingTest, WildcardPrint) {
  auto w1 = std::make_shared<Wildcard>();

  std::ostringstream os;

  os << *w1;

  EXPECT_EQ(os.str(), "*");
}

TEST(SQLPrintingTest, WildcardColumnPrint) {
  auto t1 = std::make_shared<Source>(std::make_shared<Table>("T1", 2));
  auto w1 = std::make_shared<Wildcard>(t1);

  std::ostringstream os;

  os << *w1;

  EXPECT_EQ(os.str(), "T1.*");
}

TEST(SQLPrintingTest, SelectStatementWithColumnAlias) {
  auto t1 = std::make_shared<Source>(std::make_shared<Table>("T1", 2));
  auto c1 = std::make_shared<Column>("A1", t1);
  auto c2 = std::make_shared<Column>("A2", t1);

  auto a1 = std::make_shared<TermSelectable>(c1, "X");
  auto a2 = std::make_shared<TermSelectable>(c2);

  auto f1 = std::make_shared<FromStatement>(std::vector<std::shared_ptr<Source>>{t1});

  auto ss1 = std::make_shared<SelectStatement>(std::vector<std::shared_ptr<Selectable>>{a1, a2}, f1);

  std::ostringstream os;

  os << *ss1;

  EXPECT_EQ(os.str(), "SELECT T1.A1 AS X, T1.A2 FROM T1");
}

TEST(SQLPrintingTest, SelectConstantPrint) {
  auto c1 = std::make_shared<Constant>(1);
  auto c2 = std::make_shared<Constant>("Smith");

  std::ostringstream os;

  os << *c1 << " " << *c2;

  EXPECT_EQ(os.str(), "1 'Smith'");
}

TEST(SQLPrintingTest, Values) {
  auto v = std::make_shared<Values>(std::vector<std::vector<constant_t>>{{1, 2}, {3, 4}});

  std::ostringstream os;

  os << *v;

  EXPECT_EQ(os.str(), "VALUES (1, 2), (3, 4)");
}

TEST(SQLPrintingTest, FromValues) {
  auto v = std::make_shared<Values>(std::vector<std::vector<constant_t>>{{1, 2}, {3, 4}});
  auto s = std::make_shared<Source>(v, "R(A, B)");
  auto f = std::make_shared<FromStatement>(std::vector<std::shared_ptr<Source>>{s});

  std::ostringstream os;

  os << *f;

  EXPECT_EQ(os.str(), "FROM (VALUES (1, 2), (3, 4)) AS R(A, B)");
}

TEST(SQLPrintingTest, CaseWhen) {
  auto t1 = std::make_shared<Source>(std::make_shared<Table>("T", 1));
  auto col1 = std::make_shared<Column>("I", t1);

  auto t2 = std::make_shared<Source>(std::make_shared<Table>("R", 2));
  auto col2 = std::make_shared<Column>("A1", t2);
  auto col3 = std::make_shared<Column>("A2", t2);

  auto a1 = std::dynamic_pointer_cast<Term>(col2);
  auto a2 = std::dynamic_pointer_cast<Term>(col3);

  auto c1 = std::dynamic_pointer_cast<Condition>(std::make_shared<ComparisonCondition>(col1, CompOp::EQ, 1));
  auto c2 = std::dynamic_pointer_cast<Condition>(std::make_shared<ComparisonCondition>(col1, CompOp::EQ, 2));

  std::vector<std::pair<std::shared_ptr<Condition>, std::shared_ptr<Term>>> cases = {{c1, a1}, {c2, a2}};

  auto w1 = std::make_shared<CaseWhen>(cases);

  std::ostringstream os;

  os << *w1;

  EXPECT_EQ(os.str(), "CASE WHEN T.I = 1 THEN R.A1 WHEN T.I = 2 THEN R.A2 END");
}

TEST(SQLPrintingTest, CTEs) {
  auto t1 = std::make_shared<Source>(std::make_shared<Table>("T1", 1));
  auto c1 = std::make_shared<Column>("A1", t1);

  auto a1 = std::make_shared<TermSelectable>(c1);

  auto vc1 = std::make_shared<ComparisonCondition>(c1, CompOp::EQ, 1);

  auto f1 = std::make_shared<FromStatement>(std::vector<std::shared_ptr<Source>>{t1}, vc1);

  auto ss1 = std::make_shared<SelectStatement>(std::vector<std::shared_ptr<Selectable>>{a1}, f1);

  auto cte =
      std::make_shared<Source>(ss1, std::make_shared<AliasStatement>("S1", std::vector<std::string>{"A1"}), true);

  auto c4 = std::make_shared<Column>("A1", cte);

  auto t2 = std::make_shared<Source>(std::make_shared<Table>("T2", 2));

  auto c2 = std::make_shared<Column>("A1", t2);
  auto c3 = std::make_shared<Column>("A2", t2);

  auto a2 = std::make_shared<TermSelectable>(c2);
  auto a3 = std::make_shared<TermSelectable>(c3);

  auto vc2 = std::make_shared<ComparisonCondition>(c2, CompOp::EQ, 2);
  auto vc3 = std::make_shared<ComparisonCondition>(c4, CompOp::EQ, 1);

  auto lc1 = std::make_shared<LogicalCondition>(std::vector<std::shared_ptr<Condition>>{vc2, vc3}, LogicalOp::AND);

  auto f2 = std::make_shared<FromStatement>(std::vector<std::shared_ptr<Source>>{t2, cte}, lc1);

  auto ss2 = std::make_shared<SelectStatement>(std::vector<std::shared_ptr<Selectable>>{a2, a3}, f2,
                                               std::vector<std::shared_ptr<Source>>{cte});

  std::ostringstream os;

  os << *ss2;

  EXPECT_EQ(os.str(),
            "WITH S1(A1) AS (SELECT T1.A1 FROM T1 WHERE T1.A1 = 1) SELECT T2.A1, T2.A2 FROM T2, S1 WHERE T2.A1 = 2 AND "
            "S1.A1 = 1");
}

TEST(SQLPrintingTest, View) {
  auto t1 = std::make_shared<Source>(std::make_shared<Table>("T1", 1));
  auto c1 = std::make_shared<Column>("A1", t1);

  auto a1 = std::make_shared<TermSelectable>(c1);

  auto vc1 = std::make_shared<ComparisonCondition>(c1, CompOp::EQ, 1);

  auto f1 = std::make_shared<FromStatement>(std::vector<std::shared_ptr<Source>>{t1}, vc1);

  auto ss1 = std::make_shared<SelectStatement>(std::vector<std::shared_ptr<Selectable>>{a1}, f1);

  auto v1 = std::make_shared<View>(ss1, "V1");

  std::ostringstream os;

  os << *v1;

  EXPECT_EQ(os.str(), "CREATE OR REPLACE VIEW V1 AS (SELECT T1.A1 FROM T1 WHERE T1.A1 = 1)");
}

TEST(SQLPrintingTest, SumGroupBy) {
  auto t1 = std::make_shared<Source>(std::make_shared<Table>("T1", 2));
  auto c1 = std::make_shared<Column>("A1", t1);
  auto sum1 = std::make_shared<Function>(AggregateFunction::SUM, c1);
  auto c2 = std::make_shared<TermSelectable>(std::make_shared<Column>("A2", t1));

  auto a1 = std::make_shared<TermSelectable>(sum1);

  auto f1 = std::make_shared<FromStatement>(std::vector<std::shared_ptr<Source>>{t1});

  auto g1 = std::make_shared<GroupBy>(std::vector<std::shared_ptr<Selectable>>{c2});

  auto ss1 = std::make_shared<SelectStatement>(std::vector<std::shared_ptr<Selectable>>{a1}, f1, g1);

  std::ostringstream os;

  os << *ss1;

  EXPECT_EQ(os.str(), "SELECT SUM(T1.A1) FROM T1 GROUP BY T1.A2");
}

TEST(SQLPrintingTest, AvgGroupBy) {
  auto t1 = std::make_shared<Source>(std::make_shared<Table>("T1", 2));
  auto c1 = std::make_shared<Column>("A1", t1);
  auto avg1 = std::make_shared<Function>(AggregateFunction::AVG, c1);
  auto c2 = std::make_shared<TermSelectable>(std::make_shared<Column>("A2", t1));

  auto a1 = std::make_shared<TermSelectable>(avg1);

  auto f1 = std::make_shared<FromStatement>(std::vector<std::shared_ptr<Source>>{t1});

  auto g1 = std::make_shared<GroupBy>(std::vector<std::shared_ptr<Selectable>>{c2});

  auto ss1 = std::make_shared<SelectStatement>(std::vector<std::shared_ptr<Selectable>>{a1}, f1, g1);

  std::ostringstream os;

  os << *ss1;

  EXPECT_EQ(os.str(), "SELECT AVG(T1.A1) FROM T1 GROUP BY T1.A2");
}

TEST(SQLPrintingTest, CountGroupBy) {
  auto t1 = std::make_shared<Source>(std::make_shared<Table>("T1", 2));
  auto c1 = std::make_shared<Column>("A1", t1);
  auto count1 = std::make_shared<Function>(AggregateFunction::COUNT, c1);
  auto c2 = std::make_shared<TermSelectable>(std::make_shared<Column>("A2", t1));

  auto a1 = std::make_shared<TermSelectable>(count1);

  auto f1 = std::make_shared<FromStatement>(std::vector<std::shared_ptr<Source>>{t1});

  auto g1 = std::make_shared<GroupBy>(std::vector<std::shared_ptr<Selectable>>{c2});

  auto ss1 = std::make_shared<SelectStatement>(std::vector<std::shared_ptr<Selectable>>{a1}, f1, g1);

  std::ostringstream os;

  os << *ss1;

  EXPECT_EQ(os.str(), "SELECT COUNT(T1.A1) FROM T1 GROUP BY T1.A2");
}

TEST(SQLPrintingTest, MinGroupBy) {
  auto t1 = std::make_shared<Source>(std::make_shared<Table>("T1", 2));
  auto c1 = std::make_shared<Column>("A1", t1);
  auto min1 = std::make_shared<Function>(AggregateFunction::MIN, c1);
  auto c2 = std::make_shared<TermSelectable>(std::make_shared<Column>("A2", t1));

  auto a1 = std::make_shared<TermSelectable>(min1);

  auto f1 = std::make_shared<FromStatement>(std::vector<std::shared_ptr<Source>>{t1});

  auto g1 = std::make_shared<GroupBy>(std::vector<std::shared_ptr<Selectable>>{c2});

  auto ss1 = std::make_shared<SelectStatement>(std::vector<std::shared_ptr<Selectable>>{a1}, f1, g1);

  std::ostringstream os;

  os << *ss1;

  EXPECT_EQ(os.str(), "SELECT MIN(T1.A1) FROM T1 GROUP BY T1.A2");
}

TEST(SQLPrintingTest, MaxGroupBy) {
  auto t1 = std::make_shared<Source>(std::make_shared<Table>("T1", 2));
  auto c1 = std::make_shared<Column>("A1", t1);
  auto max1 = std::make_shared<Function>(AggregateFunction::MAX, c1);
  auto c2 = std::make_shared<TermSelectable>(std::make_shared<Column>("A2", t1));

  auto a1 = std::make_shared<TermSelectable>(max1);

  auto f1 = std::make_shared<FromStatement>(std::vector<std::shared_ptr<Source>>{t1});

  auto g1 = std::make_shared<GroupBy>(std::vector<std::shared_ptr<Selectable>>{c2});

  auto ss1 = std::make_shared<SelectStatement>(std::vector<std::shared_ptr<Selectable>>{a1}, f1, g1);

  std::ostringstream os;

  os << *ss1;

  EXPECT_EQ(os.str(), "SELECT MAX(T1.A1) FROM T1 GROUP BY T1.A2");
}

}  // namespace sql::ast
}  // namespace rel2sql
