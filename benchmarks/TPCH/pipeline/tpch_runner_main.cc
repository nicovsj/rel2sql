#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>

#include "pipeline/manifest_data.h"
#include "pipeline/tpch_pipeline_lib.h"

namespace {

void Usage(const char* argv0) {
  std::cerr << "Usage: " << argv0 << " [--compare] [--query N] [--all]\n"
            << "  --compare   Compare ref vs translated SQL (requires TPCH_DUCKDB_PATH)\n"
            << "  --query N   Run pipeline for query N (default 18)\n"
            << "  --all       Run manifest pipeline for all queries\n";
}

const rel2sql::tpch_pipeline::QueryManifestEntry* FindEntry(int query) {
  for (const auto& e : rel2sql::tpch_pipeline::kManifestEntries) {
    if (e.query == query) return &e;
  }
  return nullptr;
}

}  // namespace

int main(int argc, char** argv) {
  bool compare = false;
  bool run_all = false;
  int query = 18;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--compare") {
      compare = true;
    } else if (arg == "--all") {
      run_all = true;
    } else if (arg == "--query" && i + 1 < argc) {
      query = std::stoi(argv[++i]);
    } else if (arg == "--help" || arg == "-h") {
      Usage(argv[0]);
      return 0;
    } else {
      query = std::stoi(arg);
    }
  }

  auto root = rel2sql::tpch_pipeline::FindRepoRoot();
  auto paths = rel2sql::tpch_pipeline::DefaultPaths(root);

  auto run_one = [&](int q) -> int {
    const auto* entry = FindEntry(q);
    if (!entry) {
      std::cerr << "Unknown query " << q << "\n";
      return 1;
    }

    if (!compare) {
      auto err = rel2sql::tpch_pipeline::RunPipelineStages(*entry, paths);
      if (err) {
        std::cerr << *err << "\n";
        return 1;
      }
      std::cout << "Q" << q << " pipeline OK (rewrite, translate, execute_empty)\n";
      return 0;
    }

    const char* db = std::getenv("TPCH_DUCKDB_PATH");
    if (!db || !*db) {
      std::cerr << "TPCH_DUCKDB_PATH is required for --compare\n";
      return 1;
    }

    auto rw = rel2sql::tpch_pipeline::RunRewrite(q, paths, true);
    if (!rw.success) {
      std::cerr << "rewrite: " << rw.error << "\n";
      return 1;
    }
    auto tr = rel2sql::tpch_pipeline::RunTranslate(rw.rel_text, *entry, paths);
    if (!tr.success) {
      std::cerr << "translate: " << tr.error << "\n";
      return 1;
    }
    auto cmp = rel2sql::tpch_pipeline::RunCompare(q, tr.sql, db, paths);
    if (!cmp.success) {
      std::cerr << "compare: " << cmp.message << "\n";
      return 1;
    }
    std::cout << "Q" << q << " compare OK (ref_rows=" << cmp.ref_rows << ", gen_rows=" << cmp.gen_rows << ")\n";
    return 0;
  };

  if (run_all) {
    int failures = 0;
    for (const auto& entry : rel2sql::tpch_pipeline::kManifestEntries) {
      if (run_one(entry.query) != 0) ++failures;
    }
    return failures > 0 ? 1 : 0;
  }

  return run_one(query);
}
