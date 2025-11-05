#include "constant_optimizer.h"

#include "replacers.h"

namespace rel2sql {

namespace sql::ast {


struct ConstantSourceData {
  std::string source_alias;
  std::string term_alias;
  std::shared_ptr<Constant> constant;
};

bool ExtractConstantSourceData(const std::shared_ptr<Source>& source, ConstantSourceData& out) {
  auto select = std::dynamic_pointer_cast<SelectStatement>(source->sourceable);
  // Source must be a SELECT statement
  if (!select) return false;

  // SELECT statement must have a single column
  if (select->columns.size() != 1) return false;

  // Column must be a TermSelectable
  auto term_selectable = std::dynamic_pointer_cast<TermSelectable>(select->columns[0]);
  if (!term_selectable) return false;

  // Term must be a Constant
  auto constant = std::dynamic_pointer_cast<Constant>(term_selectable->term);
  if (!constant) return false;

  // Extract data
  out.source_alias = source->Alias();
  out.term_alias = term_selectable->Alias();
  out.constant = constant;
  return true;
}

void ApplyConstantReplacement(const ConstantSourceData& data, FromStatement& from_statement) {
  if (!from_statement.where) return;
  ConstantReplacer replacer(data.source_alias, data.term_alias, data.constant);
  from_statement.where.value()->Accept(replacer);
}

void ConstantOptimizer::Visit(FromStatement& from_statement) {
  // First pass: extract data for suitable sources (nullptr if not suitable)
  std::vector<std::unique_ptr<ConstantSourceData>> data_per_source;
  data_per_source.reserve(from_statement.sources.size());
  bool has_not_suitable = false;
  for (const auto& source : from_statement.sources) {
    auto data = std::make_unique<ConstantSourceData>();
    if (ExtractConstantSourceData(source, *data)) {
      data_per_source.push_back(std::move(data));
    } else {
      data_per_source.push_back(nullptr);
      has_not_suitable = true;
    }
  }

  // Second pass: if at least one source is not suitable, replace each suitable source
  if (has_not_suitable && from_statement.where) {
    std::vector<std::shared_ptr<Source>> new_sources;
    new_sources.reserve(from_statement.sources.size());
    for (std::size_t i = 0; i < from_statement.sources.size(); ++i) {
      const auto& source = from_statement.sources[i];
      if (data_per_source[i]) {
        ApplyConstantReplacement(*data_per_source[i], from_statement);
        // Do not carry this source over (it's inlined into WHERE)
        continue;
      }
      new_sources.push_back(source);
    }
    from_statement.sources = std::move(new_sources);
  }

  // Recurse into remaining sources
  for (auto& source : from_statement.sources) {
    Visit(*source);
  }
}



}  // namespace sql::ast
}  // namespace rel2sql
