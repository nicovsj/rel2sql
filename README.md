# rel-to-sql-semantics

This repository contains a rel-to-sql translator experimental tool. This project is mainly implemented in C++ with ANTLR for parsing Rel queries.

## Table of Contents

- [Prerequisites](#prerequisites)
- [Building the Project](#building-the-project)
- [Development](#development)
  - [Generating `compile_commands.json`](#generating-compile_commandsjson)
  - [Testing](#testing)
- [Extra Links](#extra-links)

## Prerequisites

This project uses Bazel as its build system. Before you begin, ensure you have the following installed on your machine:

- [Bazel](https://bazel.build) (version 7.0.0 or later)
- A C++ compiler (e.g., `g++` or `clang++`)
- Java Development Kit (JDK) 11 or later

## Building the Project

To build the project, follow these steps:

1. Clone the repository:

    ```sh
    git clone https://github.com/RelationalAI/rel-to-sql-semantics.git
    cd rel-to-sql-semantics
    ```

2. Build the project using Bazel:

    ```sh
    bazel build //:rel-to-sql
    ```

   This command will build the `rel-to-sql` executable.


## Development

### Generating `compile_commands.json`

To generate `compile_commands.json` for use with clangd, run the following command:

```sh
bazel run @hedron_compile_commands//:refresh_all
```

This will generate a `compile_commands.json` file in the root of the project and allow clangd to provide code completion and other features.

### Testing

To run the tests, use the following command:

```sh
bazel test //...
```


## Extra Links

- [Useful issue to make debugging work with Bazel and MacOS](https://github.com/bazelbuild/bazel/issues/6327)
