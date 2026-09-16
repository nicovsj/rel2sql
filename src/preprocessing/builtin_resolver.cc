#include "preprocessing/builtin_resolver.h"

#include <cctype>
#include <optional>
#include <stdexcept>
#include <string>
#include <variant>

#include "rel_ast/rel_ast.h"
#include "sql/aggregate_map.h"

namespace rel2sql {

namespace {

std::shared_ptr<RelExpr> GetParamExpr(const std::shared_ptr<RelApplParam>& p) {
  if (!p) return nullptr;
  return p->GetExpr();
}

std::optional<int> TryIntFromExpr(const std::shared_ptr<RelExpr>& e) {
  if (!e) return std::nullopt;
  if (auto* n = dynamic_cast<RelNumTerm*>(e.get())) {
    return std::visit(
        [](const auto& v) -> std::optional<int> {
          using T = std::decay_t<decltype(v)>;
          if constexpr (std::is_same_v<T, int>) return v;
          if constexpr (std::is_same_v<T, double>) return static_cast<int>(v);
          return std::nullopt;
        },
        n->value);
  }
  if (auto* lit = dynamic_cast<RelLiteral*>(e.get())) {
    if (std::holds_alternative<int>(lit->value)) return std::get<int>(lit->value);
  }
  return std::nullopt;
}

std::string NormalizeRelStringLiteral(std::string s) {
  // Interpolation literals ("...") keep their surrounding double-quotes because
  // `%` in the body excludes them from T_STATIC_STR_LIT. Strip a pair of outer
  // quotes when present so builtins (like_match, parse_date) see the raw text.
  if (s.size() >= 2 && s.front() == '"' && s.back() == '"') s = s.substr(1, s.size() - 2);
  // Rel raw"..."/raw"%..." forms (TPC-H like_match) -> DuckDB/SQL string body.
  if (s.size() >= 5 && s.rfind("raw\"", 0) == 0 && s.back() == '"') {
    s = s.substr(4, s.size() - 5);
  }
  return s;
}

std::optional<std::string> TryStringFromExpr(const std::shared_ptr<RelExpr>& e) {
  if (!e) return std::nullopt;
  if (auto* lit = dynamic_cast<RelLiteral*>(e.get())) {
    if (std::holds_alternative<std::string>(lit->value)) {
      return NormalizeRelStringLiteral(std::get<std::string>(lit->value));
    }
  }
  return std::nullopt;
}

std::optional<std::string> TryIdFromExpr(const std::shared_ptr<RelExpr>& e) {
  if (!e) return std::nullopt;
  if (auto* idt = dynamic_cast<RelIDTerm*>(e.get())) return idt->id;
  return std::nullopt;
}

RelBuiltinAggregateOp MapSqlAggToRel(sql::ast::AggregateFunction f) {
  switch (f) {
    case sql::ast::AggregateFunction::SUM:
      return RelBuiltinAggregateOp::SUM;
    case sql::ast::AggregateFunction::COUNT:
      return RelBuiltinAggregateOp::COUNT;
    case sql::ast::AggregateFunction::AVG:
      return RelBuiltinAggregateOp::AVG;
    case sql::ast::AggregateFunction::MIN:
      return RelBuiltinAggregateOp::MIN;
    case sql::ast::AggregateFunction::MAX:
      return RelBuiltinAggregateOp::MAX;
  }
  return RelBuiltinAggregateOp::SUM;
}

}  // namespace

std::shared_ptr<RelExpr> BuiltinResolver::Visit(const std::shared_ptr<RelPartialApplication>& node) {
  node->base = Visit(node->base);
  for (auto& p : node->params) {
    if (p) p = Visit(p);
  }
  if (auto lowered = TryLowerPartial(node)) {
    return lowered;
  }
  return node;
}

std::shared_ptr<RelFormula> BuiltinResolver::Visit(const std::shared_ptr<RelFullApplication>& node) {
  node->base = Visit(node->base);
  for (auto& p : node->params) {
    if (p) p = Visit(p);
  }
  if (auto lowered = TryLowerFull(node)) {
    return lowered;
  }
  return node;
}

std::shared_ptr<RelExpr> BuiltinResolver::TryLowerPartial(const std::shared_ptr<RelPartialApplication>& node) {
  auto* id_base = dynamic_cast<RelIDApplBase*>(node->base.get());
  if (!id_base) return nullptr;
  const std::string& id = id_base->id;

  auto agg_it = GetAggregateMap().find(id);
  if (agg_it != GetAggregateMap().end()) {
    if (node->params.size() != 1) return nullptr;
    auto body = GetParamExpr(node->params[0]);
    if (!body) return nullptr;
    auto op = MapSqlAggToRel(agg_it->second);
    auto out = std::make_shared<RelBuiltinAggregateExpr>(op, body);
    out->ctx = node->ctx;
    return out;
  }

  if (id == "sort" && !node->params.empty()) {
    auto body = GetParamExpr(node->params[0]);
    if (!body) return nullptr;
    auto out = std::make_shared<RelBuiltinOrderExpr>(RelBuiltinOrderKind::SortAsc, body);
    out->ctx = node->ctx;
    return out;
  }

  if (id == "reverse_sort" && !node->params.empty()) {
    auto body = GetParamExpr(node->params[0]);
    if (!body) return nullptr;
    auto out = std::make_shared<RelBuiltinOrderExpr>(RelBuiltinOrderKind::SortDesc, body);
    out->ctx = node->ctx;
    return out;
  }

  if (id == "parse_date" && node->params.size() == 2) {
    auto d = GetParamExpr(node->params[0]);
    auto fmt = GetParamExpr(node->params[1]);
    if (!d || !fmt) return nullptr;
    auto out = std::make_shared<RelBuiltinDateExpr>(RelBuiltinDateOp::ParseDate,
                                                    std::vector<std::shared_ptr<RelExpr>>{d, fmt});
    out->ctx = node->ctx;
    return out;
  }

  if ((id == "date_add" || id == "date_subtract") && node->params.size() == 2) {
    auto a0 = GetParamExpr(node->params[0]);
    auto a1 = GetParamExpr(node->params[1]);
    if (!a0 || !a1) return nullptr;
    auto op = id == "date_add" ? RelBuiltinDateOp::DateAdd : RelBuiltinDateOp::DateSubtract;
    auto out = std::make_shared<RelBuiltinDateExpr>(op, std::vector<std::shared_ptr<RelExpr>>{a0, a1});
    out->ctx = node->ctx;
    return out;
  }

  if (id == "date_year" && node->params.size() == 1) {
    auto a0 = GetParamExpr(node->params[0]);
    if (!a0) return nullptr;
    auto out =
        std::make_shared<RelBuiltinDateExpr>(RelBuiltinDateOp::ExtractYear, std::vector<std::shared_ptr<RelExpr>>{a0});
    out->ctx = node->ctx;
    return out;
  }

  if (id == "decimal" && node->params.size() == 2) {
    auto p = TryIntFromExpr(GetParamExpr(node->params[0]));
    auto s = TryIntFromExpr(GetParamExpr(node->params[1]));
    if (!p || !s) return nullptr;
    auto out = std::make_shared<RelBuiltinDecimalCastExpr>(*p, *s, nullptr);
    out->ctx = node->ctx;
    return out;
  }

  if (id == "parse_decimal" && node->params.size() == 3) {
    auto p = TryIntFromExpr(GetParamExpr(node->params[0]));
    auto s = TryIntFromExpr(GetParamExpr(node->params[1]));
    auto lit = GetParamExpr(node->params[2]);
    if (!p || !s || !lit) return nullptr;
    auto out = std::make_shared<RelBuiltinDecimalCastExpr>(*p, *s, lit);
    out->ctx = node->ctx;
    return out;
  }

  if (id == "default_value" && node->params.size() == 2) {
    auto a = GetParamExpr(node->params[0]);
    auto b = GetParamExpr(node->params[1]);
    if (!a || !b) return nullptr;
    auto out = std::make_shared<RelBuiltinCoalesceExpr>(a, b);
    out->ctx = node->ctx;
    return out;
  }

  if (id == "substring" && node->params.size() == 3) {
    auto a = GetParamExpr(node->params[0]);
    auto b = GetParamExpr(node->params[1]);
    auto c = GetParamExpr(node->params[2]);
    if (!a || !b || !c) return nullptr;
    auto out = std::make_shared<RelBuiltinSubstringExpr>(a, b, c);
    out->ctx = node->ctx;
    return out;
  }

  if (!id.empty() && id[0] == '^') {
    if (id == "^Day" && node->params.size() == 1) {
      auto n = TryIntFromExpr(GetParamExpr(node->params[0]));
      if (!n) return nullptr;
      auto out = std::make_shared<RelTypedLiteralExpr>(RelTypedLiteralKind::Day, *n);
      out->ctx = node->ctx;
      return out;
    }
    if (id == "^Month" && node->params.size() == 1) {
      auto n = TryIntFromExpr(GetParamExpr(node->params[0]));
      if (!n) return nullptr;
      auto out = std::make_shared<RelTypedLiteralExpr>(RelTypedLiteralKind::Month, *n);
      out->ctx = node->ctx;
      return out;
    }
    if (id == "^Year" && node->params.size() == 1) {
      auto n = TryIntFromExpr(GetParamExpr(node->params[0]));
      if (!n) return nullptr;
      auto out = std::make_shared<RelTypedLiteralExpr>(RelTypedLiteralKind::Year, *n);
      out->ctx = node->ctx;
      return out;
    }
    if (id == "^FixedDecimal" && node->params.size() == 2) {
      auto p = TryIntFromExpr(GetParamExpr(node->params[0]));
      auto s = TryIntFromExpr(GetParamExpr(node->params[1]));
      if (!p || !s) return nullptr;
      auto out = std::make_shared<RelTypedLiteralExpr>(RelTypedLiteralKind::FixedDecimalType, *p, *s);
      out->ctx = node->ctx;
      return out;
    }
  }

  return nullptr;
}

std::shared_ptr<RelFormula> BuiltinResolver::TryLowerFull(const std::shared_ptr<RelFullApplication>& node) {
  auto* id_base = dynamic_cast<RelIDApplBase*>(node->base.get());
  if (!id_base) return nullptr;
  const std::string& id = id_base->id;

  if (id == "like_match" && node->params.size() == 2) {
    auto pat = TryStringFromExpr(GetParamExpr(node->params[0]));
    auto val = GetParamExpr(node->params[1]);
    if (!pat || !val) return nullptr;
    auto out = std::make_shared<RelBuiltinLikeMatchFormula>(*pat, val);
    out->ctx = node->ctx;
    return out;
  }

  if (id == "bottom" && node->params.size() == 5) {
    auto lim = TryIntFromExpr(GetParamExpr(node->params[0]));
    auto body = GetParamExpr(node->params[1]);
    auto sort_col = TryIdFromExpr(GetParamExpr(node->params[3]));
    if (!lim || !body || !sort_col) return nullptr;
    auto out = std::make_shared<RelBuiltinOrderExpr>(RelBuiltinOrderKind::BottomDesc, body, static_cast<int64_t>(*lim),
                                                     sort_col);
    out->ctx = node->ctx;
    return out;
  }

  return nullptr;
}

}  // namespace rel2sql
