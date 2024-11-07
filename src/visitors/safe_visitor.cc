#include "safe_visitor.h"

SafeVisitor::SafeVisitor(std::shared_ptr<ExtendedASTData> data) : BaseVisitor(data) {}

std::any SafeVisitor::visitProgram(psr::ProgramContext *ctx) {
  visitChildren(ctx);
  return {};
}

std::any SafeVisitor::visitRelDef(psr::RelDefContext *ctx) {
  visit(ctx->relAbs());

  return {};
}

std::any SafeVisitor::visitRelAbs(psr::RelAbsContext *ctx) {
  visitChildren(ctx);

  auto &current_node = GetNode(ctx);

  std::vector<std::unordered_set<TupleBinding>> sets_to_intersect;

  for (auto &expr_ctx : ctx->expr()) {
    if (!GetNode(expr_ctx).safeness.has_value()) {
      current_node.safeness = std::nullopt;
      return {};
    }
    auto &expr_safeness = GetNode(expr_ctx).safeness.value();
    sets_to_intersect.push_back(expr_safeness);
  }

  // Intersect all the sets
  current_node.safeness = utl::IntersectSets(sets_to_intersect);

  auto special_intersection = SpecialIntersectionOfTupleBindings(sets_to_intersect);

  current_node.safeness.value().insert(special_intersection.begin(), special_intersection.end());

  return {};
}

std::any SafeVisitor::visitIDExpr(psr::IDExprContext *ctx) {
  GetNode(ctx).safeness = {};

  return {};
}

std::any SafeVisitor::visitProductExpr(psr::ProductExprContext *ctx) {
  visitChildren(ctx);

  auto &current_node = GetNode(ctx);

  current_node.safeness = std::unordered_set<TupleBinding>();

  for (auto sub_ctx : ctx->productInner()->expr()) {
    if (!GetNode(sub_ctx).safeness.has_value()) {
      current_node.safeness = std::nullopt;
      return {};
    }

    current_node.safeness.value().insert(GetNode(sub_ctx).safeness.value().begin(),
                                         GetNode(sub_ctx).safeness.value().end());
  }

  return {};
}

std::any SafeVisitor::visitConditionExpr(psr::ConditionExprContext *ctx) {
  visit(ctx->expr());
  visit(ctx->formula());

  auto &current_node = GetNode(ctx);

  current_node.safeness = std::unordered_set<TupleBinding>();

  auto expr_node = GetNode(ctx->expr());
  auto formula_node = GetNode(ctx->formula());

  current_node.safeness.value().insert(expr_node.safeness.value().begin(), expr_node.safeness.value().end());

  current_node.safeness.value().insert(formula_node.safeness.value().begin(), formula_node.safeness.value().end());

  return {};
}

std::any SafeVisitor::visitRelAbsExpr(psr::RelAbsExprContext *ctx) {
  visitChildren(ctx);

  GetNode(ctx).safeness = GetNode(ctx->relAbs()).safeness;

  return {};
}

std::any SafeVisitor::visitFormulaExpr(psr::FormulaExprContext *ctx) {
  visit(ctx->formula());

  GetNode(ctx).safeness = GetNode(ctx->formula()).safeness;

  return {};
}

std::any SafeVisitor::visitBindingsExpr(psr::BindingsExprContext *ctx) {
  visit(ctx->expr());
  auto &current_node = GetNode(ctx);

  // If the expression is not safe, the whole expression is not safe
  if (!GetNode(ctx->expr()).safeness.has_value()) {
    current_node.safeness = std::nullopt;
    return {};
  }

  // Get variables that are being bind
  std::unordered_set<std::string> binding_vars;
  for (auto &binding : ctx->bindingInner()->binding()) {
    auto &node = GetNode(binding);
    binding_vars.insert(node.variables.begin(), node.variables.end());
  }

  current_node.safeness = std::unordered_set<TupleBinding>();

  for (auto &tuple_binding : GetNode(ctx->expr()).safeness.value()) {
    if (tuple_binding.union_domain.size() != 1) {
      throw std::runtime_error("Expected exactly one projection table");
    }

    ProjectionTable child_projection = *tuple_binding.union_domain.begin();

    std::vector<int> indices;
    std::vector<std::string> new_vars_tuple;
    for (int i = 0; i < tuple_binding.vars_tuple.size(); i++) {
      if (binding_vars.find(tuple_binding.vars_tuple[i]) != binding_vars.end()) {
        indices.push_back(i);
      } else {
        new_vars_tuple.push_back(tuple_binding.vars_tuple[i]);
      }
    }

    auto new_projection = ProjectionTable(indices, child_projection, true);

    TupleBinding new_tuple_binding = {new_vars_tuple, {new_projection}};

    current_node.safeness.value().insert(new_tuple_binding);
  }

  GetNode(ctx).safeness = GetNode(ctx->expr()).safeness;

  return {};
}

