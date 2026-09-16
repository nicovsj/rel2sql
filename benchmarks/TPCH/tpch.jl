module TPCH
using Dates: Dates, Date, DateTime, now
using JSON: JSON
using StatsBase: StatsBase, geomean
using Printf: Printf, @sprintf
using RelationalAI: LocalConnection
using RelationalAIBase: @dassert0
using HydraMetrics: hydra_metrics
using RAI_Primitives: debug_natives_enabled, set_debug_natives_on_off
using RAI_Metrics: get_default_registry, value_of
import RAI_PagerCore

const TOP_SRC_DIR = abspath(joinpath(dirname(@__FILE__), "..", ".."))
const VALIDATION_TOP_DIR = "https://s3.amazonaws.com/relationalai-benchmarks-public/tpch/validation/"
const CAPTURED_METRICS = ["pager_hard_page_faults"]

mutable struct QueryResult
    query :: Int
    duration :: Float64
    qe_duration :: Float64 # Only time spent in Query Evaluator excluding e.g., frontend
    begin_timestamp :: DateTime
    end_timestamp :: DateTime
    query_params :: Vector{String}
    metrics :: Dict{String, Float64}
end

mutable struct CompiledRun
    simulated_latency :: Float64
    force_dbopen :: Bool
    queries :: Vector{QueryResult}
end

mutable struct PowerResult
    streams :: Int
    power_size :: Float64
    qphh_size :: Float64
    power_rf1 :: Float64
    power_rf2 :: Float64
    queries :: Vector{QueryResult}
end

mutable struct StreamResult
    streams :: Int
    rf1 :: Float64
    rf2 :: Float64
    queries :: Vector{QueryResult}
end

mutable struct ThroughputResult
    throughput_size :: Float64
    throughputs :: Vector{StreamResult}
end

struct PerformanceRun
    power :: PowerResult
    throughput :: ThroughputResult
end

mutable struct FileLoadResult
    csv_file :: String
    duration :: Float64
    begin_timestamp :: DateTime
    end_timestamp :: DateTime
end

mutable struct DataLoadResult
    total_time :: Float64
    result :: Vector{FileLoadResult}
end

mutable struct DBSizeResult
    relations_bytes :: Int
    metadata_bytes :: Int
end

mutable struct TPCHResult
    scale_factor :: String
    load :: DataLoadResult
    performance_runs :: Vector{PerformanceRun}
    compiled_runs:: Vector{CompiledRun}
    validation :: StreamResult
    db_size :: DBSizeResult
end

mutable struct TPCHExecution
    metrics :: TPCHResult
end


TPCHExecution() = TPCHExecution(TPCHResult())
TPCHResult() = TPCHResult("", DataLoadResult(), Vector(), Vector(), StreamResult(), DBSizeResult())
DataLoadResult() = DataLoadResult(0.0, Vector())
FileLoadResult() = FileLoadResult("", 0.0, DateTime(now()), DateTime(now()))
StreamResult() = StreamResult(0, 0, 0, Vector())
DBSizeResult() = DBSizeResult(0, 0)
PowerResult() = PowerResult(0, 0.0, 0.0, 0.0, 0.0, Vector())
ThroughputResult() = ThroughputResult(0.0, Vector())
PerformanceRun() = PerformanceRun(PowerResult(), ThroughputResult())
CompiledRun() = CompiledRun(0.0, false, Vector())
QueryResult() = QueryResult(0, 0.0, 0.0, DateTime(now()), DateTime(now()), Vector(), Dict{String, Float64}())

include("./utils.jl") # Helper functions
include("./params.jl") # Params from TPCH specification
include("./validator.jl")
include("./export.jl") # routines to export the result to csv files or stdout
include("./rel/rel.jl") # RAI implementation of TPCH
include("./generator.jl") # Functions to generate queries and validation data

