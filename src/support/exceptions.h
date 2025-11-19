#ifndef EXCEPTIONS_H
#define EXCEPTIONS_H

#include <optional>
#include <stdexcept>
#include <string>

namespace rel2sql {

// Source location information for error reporting
struct SourceLocation {
  int line;
  int column;
  std::string text_snippet;

  SourceLocation(int line, int column, const std::string& text_snippet = "")
      : line(line), column(column), text_snippet(text_snippet) {}
};

// Internal error codes for categorization
enum class ErrorCode {
  // Parsing errors (E001-E099)
  SYNTAX_ERROR = 1,
  UNEXPECTED_TOKEN = 2,
  MISSING_TOKEN = 3,

  // Semantic errors (E100-E199)
  UNDEFINED_RELATION = 100,
  ARITY_MISMATCH = 101,
  UNBALANCED_VARIABLE = 102,
  UNSAFE_QUANTIFICATION = 103,
  UNKNOWN_BINARY_OPERATOR = 104,
  EXPECTED_SINGLE_PROJECTION = 105,
  QUANTIFIED_VARIABLE_NOT_FOUND = 106,
  UNKNOWN_QUANTIFICATION = 107,
  RESERVED_RELATION_NAME = 108,

  // Internal/infra errors (E900+)
  INTERNAL_ERROR = 900,
  NOT_IMPLEMENTED = 901
};

// Base exception class for all rel2sql errors
class TranslationException : public std::runtime_error {
 public:
  TranslationException(const std::string& message, ErrorCode code = ErrorCode::SYNTAX_ERROR,
                       std::optional<SourceLocation> location = std::nullopt)
      : std::runtime_error(formatMessage(message, code, location)), error_code_(code), location_(location) {}

  ErrorCode getErrorCode() const { return error_code_; }
  const std::optional<SourceLocation>& getLocation() const { return location_; }

 private:
  ErrorCode error_code_;
  std::optional<SourceLocation> location_;

  static std::string formatMessage(const std::string& message, ErrorCode code,
                                   const std::optional<SourceLocation>& location);
};

// Syntax/Parsing errors
class ParseException : public TranslationException {
 public:
  ParseException(const std::string& message, std::optional<SourceLocation> location = std::nullopt)
      : TranslationException(message, ErrorCode::SYNTAX_ERROR, location) {}
};

// Base class for semantic errors
class SemanticException : public TranslationException {
 public:
  SemanticException(const std::string& message, ErrorCode code, std::optional<SourceLocation> location = std::nullopt)
      : TranslationException(message, code, location) {}
};

// Arity-related errors
class ArityException : public SemanticException {
 public:
  ArityException(const std::string& message, std::optional<SourceLocation> location = std::nullopt)
      : SemanticException(message, ErrorCode::UNDEFINED_RELATION, location) {}
};

// Variable-related errors
class VariableException : public SemanticException {
 public:
  VariableException(const std::string& message, std::optional<SourceLocation> location = std::nullopt)
      : SemanticException(message, ErrorCode::UNBALANCED_VARIABLE, location) {}
};

// Quantification-related errors
class QuantificationException : public SemanticException {
 public:
  QuantificationException(const std::string& message, std::optional<SourceLocation> location = std::nullopt)
      : SemanticException(message, ErrorCode::UNSAFE_QUANTIFICATION, location) {}
};

// Internal errors
class InternalException : public TranslationException {
 public:
  InternalException(const std::string& message, std::optional<SourceLocation> location = std::nullopt)
      : TranslationException(message, ErrorCode::INTERNAL_ERROR, location) {}
};

// Not implemented feature
class NotImplementedException : public TranslationException {
 public:
  NotImplementedException(const std::string& message, std::optional<SourceLocation> location = std::nullopt)
      : TranslationException(message, ErrorCode::NOT_IMPLEMENTED, location) {}
};

}  // namespace rel2sql

#endif  // EXCEPTIONS_H
