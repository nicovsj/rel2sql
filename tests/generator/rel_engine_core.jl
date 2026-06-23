# Shared Rel program execution via RAICode EmbeddedConnection.
module RelEngineCore

using JSON
using RAICode.API: EmbeddedConnection
using RelationalAI: collect_problems, create_database, load_edb, query
using RelationalAITypes

const SERVER_VERSION = "1"
const MAX_ERROR_LEN = 8000
const query_problems = Ref{Vector{String}}(String[])

export SERVER_VERSION, run_request, handle_message, init_server!, shutdown_server!

function truncate_error(msg::AbstractString; max_len::Integer=MAX_ERROR_LEN)
    if length(msg) <= max_len
        return msg
    end
    return msg[1:max_len] * "\n... (truncated)"
end

function clear_query_problems!()
    empty!(query_problems[])
    return nothing
end

function problem_to_string(problem)
    io = IOBuffer()
    RelationalAITypes.report(io, problem)
    return String(take!(io))
end

function capture_and_report_problem(problem)
    text = problem_to_string(problem)
    if !isempty(strip(text))
        push!(query_problems[], text)
    end
    RelationalAITypes.report_stderr(problem)
    return nothing
end

function make_embedded_connection()
    return EmbeddedConnection(dbname=:rel2sql_correctness, report=capture_and_report_problem)
end

function append_collect_problems!(conn)
    try
        seen = Set(query_problems[])
        for problem in collect_problems(conn)
            text = problem_to_string(problem)
            if !isempty(strip(text)) && !(text in seen)
                push!(query_problems[], text)
                push!(seen, text)
            end
        end
    catch
        nothing
    end
    return nothing
end

function format_execution_error(exc)
    parts = copy(query_problems[])
    exc_text = sprint(showerror, exc)
    if !isempty(strip(exc_text))
        push!(parts, exc_text)
    end
    if isempty(parts)
        return "unknown RAICode execution error"
    end
    return truncate_error(join(parts, "\n\n"))
end

function error_response(exc, conn)
    append_collect_problems!(conn)
    msg = format_execution_error(exc)
    problems = copy(query_problems[])
    response = Dict("error" => msg)
    if !isempty(problems)
        response["problems"] = problems
    end
    return response
end

function extract_program_def_symbols(program::AbstractString)
    syms = Symbol[]
    for m in eachmatch(r"def\s+(Gen\d+)\s*\{", program)
        push!(syms, Symbol(m.captures[1]))
    end
    return syms
end

function relation_row_count(rel)
    try
        return length(collect(rel))
    catch
        try
            return length(rel)
        catch
            return -1
        end
    end
end

function describe_def_availability(result_dict, program_defs)
    lines = String[]
    for sym in program_defs
        if haskey(result_dict, sym)
            rel = get(result_dict, sym, nothing)
            row_count = relation_row_count(rel)
            if row_count >= 0
                push!(lines, "  :$sym — present ($row_count rows)")
            else
                push!(lines, "  :$sym — present")
            end
        else
            push!(lines, "  :$sym — missing")
        end
    end
    return lines
end

function missing_output_response(conn, output_sym::Symbol, program_defs, result_dict)
    append_collect_problems!(conn)
    lines = String[]
    if !isempty(query_problems[])
        append!(lines, query_problems[])
        push!(lines, "")
    end
    push!(lines, "Requested output :$output_sym was not materialized by RAICode.")
    if isempty(program_defs)
        push!(lines, "No Gen* definitions were found in the program text.")
    else
        push!(lines, "Definition availability:")
        append!(lines, describe_def_availability(result_dict, program_defs))
    end
    push!(lines, "")
    push!(lines, "This usually means an upstream def failed to evaluate or is not queryable (for example expression defs like average[R] chained into GenN(x) rules).")
    msg = truncate_error(join(lines, "\n"))
    response = Dict("error" => msg)
    if !isempty(query_problems[])
        response["problems"] = copy(query_problems[])
    end
    return response
end

function execute_program_request(conn, program::AbstractString, edb_inputs, output_sym::Symbol; use_edb_inputs::Bool=true)
    program_defs = extract_program_def_symbols(program)
    request_outputs = unique(vcat(program_defs, [output_sym]))

    result_dict = if use_edb_inputs && !isempty(edb_inputs)
        query(conn, program; inputs=edb_inputs, outputs=request_outputs)
    else
        query(conn, program; outputs=request_outputs)
    end
    append_collect_problems!(conn)

    if !haskey(result_dict, output_sym)
        return missing_output_response(conn, output_sym, program_defs, result_dict), nothing
    end

    return nothing, result_dict[output_sym]
end

mutable struct ServerState
    conn::Union{Nothing, EmbeddedConnection}
    edb_loaded::Bool
    edb_fingerprint::UInt64
end

const server_state = ServerState(nothing, false, UInt64(0))

function edb_fingerprint(edb)
    return hash(JSON.json(edb))
end

function ensure_edb_loaded!(conn, edb, edb_inputs)
    fp = edb_fingerprint(edb)
    if server_state.conn === conn && server_state.edb_loaded && server_state.edb_fingerprint == fp
        return false
    end

    println(stderr, "rel_engine_server: loading EDB ($(edb_summary(edb)))...")
    flush(stderr)
    create_database(conn; overwrite=true)
    if !isempty(edb_inputs)
        load_edb(conn, edb_inputs)
    end
    server_state.edb_loaded = true
    server_state.edb_fingerprint = fp
    return true