function run_validation_query(config, i::Int, db::String = config.schema_name; print=true)
    info("running validation query $i on $db")
    if config.supported_queries == [] || i in config.supported_queries
        q = generate_validation_query(config, i)
        df = ""
        if config.output_dir != ""
            ExportResults.write_output(config, "validate/q$i", q)
            df = "$(config.output_dir)/validate/q$i.csv"
        end
        validate_file = config.validation ? joinpath(VALIDATION_TOP_DIR, "tpch-sf$(config.scale_factor)-validation", "q$(i).csv") : ""

        begin_timestamp = DateTime(now())
        t, qe_time = execute_query(config, q, df, print, validate_file)
        push!(config.result.metrics.validation.queries, QueryResult(i, t, qe_time, begin_timestamp, DateTime(now()), Vector(), Dict{String, Float64}()))
        info("ran validation Q$i on $db: $t")
    else
        t = 0
    end
    t
end

function run_validation_queries(config)
    config.result.metrics.validation = StreamResult(1, 0, 0, Vector())
    for i in config.supported_queries
        t = run_validation_query(config, i, config.schema_name; print=false)
    end
end

function capture_metrics(config, query_result::QueryResult)
    get_value(name) = something(value_of(get_default_registry(), name), 0.0)
    for metric in CAPTURED_METRICS
        query_result.metrics[metric] = get_value(metric) - get(query_result.metrics, metric, 0.0)
    end
end

function run_query(config, i::Int, prefix::String = "", query_result::QueryResult = QueryResult(), print=false)
    @assert config.connections != []
    if config.supported_queries == [] || i in config.supported_queries
        info("running Q$i")
        q = generate_query(config, i)
        data_file = ""
        if config.output_dir != ""
            ExportResults.write_output(config, "$prefix/q$i", q)
            data_file = "$(config.output_dir)/$prefix/q$i.csv"
        end
        capture_metrics(config, query_result)
        begin_timestamp = DateTime(now())
        t, qe_time = execute_query(config, q, data_file, print)

        info("ran Q$i: $t")

        query_result.query = i
        query_result.duration = t
        query_result.qe_duration = qe_time
        query_result.begin_timestamp = begin_timestamp
        query_result.end_timestamp = DateTime(now())
        capture_metrics(config, query_result)

        t, qe_time
    else
        0, 0
    end
end

function power_test(config, r::Int)
    run = config.result.metrics.performance_runs[r].power
    # tpch specification
    run.streams = 1

    if config.refresh_streams
        run.power_rf1 = run_rf1(config, r)
    end

    for i = ordered_sets[1]
        if i in config.supported_queries
            query_result = QueryResult()
            run_query(config, i, "run$r/power", query_result)
            push!(run.queries, query_result)
        end
    end

    if config.refresh_streams
        run.power_rf2 = run_rf2(config, r)
    end

    results = [ qr.duration for qr in run.queries ]

    if config.refresh_streams
        push!(results, run.power_rf1)
        push!(results, run.power_rf2)
    end

    results = filter(x -> x != 0, results)
    geo_mean = geomean(results)
    power = (3600/geo_mean)*parse(Float64, config.scale_factor)
    # remove round once the json big float issue is fixed in Rel
    run.power_size = round(power, digits=9)

    info("Run $r Power@Size = $power")
end

function run_stream(config, s, r)
    queries = Vector()

    t1 = run_rf1(config, s+2)
    for i = ordered_sets[s+1]
        # new query result reference for each run
        if i in config.supported_queries
            query_result = QueryResult()
            run_query(config, i, "run$r/throughput/s$s", query_result)
            push!(queries, query_result)
        end
    end
    t2 = run_rf2(config, s+2)
    StreamResult(s, t1, t2, queries)
end

function throughput_test(config, r::Int)
    run = config.result.metrics.performance_runs[r].throughput

    # get streams
    config.streams = config.streams == -1 ? get_streams(config.scale_factor) : config.streams
    streams = config.streams

    # Construct the stream results
    v = Vector{StreamResult}()
    for i in 1:streams
        push!(v, StreamResult())
    end

    if Threads.nthreads() < streams
        @warn("Not enough threads to run all streams")
    end
    # Run each stream in different thread
    t = @elapsed Threads.@threads for s in 1:streams
        v[s] = run_stream(config, s, r)
    end

    for s = 1:streams
        push!(run.throughputs, v[s])
    end
    # remove round once the json big float issue is fixed in Rel
    run.throughput_size = round((parse(Float64, config.scale_factor)*3600*streams*length(config.supported_queries))/t, digits=9)
    info("Run $r Throughput@Size = $(run.throughput_size)")
