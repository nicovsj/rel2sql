#!/usr/bin/env bash
# Idempotent Cloud Agent setup for Rel2SQL.
#
# The base image already provides JDK, Node, npm, python3, clang, g++, git and
# curl. This script adds the two build-runner binaries the repo expects
# (Bazelisk + Task), the libstdc++ headers Clang needs, and a home-level bazelrc
# that selects the Clang toolchain the source relies on.
set -euo pipefail

BAZELISK_VERSION="v1.25.0"
TASK_VERSION="v3.53.1"
ARCH="$(uname -m)"
case "$ARCH" in
  x86_64) BAZELISK_ARCH="amd64"; TASK_ARCH="amd64" ;;
  aarch64 | arm64) BAZELISK_ARCH="arm64"; TASK_ARCH="arm64" ;;
  *) echo "Unsupported arch: $ARCH" >&2; exit 1 ;;
esac

# --- System packages -------------------------------------------------------
# Clang 18 auto-detects the highest installed GCC toolchain (GCC 14 on Ubuntu
# 24.04) but the base image only ships libstdc++-13-dev headers, so <cstdint>
# and friends are not found. Install the matching libstdc++-14-dev.
if [ ! -d /usr/include/c++/14 ]; then
  sudo apt-get update -qq
  sudo apt-get install -y --no-install-recommends libstdc++-14-dev
fi

# --- Bazelisk (provides `bazel`, honoring .bazelversion) --------------------
if ! command -v bazelisk >/dev/null 2>&1; then
  curl -fsSL -o /tmp/bazelisk \
    "https://github.com/bazelbuild/bazelisk/releases/download/${BAZELISK_VERSION}/bazelisk-linux-${BAZELISK_ARCH}"
  sudo install -m 0755 /tmp/bazelisk /usr/local/bin/bazelisk
  sudo ln -sf /usr/local/bin/bazelisk /usr/local/bin/bazel
fi

# --- Task (go-task) --------------------------------------------------------
if ! command -v task >/dev/null 2>&1; then
  curl -fsSL -o /tmp/task.tar.gz \
    "https://github.com/go-task/task/releases/download/${TASK_VERSION}/task_linux_${TASK_ARCH}.tar.gz"
  mkdir -p /tmp/task-install
  tar -xzf /tmp/task.tar.gz -C /tmp/task-install
  sudo install -m 0755 /tmp/task-install/task /usr/local/bin/task
fi

# --- Home bazelrc ----------------------------------------------------------
# The source relies on Clang-only warning flags (e.g. -Wno-deprecated-builtins)
# and GCC rejects it (-Wchanges-meaning), so select Clang. Clang's module
# layering_check is stricter than the vendored abseil BUILD targets declare, so
# turn it off. These belong in the home rc (read by every bazel/task run)
# rather than the committed //.bazelrc which is shared with macOS.
cat > "$HOME/.bazelrc" <<'EOF'
# Managed by .cursor/install.sh (Cloud Agent environment).
build --repo_env=CC=clang
build --repo_env=CXX=clang++
build --features=-layering_check
EOF

# --- Warm the Bazel cache --------------------------------------------------
# Downloads the pinned Bazel, ANTLR jar, DuckDB, abseil, etc., and compiles the
# library + CLI so agents (and snapshots) start ready to build.
cd "$(dirname "$0")/.."
bazel build --config=default //:rel2sql //:rel2sql_bin

echo "Rel2SQL Cloud Agent environment setup complete."