end

function init_server!()
    if server_state.conn !== nothing
        return
    end
    server_state.conn = make_embedded_connection()
    nothing
end

function shutdown_server!()
    server_state.conn = nothing
    server_state.edb_loaded = false
    server_state.edb_fingerprint = UInt64(0)
    nothing
end

export cell_to_string

function cell_to_string(value)
    if value === nothing
        return "NULL"
    elseif value isa Char
        return string(value)
    elseif value isa AbstractString
        return string(value)
    elseif value isa Integer
        return string(value)
    elseif value isa AbstractFloat
        return string(value)
    elseif value isa Bool
        return value ? "true" : "false"
    else
        return string(value)
    end
end

function row_to_strings(row)
    if row isa Tuple
        return [cell_to_string(v) for v in row]
    end
    return [cell_to_string(row)]
end

function normalize_rows(result)
    if result isa AbstractVector
        return [row_to_strings(row) for row in result]
    end
    return [row_to_strings(result)]
end

function infer_columns(rows, output_arity::Integer=0)
    width = if !isempty(rows)
        length(rows[1])
    elseif output_arity > 0
        Int(output_arity)
    else
        1
    end
    return ["A$(i)" for i in 1:width]
end

function shape_edb_rows(rows)
    if isempty(rows)
        return rows
    end
    first_row = rows[1]
    if length(first_row) == 1
        return [row[1] for row in rows]
    end
    return [Tuple(row) for row in rows]
end

function describe_value(value)
    if value isa Tuple
        return Dict("kind" => "tuple", "width" => length(value), "repr" => repr(value))
    elseif value isa AbstractVector
        return Dict("kind" => "vector", "length" => length(value), "repr" => repr(value))
    end
    return Dict("kind" => string(typeof(value)), "repr" => repr(value))
end

function raw_preview(result, limit=5)
    if result isa AbstractVector
        items = [describe_value(v) for v in result[1:min(end, limit)]]
        extra = max(length(result) - limit, 0)
        return Dict(
            "shape" => "vector",
            "length" => length(result),
            "items" => items,
            "truncated" => extra,
        )
    end
    return Dict("shape" => "scalar", "value" => describe_value(result))
end

function edb_summary(edb)
    if isempty(edb)
        return "0 tables"
    end
    parts = String[]
    for (name, rows) in edb
        n = rows isa AbstractVector ? length(rows) : 0
        push!(parts, "$name($n rows)")
    end
    return join(parts, ", ")
end

function run_request(request::AbstractDict)
    program = get(request, "program", "")
    output_name = get(request, "output", "output")
    edb = get(request, "edb", Dict{String, Any}())
    include_raw = get(request, "raw", false)

    if isempty(program)
        return Dict("error" => "missing program")
    end

    clear_query_problems!()

    println(stderr, "rel_engine_server: run output=$output_name program=$(length(program)) chars edb=$(edb_summary(edb))")
    flush(stderr)

    conn = if server_state.conn !== nothing
        server_state.conn
    else
        make_embedded_connection()
    end

    edb_inputs = Dict{Symbol, Any}()
    for (name, rows) in edb
        edb_inputs[Symbol(name)] = shape_edb_rows(rows)
    end

    reloaded_edb = ensure_edb_loaded!(conn, edb, edb_inputs)
    if reloaded_edb
        println(stderr, "rel_engine_server: EDB loaded (database reset)")
    else
        println(stderr, "rel_engine_server: reusing cached EDB")
    end
    flush(stderr)

    output_sym = Symbol(output_name)
    output_arity = Int(get(request, "output_arity", 0))
    println(stderr, "rel_engine_server: querying (out=:$output_name)...")
    flush(stderr)
    start = time()
    try
        err_response, result = execute_program_request(
            conn,
            program,
            edb_inputs,
            output_sym;
            use_edb_inputs=reloaded_edb,
        )
        if err_response !== nothing
            elapsed = round(Int, time() - start)
            println(stderr, "rel_engine_server: query failed in $(elapsed)s")
            println(stderr, get(err_response, "error", ""))
            flush(stderr)
            return err_response
        end

        elapsed = round(Int, time() - start)
        println(stderr, "rel_engine_server: query finished in $(elapsed)s")
        flush(stderr)

        rows = normalize_rows(result)
        columns = infer_columns(rows, output_arity)
        response = Dict("columns" => columns, "rows" => rows)
        if include_raw
            response["raw"] = raw_preview(result)
        end
        return response
    catch exc
        response = error_response(exc, conn)
        elapsed = round(Int, time() - start)
        println(stderr, "rel_engine_server: query failed in $(elapsed)s")
        println(stderr, get(response, "error", ""))
        flush(stderr)
        return response
    end
end

function handle_message(request::AbstractDict)
    op = get(request, "op", "run")
    if op == "ping"
        return Dict("ok" => true, "version" => SERVER_VERSION)
    elseif op == "shutdown"
        shutdown_server!()
        return Dict("ok" => true, "shutdown" => true)
    elseif op == "run"
        try
            return run_request(request)
        catch exc
            conn = server_state.conn
            response = if conn !== nothing
                error_response(exc, conn)
            else
                Dict("error" => truncate_error(sprint(showerror, exc)))
            end
            println(stderr, "rel_engine_server: error: ", get(response, "error", ""))
            flush(stderr)
            return response
        end
    else
        return Dict("error" => "unknown op: $op")
    end
end

end