end

function performance_test(config, r::Int)
    # init performance runs vector
    push!(config.result.metrics.performance_runs, PerformanceRun())

    power_test(config, r)
    throughput_test(config, r)
    # remove round once the json big float issue is fixed in Rel
    config.result.metrics.performance_runs[r].power.qphh_size = round((config.result.metrics.performance_runs[r].power.power_size*config.result.metrics.performance_runs[r].throughput.throughput_size)^(1/2), digits=9)
end

# All connections point to the same database and server
function enable_qe_stats(config)
    query(
        rand(config.connections),
        "def insert[:rel, :config, :extra_output]: :qe_stats";
        readonly=false,
    )
    return nothing
end

function debug_primitive_set_latency(config, read_ms, write_ms)
    query(rand(config.connections), "def output { rel_primitive_debug_pager_set_disk_latency[$(read_ms), $(write_ms)] }")
    return nothing
end

# We have to execute the different debug natives in the same transaction to avoid
# having e.g., the database opened by the second transaction after the first one
# has cleared the database cache.
function debug_primitive_setup(
    config,
    clear_db_cache::Bool,
    clear_pager_cache::Bool,
    spec_prefetch_mode::Int,
)
    @dassert0 clear_db_cache || clear_pager_cache "must set at least one of database and pager \
        to be cleared"

    # Create dependency between definitions to make sure that we clear pager cache at the end
    query_str = if clear_db_cache && !clear_pager_cache
        """
        def spec_prefetch(z) : rel_primitive_debug_spec_prefetch_reset($(spec_prefetch_mode), z)
        def output(x, z) : rel_primitive_debug_database_clear_cache(x) and spec_prefetch(z)
        """
    elseif !clear_db_cache && clear_pager_cache
        """
        def spec_prefetch(z) : rel_primitive_debug_spec_prefetch_reset($(spec_prefetch_mode), z)
        def output(y, z) : rel_primitive_debug_pager_clear_cache(y) and spec_prefetch(z)
        """
    else
        """
        def db_clear(x) : rel_primitive_debug_database_clear_cache(x)
        def spec_prefetch(z) : rel_primitive_debug_spec_prefetch_reset($(spec_prefetch_mode), z)
        def output(x, y, z) : db_clear(x)
            and rel_primitive_debug_pager_clear_cache(y) and spec_prefetch(z)
        """
    end
    return query(rand(config.connections), query_str)
end


# Because RAI_Benchmark is a private package, we cannot use the constants
# from RAICode.Database directly, we thus manually keep the values in sync.
const SpeculativePrefetchingTrigger_DISABLED::Int = 0
const SpeculativePrefetchingTrigger_DB_OPEN::Int = 1
const SpeculativePrefetchingTrigger_NEXT_TX::Int = 2

function debug_primitive_spec_prefetch_reset(config, mode::Int)
    res = query(rand(config.connections), "def output { rel_primitive_debug_spec_prefetch_reset[$(mode)] }", outputs=:output)
    return first(first(res[:output]))
end