std::any SafeVisitor::visitBindingsFormula(psr::BindingsFormulaContext *ctx) {
  visit(ctx->formula());

  auto formula_node = GetNode(ctx->formula());

  GetNode(ctx).safeness = {};

  for (auto &binding : ctx->bindingInner()->binding()) {
    visit(binding);
  }

  return {};
}

std::any SafeVisitor::visitPartialAppl(psr::PartialApplContext *ctx) {
  if (ctx->applBase()->T_ID() && AGGREGATE_MAP.find(ctx->applBase()->T_ID()->getText()) != AGGREGATE_MAP.end()) {
    auto param_ctx = *ctx->applParams()->applParam().begin();
    visit(param_ctx);

    auto &node = GetNode(ctx);

    auto &child_node = GetNode(param_ctx);

    node.safeness = child_node.safeness;

    return {};
  }

  if (ctx->applBase()->T_ID()) {
    bool all_vars = true;
    for (auto &param : ctx->applParams()->applParam()) {
      visit(param);
      if (*GetNode(param).variables.begin() != param->getText()) {
        all_vars = false;
      }
    }

    if (all_vars) {
      std::vector<int> indices;

      for (int i = 0; i < ctx->applParams()->applParam().size(); i++) {
        indices.push_back(i);
      }

      TupleBinding tuple_binding;

      ProjectionTable projection = {indices, ctx->applBase()->T_ID()->getText(), GetNode(ctx->applBase()).arity};

      tuple_binding.union_domain.insert(projection);

      for (auto &param : ctx->applParams()->applParam()) {
        auto variable = param->getText();

        tuple_binding.vars_tuple.push_back(variable);
      }

      GetNode(ctx).safeness = {tuple_binding};

      return {};
    }
  }

  visit(ctx->applBase());

  return {};
}

std::any SafeVisitor::visitFullAppl(psr::FullApplContext *ctx) {
  std::string base_id = ctx->getText();

  visit(ctx->applBase());

  if (!ctx->applBase()->T_ID()) {
    return {};
  }

  TupleBinding tuple_binding;

  ProjectionTable projection = {ctx->applBase()->T_ID()->getText(), GetNode(ctx->applBase()).arity};

  tuple_binding.union_domain.insert(projection);

  for (auto &param : ctx->applParams()->applParam()) {
    auto id_expr = dynamic_cast<psr::IDExprContext *>(param->expr());
    if (!id_expr) {
      return {};
    }

    auto variable = id_expr->T_ID()->getText();

    tuple_binding.vars_tuple.push_back(variable);
  }

  GetNode(ctx).safeness = {tuple_binding};

  return {};
}

std::any SafeVisitor::visitBinOp(psr::BinOpContext *ctx) {
  if (ctx->K_and()) {
    return VisitConjunction(ctx);
  } else if (ctx->K_or()) {
    return VisitDisjunction(ctx);
  }

  throw std::runtime_error("Unknown binary operator");
}

std::any SafeVisitor::VisitConjunction(psr::BinOpContext *ctx) {
  visit(ctx->lhs);
  visit(ctx->rhs);

  auto lhs_safeness = GetNode(ctx->lhs).safeness;
  auto rhs_safeness = GetNode(ctx->rhs).safeness;

  auto &current_node = GetNode(ctx);

  if (!lhs_safeness.has_value() || !rhs_safeness.has_value()) {
    current_node.safeness = std::nullopt;
    return {};
  }

  current_node.safeness = std::unordered_set<TupleBinding>();

  current_node.safeness.value().insert(lhs_safeness.value().begin(), lhs_safeness.value().end());
  current_node.safeness.value().insert(rhs_safeness.value().begin(), rhs_safeness.value().end());

  return {};
}

