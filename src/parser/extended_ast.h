#ifndef EXTENDED_DATA_H
#define EXTENDED_DATA_H

#include <antlr4-runtime.h>

#include <set>
#include <string>

#include "sql_ast/sql_ast.h"
#include "utils/utils.h"

struct TupleBinding {
  std::vector<std::string> vars_tuple;
  std::unordered_set<std::string> union_domain;

  bool operator==(const TupleBinding &other) const {
    return vars_tuple == other.vars_tuple && union_domain == other.union_domain;
  }
};

namespace std {
template <>
struct hash<TupleBinding> {
  std::size_t operator()(const TupleBinding &tb) const {
    std::size_t seed = 0;
    utl::hash_range(seed, tb.vars_tuple.begin(), tb.vars_tuple.end());
    utl::hash_range(seed, tb.union_domain.begin(), tb.union_domain.end());
    return seed;
  }
};
}  // namespace std

struct ExtendedNode {
  // Variables are the variables that are bound in the current context
  std::set<std::string> variables;
  std::set<std::string> free_variables;

  // SQL expression is the SQL expression that corresponds to the current context
  std::shared_ptr<sql::ast::Expression> sql_expression;

  // Constant value that unambiguously corresponds to the current context
  std::optional<sql::ast::constant_t> constant;

  // Arity of the current context
  int arity;

  // Output of the safeness analysis
  std::optional<std::unordered_set<TupleBinding>> safeness;

  void VariablesInplaceUnion(const ExtendedNode &other) {
    variables.insert(other.variables.begin(), other.variables.end());
    free_variables.insert(other.free_variables.begin(), other.free_variables.end());
  }

  void VariablesInplaceDifference(const ExtendedNode &other) {
    ExtendedNode result;
    variables.insert(other.variables.begin(), other.variables.end());
    for (const auto &var : other.free_variables) {
      free_variables.erase(var);
    }
  }
};

struct ExtendedASTData {
  std::unordered_map<antlr4::ParserRuleContext *, ExtendedNode> index;
  std::unordered_map<std::string, int> arity_by_id;

  std::unordered_map<std::string, std::vector<std::string>> ids_dependencies;

  std::unordered_set<std::string> ids;
  std::unordered_set<std::string> internal_dbs;
  std::unordered_set<std::string> external_dbs;
  std::unordered_set<std::string> vars;

  std::vector<std::string> sorted_ids;

  ExtendedASTData() = default;

  ExtendedASTData(const std::unordered_map<std::string, int> &external_arity_map) {
    for (const auto &[id, arity] : external_arity_map) {
      AddEDB(id, arity);
    }
  }

  void AddIDB(const std::string &id) {
    ids.insert(id);

    if (external_dbs.find(id) != external_dbs.end()) {
      throw std::runtime_error("IDB " + id + " already in the set of EDBs");
    }

    vars.erase(id);
    internal_dbs.insert(id);
  }

  void AddEDB(const std::string &edb, int arity) {
    if (internal_dbs.find(edb) != internal_dbs.end()) {
      throw std::runtime_error("EDB " + edb + " already in the set of IDBs");
    }

    if (vars.find(edb) != vars.end()) {
      throw std::runtime_error("EDB " + edb + " already in the set of variables");
    }

    ids.insert(edb);
    external_dbs.insert(edb);

    arity_by_id[edb] = arity;
  }

  void AddVar(const std::string &var) {
    if (internal_dbs.find(var) != internal_dbs.end() || external_dbs.find(var) != external_dbs.end()) {
      // ID is already in the set of IDBs or EDBs then do nothing
      return;
    }

    ids.insert(var);
    vars.insert(var);
  }

  void AddDependency(const std::string &id, const std::string &dep) { ids_dependencies[id].push_back(dep); }
};

class ExtendedAST {
 public:
  ExtendedAST(antlr4::ParserRuleContext *root, std::shared_ptr<ExtendedASTData> data) : root_(root), data_(data) {}

  ExtendedNode Root() const { return data_->index[root_]; }

  std::shared_ptr<ExtendedASTData> Data() const { return data_; }

  ExtendedNode &Get(antlr4::ParserRuleContext *ctx) {
    auto it = data_->index.find(ctx);
    if (it == data_->index.end()) {
      data_->index[ctx] = ExtendedNode{};
    }
    return it->second;
  }

  int Arity(const std::string &id) const { return data_->arity_by_id.at(id); }

 private:
  antlr4::ParserRuleContext *root_;
  std::shared_ptr<ExtendedASTData> data_;
};

const std::map<std::string, sql::ast::AggregateFunction> AGGREGATE_MAP = {
    {"max", sql::ast::AggregateFunction::MAX},
    {"min", sql::ast::AggregateFunction::MIN},
    {"sum", sql::ast::AggregateFunction::SUM},
    {"average", sql::ast::AggregateFunction::AVG},
    {"count", sql::ast::AggregateFunction::COUNT}};

#endif  // EXTENDED_DATA_H
