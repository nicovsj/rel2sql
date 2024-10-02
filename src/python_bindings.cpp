#include <pybind11/pybind11.h>

#include "rel2sql/rel2sql.h"

PYBIND11_MODULE(pyrel2sql, m) {
  m.doc() = "Python bindings for rel2sql library";
  m.def("translate", &rel2sql::Translate, "Translates the given input string into SQL");
}
