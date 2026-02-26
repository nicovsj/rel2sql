# rel2sql

A tool that translates [Rel](https://relational.ai/rel) queries to SQL. Implemented in C++ with ANTLR for parsing Rel queries.

## Usage

The CLI reads Rel from stdin or a file and prints the translated SQL:

```sh
# From stdin
echo 'def output {"Hello World!"}' | bazel run //:rel2sql_bin

# From file
bazel run //:rel2sql_bin -- -f query.rl

# Unoptimized translation (skip optimizations)
bazel run //:rel2sql_bin -- -u -f query.rl
```

## Table of Contents

- [rel2sql](#rel2sql)
  - [Table of Contents](#table-of-contents)
  - [Prerequisites](#prerequisites)
  - [Building the Project](#building-the-project)
  - [Development](#development)
    - [Generating `compile_commands.json`](#generating-compile_commandsjson)
    - [Testing](#testing)
  - [WebAssembly (WASM) Support](#webassembly-wasm-support)
    - [Building WASM](#building-wasm)
    - [Testing WASM](#testing-wasm)
    - [WASM Documentation](#wasm-documentation)
  - [Extra](#extra)
  - [Available Tasks](#available-tasks)

## Prerequisites

This project uses Bazel as its build system and Task as its task runner. Before you begin, ensure you have the following installed on your machine:

- [Bazel](https://bazel.build) (version 7.0.0 or later)
  - We recommend using [Bazelisk](https://github.com/bazelbuild/bazelisk) to automatically use the correct Bazel version specified in the `.bazelversion` file.
- [Task](https://taskfile.dev/) (version 3.0.0 or later)
- A C++ compiler (e.g., `g++` or `clang++`)
- Java Development Kit (JDK) 11 or later

## Building the Project

To build the project, run the following command:

```sh
task build
```

This builds the rel2sql library. To build the shared library instead:

```sh
task build-shared
```

Run the CLI with:

```sh
bazel run //:rel2sql_bin -- -f query.rl
```

## Development

### Generating `compile_commands.json`

To generate `compile_commands.json` for use with clangd, run the following command:

```sh
task generate-compile-commands
```

This will generate a `compile_commands.json` file in the root of the project and allow clangd to provide code completion and other features.

This project uses [hedron_compile_commands](https://github.com/hedronvision/bazel-compile-commands-extractor) to generate the `compile_commands.json` file. For more information on how this works with Bazel, please refer to the repository's documentation.

### Testing

To run all tests (library and WASM):

```sh
task test
```

To run only library tests:

```sh
task test:lib
```

## WebAssembly (WASM) Support

Rel2SQL can be compiled to WebAssembly for use in web browsers and Node.js. This enables running Rel2SQL directly in the browser without server-side processing.

### Building WASM

To build the WebAssembly modules (Node.js and browser):

```sh
task build-wasm
```

For individual targets:

```sh
task build-wasm-node   # Node.js only
task build-wasm-browser  # Browser only
```

### Testing WASM

The full test suite (`task test`) includes WASM tests. To run only WASM tests:

```sh
task test:wasm
```

This runs:
- **test:wasm:bindings** — Tests the WASM bindings contract (requires `bazel build //wasm:rel2sql_wasm_node --config=emcc` first)
- **test:wasm:npm** — Tests npm package build, pack, and consumption
- **test:wasm:browser** — Starts a local HTTP server on port 8080; open `http://localhost:8080/wasm/tests/browser/test_browser.html` in your browser for interactive testing

### WASM Documentation

For detailed information about the WASM implementation, testing, and troubleshooting, see [`wasm/TESTING.md`](wasm/TESTING.md).

## Extra

- [Useful issue to make debugging work with Bazel and MacOS](https://github.com/bazelbuild/bazel/issues/6327)
- You can use the following command to show a GUI with the AST:
  ```sh
  task parse-antlr -- test.rl
  ```

## Available Tasks

To see all available tasks for this project, run:

```sh
task
```

This will display a list of tasks you can run using the `task` command.
