#ifndef REL2SQL_EDB_FILE_H
#define REL2SQL_EDB_FILE_H

#include <string>

#include "rel_ast/relation_info.h"

namespace rel2sql {

// Load an extensional database description from a text file (see `benchmarks/TPCH/rel/tpch_edb.edb`
// for the TPC-H example). Lines are either blank, `#` comments, or:
//   relation_name <arity>
// where <arity> is a positive decimal integer (columns default to A1..An).
RelationMap LoadEdbFile(const std::string& path);

}  // namespace rel2sql

#endif  // REL2SQL_EDB_FILE_H
