cmake_minimum_required(VERSION 3.15)
project(rel-sql VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Add source files
add_executable(rel-sql src/main.cpp)

# Include directories
target_include_directories(rel-sql PUBLIC include)

# Add subdirectory for tests
add_subdirectory(tests)
