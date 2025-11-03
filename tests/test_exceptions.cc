#include <gtest/gtest.h>

#include "utils/exceptions.h"
#include "rel2sql/rel2sql.h"

namespace rel2sql {

class ExceptionTest : public ::testing::Test {
 protected:
  void SetUp() override {}
  void TearDown() override {}
};

// Test basic exception hierarchy
TEST_F(ExceptionTest, BasicExceptionTypes) {
  // Test TranslationException
  TranslationException base("Test message");
  EXPECT_EQ(std::string(base.what()), "[Parse Error E1] Test message");
  EXPECT_EQ(base.getErrorCode(), ErrorCode::SYNTAX_ERROR);
  EXPECT_FALSE(base.getLocation().has_value());

  // Test ParseException
  ParseException parse("Syntax error");
  EXPECT_EQ(std::string(parse.what()), "[Parse Error E1] Syntax error");
  EXPECT_EQ(parse.getErrorCode(), ErrorCode::SYNTAX_ERROR);

  // Test ArityException
  ArityException arity("Undefined relation");
  EXPECT_EQ(std::string(arity.what()), "[Semantic Error E100] Undefined relation");
  EXPECT_EQ(arity.getErrorCode(), ErrorCode::UNDEFINED_RELATION);

  // Test VariableException
  VariableException variable("Unbalanced variable");
  EXPECT_EQ(std::string(variable.what()), "[Semantic Error E102] Unbalanced variable");
  EXPECT_EQ(variable.getErrorCode(), ErrorCode::UNBALANCED_VARIABLE);

  // Test QuantificationException
  QuantificationException quant("Unsafe quantification");
  EXPECT_EQ(std::string(quant.what()), "[Semantic Error E103] Unsafe quantification");
  EXPECT_EQ(quant.getErrorCode(), ErrorCode::UNSAFE_QUANTIFICATION);
}

// Test source location formatting
TEST_F(ExceptionTest, SourceLocationFormatting) {
  SourceLocation loc(5, 10, "def F { G(x) }");

  TranslationException ex("Test error", ErrorCode::UNDEFINED_RELATION, loc);
  std::string message = ex.what();

  EXPECT_TRUE(message.find("at line 5, column 10") != std::string::npos);
  EXPECT_TRUE(message.find("def F { G(x) }") != std::string::npos);
  EXPECT_TRUE(message.find("^") != std::string::npos);
}

// Test parsing errors (these will be caught by ANTLR error listener)
TEST_F(ExceptionTest, ParsingErrors) {
  // Test invalid syntax - missing closing brace
  EXPECT_THROW(
      {
        try {
          rel2sql::Translate("def F { G(x)");
        } catch (const ParseException& e) {
          // Should contain parsing error information
          std::string error = e.what();
          EXPECT_TRUE(error.find("def F { G") != std::string::npos);
          throw e;
        }
      },
      ParseException);

  // Test unexpected token
  EXPECT_THROW(
      {
        try {
          rel2sql::Translate("def F { G(x) } invalid_token");
        } catch (const ParseException& e) {
          std::string error = e.what();
          EXPECT_TRUE(error.find("invalid_token") != std::string::npos);
          throw e;
        }
      },
      ParseException);
}

// Test semantic errors - these should be caught by our visitors
TEST_F(ExceptionTest, SemanticErrors) {
  // Test undefined relation (should throw ArityException)
  EXPECT_THROW(
      {
        try {
          rel2sql::Translate("def F { G(x) }");  // G is not defined
        } catch (const ArityException& e) {
          std::string error = e.what();
          EXPECT_TRUE(error.find("G") != std::string::npos);
          EXPECT_TRUE(error.find("not defined") != std::string::npos);
          throw e;
        }
      },
      ArityException);
}

// Test error message quality
TEST_F(ExceptionTest, ErrorMessageQuality) {
  SourceLocation loc(2, 5, "def F { G(x) }");

  ArityException ex("Relation 'G' is not defined", loc);
  std::string message = ex.what();

  // Should contain error type
  EXPECT_TRUE(message.find("[Semantic Error E100]") != std::string::npos);

  // Should contain clear description
  EXPECT_TRUE(message.find("Relation 'G' is not defined") != std::string::npos);

  // Should contain location
  EXPECT_TRUE(message.find("at line 2, column 5") != std::string::npos);

  // Should contain code snippet
  EXPECT_TRUE(message.find("def F { G(x) }") != std::string::npos);

  // Should contain visual indicator
  EXPECT_TRUE(message.find("^") != std::string::npos);
}

// Test exception inheritance
TEST_F(ExceptionTest, ExceptionInheritance) {
  // All exceptions should be catchable as std::exception
  EXPECT_THROW({ throw TranslationException("Base error"); }, std::exception);

  EXPECT_THROW({ throw ParseException("Parse error"); }, std::exception);

  EXPECT_THROW({ throw ArityException("Arity error"); }, std::exception);

  EXPECT_THROW({ throw VariableException("Variable error"); }, std::exception);

  EXPECT_THROW({ throw QuantificationException("Quantification error"); }, std::exception);
}

// Test error codes
TEST_F(ExceptionTest, ErrorCodes) {
  EXPECT_EQ(TranslationException("", ErrorCode::SYNTAX_ERROR).getErrorCode(), ErrorCode::SYNTAX_ERROR);
  EXPECT_EQ(TranslationException("", ErrorCode::UNDEFINED_RELATION).getErrorCode(), ErrorCode::UNDEFINED_RELATION);
  EXPECT_EQ(TranslationException("", ErrorCode::ARITY_MISMATCH).getErrorCode(), ErrorCode::ARITY_MISMATCH);
  EXPECT_EQ(TranslationException("", ErrorCode::UNBALANCED_VARIABLE).getErrorCode(), ErrorCode::UNBALANCED_VARIABLE);
  EXPECT_EQ(TranslationException("", ErrorCode::UNSAFE_QUANTIFICATION).getErrorCode(),
            ErrorCode::UNSAFE_QUANTIFICATION);
}

}  // namespace rel2sql
