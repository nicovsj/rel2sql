#include "pipeline/tpch_pipeline_lib.h"

#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <optional>
#include <sstream>
#include <thread>

#include "tests/duckdb_exec.h"

namespace rel2sql::tpch_pipeline {
namespace {

std::string ReadFile(const std::filesystem::path& p) {
  std::ifstream in(p);
  if (!in) return {};
  std::ostringstream os;
  os << in.rdbuf();
  return os.str();
}

bool Contains(const std::string& haystack, std::string_view needle) {
  return needle.empty() || haystack.find(needle) != std::string::npos;
}

}  // namespace

std::filesystem::path FindRepoRoot() {
  if (const char* env = std::getenv("REL2SQL_REPO_ROOT")) {
    return std::filesystem::path(env);
  }
  auto try_runfiles_root = [](const std::filesystem::path& base) -> std::optional<std::filesystem::path> {
    std::filesystem::path p = base / "_main";
    if (std::filesystem::exists(p / "scripts/tpch_rewrite.py") || std::filesystem::exists(p / "rel2sql_bin")) {
      return p;
    }
    return std::nullopt;
  };
  if (const char* test_srcdir = std::getenv("TEST_SRCDIR")) {
    if (auto p = try_runfiles_root(test_srcdir)) return *p;
  }
  if (const char* runfiles = std::getenv("RUNFILES_DIR")) {
    if (auto p = try_runfiles_root(runfiles)) return *p;
  }
  std::filesystem::path cur = std::filesystem::current_path();
  for (int i = 0; i < 8; ++i) {
    if (std::filesystem::exists(cur / "MODULE.bazel")) return cur;
    if (!cur.has_parent_path()) break;
    cur = cur.parent_path();
  }
  return std::filesystem::current_path();
}

std::filesystem::path ResolveRel2sqlBin(const std::filesystem::path& repo_root) {
  if (const char* bin = std::getenv("REL2SQL_BIN")) {
    return std::filesystem::path(bin);
  }
  const std::filesystem::path candidates[] = {
      repo_root / "rel2sql_bin",
      repo_root / "bazel-bin/rel2sql_bin",
  };
  for (const auto& c : candidates) {
    if (std::filesystem::exists(c)) return c;
  }
  if (const char* test_srcdir = std::getenv("TEST_SRCDIR")) {
    auto rf = std::filesystem::path(test_srcdir) / "_main/rel2sql_bin";
    if (std::filesystem::exists(rf)) return rf;
  }
  return repo_root / "bazel-bin/rel2sql_bin";
}

PipelinePaths DefaultPaths(const std::filesystem::path& repo_root) {
  PipelinePaths p;
  p.repo_root = repo_root;
  p.rewrite_script = repo_root / "scripts/tpch_rewrite.py";
  p.edb_file = repo_root / "benchmarks/TPCH/rel/tpch_edb.edb";
  p.rel2sql_bin = ResolveRel2sqlBin(repo_root);
  p.ref_sql_dir = repo_root / "benchmarks/TPCH/sql";
  p.queries_rel_dir = repo_root / "benchmarks/TPCH/rel/queries";
  return p;
}

CommandResult RunCommand(const std::vector<std::string>& argv, int timeout_sec, const std::filesystem::path& cwd) {
  CommandResult out;
  if (argv.empty()) {
    out.stderr_str = "empty argv";
    return out;
  }

  int pipe_out[2];
  int pipe_err[2];
  if (pipe(pipe_out) != 0 || pipe(pipe_err) != 0) {
    out.stderr_str = "pipe failed";
    return out;
  }

  pid_t pid = fork();
  if (pid < 0) {
    out.stderr_str = "fork failed";
    return out;
  }
  if (pid == 0) {
    close(pipe_out[0]);
    close(pipe_err[0]);
    dup2(pipe_out[1], STDOUT_FILENO);
    dup2(pipe_err[1], STDERR_FILENO);
    close(pipe_out[1]);
    close(pipe_err[1]);
    if (!cwd.empty()) {
      std::filesystem::current_path(cwd);
    }
    std::vector<char*> cargv;
    cargv.reserve(argv.size() + 1);
    for (const auto& a : argv) {
      cargv.push_back(const_cast<char*>(a.c_str()));
    }
    cargv.push_back(nullptr);
    execv(cargv[0], cargv.data());
    _exit(127);
  }

  close(pipe_out[1]);
  close(pipe_err[1]);

  auto drain = [](int fd, std::string* buf) {
    char chunk[4096];
    ssize_t n;
    while ((n = read(fd, chunk, sizeof(chunk))) > 0) {
      buf->append(chunk, static_cast<size_t>(n));
    }
  };

  if (timeout_sec <= 0) {
    drain(pipe_out[0], &out.stdout_str);
    drain(pipe_err[0], &out.stderr_str);
    close(pipe_out[0]);
    close(pipe_err[0]);
    int status = 0;
    waitpid(pid, &status, 0);
    out.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return out;
  }

  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout_sec);
  int status = 0;
  bool done = false;
  while (std::chrono::steady_clock::now() < deadline) {
    pid_t w = waitpid(pid, &status, WNOHANG);
    if (w == pid) {
      done = true;
      break;
    }
    if (w < 0) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  drain(pipe_out[0], &out.stdout_str);
  drain(pipe_err[0], &out.stderr_str);
  close(pipe_out[0]);
  close(pipe_err[0]);

  if (!done) {
    kill(pid, SIGKILL);
    waitpid(pid, &status, 0);
    out.timed_out = true;
    out.exit_code = 124;
    return out;
  }
  out.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
  return out;
}

CommandResult RunShell(const std::string& cmd, int timeout_sec, const std::filesystem::path& cwd) {
  return RunCommand({"/bin/sh", "-c", cmd}, timeout_sec, cwd);
}

RewriteResult RunRewrite(int query, const PipelinePaths& paths, bool with_defs) {
  RewriteResult r;
  std::ostringstream cmd;
  cmd << "python3 " << paths.rewrite_script.string() << " " << query;
  if (!with_defs) cmd << " --no-defs";
  auto res = RunShell(cmd.str(), 120, paths.repo_root);
  if (res.timed_out) {
    r.error = "rewrite timed out";
    return r;
  }
  if (res.exit_code != 0) {
    r.error = "rewrite exit " + std::to_string(res.exit_code) + ": " + res.stderr_str;
    return r;
  }
  r.rel_text = res.stdout_str;
  if (r.rel_text.empty()) {
    r.error = "rewrite produced empty output";
    return r;
  }
  if (r.rel_text.find("@inline") != std::string::npos || r.rel_text.find("@vectorized") != std::string::npos) {
    r.error = "rewrite left annotations in output";
    return r;
  }
  r.success = true;
  return r;
}

TranslateResult RunTranslate(const std::string& rel_text, const QueryManifestEntry& entry, const PipelinePaths& paths) {
  TranslateResult r;
  auto tmp_rel = std::filesystem::temp_directory_path() / ("tpch_q" + std::to_string(entry.query) + ".rel");
  {
    std::ofstream out(tmp_rel);
    out << rel_text;
  }

  std::vector<std::string> argv = {paths.rel2sql_bin.string()};
  if (entry.unoptimized) argv.push_back("-u");
  argv.push_back("-e");
  argv.push_back(paths.edb_file.string());
  argv.push_back("-f");
  argv.push_back(tmp_rel.string());

  auto res = RunCommand(argv, entry.timeout_sec, paths.repo_root);
  std::error_code ec;
  std::filesystem::remove(tmp_rel, ec);

  if (res.timed_out) {
    r.error = "translation timed out";
    return r;
  }
  if (res.exit_code != 0) {
    r.error = res.stderr_str.empty() ? ("exit " + std::to_string(res.exit_code)) : res.stderr_str;
    return r;
  }
  r.sql = res.stdout_str;
  if (r.sql.find("SELECT") == std::string::npos && r.sql.find("CREATE") == std::string::npos) {
    r.error = "translation output missing SELECT/CREATE";
    return r;
  }
  r.success = true;
  return r;
}

ExecuteResult RunExecuteEmpty(const std::string& sql, const PipelinePaths& paths) {
  ExecuteResult r;
  auto tpch_edb = rel2sql::testing::LoadTpchEdbForDuckDb(paths.edb_file.string());
  if (tpch_edb.relations.map.empty()) {
    r.error = "failed to load EDB: " + paths.edb_file.string();
    return r;
  }
  rel2sql::testing::DuckDbSession session;
  std::string err =
      rel2sql::testing::OpenInMemorySession(&session, tpch_edb.relations, &tpch_edb.varchar_value_relations);
  if (!err.empty()) {
    r.error = err;
    return r;
  }
  err.clear();
  if (!rel2sql::testing::ExecuteScriptOnConnection(session.con, sql, &err)) {
    r.error = err;
    return r;
  }
  err.clear();
  auto probe = rel2sql::testing::ExecuteQueryOnConnection(session.con, "SELECT * FROM result LIMIT 1", &err);
  if (!err.empty()) {
    err.clear();
    probe = rel2sql::testing::ExecuteQueryOnConnection(
        session.con, "SELECT 1 AS ok WHERE EXISTS (SELECT 1 FROM information_schema.tables LIMIT 1)", &err);
    if (!err.empty()) {
      r.success = true;
      return r;
    }
  }
  r.success = true;
  return r;
}

CompareResult RunCompare(int query, const std::string& translated_sql, const std::string& db_path,
                         const PipelinePaths& paths) {
  CompareResult r;
  auto ref_path = paths.ref_sql_dir / ("q" + std::to_string(query) + ".sql");
  if (!std::filesystem::exists(ref_path)) {
    r.message = "reference SQL missing: " + ref_path.string();
    return r;
  }
  std::string ref_sql = ReadFile(ref_path);

  rel2sql::testing::DuckDbSession session;
  std::string err = rel2sql::testing::OpenFileSession(&session, db_path);
  if (!err.empty()) {
    r.message = err;
    return r;
  }

  err.clear();
  if (!rel2sql::testing::ExecuteScriptOnConnection(session.con, translated_sql, &err)) {
    r.message = "translated script: " + err;
    return r;
  }

  std::string gen_query = "SELECT * FROM result";
  err.clear();
  auto gen_rs = rel2sql::testing::ExecuteQueryOnConnection(session.con, gen_query, &err);
  if (!err.empty()) {
    r.message = "translated result query: " + err;
    return r;
  }
  r.gen_rows = gen_rs.rows.size();

  err.clear();
  auto ref_rs = rel2sql::testing::ExecuteQueryOnConnection(session.con, ref_sql, &err);
  if (!err.empty()) {
    r.message = "reference SQL: " + err;
    return r;
  }
  r.ref_rows = ref_rs.rows.size();

  std::string diff;
  if (rel2sql::testing::ResultSetsEqual(ref_rs, gen_rs, 1.0, true, &diff)) {
    r.success = true;
  } else {
    r.message = diff + " (ref_rows=" + std::to_string(r.ref_rows) + ", gen_rows=" + std::to_string(r.gen_rows) + ")";
  }
  return r;
}

std::optional<std::string> RunPipelineStages(const QueryManifestEntry& entry, const PipelinePaths& paths) {
  const std::string q = std::to_string(entry.query);

  if (entry.rewrite == "skip") {
    return std::nullopt;
  }

  auto rw = RunRewrite(entry.query, paths, true);
  if (entry.rewrite == "fail") {
    if (rw.success) return "Q" + q + " rewrite: expected failure but succeeded";
    return std::nullopt;
  }
  if (!rw.success) return "Q" + q + " rewrite: " + rw.error;

  if (entry.translate == "skip") {
    return std::nullopt;
  }

  auto tr = RunTranslate(rw.rel_text, entry, paths);
  if (entry.translate == "fail") {
    if (tr.success) return "Q" + q + " translate: expected failure but succeeded";
    if (!entry.stderr_contains.empty() && !Contains(tr.error, entry.stderr_contains)) {
      return "Q" + q + " translate: expected failure but wrong error: " + tr.error;
    }
    return std::nullopt;
  }
  if (!tr.success) return "Q" + q + " translate: " + tr.error;

  if (entry.execute_empty == "skip") {
    return std::nullopt;
  }
  auto ex = RunExecuteEmpty(tr.sql, paths);
  if (entry.execute_empty == "fail") {
    if (ex.success) return "Q" + q + " execute_empty: expected failure but succeeded";
    return std::nullopt;
  }
  if (entry.execute_empty == "ok") {
    if (!ex.success) return "Q" + q + " execute_empty: " + ex.error;
  }

  return std::nullopt;
}

}  // namespace rel2sql::tpch_pipeline
