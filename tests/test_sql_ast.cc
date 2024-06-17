// my_test.cc

#include <gtest/gtest.h>

#include "sql.h"

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

  auto vc1 = std::make_shared<ValueCondition>(a1, CompOp::EQ, 1);
  auto vc2 = std::make_shared<ValueCondition>(a2, CompOp::EQ, "Smith");

  std::ostringstream os;

  os << *vc1 << " " << *vc2;

  EXPECT_EQ(os.str(), "A1 = 1 A2 = 'Smith'");
}

TEST(SQLPrintingTest, ColumnComparisonConditionPrint) {
  auto t1 = std::make_shared<Table>("T1");
  auto a1 = std::make_shared<Column>("A1");
  auto a2 = std::make_shared<Column>("A2");

  auto cc1 = std::make_shared<ColumnComparisonCondition>(a1, CompOp::EQ, a2);

  std::ostringstream os;

  os << *cc1;

  EXPECT_EQ(os.str(), "A1 = A2");
}

TEST(SQLPrintingTest, LogicalConditionPrint) {
  auto t1 = std::make_shared<Table>("T1");
  auto a1 = std::make_shared<Column>("A1");
  auto a2 = std::make_shared<Column>("A2");

  auto vc1 = std::make_shared<ValueCondition>(a1, CompOp::EQ, 1);
  auto vc2 = std::make_shared<ValueCondition>(a2, CompOp::EQ, "Smith");

  auto cc1 = std::make_shared<ColumnComparisonCondition>(a1, CompOp::EQ, a2);

  auto lc1 = LogicalCondition({vc1, vc2, cc1}, LogicalOp::AND);

  std::ostringstream os;

  os << lc1;

  EXPECT_EQ(os.str(), "A1 = 1 AND A2 = 'Smith' AND A1 = A2");
}

TEST(SQLPrintingTest, SelectStatementPrint) {
  auto t1 = std::make_shared<Table>("T1");
  auto a1 = std::make_shared<Column>("A1", t1);
  auto a2 = std::make_shared<Column>("A2", t1);

  auto t2 = std::make_shared<Table>("T2");
  auto a3 = std::make_shared<Column>("A1", t2);

  auto vc1 = std::make_shared<ValueCondition>(a1, CompOp::EQ, 1);
  auto vc2 = std::make_shared<ValueCondition>(a2, CompOp::EQ, "Smith");

  auto cc1 = std::make_shared<ColumnComparisonCondition>(a1, CompOp::EQ, a3);

  auto lc1 = std::make_shared<LogicalCondition>(std::vector<std::shared_ptr<Condition>>{vc1, vc2, cc1}, LogicalOp::AND);

  auto ss1 = std::make_shared<SelectStatement>(std::vector<std::shared_ptr<Selectable>>{a1, a2, a3},
                                               std::vector<std::shared_ptr<Source>>{t1, t2}, lc1);

  std::ostringstream os;

  os << *ss1;

  EXPECT_EQ(os.str(), "SELECT T1.A1, T1.A2, T2.A1 FROM T1, T2 WHERE T1.A1 = 1 AND T1.A2 = 'Smith' AND T1.A1 = T2.A1");
}

TEST(SQLPrintingTest, SelectSubqueryPrint) {
  auto t1 = std::make_shared<Table>("T1");
  auto a1 = std::make_shared<Column>("A1", t1);

  auto vc1 = std::make_shared<ValueCondition>(a1, CompOp::EQ, 1);

  auto ss1 = std::make_shared<SelectStatement>(std::vector<std::shared_ptr<Selectable>>{a1},
                                               std::vector<std::shared_ptr<Source>>{t1}, vc1);

  auto sq = std::make_shared<Subquery>(ss1, "subquery1");
  auto sa1 = std::make_shared<Column>("A1", sq);

  auto vc2 = std::make_shared<ValueCondition>(sa1, CompOp::GTE, 1);

  auto ss2 = std::make_shared<SelectStatement>(std::vector<std::shared_ptr<Selectable>>{sa1},
                                               std::vector<std::shared_ptr<Source>>{sq}, vc2);

  std::ostringstream os;

  os << *ss2;

  EXPECT_EQ(os.str(),
            "SELECT subquery1.A1 FROM (SELECT T1.A1 FROM T1 WHERE T1.A1 = 1) AS subquery1 WHERE subquery1.A1 >= 1");
}

