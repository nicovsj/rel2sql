# TPCH Benchmark

## Dependencies

#### RelationalAI

A modified tpch-dbgen is needed, which is automatically downloaded
when building RelationalAI. (Found in `raicode/deps/usr/tpch-dbgen` ) --  also qgen-RAI? Clarify

To run the benchmark using RAI, you need a database connection.

```julia
    import RelationalAI
    dbname = "tpch"
    connection = RelationalAI.LocalConnection(dbname=Symbol(dbname); report=nothing)
```
This creates a connection to a local server, for executing the queries.
(See the RelationalAI docs for how to start a local server.)

%% will this overwrite the existing dbname? No, it just generates the data files
%% goes in raicode/data, need to fix to use a sub-directory as before

To generate the data files to import into RelationalAI, if you have not yet done so,

```julia
    import RAI_Benchmarks.TPCH
    scale_factor = "0.01"
    conf = TPCH.rel_config(scale_factor, connection) # connection is actually not used in generate_data
    TPCH.generate_data(conf)
```

To do a full TPCH run: (note: this will erase your previous TPCH DB as determined by `conf.connection`):

```julia
    julia> TPCH.run_tpch(conf)
```
This will import the data files into RelationalAI (overwriting the `dbname` DB, if it exists), and run the queries.

To just rebuild the RelationalAI database, but not run a full TPCH benchmark:

```julia
    julia> TPCH.init_db(conf)
```

The queries themselves are in `raicode/examples/tpch`

If you want to run a TPCH query, e.g. query 5:

```julia
    julia> TPCH.run_query(conf, 5)
```

You can also generate a query, and then use it with `query`:

```julia
    julia> q = TPCH.generate_query(conf, 5)
    julia> query(connection, q; out=:result, debug = true)
```

To run a validation query:

```julia
    julia> TPCH.run_validation_query(conf, 5)
```

## End-to-end Scripts

The following scripts may be helpful (note that you need a server up an running):

tpch-init.jl (initialize a database)
```julia
using RelationalAI
import RAI_Benchmarks.TPCH

conn = LocalConnection(dbname=:tpch)
conf = TPCH.rel_config("0.01", conn)
TPCH.generate_data(conf)
TPCH.init_db(conf)
```

tpch-query.jl (run a query using an existing database)

```julia
using RelationalAI
import RAI_Benchmarks.TPCH

conn = LocalConnection(dbname=:tpch)
conf = TPCH.rel_config("0.01", conn)
@time TPCH.run_query(conf, 1, conf.schema_name)
```