std::any SafeVisitor::VisitDisjunction(psr::BinOpContext *ctx) {
  visit(ctx->lhs);
  visit(ctx->rhs);

  auto lhs_safeness = GetNode(ctx->lhs).safeness;
  auto rhs_safeness = GetNode(ctx->rhs).safeness;

  GetNode(ctx).safeness = std::unordered_set<TupleBinding>();

  if (!lhs_safeness.has_value() || !rhs_safeness.has_value()) {
    GetNode(ctx).safeness = std::nullopt;
    return {};
  }

  for (const auto &lhs : lhs_safeness.value()) {
    for (const auto &rhs : rhs_safeness.value()) {
      if (lhs.vars_tuple == rhs.vars_tuple) {
        std::unordered_set<ProjectionTable> new_union_domain(lhs.union_domain);
        new_union_domain.insert(rhs.union_domain.begin(), rhs.union_domain.end());
        GetNode(ctx).safeness.value().insert({lhs.vars_tuple, new_union_domain});
      }
    }
  }

  return {};
}

std::any SafeVisitor::visitUnOp(psr::UnOpContext *ctx) {
  visit(ctx->formula());

  GetNode(ctx).safeness = GetNode(ctx->formula()).safeness;

  return {};
}

std::any SafeVisitor::visitQuantification(psr::QuantificationContext *ctx) {
  visit(ctx->formula());

  // if (!ctx->K_exists()) {
  //   // If the quantifier is not an existential quantifier, no safeness is guaranteed
  //   GetNode(ctx).safeness = std::nullopt;
  //   return {};
  // }

  if (ctx->bindingInner()->binding().size() != 1) {
    // TODO: Handle multiple bindings?
    GetNode(ctx).safeness = std::nullopt;
    return {};
  }

  std::string quant_var = ctx->bindingInner()->binding(0)->id->getText();

  auto formula_node = GetNode(ctx->formula());

  if (!formula_node.safeness.has_value()) {
    GetNode(ctx).safeness = std::nullopt;
    return {};
  }

  auto &current_node = GetNode(ctx);

  current_node.safeness = std::unordered_set<TupleBinding>();

  for (auto &tuple_binding : formula_node.safeness.value()) {
    auto it = std::find(tuple_binding.vars_tuple.begin(), tuple_binding.vars_tuple.end(), quant_var);
    if (it == tuple_binding.vars_tuple.end()) {
      throw std::runtime_error("Quantified variable not found in tuple");
    }
    int index = std::distance(tuple_binding.vars_tuple.begin(), it);

    if (tuple_binding.union_domain.size() != 1) {
      throw std::runtime_error("Expected exactly one projection table");
    }

    ProjectionTable child_projection = *tuple_binding.union_domain.begin();

    auto new_projection = ProjectionTable({index}, child_projection, true);

    std::vector<std::string> new_vars_tuple;

    for (int i = 0; i < tuple_binding.vars_tuple.size(); i++) {
      if (i != index) {
        new_vars_tuple.push_back(tuple_binding.vars_tuple[i]);
      }
    }

    TupleBinding new_tuple_binding = {new_vars_tuple, {new_projection}};

    current_node.safeness.value().insert(new_tuple_binding);
  }

  return {};
}

std::any SafeVisitor::visitParen(psr::ParenContext *ctx) {
  visit(ctx->formula());

  GetNode(ctx).safeness = GetNode(ctx->formula()).safeness;

  return {};
}

std::any SafeVisitor::visitApplBase(psr::ApplBaseContext *ctx) {
  visitChildren(ctx);

  GetNode(ctx).safeness = {};

  return {};
}

std::any SafeVisitor::visitApplParam(psr::ApplParamContext *ctx) {
  visitChildren(ctx);

  if (ctx->expr()) {
    GetNode(ctx).safeness = GetNode(ctx->expr()).safeness;
  }

  return {};
}

std::unordered_set<TupleBinding> SafeVisitor::SpecialIntersectionOfTupleBindings(
    const std::vector<std::unordered_set<TupleBinding>> &sets) const {
  if (sets.empty()) {
    return {};
  }

  std::unordered_set<TupleBinding> intersection = sets[0];

  for (size_t i = 1; i < sets.size(); ++i) {
    std::unordered_set<TupleBinding> currentIntersection;

    for (const TupleBinding &elem : intersection) {
      // If the element is in the next set, add it to the current intersection

      for (const TupleBinding &otherElem : sets[i]) {
        if (elem.vars_tuple == otherElem.vars_tuple) {
          std::unordered_set<ProjectionTable> new_union_domain(elem.union_domain);
          new_union_domain.insert(otherElem.union_domain.begin(), otherElem.union_domain.end());
          currentIntersection.insert({elem.vars_tuple, new_union_domain});
        }
      }
    }

    intersection = std::move(currentIntersection);

    if (intersection.empty()) {
      break;
    }
  }

  return intersection;
}
