using CSV: CSV
using DataFrames: DataFrames, DataFrame
using Dates: Dates, Date, DateTime, now
using RelationalAI: ArrowDict, query, DBConnection,
    create_database, install_source, LocalConnection
using RelationalAITypes: SourceFile, RelKey
using RAI_VariableSizeStrings: VariableSizeString
using FixedPointDecimals: FixedDecimal
using Printf: @sprintf
using Test: @test, @testset

Base.@kwdef mutable struct RAITPCHConfig
    display_name :: String
    scale_factor :: String
    query_dir :: String
    tpch_dir :: Union{String, Nothing}
    data_dir :: String
    streams :: Int
    compressed :: Bool = false
    schema_name :: String
    output_dir :: String
    qgen_seed :: Int
    result :: TPCHExecution = TPCHExecution()
    supported_queries :: Vector{Int} = collect(1:22)
    init_db :: Bool = true
    refresh_streams :: Bool = true
    validation :: Bool = true
    compiled_runs :: Int = 0
    runs :: Int = 1
    cores :: Int = 0
    skewed :: Bool = false
    metadata :: Dict{Any, Any}
    connections :: Array{DBConnection} = []
    validation_conns :: Array{DBConnection} = []
end

function rel_config(sf,
                      connections;
                      runs = 2,
                      output = "",
                      qgen_seed = 1,
                      streams = -1,
                      metadata = Dict(),
                      validation = true,
                      refresh_streams = true,
                      tpch_dir = tpch_dir("dbgen-rel"),
                      data_dir = joinpath(TOP_SRC_DIR, "src/TPCH/data", string("sf-", sf)),
                      compressed = false,
                      skewed=false)

    @info "Calling rel_config"

    schema = replace("tpch_sf$(sf)", "." => "")

    RAITPCHConfig(display_name = "rel",
                    scale_factor = sf,
                    query_dir = joinpath(TOP_SRC_DIR, "src/TPCH/rel/queries"),
                    tpch_dir = tpch_dir,
                    data_dir = data_dir,
                    streams = streams,
                    validation = validation,
                    refresh_streams = refresh_streams,
                    compressed = compressed,
                    schema_name = schema,
                    output_dir = output,
                    qgen_seed = qgen_seed,
                    metadata = metadata,
                    runs = runs,
                    skewed = skewed,
                    connections = connections)
end

function rel_config(sf,
                      connection::DBConnection;
                      runs = 2,
                      output = "",
                      qgen_seed = 1,
                      streams = -1,
                      validation = false,
                      refresh_streams = true,
                      tpch_dir = tpch_dir("dbgen-rel"),
                      data_dir = joinpath(TOP_SRC_DIR, "src/TPCH/data", string("sf-", sf)),
                      skewed = false,
                      metadata = Dict(),
                      compressed = false)

    rel_config(sf,
                 [connection];
                 runs = runs,
                 output = output,
                 qgen_seed = qgen_seed,
                 streams = streams,
                 refresh_streams = refresh_streams,
                 validation = validation,
                 tpch_dir = tpch_dir,
                 data_dir = data_dir,
                 metadata = metadata,
                 skewed = skewed,
                 compressed = compressed)
end

# tpch benchmark tables
const tpch_tables = [:nation, :region, :part, :supplier, :partsupp, :customer, :orders, :lineitem]

function load_data(connection::DBConnection, config::RAITPCHConfig)
    info("Loading data for TPC-H SF $(config.scale_factor)")
    for table in tpch_tables
        tbl_path = config.compressed ? joinpath(config.data_dir, "$(table).tbl.gz") : joinpath(config.data_dir, "$(table).tbl")
        info(" setting $(table)_csv_path path to $(tbl_path)")
        query(connection, """ def insert[:$(table)_csv_path]: "$(tbl_path)" """; readonly=false)
    end

    install_tpch_source(connection, "tpch_dataload_schema_mapping.rel")
    install_tpch_source(connection, "tpch_schema.rel")

    info("Start loading tpch data")
    query(connection, SourceFile(joinpath(@__DIR__, "tpch_dataload.rel")); readonly=false)
end

function install_tpch_source(connection::DBConnection, file::String)
    src_path = joinpath(@__DIR__, file)
    info(" installing $(file)")
    t = @elapsed install_source(connection; path = src_path)
    info(" $(file) installed: $(t)")
end

function install_sources(connection::DBConnection, config::RAITPCHConfig)
    info("installing sources for TPC-H SF $(config.scale_factor)")

    # install tpch_schema_mapping.rel
    install_tpch_source(connection, "tpch_schema_mapping.rel")
    # installing tpch_common_defs.rel
    install_tpch_source(connection, "tpch_common_defs.rel")
end

function init_db(config::RAITPCHConfig; validation=false)
    connection = config.connections[1]

    create_database(connection)

    load_data(connection, config)

    install_sources(connection, config)
end

# Check TPCH specification refresh function RF1 for more details
# The refresh function 1 adds new sales information to the database
function run_rf1(config::RAITPCHConfig, s::Int)
    connection = config.connections[1]
    refresh_dir = joinpath(config.data_dir, "refresh")
    orders = joinpath(refresh_dir, string("orders.tbl.u", s, config.compressed ? ".gz" : ""))
    lineitem = joinpath(refresh_dir, string("lineitem.tbl.u", s, config.compressed ? ".gz" : ""))

    # specification: load orders and lineitem refresh data in a single transaction
    t = @elapsed query(connection, """
        def orders_csv_config_override { orders_csv_config ++> (:path, "$(orders)") }
        def insert[:orders_rel]: schema_mapping_orders[load_csv[orders_csv_config_override]]

        def lineitem_csv_config_override { lineitem_csv_config ++> (:path, "$(lineitem)") }
        def insert[:lineitem_rel]: schema_mapping_lineitem[load_csv[lineitem_csv_config_override]]
    """; readonly=false)

    info("ran RF1: $(t), updates: $(s)")
    t
