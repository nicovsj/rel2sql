#ifndef REWRITER_UNDERSCORE_REWRITER_H
#define REWRITER_UNDERSCORE_REWRITER_H

#include "rewriter/base_rewriter.h"

namespace rel2sql {

class RelContext;

/**
 * Rewrites underscore placeholders in applications:
 *
 * 1. Full application: A(..., _, ..., _, ...) =>
 *    exists((z1, ..., zk) | A(..., z1, ..., zk, ...))
 *
 * 2. Partial application: A[arg1, ..., argi-1, _, argi+1, ..., argk] =>
 *   (zk+1, ..., z|A|) : exists((z) | A(arg1, ..., z, ..., argk, zk+1, ..., z|A|))
 */
class UnderscoreRewriter : public BaseRelRewriter {
 public:
  UnderscoreRewriter() : container_(nullptr) {}
  explicit UnderscoreRewriter(const RelContext* container) : container_(container) {}

  void Visit(RelFullAppl& node) override;
  void Visit(RelPartialAppl& node) override;

 private:
  std::string FreshVarName();
  std::shared_ptr<RelApplParam> MakeVarParam(const std::string& var);
  int GetRelationArity(const std::string& id) const;

  const RelContext* container_;
  int fresh_var_counter_ = 0;
};

}  // namespace rel2sql

#endif  // REWRITER_UNDERSCORE_REWRITER_H