# Evict all pages from the cache before each query.
# This assumes that the necessary Julia compilation is already done
# and engine was previously warmed up.
function cold_io_perf(
    config,
    r::Int,
    force_dbopen::Bool,
    read_latency_ms::Float64;
    queries = config.supported_queries,
    spec_prefetch_enabled=true,
    warmup=false,
)
    queries_results = Vector{QueryResult}()
    title = force_dbopen ? "dbopen" : "compiled"
    spec_prefetch_mode = if spec_prefetch_enabled
        if force_dbopen
            # Here we are simulating 0 cost database open scenario
            # and speculative prefetch for each transaction without clearing
            # the database cache.
            SpeculativePrefetchingTrigger_DB_OPEN
        else
            SpeculativePrefetchingTrigger_NEXT_TX
        end
    else
        SpeculativePrefetchingTrigger_DISABLED
    end

    # Disable speculative prefetching
    default_mode = debug_primitive_spec_prefetch_reset(
        config,
        SpeculativePrefetchingTrigger_DISABLED,
    )
    debug_primitive_set_latency(config, read_latency_ms, 0.0)
    function reset_server_state()
        # All database and/or pager cache resets happen in the following transaction.
        # Clearing DB cache simulates a first transaction against a not yet opened/cached
        # database.
        # Note that we must clear database cache just BEFORE running the query
        # because any other query that comes in between will open
        # the database and will cause the DB cache to be filled again.
        debug_primitive_setup(config, force_dbopen, true, spec_prefetch_mode)
    end

    if warmup
        println("Warmup Cold IO perf run to eliminate Julia compilation effects")
    end

    try
        info("Measuring $(title) performance test with latency $(read_latency_ms)ms")
        for i in queries
            GC.gc()
            query_result = QueryResult()
            reset_server_state()
            run_query(config, i, "$(title)_$(r)", query_result)
            push!(queries_results, query_result)
        end
        if !warmup
            push!(
                config.result.metrics.compiled_runs,
                CompiledRun(read_latency_ms, force_dbopen, queries_results)
            )
        end
    finally
        # Reset to the default speculative prefetching mode
        debug_primitive_spec_prefetch_reset(
            config,
            default_mode,
        )
        debug_primitive_set_latency(config, 0.0, 0.0)
    end
    return queries_results
end

function get_db_size(config::RAITPCHConfig)
    conn = rand(config.connections)

    res = query(conn, "def output { rel_primitive_debug_report_db_total_relations_size }", outputs=:output)
    config.result.metrics.db_size.relations_bytes = first(first(res[:output]))

    res = query(conn, "def output { rel_primitive_debug_report_db_metadata_size }", outputs=:output)
    config.result.metrics.db_size.metadata_bytes = first(first(res[:output]))
end

function run_tpch(config::RAITPCHConfig)
    prev_debug_native_value = debug_natives_enabled()
    try
        set_debug_natives_on_off(true)
        hydra_metrics("run_tpch"; include_rusage=true) do
            if config.init_db
                t = @elapsed init_db(config)
                config.result.metrics.load.total_time = t
                get_db_size(config)
            end

            # After creating the database, we can set DB configurations
            enable_qe_stats(config)

            for i = 1:config.runs
                performance_test(config, i)
            end

            # Note: this must run after performance_test to ensure that Julia compilations are
            # already done and we have `compiled` performance.
            for r in 1:config.compiled_runs
                for read_latency_ms in [0.0, 80.0]
                    for force_dbopen in [false, true]
                        # Warmup run to eliminate Julia compilation effects using a subset of TPC-H queries
                        cold_io_perf(
                            config, r, force_dbopen, read_latency_ms;
                            # Only subset is enough because we are interested in warming up
                            # the page access and spec prefetch code paths.
                            # The QE and frontend parts should be already warmed up.
                            queries=length(config.supported_queries) < 22 ? config.supported_queries : [1,3,5,9,13], warmup=true,
                        )
                        # Real run
                        cold_io_perf(config, r, force_dbopen, read_latency_ms)
                    end
                end
            end

            if config.validation
                # Swap `connections` with `validation_conns` to continue with
                # the validation step.
                conns = config.connections
                config.connections = config.validation_conns
                init_db(config)
                # After creating the database, we can set DB configurations
                enable_qe_stats(config)
                run_validation_queries(config)
                config.connections = conns
            end

            # Remote connection can't be serialized so we set it to nothing
            let current_connections = config.connections, current_validation_conns = config.validation_conns
                config.connections = []
                config.validation_conns = []
                try
                    ExportResults.write_output(config, "result.json", JSON.json(config))
                finally
                    config.connections = current_connections
                    config.validation_conns = current_validation_conns
                end
            end

            if config.runs + config.compiled_runs != 0
                ExportResults.print_tpch_result(config)
                ExportResults.export_queries_csv(config)
            end
            nothing
        end
    finally
        set_debug_natives_on_off(prev_debug_native_value)
    end
end

end # module