end

# Check TPCH specification refresh function RF2 for more details
# The refresh function 2 removes old sales information from the database
function run_rf2(config::RAITPCHConfig, s::Int)
    connection = config.connections[1]
    refresh_dir = joinpath(config.data_dir, "refresh")
    delete = joinpath(refresh_dir, string("delete.", s, config.compressed ? ".gz" : ""))

    t = @elapsed query(connection, """
        def delete_orders_csv_config[:path]: "$(delete)"
        def delete_orders_csv_config[:syntax, :header_row]: 0
        def delete_orders_csv_config[:syntax, :header]: (1, :o_orderkey)
        def delete_orders_csv_config[:schema, :o_orderkey]: "int"

        def orders_to_delete(o): load_csv(delete_orders_csv_config, _, _, o)

        def delete(:lineitem_rel, col, o, num, v):
            lineitem_rel(col, o, num, v) and orders_to_delete(o)
        def delete(:orders_rel, col, o, v):
            orders_rel(col, o, v) and orders_to_delete(o)
    """; readonly=false)

    info("ran RF2: $(t), delete: $(s)")
    t
end

function export_result(d, data_file)
    d = collect(d)
    if length(d) == 0 || length(d[1]) == 0
        df = DataFrame()
    else
        data = Array{Any}(undef, length(d),length(d[1]))
        for i in 1:length(d)
            for j in 1:length(d[1])
                if typeof(d[i][j]) <: VariableSizeString
                    data[i,j] = String(d[i][j])
                else
                    data[i,j] = d[i][j]
                end
            end
        end
        df = DataFrame(data, :auto)
    end
    if data_file == ""
        data_file = IOBuffer()
    end
    # @info "data_file", data_file
    df |> CSV.write(data_file; delim='|', writeheader=false) # CSV API has changed
    String(data_file)
end

# Helpers used to convert from Julia types to strings
cnvt(x::Char) = string(x)
cnvt(x::Int64) = string(x)
cnvt(x::FixedDecimal) = @sprintf("%.5f", x)
cnvt(x::Date) = Dates.format(x, "yyyy-mm-dd")
cnvt(x::Any) = convert(String, x)

combine_tuple(x::Pair) = (x.first..., x.second...)
combine_tuple(x::Tuple) = x

function validate_result(result, validation_file::String)
        @assert validation_file != ""
        if startswith(validation_file, "https://") || ispath(validation_file)
            info("validating output against $(validation_file)")

            # First export the result into a csv string, and then compare it with the validation file
            result_csv = []
            for i in result
                push!(result_csv, join(map(cnvt, combine_tuple(i)), '|'))
            end
            # These are queries that order the result. We need to
            # separately validate them since we add an index to the
            # result in Rel. Which we remove in the validation
            # Missing: 1, 4, 7, 15, 20, 22,
	    if basename(validation_file) in ["q1.csv", "q2.csv", "q3.csv",
					     "q4.csv", "q5.csv", "q7.csv",
					     "q8.csv", "q10.csv", "q13.csv",
					     "q15.csv", "q16.csv", "q18.csv",
					     "q20.csv", "q21.csv", "q22.csv"]
                # Adding q16.csv here
                info("taking order into account")
                return Validator.validate(join(result_csv, "\n"), validation_file, order_matters=true)
            else
                return Validator.validate(join(result_csv, "\n"), validation_file)
            end
        else
            @warn "could not find validation file at $(validation_file)"
            return false
        end
end

"""
Executes the query, validates if config.validation is true
"""
function execute_query(config::RAITPCHConfig, q::String, data_file::String = "", print = data_file == "", validate_file="")
    local results, result_relation
    local result_relkey = nothing

    connection = rand(config.connections)
    t = @elapsed begin
        results = query(connection, q; outputs=[:result])

        if config.validation && ( validate_file != "" ) &&
                isa(connection, LocalConnection)
            # If validating the results over a local connection, we need to
            # interpret the Arrow data, which is not done by default as is the
            # case for embedded connections.
            results = ArrowDict(results, true)
        end

        # The `:result` relation may be missing if it was empty. In this case,
        # we use an empty relation.
        result_relation = get(results, :result, Vector{Tuple}())
    end

    if print
        println()
        println("Results:")
        println()
        for r in result_relation
            println(r)
        end
        println()
    else
        if data_file != ""
            export_result(result_relation, data_file)
        end
    end

    if config.validation && ( validate_file != "" )
        query_id = first(split(basename(validate_file), "."))
        @testset  "$(query_id)" begin
            @test validate_result(result_relation, validate_file)
        end
    end

    qe_time = begin
        qe_time_key_sym = Symbol("/:rel/:qe/:qe_time_ms/Float64")
        qe_time_key_rel = RelKey(:rel, Tuple{:qe, :qe_time_ms}, Float64)
        if haskey(results, qe_time_key_sym)
            first(first(results[qe_time_key_sym]))
        elseif haskey(results, qe_time_key_rel)
            first(first(results[qe_time_key_rel]))
        else
            info("QE time key not found in results")
            0
        end
    end

    return t, qe_time/1e3
end
