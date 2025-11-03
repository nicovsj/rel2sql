#include "utils/exceptions.h"

#include <sstream>

namespace rel2sql {

std::string TranslationException::formatMessage(const std::string& message, ErrorCode code,
                                                const std::optional<SourceLocation>& location) {
  std::ostringstream oss;

  // Add error type prefix
  if (static_cast<int>(code) < 100) {
    oss << "[Parse Error E" << static_cast<int>(code) << "] ";
  } else {
    oss << "[Semantic Error E" << static_cast<int>(code) << "] ";
  }

  oss << message;

  // Add location information if available
  if (location.has_value()) {
    oss << "\n  at line " << location->line << ", column " << location->column;

    if (!location->text_snippet.empty()) {
      oss << ":\n\n    " << location->text_snippet;

      // Add visual indicator for the error position
      if (location->column > 0) {
        oss << "\n    " << std::string(location->column - 1, ' ') << "^";
      }
    }
  }

  return oss.str();
}

}  // namespace rel2sql