TEST(SQLPrintingTest, SelectStatementWithoutConditionPrint) {
  auto t1 = std::make_shared<Table>("T1");
  auto a1 = std::make_shared<Column>("A1", t1);
  auto a2 = std::make_shared<Column>("A2", t1);

  auto ss1 = std::make_shared<SelectStatement>(std::vector<std::shared_ptr<Selectable>>{a1, a2},
                                               std::vector<std::shared_ptr<Source>>{t1});

  std::ostringstream os;

  os << *ss1;

  EXPECT_EQ(os.str(), "SELECT T1.A1, T1.A2 FROM T1");
}

TEST(SQLPrintingTest, ExistsPrint) {
  auto t1 = std::make_shared<Table>("T1");
  auto a1 = std::make_shared<Column>("A1", t1);

  auto vc1 = std::make_shared<ValueCondition>(a1, CompOp::EQ, 1);

  auto ss1 = std::make_shared<SelectStatement>(std::vector<std::shared_ptr<Selectable>>{a1},
                                               std::vector<std::shared_ptr<Source>>{t1}, vc1);

  auto e1 = std::make_shared<Exists>(ss1);

  std::ostringstream os;

  os << *e1;

  EXPECT_EQ(os.str(), "EXISTS (SELECT T1.A1 FROM T1 WHERE T1.A1 = 1)");
}

TEST(SQLPrintingTest, InclusionPrint) {
  auto t1 = std::make_shared<Table>("T1");
  auto a1 = std::make_shared<Column>("A1", t1);

  auto vc1 = std::make_shared<ValueCondition>(a1, CompOp::EQ, 1);

  auto ss1 = std::make_shared<SelectStatement>(std::vector<std::shared_ptr<Selectable>>{a1},
                                               std::vector<std::shared_ptr<Source>>{t1}, vc1);

  auto i1 = std::make_shared<Inclusion>(std::vector<std::shared_ptr<Column>>{a1}, ss1, false);

  std::ostringstream os;

  os << *i1;

  EXPECT_EQ(os.str(), "T1.A1 IN (SELECT T1.A1 FROM T1 WHERE T1.A1 = 1)");
}

TEST(SQLPrintingTest, NotInclusionPrint) {
  auto t1 = std::make_shared<Table>("T1");
  auto a1 = std::make_shared<Column>("A1", t1);

  auto vc1 = std::make_shared<ValueCondition>(a1, CompOp::EQ, 1);

  auto ss1 = std::make_shared<SelectStatement>(std::vector<std::shared_ptr<Selectable>>{a1},
                                               std::vector<std::shared_ptr<Source>>{t1}, vc1);

  auto i1 = std::make_shared<Inclusion>(std::vector<std::shared_ptr<Column>>{a1}, ss1, true);

  std::ostringstream os;

  os << *i1;

  EXPECT_EQ(os.str(), "T1.A1 NOT IN (SELECT T1.A1 FROM T1 WHERE T1.A1 = 1)");
}

TEST(SQLPrintingTest, TupleInclusionPrint) {
  auto t1 = std::make_shared<Table>("T1");
  auto a1 = std::make_shared<Column>("A1", t1);
  auto a2 = std::make_shared<Column>("A2", t1);

  auto vc1 = std::make_shared<ValueCondition>(a1, CompOp::EQ, 1);
  auto vc2 = std::make_shared<ValueCondition>(a2, CompOp::EQ, "Smith");

  auto ss1 = std::make_shared<SelectStatement>(std::vector<std::shared_ptr<Selectable>>{a1, a2},
                                               std::vector<std::shared_ptr<Source>>{t1}, vc1);

  auto i1 = std::make_shared<Inclusion>(std::vector<std::shared_ptr<Column>>{a1, a2}, ss1, false);

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
