#include "canonical_form.h"

#include <cmath>

#include "support/utils.h"

namespace rel2sql {
namespace sql::ast {

namespace {

std::optional<double> ConstantToDouble(const Constant& c) {
  return std::visit(
      utl::overloaded{[](int i) -> std::optional<double> { return static_cast<double>(i); },
                      [](double d) -> std::optional<double> { return d; },
                      [](const auto&) -> std::optional<double> { return std::nullopt; }},
      c.value);
}

// Returns (coefficient, subexpr) when term = coeff * subexpr. subexpr is nullptr for constants.
std::optional<std::pair<double, std::shared_ptr<Term>>> ExtractCoeffAndSubexpr(
    const std::shared_ptr<Term>& term) {
  if (!term) return std::nullopt;

  if (auto c = std::dynamic_pointer_cast<Constant>(term)) {
    auto d = ConstantToDouble(*c);
    if (d) return std::make_pair(*d, std::shared_ptr<Term>(nullptr));
    return std::nullopt;
  }

  if (auto col = std::dynamic_pointer_cast<Column>(term)) {
    return std::make_pair(1.0, std::static_pointer_cast<Term>(col));
  }

  if (auto paren = std::dynamic_pointer_cast<ParenthesisTerm>(term)) {
    return ExtractCoeffAndSubexpr(paren->term);
  }

  if (auto op = std::dynamic_pointer_cast<Operation>(term)) {
    if (op->op == "*") {
      auto lhs_const = std::dynamic_pointer_cast<Constant>(op->lhs);
      auto rhs_const = std::dynamic_pointer_cast<Constant>(op->rhs);
      if (lhs_const) {
        auto k = ConstantToDouble(*lhs_const);
        if (k && *k != 0.0) {
          auto inner = ExtractCoeffAndSubexpr(op->rhs);
          if (inner && inner->second) {
            return std::make_pair(*k * inner->first, inner->second);
          }
          return std::make_pair(*k, op->rhs);
        }
      }
      if (rhs_const) {
        auto k = ConstantToDouble(*rhs_const);
        if (k && *k != 0.0) {
          auto inner = ExtractCoeffAndSubexpr(op->lhs);
          if (inner && inner->second) {
            return std::make_pair(*k * inner->first, inner->second);
          }
          return std::make_pair(*k, op->lhs);
        }
      }
    }
    if (op->op == "/") {
      auto rhs_const = std::dynamic_pointer_cast<Constant>(op->rhs);
      if (rhs_const) {
        auto d = ConstantToDouble(*rhs_const);
        if (d && *d != 0.0) {
          auto inner = ExtractCoeffAndSubexpr(op->lhs);
          if (inner && inner->second) {
            return std::make_pair(inner->first / *d, inner->second);
          }
          return std::make_pair(1.0 / *d, op->lhs);
        }
      }
    }
  }

  return std::make_pair(1.0, term);
}

std::vector<std::pair<int, std::shared_ptr<Term>>> FlattenSum(const std::shared_ptr<Term>& term) {
  if (!term) return {};

  if (auto op = std::dynamic_pointer_cast<Operation>(term)) {
    if (op->op == "+") {
      auto lhs_addends = FlattenSum(op->lhs);
      auto rhs_addends = FlattenSum(op->rhs);
      lhs_addends.insert(lhs_addends.end(), rhs_addends.begin(), rhs_addends.end());
      return lhs_addends;
    }
    if (op->op == "-") {
      auto lhs_addends = FlattenSum(op->lhs);
      auto rhs_addends = FlattenSum(op->rhs);
      for (auto& [sign, t] : rhs_addends) {
        lhs_addends.emplace_back(-sign, t);
      }
      return lhs_addends;
    }
  }

  if (auto paren = std::dynamic_pointer_cast<ParenthesisTerm>(term)) {
    return FlattenSum(paren->term);
  }

  return {{1, term}};
}

bool TermsEqualModuloAlias(const std::shared_ptr<Term>& lhs, const std::shared_ptr<Term>& rhs) {
  if (!lhs || !rhs) return !lhs && !rhs;

  if (auto lcol = std::dynamic_pointer_cast<Column>(lhs)) {
    auto rcol = std::dynamic_pointer_cast<Column>(rhs);
    if (!rcol) return false;
    return lcol->name == rcol->name;
  }

  if (auto lconst = std::dynamic_pointer_cast<Constant>(lhs)) {
    auto rconst = std::dynamic_pointer_cast<Constant>(rhs);
    if (!rconst) return false;
    return lconst->value == rconst->value;
  }

  if (auto lop = std::dynamic_pointer_cast<Operation>(lhs)) {
    auto rop = std::dynamic_pointer_cast<Operation>(rhs);
    if (!rop) return false;
    if (lop->op != rop->op) return false;
    return TermsEqualModuloAlias(lop->lhs, rop->lhs) && TermsEqualModuloAlias(lop->rhs, rop->rhs);
  }

  if (auto lparen = std::dynamic_pointer_cast<ParenthesisTerm>(lhs)) {
    auto rparen = std::dynamic_pointer_cast<ParenthesisTerm>(rhs);
    if (!rparen) return false;
    return TermsEqualModuloAlias(lparen->term, rparen->term);
  }

  return false;
}

void AddToCanonicalForm(CanonicalForm& form, double signed_coeff, const std::shared_ptr<Term>& sub) {
  if (sub == nullptr) {
    form.constant += signed_coeff;
    return;
  }
  auto op = std::dynamic_pointer_cast<Operation>(sub);
  if (op && (op->op == "+" || op->op == "-")) {
    auto lhs_addends = FlattenSum(op->lhs);
    auto rhs_addends = FlattenSum(op->rhs);
    if (op->op == "-") {
      for (auto& [s, _] : rhs_addends) s = -s;
    }
    for (const auto& [s, t] : lhs_addends) {
      auto ext = ExtractCoeffAndSubexpr(t);
      if (ext) AddToCanonicalForm(form, signed_coeff * s * ext->first, ext->second);
    }
    for (const auto& [s, t] : rhs_addends) {
      auto ext = ExtractCoeffAndSubexpr(t);
      if (ext) AddToCanonicalForm(form, signed_coeff * s * ext->first, ext->second);
    }
    return;
  }
  bool found = false;
  for (auto& [repr, coeff] : form.terms) {
    if (TermsEqualModuloAlias(repr, sub)) {
      coeff += signed_coeff;
      found = true;
      break;
    }
  }
  if (!found) form.terms.emplace_back(sub, signed_coeff);
}

}  // namespace

std::optional<CanonicalForm> ComputeCanonicalForm(const std::shared_ptr<Term>& term) {
  if (!term) return std::nullopt;

  auto addends = FlattenSum(term);
  CanonicalForm form;

  for (const auto& [sign, t] : addends) {
    auto extracted = ExtractCoeffAndSubexpr(t);
    if (!extracted) return std::nullopt;
    AddToCanonicalForm(form, sign * extracted->first, extracted->second);
  }

  form.terms.erase(
      std::remove_if(form.terms.begin(), form.terms.end(),
                     [](const auto& p) { return std::abs(p.second) < 1e-10; }),
      form.terms.end());

  return form;
}

bool CanonicalFormsEqual(const CanonicalForm& a, const CanonicalForm& b) {
  if (std::abs(a.constant - b.constant) >= 1e-10) return false;
  if (a.terms.size() != b.terms.size()) return false;

  for (const auto& [sub_a, coeff_a] : a.terms) {
    bool matched = false;
    for (const auto& [sub_b, coeff_b] : b.terms) {
      if (TermsEqualModuloAlias(sub_a, sub_b) && std::abs(coeff_a - coeff_b) < 1e-10) {
        matched = true;
        break;
      }
    }
    if (!matched) return false;
  }
  return true;
}

bool AreEqualityExpressionsEqual(const std::shared_ptr<ComparisonCondition>& comp) {
  if (!comp || comp->op != CompOp::EQ) return false;
  if (comp->lhs_canonical && comp->rhs_canonical) {
    return CanonicalFormsEqual(*comp->lhs_canonical, *comp->rhs_canonical);
  }
  auto form_lhs = ComputeCanonicalForm(comp->lhs);
  auto form_rhs = ComputeCanonicalForm(comp->rhs);
  if (!form_lhs || !form_rhs) return false;
  return CanonicalFormsEqual(*form_lhs, *form_rhs);
}

namespace {

std::shared_ptr<Column> ExtractSingleColumnFromTerm(const std::shared_ptr<Term>& term) {
  if (!term) return nullptr;
  if (auto col = std::dynamic_pointer_cast<Column>(term)) return col;
  if (auto paren = std::dynamic_pointer_cast<ParenthesisTerm>(term)) {
    return ExtractSingleColumnFromTerm(paren->term);
  }
  if (auto op = std::dynamic_pointer_cast<Operation>(term)) {
    auto left_col = ExtractSingleColumnFromTerm(op->lhs);
    auto right_col = ExtractSingleColumnFromTerm(op->rhs);
    if (left_col && !right_col) return left_col;
    if (right_col && !left_col) return right_col;
    return nullptr;
  }
  return nullptr;
}

}  // namespace

bool IsTautologyByCanonicalForm(const std::shared_ptr<ComparisonCondition>& comp) {
  if (!comp || comp->op != CompOp::EQ) return false;
  if (!AreEqualityExpressionsEqual(comp)) return false;
  auto left_col = ExtractSingleColumnFromTerm(comp->lhs);
  auto right_col = ExtractSingleColumnFromTerm(comp->rhs);
  if (!left_col || !right_col) return false;
  if (!left_col->source.has_value() || !right_col->source.has_value()) return false;
  return left_col->source.value()->Alias() == right_col->source.value()->Alias() &&
         left_col->name == right_col->name;
}

}  // namespace sql::ast
}  // namespace rel2sql
