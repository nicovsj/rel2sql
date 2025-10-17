# rel2sql

This repository contains a rel2sql translation experimental tool. This project is mainly implemented in C++ with ANTLR for parsing Rel queries.

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
      - [Interactive Testing](#interactive-testing)
      - [Automated Testing (CI/CD)](#automated-testing-cicd)
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

This command will build the `rel2sql` executable. It should be built in the `bazel-bin` directory.

## Development

### Generating `compile_commands.json`

To generate `compile_commands.json` for use with clangd, run the following command:

```sh
task generate-compile-commands
```

This will generate a `compile_commands.json` file in the root of the project and allow clangd to provide code completion and other features.

This project uses [hedron_compile_commands](https://github.com/hedronvision/bazel-compile-commands-extractor) to generate the `compile_commands.json` file. For more information on how this works with Bazel, please refer to the repository's documentation.

### Testing

To run the tests, use the following command:

```sh
task test
```

## WebAssembly (WASM) Support

Rel2SQL can be compiled to WebAssembly for use in web browsers. This enables running Rel2SQL directly in the browser without server-side processing.

### Building WASM

To build the WebAssembly module:

```sh
task build-wasm
```

### Testing WASM

#### Interactive Testing

To build and test the WASM module with an interactive web interface:

```sh
task test-wasm
```

This will:
1. Build the WASM module
2. Start a local HTTP server on port 8000
3. Open `http://localhost:8000/wasm/test_wasm.html` in your browser

The test page provides:
- Interactive Rel-to-SQL translation
- System tests to verify WASM functionality
- Real-time status indicators

#### Automated Testing (CI/CD)

For automated testing in CI workflows:

```sh
task test-wasm-bazel
```

This automated test:
- Builds the WASM module
- Verifies WASM files are generated correctly
- Loads and tests the WASM module using Node.js
- Runs translation tests with sample Rel expressions
- Reports pass/fail status for CI integration

### WASM Documentation

For detailed information about the WASM implementation, testing, and troubleshooting, see [`wasm/README.md`](wasm/README.md).

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
