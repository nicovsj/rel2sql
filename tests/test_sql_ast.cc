// my_test.cc

#include <gtest/gtest.h>

#include "sql.h"

using namespace sql::ast;

TEST(SQLPrintingTest, TablePrint) {
  auto t1 = std::make_shared<Table>("T1");

  std::ostringstream os;

  os << *t1;

  EXPECT_EQ(os.str(), "T1");
}

TEST(SQLPrintingTest, ColumnPrint) {
  auto t1 = std::make_shared<Table>("T1");
  auto a1 = std::make_shared<Column>("A1");
  auto a2 = std::make_shared<Column>("A2", t1);

  std::ostringstream os;

  os << *a1 << " " << *a2;

  EXPECT_EQ(os.str(), "A1 T1.A2");
}

TEST(SQLPrintingTest, ColumnTablePrint) {
  auto t1 = std::make_shared<Table>("T1", "T1_alias");
  auto a1 = std::make_shared<Column>("A1");
  auto a2 = std::make_shared<Column>("A2", t1);

  std::ostringstream os;

  os << *a1 << " " << *a2;

  EXPECT_EQ(os.str(), "A1 T1_alias.A2");
}

TEST(SQLPrintingTest, ValueConditionPrint) {
  auto t1 = std::make_shared<Table>("T1");
  auto a1 = std::make_shared<Column>("A1");
  auto a2 = std::make_shared<Column>("A2");

  auto vc1 = std::make_shared<ComparisonCondition>(a1, CompOp::EQ, 1);
  auto vc2 = std::make_shared<ComparisonCondition>(a2, CompOp::EQ, "Smith");

  std::ostringstream os;

  os << *vc1 << " " << *vc2;

  EXPECT_EQ(os.str(), "A1 = 1 A2 = 'Smith'");
}

TEST(SQLPrintingTest, ColumnComparisonConditionPrint) {
  auto t1 = std::make_shared<Table>("T1");
  auto a1 = std::make_shared<Column>("A1");
  auto a2 = std::make_shared<Column>("A2");

  auto cc1 = std::make_shared<ComparisonCondition>(a1, CompOp::EQ, a2);

  std::ostringstream os;

  os << *cc1;

  EXPECT_EQ(os.str(), "A1 = A2");
}

TEST(SQLPrintingTest, LogicalConditionPrint) {
  auto t1 = std::make_shared<Table>("T1");
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
  auto t1 = std::make_shared<Table>("T1");
  auto c1 = std::make_shared<Column>("A1", t1);
  auto c2 = std::make_shared<Column>("A2", t1);

  auto a1 = std::make_shared<TermSelectable>(c1);
  auto a2 = std::make_shared<TermSelectable>(c2);

  auto t2 = std::make_shared<Table>("T2");
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
  auto t1 = std::make_shared<Table>("T1");
  auto c1 = std::make_shared<Column>("A1", t1);

  auto a1 = std::make_shared<TermSelectable>(c1);

  auto vc1 = std::make_shared<ComparisonCondition>(c1, CompOp::EQ, 1);

  auto f1 = std::make_shared<FromStatement>(std::vector<std::shared_ptr<Source>>{t1}, vc1);

  auto ss1 = std::make_shared<SelectStatement>(std::vector<std::shared_ptr<Selectable>>{a1}, f1);

  auto sq = std::make_shared<Subquery>(ss1, "subquery1");
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
  auto t1 = std::make_shared<Table>("T1");
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
  auto t1 = std::make_shared<Table>("T1");
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
  auto t1 = std::make_shared<Table>("T1");
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
  auto t1 = std::make_shared<Table>("T1");
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
  auto t1 = std::make_shared<Table>("T1");
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
  auto t1 = std::make_shared<Table>("T1");

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
  auto t1 = std::make_shared<Table>("T1");
  auto w1 = std::make_shared<Wildcard>(t1);

  std::ostringstream os;

  os << *w1;

  EXPECT_EQ(os.str(), "T1.*");
}

TEST(SQLPrintingTest, SelectStatementWithColumnAlias) {
  auto t1 = std::make_shared<Table>("T1");
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
