# rel-to-sql-semantics

This repository contains a rel-to-sql translator experimental tool. This project is mainly implemented in C++ with ANTLR for parsing Rel queries. 

## Table of Contents

- [Prerequisites](#prerequisites)
- [Building the Project](#building-the-project)

## Prerequisites

This project uses Bazel as its build system. Before you begin, ensure you have the following installed on your machine:

- [Bazel](https://bazel.build) (version 7.0.0 or later)
- A C++ compiler (e.g., `g++` or `clang++`)
- Java Development Kit (JDK) 11 or later

## Building the Project

To build the project, follow these steps:

1. Clone the repository:

    ```sh
    git clone https://github.com/yourusername/yourproject.git
    cd yourproject
    ```

2. Build the project using Bazel:

    ```sh
    bazel build //:rel-to-sql
    ```

   This command will build the `rel-to-sql` executable.